#include "vector/hrz_core_vector_data_loader.h"

#include "assets_loader/hrz_core_assets_loader.h"
#include "hrz_core_channel_group.h"
#include "hrz_core_client_messages.h"
#include "hrz_core_loading_priorities.h"
#include "hrz_core_pmtiles.h"
#include "hrz_core_tile_url_generator.h"
#include "hrz_core_tilejson.h"
#include "hrz_fnd_meta.h"
#include "hrz_jobs_tickets.h"
#include "vector/hrz_core_vector_in_memory.h"

#include <hrz_common_attributes.h>
#include <hrz_common_blob_allocator.h>
#include <hrz_common_color.h>
#include <hrz_common_fmt.h>
#include <hrz_common_metrics.h>
#include <hrz_common_profiling.h>
#include <hrz_common_proto_geo.h>
#include <hrz_common_tickets.h>
#include <hrz_common_ui_utils.h>
#include <hrz_common_vector_data.h>
#include <hrz_fnd_class.h>
#include <hrz_fnd_flat_hash_set.h>
#include <hrz_fnd_format.h>
#include <hrz_fnd_gen_index_pool.h>
#include <hrz_fnd_gen_object_pool.h>
#include <hrz_fnd_hash.h>
#include <hrz_fnd_log.h>
#include <hrz_fnd_maths.h>
#include <hrz_fnd_node_hash_map.h>
#include <hrz_fnd_shared_object_pool.h>
#include <hrz_fnd_time.h>
#include <hrz_protocol_path_builder.h>

extern "C"
{
#include <microui/microui.h>
}

#include <hrz_fnd_thread.h>

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <mutex>
#include <queue>
#include <unordered_map>
#include <utility>

namespace hrz
{
namespace in_memory = vector_data::in_memory;

// This system works by splitting all the work into small tasks. For example retrieving the geometry
// of a tile may involve downloading a file, decoding the file, and extracting the geometry from the
// decoded data. Retrieving attribute values can involve downloading and decoding the same file,
// then extracting the feature IDs of the tile, sending an attribute value request to the client for
// these IDs, and waiting for the response. Each of these elementary chunks of work constitutes a
// task. A task can create sub-tasks if needed. The tasks are deduplicated, so if a task requires a
// sub-task that already exists, it gets that task.
//
// The API offers a few different options for data requests. One request creates one task. These
// tasks are the roots of a directed acyclic graph of tasks.
//
// A task can refer to multiple types of objects: feature ID lists, attribute value lists, vector
// data objects, and other tasks. All these objects are deduplicated when they are referred to by
// multiple tasks. They are reference counted, and are freed when unused. Tasks can also refer to
// the data in their sub-tasks. (Which is only available when the sub-task is in the `Loaded`
// state.) A task can signal to a sub-task whether or not it would like its data to be loaded.
//
// Every task has a `version` number field. This is used to keep track of source data updates. Some
// leaf task, that fetch source data, can receive notifications of data updates. When it happens,
// these tasks and all transitive parent tasks are restarted, so that they fetch and process updated
// data. When it happens, their version number is incremented. Each data request made to the vector
// data loader keeps the version of the data it received in its ticket. This mechanism allows data
// requesters to be made aware that they should reload the data they requested.
//
// Dependencies between tasks are handled through a dedicated type of reference. It allows tasks to
// be aware of which other tasks depend on them (used for data updates). The dependent task can also
// indicate through the reference whether or not it needs the dependency to keep its data alive.
//
// Life of a task:
//
//   Tasks are created by the `get_or_create_..._task()` functions. These functions ensure identical
//   tasks are deduplicated. When a task is created, it is given its parameters (that indicate what
//   it should do), and is in the `New` state. At the next execution of `work()`, all the necessary
//   sub-tasks are created. This creates a task graph, but no actual work is done yet. The task is
//   now in the `Unloaded` state.
//
//	 As soon as the task is made aware that something needs its data (through its `data_use_count`
//   field, itself updated through the task references), it signals its sub-tasks that it needs
//   their data (which can in turn make them start loading), or starts other kind of jobs (like
//   downloading a file), depending on the task's type. The task is now in the `Loading` state. When
//   in this state, it checks the state of its sub-tasks, and/or if its jobs. When everything is
//   loaded and processed, the task switches to the `Loaded` state. (If somewhere along the way an
//   error occurred, it goes in the `DataError` state.) Some tasks signal their sub-tasks they still
//   need their data when they are loaded; others don't need this data, and cease to require their
//   sub-tasks' data.
//
//	 When the task is loading or loaded, and nothing requires the task's data any more, the task
//   unloads its data, and signal its sub-tasks that it doesn't need their data. The task switches
//   back to the `Unloaded` state. (And so can its sub-tasks, if nothing else requires their data.)
//
//	 When the data of a task has been externally updated, it unloads its current data, cancels its
//   jobs, increments its version, and switches to the `Unloaded` state. The same is done for its
//   transitive parents. The data of the sub-tasks, if still retained, is not released, as it will
//   be required just later. Indeed, if the data of the updated task is still required, it will go
//   from the `Unloaded` to the `Loading` state, and require the data of its sub-tasks, next time
//   `work()` is called.
//
// Ultimately, whether some data is needed or not comes from the data requests. Requesters can
// themselves signal whether they require the data or not, through dedicated functions. They can
// only retrieve the data the requested when it's loaded, and it only loads if it is required to.
//
// When the data model changes, all data requests that depended on this model are recreated. This in
// turn destroys and recreated the tasks as needed.

struct VectorDataLoader
{
    using DataKind = vector_data::DataKind;

    struct LayerModel;
    using LayerModelPool = SharedObjectPool<LayerModel, 16>;
    using LayerModelRef = LayerModelPool::Ref;

    struct Task;
    using TaskPool = SharedObjectPool<Task, 32>;
    using TaskRef = TaskPool::Ref;
    using WeakTaskRef = TaskPool::WeakRef;

    using FeatureIdListPool = SharedObjectPool<vector_data::FeatureIds, 32>;
    using FeatureIdListRef = FeatureIdListPool::Ref;
    using WeakFeatureIdListRef = FeatureIdListPool::WeakRef;

    using AttributeValueListPool = SharedObjectPool<vector_data::AttributeValues, 32>;
    using AttributeValueListRef = AttributeValueListPool::Ref;

    using TileGeometry = vector_data::VectorTileGeometry;
    using TileGeometryPool = SharedObjectPool<TileGeometry, 16>;
    using TileGeometryRef = TileGeometryPool::Ref;

    using ClientTicket = uint32_t;
    static constexpr ClientTicket NO_CLIENT_TICKET = 0;

    hrz::TicketGenerator<ClientTicket> client_ticket_generator;

    struct TaskDependency
    {
    public:
        TaskDependency() : task(TaskRef{}), dependent_task(WeakTaskRef{}), retains_data(false) {}

    private:
        TaskDependency(TaskRef task, const TaskRef& dependent_task) :
            task(task), dependent_task(dependent_task.make_weak_ref()), retains_data(false)
        {
            assert(task.value().dependents.count(this->dependent_task) == 0);
            this->task.value().dependents.insert(this->dependent_task);
        }

        explicit TaskDependency(TaskRef task) :
            task(task), dependent_task(WeakTaskRef{}), retains_data(false)
        {
        }

    public:
        static TaskDependency between_tasks(TaskRef dependee_task, const TaskRef& dependent_task)
        {
            return TaskDependency(dependee_task, dependent_task);
        }

        static TaskDependency on_task(TaskRef task) { return TaskDependency(task); }

        TaskDependency(TaskDependency&& dep) :
            task(std::exchange(dep.task, {})),
            dependent_task(std::exchange(dep.dependent_task, {})),
            retains_data(std::exchange(dep.retains_data, false))
        {
        }

        TaskDependency& operator=(TaskDependency&& dep)
        {
            task = std::exchange(dep.task, {});
            dependent_task = std::exchange(dep.dependent_task, {});
            retains_data = std::exchange(dep.retains_data, false);
            return *this;
        }

        ~TaskDependency() { release(); }

        bool has_task() const { return task.has_value(); }

        TaskRef& get_task_ref() { return task; }

        const TaskRef& get_task_ref() const { return task; }

        Task& get_task() { return task.value(); }

        const Task& get_task() const { return task.value(); }

        bool is_data_retained() const { return retains_data; }

        void retain_data()
        {
            if (!retains_data && task.has_value())
            {
                task.value().data_use_count += 1;
                retains_data = true;

                if (task.value().data_use_count == 1)
                {
                    task.value().loader->activate_task(task.make_weak_ref());
                }
            }
        }

        void release_data()
        {
            if (retains_data && task.has_value())
            {
                assert(task.value().data_use_count >= 1);
                task.value().data_use_count -= 1;

                if (task.value().data_use_count == 0)
                {
                    task.value().loader->activate_task(task.make_weak_ref());
                }

                retains_data = false;
            }
        }

        uint8_t get_version() const
        {
            if (!task.has_value()) return 0;

            return task.value().version;
        }

        void release()
        {
            release_data();

            if (task.has_value())
            {
                if (dependent_task.has_value())
                {
                    assert(task.value().dependents.count(dependent_task) > 0);
                    task.value().dependents.erase(dependent_task);
                }

                if (retains_data)
                {
                    task.value().data_use_count -= 1;
                }
            }

            task.release();
            retains_data = false;
        }

    private:
        TaskRef task;
        WeakTaskRef dependent_task;
        bool retains_data;
    };

    struct LayerModel
    {
        static constexpr uint32_t NO_SOURCE = std::numeric_limits<uint32_t>::max();

        struct Source
        {
            struct TiledDataProvider
            {
                hrz::PatternTileUrlGenerator tile_url_generator;
                hrz::HttpHeaders headers;
                hrz_proto::VectorDataFormat format;
                std::string layer_name;
                uint32_t min_lod;
                uint32_t max_lod;
                hrz::GeoBounds bounds;
                AttributionHandle attribution;
            };

            struct ClientDataProvider
            {
                hrz_proto::VectorDataSourceAccess access;
                std::optional<double> timeout_duration; // ms
                uint32_t min_lod;
                uint32_t max_lod;
                hrz::GeoBounds bounds;
            };

            struct InMemoryDataProvider
            {
                uint32_t in_memory_layer_id;
            };

            struct TileJsonDataProvider
            {
                std::string url;
                hrz::HttpHeaders headers;
                bool preserve_query_parameters;
                std::string layer_name;

                // Initially contains the attribution from the layer model.
                // The TileJSON file itself may have its own attribution.
                // The two must be combined.
                AttributionHandle attribution;

                TaskDependency load_tilejson_task;
            };

            struct PmTilesDataProvider
            {
                std::string url;
                hrz::HttpHeaders headers;
                std::string layer_name;

                // Initially contains the attribution from the layer model.
                // The PMTiles source itself may have its own attribution.
                // The two must be combined.
                AttributionHandle attribution;

                TaskDependency load_pmtiles_task;
            };

            struct UntiledDataProvider
            {
                std::string url;
                hrz::HttpHeaders headers;
                hrz_proto::VectorDataFormat format;
                float tolerance;
                bool clip_margin;
                uint32_t min_lod;
                uint32_t max_lod;
                hrz::GeoBounds bounds;
                AttributionHandle attribution;
            };

            std::variant<
                TiledDataProvider,
                ClientDataProvider,
                InMemoryDataProvider,
                TileJsonDataProvider,
                PmTilesDataProvider,
                UntiledDataProvider>
                provider;
            bool has_geometry;
            std::optional<uint32_t> source_feature_id_attribute;
            metrics::MetricDesc request_count_metric;

            // Set by LoadSourceModel task
            std::optional<uint32_t> min_lod;
            std::optional<uint32_t> max_lod;
            std::optional<hrz::GeoBounds> bounds;

            bool has_tiled_data_provider() const
            {
                return std::holds_alternative<TiledDataProvider>(provider);
            }

            const TiledDataProvider& tiled_data_provider() const
            {
                assert(std::holds_alternative<TiledDataProvider>(provider));
                return std::get<TiledDataProvider>(provider);
            }

            TiledDataProvider& tiled_data_provider()
            {
                assert(std::holds_alternative<TiledDataProvider>(provider));
                return std::get<TiledDataProvider>(provider);
            }

            bool has_client_data_provider() const
            {
                return std::holds_alternative<ClientDataProvider>(provider);
            }

            const ClientDataProvider& client_data_provider() const
            {
                assert(std::holds_alternative<ClientDataProvider>(provider));
                return std::get<ClientDataProvider>(provider);
            }

            ClientDataProvider& client_data_provider()
            {
                assert(std::holds_alternative<ClientDataProvider>(provider));
                return std::get<ClientDataProvider>(provider);
            }

            bool has_in_memory_data_provider() const
            {
                return std::holds_alternative<InMemoryDataProvider>(provider);
            }

            const InMemoryDataProvider& in_memory_data_provider() const
            {
                assert(std::holds_alternative<InMemoryDataProvider>(provider));
                return std::get<InMemoryDataProvider>(provider);
            }

            InMemoryDataProvider& in_memory_data_provider()
            {
                assert(std::holds_alternative<InMemoryDataProvider>(provider));
                return std::get<InMemoryDataProvider>(provider);
            }

            bool has_tilejson_data_provider() const
            {
                return std::holds_alternative<TileJsonDataProvider>(provider);
            }

            const TileJsonDataProvider& tilejson_data_provider() const
            {
                assert(std::holds_alternative<TileJsonDataProvider>(provider));
                return std::get<TileJsonDataProvider>(provider);
            }

            TileJsonDataProvider& tilejson_data_provider()
            {
                assert(std::holds_alternative<TileJsonDataProvider>(provider));
                return std::get<TileJsonDataProvider>(provider);
            }

            bool has_pmtiles_data_provider() const
            {
                return std::holds_alternative<PmTilesDataProvider>(provider);
            }

            const PmTilesDataProvider& pmtiles_data_provider() const
            {
                assert(std::holds_alternative<PmTilesDataProvider>(provider));
                return std::get<PmTilesDataProvider>(provider);
            }

            PmTilesDataProvider& pmtiles_data_provider()
            {
                assert(std::holds_alternative<PmTilesDataProvider>(provider));
                return std::get<PmTilesDataProvider>(provider);
            }

            bool has_untiled_data_provider() const
            {
                return std::holds_alternative<UntiledDataProvider>(provider);
            }

            const UntiledDataProvider& untiled_data_provider() const
            {
                assert(std::holds_alternative<UntiledDataProvider>(provider));
                return std::get<UntiledDataProvider>(provider);
            }

            UntiledDataProvider& untiled_data_provider()
            {
                assert(std::holds_alternative<UntiledDataProvider>(provider));
                return std::get<UntiledDataProvider>(provider);
            }
        };

        struct Attribute
        {
            uint32_t id;
            bool is_source_feature_ids;
            std::string name_in_source;
            bool is_feature_id;
            uint32_t data_source;
            hrz_proto::AttributeTransform transform;

            vector_data::AttributeModel to_attribute_model() const
            {
                vector_data::AttributeModel model{};
                model.id = id;
                model.is_feature_id = is_feature_id;
                model.name = name_in_source;
                model.transform = transform;
                return model;
            }
        };

        uint64_t layer_handle;
        uint32_t id;
        uint32_t data_version;
        int8_t loading_priority;
        std::vector<Source> data_sources;
        hrz::flat_hash_map<uint32_t, Attribute> attributes;
        uint32_t geometry_source;
        uint32_t feature_id_source;

        std::vector<uint32_t> updated_headers_sources_indices;
    };

    struct FeatureSelection
    {
        FeatureSelection() : data(hrz::TileCoords(0, 0, 0)) {}

        explicit FeatureSelection(const FeatureIdListRef& ref) : data(ref) {}

        explicit FeatureSelection(hrz::TileCoords tile_coords) : data(tile_coords) {}

        bool has_feature_ids() const { return std::holds_alternative<FeatureIdListRef>(data); }

        const FeatureIdListRef& feature_ids() const
        {
            assert(std::holds_alternative<FeatureIdListRef>(data));
            return std::get<FeatureIdListRef>(data);
        }

        bool has_tile_coords() const { return std::holds_alternative<hrz::TileCoords>(data); }

        const hrz::TileCoords& tile_coords() const
        {
            assert(std::holds_alternative<hrz::TileCoords>(data));
            return std::get<hrz::TileCoords>(data);
        }

        bool operator==(const FeatureSelection& other) const { return other.data == data; }

        uint64_t hash() const
        {
            if (has_feature_ids())
            {
                return hrz::hash_mix((uint64_t)0, (uint64_t)feature_ids().get_handle().hash());
            }
            else if (has_tile_coords())
            {
                const auto& coords = tile_coords();
                return hrz::hash_mix(
                    (uint64_t)1,
                    hrz::hash_mix(
                        (uint64_t)coords.x,
                        hrz::hash_mix((uint64_t)coords.y, (uint64_t)coords.lod)));
            }
            else
            {
                assert(false && "Unhandled case");
                return 0;
            }
        }

    private:
        std::variant<FeatureIdListRef, hrz::TileCoords> data;
    };

    struct RequestId
    {
        uint64_t channel_id;
        uint64_t request_id;

        bool operator==(const RequestId& other) const
        {
            return other.channel_id == channel_id && other.request_id == request_id;
        }

        template<typename H>
        friend H AbslHashValue(H h, const RequestId& request)
        {
            return H::combine(std::move(h), request.channel_id, request.request_id);
        }
    };

    struct DataRequest
    {
        RequestId request_id;
        RequestId layer_loader_request_id{};
        uint32_t layer_id{};
        FeatureSelection feature_selection;
        hrz::vector_data::DataKind data_kind{};
        TaskDependency task;
        uint8_t data_version{};

        DataRequest() = default;
        HRZ_DELETE_COPY(DataRequest);
        HRZ_DEFAULT_MOVE(DataRequest);
        ~DataRequest() = default;
    };

    DataRequest make_data_request(
        RequestId request_id,
        RequestId layer_loader_request_id,
        uint32_t layer_id,
        FeatureSelection&& feature_selection,
        hrz::vector_data::DataKind data_kind)
    {
        DataRequest data_request;
        data_request.request_id = request_id;
        data_request.layer_loader_request_id = layer_loader_request_id;
        data_request.layer_id = layer_id;
        data_request.feature_selection = std::move(feature_selection);
        data_request.data_kind = data_kind;

        return data_request;
    }

    struct LayerLoader
    {
        RequestId request_id;
        uint32_t layer_id;
        TaskDependency load_model_task;
        bool has_full_model_update;
        bool has_max_lod_update;
        uint32_t data_version;
    };

    struct ClientRequestTimeout
    {
        ClientTicket ticket;
        double timeout_date;
    };

    class ClientRequestTimeoutComparator
    {
    public:
        bool operator()(ClientRequestTimeout a, ClientRequestTimeout b)
        {
            return a.timeout_date > b.timeout_date;
        }
    };

    struct ClientRequestHistory
    {
        static constexpr size_t CAPACITY = 128;

        enum class RequestStatus
        {
            Loading,
            Received,
            Canceled,
            Timeout,
            Error,
        };

        struct Entry
        {
            ClientTicket ticket;
            uint32_t layer_id;
            std::optional<TileCoords> tile_coords;
            std::optional<size_t> feature_count;
            std::vector<uint32_t> attribute_ids;
            bool expects_geometry;
            RequestStatus status;
        };

        size_t append(Entry&& entry)
        {
            size_t index = (head + count) % CAPACITY;
            entries[index] = std::move(entry);

            if (count < CAPACITY)
            {
                count++;
            }
            else
            {
                head = (head + 1) % CAPACITY;
            }

            return index;
        }

        void clear()
        {
            head = 0;
            count = 0;
        }

        uint32_t head;
        uint32_t count;

        Entry entries[CAPACITY];
    };

    struct DataInvalidation
    {
        hrz_proto::VectorDataInvalidation proto;

        bool applies_to_source(uint32_t layer_id, uint32_t data_source) const
        {
            return proto.vector_data_layer_id() == layer_id
                && proto.vector_data_source_index() == data_source;
        }

        bool invalidates_everything() const { return proto.has_everything(); }

        bool invalidates_tile(TileCoords tile_coords) const
        {
            return proto.has_tile_coords() && hrz::from_proto(proto.tile_coords()) == tile_coords;
        }

        bool invalidates_feature_ids(const vector_data::FeatureIds& feature_ids) const
        {
            if (!proto.has_feature_ids())
            {
                return false;
            }

            for (const auto& proto_feature_id : proto.feature_ids().feature_ids())
            {
                if (feature_ids.contains(vector_data::FeatureId::from_proto(proto_feature_id)))
                {
                    return true;
                }
            }

            return false;
        }
    };

    enum class TaskType
    {
        // Loads the geometry for a given feature selection (tile coords or list of feature IDs),
        // for one vector data layer.
        LoadGeometry,

        // Loads all attribute values for a given feature selection.
        LoadAllAttributeValues,

        // Loads values of one attribute for a given feature selection.
        LoadAttributeValues,

        // Loads feature IDs for a given feature selection. The IDs are in the canonical order
        // for the layer, i.e. the one of the geometry source, if it exists. There may duplicated
        // feature IDs.
        LoadFeatureIds,

        // Loads geometry, attribute values, and feature IDs for a given feature selection.
        // Everything respects the canonical order of features.
        LoadVectorData,

        // Loads geometry, attribute values, and feature IDs from a vector tile file for a given
        // tile coordinate, and a layer if the format is MVT.
        // The data is not sorted.
        LoadVectorTileData,

        // Loads a vector data file at the given URL, and parses it if the format is MVT.
        // All layers are retained.
        // This helps avoiding parsing the same Protobuf buffer several times when multiple
        // data sources load data from different layers inside the MVT.
        LoadVectorDataUrlPackage,

        // Loads a vector data package from a PMTiles dataset, and parses it from MVT.
        // All layers are retained.
        // This helps avoiding parsing the same Protobuf buffer several times when multiple
        // data sources load data from different layers inside the MVT.
        LoadVectorDataPmTilesPackage,

        // Loads geometry, attribute values, and feature IDs from an untiled vector data source
        // (GeoJSON or Geobuf).
        // Computes a tree of feature bounding boxes, to allow fast intersection tests with tile
        // bounding boxes.
        LoadUntiledVectorData,

        // Loads geometry, attribute values, and feature IDs from untiled vector data source.
        ExtractVectorTileData,

        // Loads geometry, attribute values, and feature IDs from an in-memory vector data layer
        // for a given feature selection.
        // The data is not sorted.
        LoadInMemoryVectorData,

        // Loads a file at the given URL.
        LoadUrlData,

        // Requests geometry, attribute values, and feature IDs from the client.
        // For attribute values, the client must respond with the correct number of values in
        // the same order as the request, so no sorting is needed.
        RequestClientData,

        // Loads a TileJSON file and parses it.
        LoadTileJson,

        // Loads a PMTiles system.
        LoadPmTiles,

        // Loads the model for the given layer. The model may not be usable if such a task has
        // not been executed. (For example, a provider may need to download a descriptor.)
        LoadLayerModel,

        // Loads the model for a given source.
        LoadSourceModel,
    };

    enum class TaskStatus
    {
        New,
        Loading,
        Blocked,
        Loaded,
        Unloaded,

        // The data received was erroneous, or it failed to download.
        // But it may be worth retrying when the data is invalidated.
        DataError,

        // The model definition is wrong, there is nothing we can do
        // until the model itself is updated.
        ModelError
    };

    bool is_loading(TaskStatus status)
    {
        return status == TaskStatus::Loading || status == TaskStatus::Blocked;
    }

    bool is_error(TaskStatus status)
    {
        return status == TaskStatus::DataError || status == TaskStatus::ModelError;
    }

    struct Task
    {
        TaskType type;
        TaskStatus status;
        uint8_t version;
        hrz::flat_hash_set<WeakTaskRef, TaskRef::Hasher> dependents;
        uint32_t data_use_count;

        bool is_active;
        uint64_t hash;

        VectorDataLoader* loader;

        struct LoadGeometry
        {
            LayerModelRef layer_model;
            FeatureSelection feature_selection;
            TaskDependency load_vector_data_task;
            TileGeometryRef geometry;
            AttributionHandle attribution;
        };

        struct LoadAllAttributeValues
        {
            LayerModelRef layer_model;
            FeatureSelection feature_selection;
            std::vector<TaskDependency> attribute_tasks;
        };

        struct LoadAttributeValues
        {
            LayerModelRef layer_model;
            uint32_t attribute_id;
            FeatureSelection feature_selection;
            TaskDependency load_vector_data_task;
            AttributeValueListRef attribute_values;
        };

        struct LoadFeatureIds
        {
            LayerModelRef layer_model;
            FeatureSelection feature_selection;
            TaskDependency load_vector_data_task;
            FeatureIdListRef feature_ids;
        };

        struct LoadVectorData
        {
            LayerModelRef layer_model;
            uint32_t data_source; // Index in the `data_sources` array of the layer model.
            FeatureSelection feature_selection;
            TaskDependency load_vector_tile_data_task;
            TaskDependency load_in_memory_vector_data_task;
            TaskDependency request_client_data_task;
            TaskDependency extract_vector_tile_data_task;
            TaskDependency load_feature_ids_task;
            hrz_jobs::SortVectorDataTicket sort_ticket;
            FeatureIdListRef feature_ids;
            TileGeometryRef geometry;
            AttributionHandle attribution;
            hrz::flat_hash_map<uint32_t, AttributeValueListRef> attribute_ids_to_values;
        };

        struct LoadVectorTileData
        {
            LayerModelRef layer_model;
            uint32_t data_source;
            FeatureSelection feature_selection;
            TaskDependency load_vector_data_url_package_task;
            TaskDependency load_vector_data_pmtiles_package_task;
            hrz_jobs::DecodeVectorTileTicket decode_ticket;
            FeatureIdListRef feature_ids;
            TileGeometryRef geometry;
            AttributionHandle attribution;
            hrz::flat_hash_map<uint32_t, AttributeValueListRef> attribute_ids_to_values;
        };

        struct LoadVectorDataUrlPackage
        {
            std::string url;
            hrz::HttpHeaders headers;
            hrz::assets_loader::Queue queue;
            uint32_t priority;
            hrz_proto::VectorDataFormat format;
            monitoring::ResourceOwner resource_owner;
            metrics::MetricDesc request_count_metric;
            TaskDependency load_url_data_task;
            hrz_jobs::ParseMvtTicket parse_ticket;
            std::optional<hrz::vector_data::VectorDataPackage> package;
        };

        struct LoadVectorDataPmTilesPackage
        {
            std::string url;
            hrz::HttpHeaders headers;
            TileCoords tile_coords;
            hrz::assets_loader::Queue queue;
            uint32_t priority;
            monitoring::ResourceOwner resource_owner;
            metrics::MetricDesc request_count_metric;

            TaskDependency load_pmtiles_task;
            std::optional<PmTiles::QueryHandle> tile_query;
            hrz_jobs::ParseMvtTicket parse_ticket;

            std::optional<hrz::vector_data::VectorDataPackage> package;
        };

        struct LoadUntiledVectorData
        {
            LayerModelRef layer_model;
            uint32_t data_source;
            TaskDependency load_vector_tile_data_task;
            hrz_jobs::BuildAabbTreeTicket build_aabb_tree_ticket;
            hrz::vector_data::AabbTree aabb_tree;
            FeatureIdListRef feature_ids;
            TileGeometryRef geometry;
            AttributionHandle attribution;
            hrz::flat_hash_map<uint32_t, AttributeValueListRef> attribute_ids_to_values;
        };

        struct ExtractVectorTileData
        {
            LayerModelRef layer_model;
            uint32_t data_source;
            TileCoords tile_coords;
            TaskDependency load_untiled_vector_tile_data_task;
            hrz_jobs::ExtractVectorTileTicket extract_ticket;
            FeatureIdListRef feature_ids;
            TileGeometryRef geometry;
            AttributionHandle attribution;
            hrz::flat_hash_map<uint32_t, AttributeValueListRef> attribute_ids_to_values;
        };

        struct LoadInMemoryVectorData
        {
            LayerModelRef layer_model;
            uint32_t data_source;
            FeatureSelection feature_selection;
            TaskDependency load_feature_ids_task;
            std::optional<uint8_t> load_feature_ids_task_version;
            std::optional<uint64_t> in_memory_vector_data_request_id;
            FeatureIdListRef feature_ids;
            TileGeometryRef geometry;
            AttributionHandle attribution;
            hrz::flat_hash_map<uint32_t, AttributeValueListRef> attribute_ids_to_values;
        };

        struct LoadUrlData
        {
            std::string url;
            hrz::HttpHeaders headers;
            hrz::assets_loader::Queue queue;
            uint32_t priority;
            monitoring::ResourceOwner resource_owner;
            metrics::MetricDesc request_count_metric;
            std::optional<uint64_t> download_request_id;
            blobs::BlobHandle blob;
        };

        struct RequestClientData
        {
            LayerModelRef layer_model;
            uint32_t data_source;
            FeatureSelection feature_selection;
            std::vector<hrz::vector_data::AttributeModel> attributes;

            TaskDependency load_feature_ids_task;

            ClientTicket client_ticket;
            std::optional<hrz_proto::VectorDataRequestResponse> client_response;
            size_t request_history_index;

            hrz_jobs::MoveClientVectorDataToBlobsTicket move_to_blobs_ticket;
            TileGeometryRef geometry;
            AttributionHandle attribution;
            FeatureIdListRef feature_ids;
            hrz::flat_hash_map<uint32_t, AttributeValueListRef> attribute_ids_to_values;
        };

        struct LoadTileJson
        {
            std::string url;
            hrz::HttpHeaders headers;
            hrz::assets_loader::Queue queue;
            uint32_t priority;
            bool preserve_query_parameters;
            monitoring::ResourceOwner resource_owner;
            metrics::MetricDesc request_count_metric;
            TaskDependency load_url_data_task;
            hrz::MultiPatternTileUrlGenerator tile_url_generator;
            uint32_t min_level;
            uint32_t max_level;
            hrz::GeoBounds bounds;
            AttributionHandle attribution;
        };

        struct LoadPmTiles
        {
            std::string url;
            hrz::HttpHeaders headers;
            hrz::assets_loader::Queue queue;
            uint32_t priority;
            monitoring::ResourceOwner resource_owner;
            metrics::MetricDesc request_count_metric;
            std::optional<uint64_t> asset_loader_channel_request_id;
            std::unique_ptr<PmTiles> pmtiles;
        };

        struct LoadLayerModel
        {
            uint32_t layer_id;
            LayerModelRef layer_model;
            std::vector<TaskDependency> load_source_model_tasks;
        };

        struct LoadSourceModel
        {
            LayerModelRef layer_model;
            uint32_t data_source;
            TaskDependency load_tilejson_task;
            TaskDependency load_pmtiles_task;
            TaskDependency load_untiled_vector_data_task;
        };

        std::variant<
            LoadGeometry,
            LoadAllAttributeValues,
            LoadAttributeValues,
            LoadFeatureIds,
            LoadVectorData,
            LoadVectorTileData,
            LoadVectorDataUrlPackage,
            LoadVectorDataPmTilesPackage,
            LoadUntiledVectorData,
            ExtractVectorTileData,
            LoadInMemoryVectorData,
            LoadUrlData,
            RequestClientData,
            LoadTileJson,
            LoadPmTiles,
            LoadLayerModel,
            LoadSourceModel>
            params;

        LoadGeometry& load_geometry() { return std::get<LoadGeometry>(params); }

        LoadAllAttributeValues& load_all_attribute_values()
        {
            return std::get<LoadAllAttributeValues>(params);
        }

        LoadAttributeValues& load_attribute_values()
        {
            return std::get<LoadAttributeValues>(params);
        }

        LoadFeatureIds& load_feature_ids() { return std::get<LoadFeatureIds>(params); }

        LoadVectorData& load_vector_data() { return std::get<LoadVectorData>(params); }

        LoadVectorTileData& load_vector_tile_data() { return std::get<LoadVectorTileData>(params); }

        LoadVectorDataUrlPackage& load_vector_data_url_package()
        {
            return std::get<LoadVectorDataUrlPackage>(params);
        }

        LoadVectorDataPmTilesPackage& load_vector_data_pmtiles_package()
        {
            return std::get<LoadVectorDataPmTilesPackage>(params);
        }

        LoadUntiledVectorData& load_untiled_vector_data()
        {
            return std::get<LoadUntiledVectorData>(params);
        }

        ExtractVectorTileData& extract_vector_tile_data()
        {
            return std::get<ExtractVectorTileData>(params);
        }

        LoadInMemoryVectorData& load_in_memory_vector_data()
        {
            return std::get<LoadInMemoryVectorData>(params);
        }

        LoadUrlData& load_url_data() { return std::get<LoadUrlData>(params); }

        RequestClientData& request_client_data() { return std::get<RequestClientData>(params); }

        LoadTileJson& load_tilejson() { return std::get<LoadTileJson>(params); }

        LoadPmTiles& load_pmtiles() { return std::get<LoadPmTiles>(params); }

        LoadLayerModel& load_layer_model() { return std::get<LoadLayerModel>(params); }

        LoadSourceModel& load_source_model() { return std::get<LoadSourceModel>(params); }

        const LoadGeometry& load_geometry() const { return std::get<LoadGeometry>(params); }

        const LoadAllAttributeValues& load_all_attribute_values() const
        {
            return std::get<LoadAllAttributeValues>(params);
        }

        const LoadAttributeValues& load_attribute_values() const
        {
            return std::get<LoadAttributeValues>(params);
        }

        const LoadFeatureIds& load_feature_ids() const { return std::get<LoadFeatureIds>(params); }

        const LoadVectorData& load_vector_data() const { return std::get<LoadVectorData>(params); }

        const LoadVectorTileData& load_vector_tile_data() const
        {
            return std::get<LoadVectorTileData>(params);
        }

        const LoadVectorDataUrlPackage& load_vector_data_url_package() const
        {
            return std::get<LoadVectorDataUrlPackage>(params);
        }

        const LoadVectorDataPmTilesPackage& load_vector_data_pmtiles_package() const
        {
            return std::get<LoadVectorDataPmTilesPackage>(params);
        }

        const LoadUntiledVectorData& load_untiled_vector_data() const
        {
            return std::get<LoadUntiledVectorData>(params);
        }

        const ExtractVectorTileData& extract_vector_tile_data() const
        {
            return std::get<ExtractVectorTileData>(params);
        }

        const LoadInMemoryVectorData& load_in_memory_vector_data() const
        {
            return std::get<LoadInMemoryVectorData>(params);
        }

        const LoadUrlData& load_url_data() const { return std::get<LoadUrlData>(params); }

        const RequestClientData& request_client_data() const
        {
            return std::get<RequestClientData>(params);
        }

        const LoadTileJson& load_tilejson() const { return std::get<LoadTileJson>(params); }

        const LoadPmTiles& load_pmtiles() const { return std::get<LoadPmTiles>(params); }

        const LoadLayerModel& load_layer_model() const { return std::get<LoadLayerModel>(params); }

        const LoadSourceModel& load_source_model() const
        {
            return std::get<LoadSourceModel>(params);
        }
    };

    LayerModelPool layer_models;
    FeatureIdListPool feature_id_lists;
    AttributeValueListPool attribute_values;
    TileGeometryPool tile_geometries;

    hrz::vector_data::FeatureIds empty_feature_ids_list;
    TileGeometry empty_geometry;

    hrz::flat_hash_map<RequestId, LayerLoader> request_ids_to_layer_loaders;
    std::unordered_multimap<uint32_t, RequestId> layer_ids_to_request_ids;

    hrz::flat_hash_map<RequestId, DataRequest> request_ids_to_data_requests;
    std::unordered_multimap<WeakTaskRef, RequestId, WeakTaskRef::Hasher> tasks_to_data_request_ids;

    // Declared here to avoid re-instantiating the set each frame.
    hrz::flat_hash_set<uint32_t> updated_layers;

    hrz::flat_hash_set<uint64_t> created_model_layers;
    hrz::flat_hash_set<uint64_t> destroyed_model_layers;
    hrz::flat_hash_set<uint64_t> updated_model_layers;
    std::multimap<uint64_t, uint32_t> updated_headers_model_layers;
    hrz::flat_hash_map<uint64_t, uint32_t> layer_handles_to_layer_ids;
    hrz::flat_hash_map<uint32_t, uint64_t> layer_ids_to_active_layer_handles;
    std::mutex model_mutex;

    std::vector<DataInvalidation> invalidations;

    TaskPool tasks;
    // Used to handle tasks deduplication in get_or_create_*_task() function.
    hrz::flat_hash_map<uint64_t, WeakTaskRef> tasks_by_hash;
    std::vector<WeakTaskRef> active_tasks;

    hrz::flat_hash_map<ClientTicket, WeakTaskRef> client_tickets_to_tasks;
    std::priority_queue<
        ClientRequestTimeout,
        std::vector<ClientRequestTimeout>,
        ClientRequestTimeoutComparator>
        client_tickets_timeouts;

    ClientRequestHistory client_request_history;

    assets_loader::Channel asset_loader_channel;
    uint64_t next_download_request_id = 0;
    hrz::flat_hash_map<uint64_t, WeakTaskRef> tasks_waiting_for_asset_loader_message;

    in_memory::InMemoryChannel in_memory_vector_data_channel;
    uint64_t next_in_memory_vector_data_request_id = 0;
    hrz::flat_hash_map<uint64_t, WeakTaskRef> tasks_waiting_for_in_memory_vector_data_message;

    ChannelGroup<vector_data::FromLoaderMessage, vector_data::ToLoaderMessage> channels;

    std::mutex dev_ui_mutex;

    std::string make_url(
        const hrz::PatternTileUrlGenerator& tile_url_generator,
        hrz::TileCoords tile_coords)
    {
        return tile_url_generator.make_url(tile_coords.x, tile_coords.y, tile_coords.lod);
    }

    std::string make_url(
        const hrz::MultiPatternTileUrlGenerator& tile_url_generator,
        hrz::TileCoords tile_coords)
    {
        return tile_url_generator.make_url(tile_coords.x, tile_coords.y, tile_coords.lod);
    }

    hrz::assets_loader::Queue get_load_queue(const LayerModel& model)
    {
        return hrz::get_request_queue(
            model.loading_priority, hrz::assets_loader::Queue::VectorData);
    }

    uint32_t compute_tile_loading_priority(const LayerModel& model, hrz::TileCoords tile_coords)
    {
        // Load high resolution tiles before low resolution ones.
        // High resolution tiles are normally closer to the camera, and roughly at the
        // bottom-centre of the screen (when the view is tilted), or at the centre (when
        // the view is straight down).
        // Low resolution tiles are more peripheral.
        return hrz::combine_loading_priorities(model.loading_priority, tile_coords.lod);
    }

    const char* get_vector_data_provider_name(hrz_proto::VectorDataProviderType provider_type)
    {
        switch (provider_type)
        {
            case hrz_proto::VectorDataProviderType::TILED_VECTOR_DATA_PROVIDER:
                return "Tiled vector data";
            case hrz_proto::VectorDataProviderType::CLIENT_VECTOR_DATA_PROVIDER:
                return "Client vector data";
            case hrz_proto::VectorDataProviderType::IN_MEMORY_VECTOR_DATA_PROVIDER:
                return "In-memory vector data";
            case hrz_proto::VectorDataProviderType::TILEJSON_VECTOR_DATA_PROVIDER:
                return "TileJSON vector data";
            case hrz_proto::VectorDataProviderType::UNTILED_VECTOR_DATA_PROVIDER:
                return "Untiled vector data";
            case hrz_proto::VectorDataProviderType::PMTILES_VECTOR_DATA_PROVIDER:
                return "PMTiles vector data";
            default: assert(false && "Unhandled case");
        }
        return "";
    }

    LayerModel make_model(
        AttributionRegistry* attributions,
        const hrz_proto::VectorDataLayer& layer,
        uint64_t layer_handle)
    {
        LayerModel model;
        model.layer_handle = layer_handle;
        model.id = layer.id();
        model.data_version = 0;
        model.loading_priority = hrz::clamp_cast<int32_t, int8_t>(layer.loading_priority());
        model.geometry_source = LayerModel::NO_SOURCE;
        model.feature_id_source = LayerModel::NO_SOURCE;

        bool has_warned_about_multiple_sources_for_feature_ids = false;
        bool has_warned_about_multiple_sources_for_geometry = false;

        for (uint32_t s = 0; s < (uint32_t)layer.sources_size(); ++s)
        {
            const auto& source = layer.sources(s);

            LayerModel::Source model_source;

            if (source.provider_type()
                == hrz_proto::VectorDataProviderType::TILED_VECTOR_DATA_PROVIDER)
            {
                const auto& provider = source.tiled_data_provider();

                LayerModel::Source::TiledDataProvider model_provider;
                model_provider.tile_url_generator =
                    hrz::PatternTileUrlGenerator(provider.url_pattern(), 1);
                model_provider.headers = hrz::assets_loader::from_proto(provider.http_headers());
                model_provider.format = provider.format();
                model_provider.layer_name = provider.layer_name();
                model_provider.min_lod = provider.min_level();
                model_provider.max_lod = provider.max_level();
                model_provider.bounds = hrz::from_proto(provider.bounds());
                model_provider.attribution =
                    attribution::register_attribution(attributions, {provider.attribution(), ""});

                model_source.provider = {model_provider};

                model_source.request_count_metric.push_label("url_pattern", provider.url_pattern());
            }
            else if (
                source.provider_type()
                == hrz_proto::VectorDataProviderType::CLIENT_VECTOR_DATA_PROVIDER)
            {
                const auto& provider = source.client_data_provider();

                LayerModel::Source::ClientDataProvider model_provider;
                model_provider.access = provider.access();

                if (provider.timeout() != 0)
                {
                    model_provider.timeout_duration = {provider.timeout() * 1000};
                }

                model_provider.min_lod = provider.min_level();
                model_provider.max_lod = provider.max_level();
                model_provider.bounds = hrz::from_proto(provider.bounds());

                model_source.provider = {model_provider};
            }
            else if (
                source.provider_type()
                == hrz_proto::VectorDataProviderType::IN_MEMORY_VECTOR_DATA_PROVIDER)
            {
                const auto& provider = source.in_memory_data_provider();

                LayerModel::Source::InMemoryDataProvider model_provider;
                model_provider.in_memory_layer_id = provider.in_memory_layer_id();

                model_source.provider = {model_provider};

                model_source.request_count_metric.push_label(
                    "layer_id", fmt::format("{}", model_provider.in_memory_layer_id));
            }
            else if (
                source.provider_type()
                == hrz_proto::VectorDataProviderType::TILEJSON_VECTOR_DATA_PROVIDER)
            {
                const auto& provider = source.tilejson_data_provider();

                model_source.request_count_metric.push_label("url", provider.url());

                LayerModel::Source::TileJsonDataProvider model_provider;
                model_provider.url = provider.url();
                model_provider.headers = hrz::assets_loader::from_proto(provider.http_headers());
                model_provider.preserve_query_parameters = provider.preserve_query_parameters();
                model_provider.layer_name = provider.layer_name();
                model_provider.attribution =
                    attribution::register_attribution(attributions, {provider.attribution(), ""});
                model_provider.load_tilejson_task =
                    TaskDependency::on_task(get_or_create_load_tilejson_task(
                        provider.url(), model_provider.headers, get_load_queue(model),
                        hrz::combine_loading_priorities(
                            model.loading_priority, std::numeric_limits<uint16_t>::max()),
                        provider.preserve_query_parameters(),
                        hrz::monitoring::ResourceOwner(
                            hrz::monitoring::systems::VectorDataLoader, model.layer_handle),
                        model_source.request_count_metric));

                model_provider.load_tilejson_task.retain_data();

                model_source.provider = {std::move(model_provider)};
            }
            else if (
                source.provider_type()
                == hrz_proto::VectorDataProviderType::PMTILES_VECTOR_DATA_PROVIDER)
            {
                const auto& provider = source.pmtiles_data_provider();

                model_source.request_count_metric.push_label("url", provider.url());

                LayerModel::Source::PmTilesDataProvider model_provider;
                model_provider.url = provider.url();
                model_provider.headers = hrz::assets_loader::from_proto(provider.http_headers());
                model_provider.layer_name = provider.layer_name();
                model_provider.attribution =
                    attribution::register_attribution(attributions, {provider.attribution(), ""});
                model_provider.load_pmtiles_task =
                    TaskDependency::on_task(get_or_create_load_pmtiles_task(
                        provider.url(), model_provider.headers, get_load_queue(model),
                        hrz::combine_loading_priorities(
                            model.loading_priority, std::numeric_limits<uint16_t>::max()),
                        hrz::monitoring::ResourceOwner(
                            hrz::monitoring::systems::VectorDataLoader, model.layer_handle),
                        model_source.request_count_metric));

                model_provider.load_pmtiles_task.retain_data();

                model_source.provider = {std::move(model_provider)};
            }
            else if (
                source.provider_type()
                == hrz_proto::VectorDataProviderType::UNTILED_VECTOR_DATA_PROVIDER)
            {
                const auto& provider = source.untiled_data_provider();

                LayerModel::Source::UntiledDataProvider model_provider;
                model_provider.url = provider.url();
                model_provider.headers = hrz::assets_loader::from_proto(provider.http_headers());
                model_provider.format = provider.format();
                model_provider.tolerance = provider.tolerance();
                model_provider.clip_margin = provider.clip_margin();
                model_provider.min_lod = provider.min_level();
                model_provider.max_lod = provider.max_level();
                model_provider.bounds = hrz::from_proto(provider.bounds());
                model_provider.attribution =
                    attribution::register_attribution(attributions, {provider.attribution(), ""});

                model_source.provider = {model_provider};

                model_source.request_count_metric.push_label("url", provider.url());
            }
            else
            {
                assert(false && "Unhandled case");
            }

            model_source.has_geometry = source.has_geometry();
            model_source.source_feature_id_attribute = std::nullopt;
            model_source.request_count_metric = metrics::MetricDesc(
                "Vector data (requests tally)", false,
                {{"layer", std::to_string(layer_handle)},
                 {"source", std::to_string(s)},
                 {"format", get_vector_data_provider_name(source.provider_type())}});

            if (model_source.has_geometry)
            {
                if (model.geometry_source == LayerModel::NO_SOURCE)
                {
                    model.geometry_source = s;
                }
                else if (!has_warned_about_multiple_sources_for_geometry)
                {
                    HRZ_LOG_WARNING(
                        "Multiple data sources with geometry. Only the first one will be used.");
                    has_warned_about_multiple_sources_for_geometry = true;
                }
            }

            for (uint32_t a = 0; a < (uint32_t)source.attributes_size(); ++a)
            {
                const auto& attribute = source.attributes(a);

                LayerModel::Attribute model_attribute;
                model_attribute.id = attribute.id();
                model_attribute.transform = attribute.transform();
                model_attribute.is_source_feature_ids = attribute.is_source_feature_ids();
                model_attribute.name_in_source = attribute.source_name();
                model_attribute.is_feature_id = attribute.is_feature_id();
                model_attribute.data_source = s;

                if (model.attributes.find(model_attribute.id) != model.attributes.end())
                {
                    HRZ_LOG_WARNING(
                        "Duplicate attribute id {} in layer {}", model_attribute.id, model.id);
                }

                if (model_attribute.is_source_feature_ids)
                {
                    if (!model_source.source_feature_id_attribute.has_value())
                    {
                        model_source.source_feature_id_attribute = {model_attribute.id};
                    }
                    else
                    {
                        HRZ_LOG_WARNING(
                            "Multiple attributes declared as source feature ID in layer {}: {} and "
                            "{}",
                            model.id, model_source.source_feature_id_attribute.value(),
                            model_attribute.id);
                    }
                }

                if (model_attribute.is_feature_id)
                {
                    if (model.feature_id_source == LayerModel::NO_SOURCE)
                    {
                        model.feature_id_source = s;
                    }
                    else if (
                        model.feature_id_source != s
                        && !has_warned_about_multiple_sources_for_feature_ids)
                    {
                        HRZ_LOG_WARNING(
                            "Multiple data sources with feature IDs. Only the first one will be "
                            "used.");
                        has_warned_about_multiple_sources_for_feature_ids = true;
                    }
                }

                model.attributes.insert({model_attribute.id, model_attribute});
            }

            model.data_sources.push_back(std::move(model_source));
        }

        return model;
    }

    void load_attribute_data_into_map(
        std::vector<vector_data::AttributeValues>& attributes,
        hrz::flat_hash_map<uint32_t, AttributeValueListRef>& attribute_ids_to_values,
        const LayerModel& layer_model,
        size_t expected_value_count)
    {
        for (size_t a = 0; a < attributes.size(); ++a)
        {
            auto& attribute = attributes[a];

            {
                const auto& it = layer_model.attributes.find(attribute.attribute_id);

                if (it == layer_model.attributes.end())
                {
                    // Unknown attribute
                    continue;
                }
            }

            assert(attribute.values.size() == expected_value_count);
            if (attribute.values.size() != expected_value_count)
            {
                continue;
            }

            auto values_ref = attribute_values.alloc();
            auto& values = values_ref.value();
            values.attribute_id = attribute.attribute_id;
            values.values = std::move(attribute.values);
            values.out_of_line_data = std::move(attribute.out_of_line_data);

            attribute_ids_to_values.insert_or_assign(attribute.attribute_id, values_ref);
        }
    }

    template<typename TaskDataType>
    static void bump_task_priority(
        Task& task,
        TaskDataType& task_data,
        hrz::assets_loader::Queue queue,
        uint32_t priority)
    {
        if (task.status == TaskStatus::New)
        {
            // If no data has been downloaded yet however,
            // we bump the priority if applicable.
            if (task_data.queue == queue && task_data.priority < priority)
            {
                task_data.priority = priority;
            }
            if (task_data.queue != hrz::assets_loader::Queue::Early
                && queue == hrz::assets_loader::Queue::Early)
            {
                task_data.queue = queue;
                task_data.priority = priority;
            }
        }
    }

    TaskRef get_or_create_load_geometry_task(
        const LayerModelRef& layer_model,
        const FeatureSelection& feature_selection)
    {
        uint64_t hash = hrz::hash_value(TaskType::LoadGeometry);
        hash = hrz::hash_mix<uint64_t>(hash, layer_model.get_handle().hash());
        hash = hrz::hash_mix<uint64_t>(hash, feature_selection.hash());

        auto it = tasks_by_hash.find(hash);
        if (it != tasks_by_hash.end() && it->second.is_valid())
        {
            const auto& task = it->second.value();
            if (task.type == TaskType::LoadGeometry)
            {
                const auto& task_data = task.load_geometry();
                if (task_data.layer_model == layer_model
                    && task_data.feature_selection == feature_selection)
                {
                    return it->second;
                }
            }
        }

        auto new_task_ref = tasks.alloc();
        Task& new_task = new_task_ref.value();
        new_task.type = TaskType::LoadGeometry;
        new_task.loader = this;
        new_task.status = TaskStatus::New;
        new_task.version = 0;
        new_task.data_use_count = 0;
        new_task.is_active = false;
        Task::LoadGeometry task_data;
        task_data.layer_model = layer_model;
        task_data.feature_selection = feature_selection;
        task_data.load_vector_data_task = {};
        task_data.geometry = TileGeometryRef{};
        task_data.attribution = {};
        new_task.params = std::move(task_data);
        new_task.hash = hash;

        tasks_by_hash.insert({new_task.hash, new_task_ref.make_weak_ref()});

        activate_task(new_task_ref.make_weak_ref());

        return new_task_ref;
    }

    TaskRef get_or_create_load_all_attributes_task(
        const LayerModelRef& layer_model,
        const FeatureSelection& feature_selection)
    {
        uint64_t hash = hrz::hash_value(TaskType::LoadAllAttributeValues);
        hash = hrz::hash_mix<uint64_t>(hash, layer_model.get_handle().hash());
        hash = hrz::hash_mix<uint64_t>(hash, feature_selection.hash());

        auto it = tasks_by_hash.find(hash);
        if (it != tasks_by_hash.end() && it->second.is_valid())
        {
            const auto& task = it->second.value();
            if (task.type == TaskType::LoadAllAttributeValues)
            {
                const auto& task_data = task.load_all_attribute_values();
                if (task_data.layer_model == layer_model
                    && task_data.feature_selection == feature_selection)
                {
                    return it->second;
                }
            }
        }

        auto new_task_ref = tasks.alloc();
        Task& new_task = new_task_ref.value();
        new_task.type = TaskType::LoadAllAttributeValues;
        new_task.loader = this;
        new_task.status = TaskStatus::New;
        new_task.version = 0;
        new_task.data_use_count = 0;
        new_task.is_active = false;
        Task::LoadAllAttributeValues task_data;
        task_data.layer_model = layer_model;
        task_data.feature_selection = feature_selection;
        new_task.params = std::move(task_data);
        new_task.hash = hash;

        tasks_by_hash.insert({new_task.hash, new_task_ref.make_weak_ref()});

        activate_task(new_task_ref.make_weak_ref());

        return new_task_ref;
    }

    TaskRef get_or_create_load_attribute_values_task(
        const LayerModelRef& layer_model,
        uint32_t attribute_id,
        const FeatureSelection& feature_selection)
    {
        uint64_t hash = hrz::hash_value(TaskType::LoadAttributeValues);
        hash = hrz::hash_mix<uint64_t>(hash, layer_model.get_handle().hash());
        hash = hrz::hash_mix<uint64_t>(hash, hrz::hash_value(attribute_id));
        hash = hrz::hash_mix<uint64_t>(hash, feature_selection.hash());

        auto it = tasks_by_hash.find(hash);
        if (it != tasks_by_hash.end() && it->second.is_valid())
        {
            const auto& task = it->second.value();
            if (task.type == TaskType::LoadAttributeValues)
            {
                const auto& task_data = task.load_attribute_values();
                if (task_data.layer_model == layer_model && task_data.attribute_id == attribute_id
                    && task_data.feature_selection == feature_selection)
                {
                    return it->second;
                }
            }
        }

        auto new_task_ref = tasks.alloc();
        Task& new_task = new_task_ref.value();
        new_task.type = TaskType::LoadAttributeValues;
        new_task.loader = this;
        new_task.status = TaskStatus::New;
        new_task.version = 0;
        new_task.data_use_count = 0;
        new_task.is_active = false;
        Task::LoadAttributeValues task_data;
        task_data.layer_model = layer_model;
        task_data.attribute_id = attribute_id;
        task_data.feature_selection = feature_selection;
        task_data.load_vector_data_task = {};
        task_data.attribute_values = AttributeValueListRef{};
        new_task.params = std::move(task_data);
        new_task.hash = hash;

        tasks_by_hash.insert({new_task.hash, new_task_ref.make_weak_ref()});

        activate_task(new_task_ref.make_weak_ref());

        return new_task_ref;
    }

    TaskRef get_or_create_load_feature_ids_task(
        const LayerModelRef& layer_model,
        const FeatureSelection& feature_selection)
    {
        uint64_t hash = hrz::hash_value(TaskType::LoadFeatureIds);
        hash = hrz::hash_mix<uint64_t>(hash, layer_model.get_handle().hash());
        hash = hrz::hash_mix<uint64_t>(hash, feature_selection.hash());

        auto it = tasks_by_hash.find(hash);
        if (it != tasks_by_hash.end() && it->second.is_valid())
        {
            const auto& task = it->second.value();
            if (task.type == TaskType::LoadFeatureIds)
            {
                const auto& task_data = task.load_feature_ids();
                if (task_data.layer_model == layer_model
                    && task_data.feature_selection == feature_selection)
                {
                    return it->second;
                }
            }
        }

        auto new_task_ref = tasks.alloc();
        Task& new_task = new_task_ref.value();
        new_task.type = TaskType::LoadFeatureIds;
        new_task.loader = this;
        new_task.status = TaskStatus::New;
        new_task.version = 0;
        new_task.data_use_count = 0;
        new_task.is_active = false;
        Task::LoadFeatureIds task_data;
        task_data.layer_model = layer_model;
        task_data.feature_selection = feature_selection;
        task_data.load_vector_data_task = {};
        task_data.feature_ids = FeatureIdListRef{};
        new_task.params = std::move(task_data);
        new_task.hash = hash;

        tasks_by_hash.insert({new_task.hash, new_task_ref.make_weak_ref()});

        activate_task(new_task_ref.make_weak_ref());

        return new_task_ref;
    }

    TaskRef get_or_create_load_vector_data_task(
        const LayerModelRef& layer_model,
        uint32_t data_source,
        const FeatureSelection& feature_selection)
    {
        uint64_t hash = hrz::hash_value(TaskType::LoadVectorData);
        hash = hrz::hash_mix<uint64_t>(hash, layer_model.get_handle().hash());
        hash = hrz::hash_mix<uint64_t>(hash, hrz::hash_value(data_source));
        hash = hrz::hash_mix<uint64_t>(hash, feature_selection.hash());

        auto it = tasks_by_hash.find(hash);
        if (it != tasks_by_hash.end() && it->second.is_valid())
        {
            const auto& task = it->second.value();
            if (task.type == TaskType::LoadVectorData)
            {
                const auto& task_data = task.load_vector_data();
                if (task_data.layer_model == layer_model && task_data.data_source == data_source
                    && task_data.feature_selection == feature_selection)
                {
                    return it->second;
                }
            }
        }

        auto new_task_ref = tasks.alloc();
        Task& new_task = new_task_ref.value();
        new_task.type = TaskType::LoadVectorData;
        new_task.loader = this;
        new_task.status = TaskStatus::New;
        new_task.version = 0;
        new_task.data_use_count = 0;
        new_task.is_active = false;
        Task::LoadVectorData task_data;
        task_data.layer_model = layer_model;
        task_data.data_source = data_source;
        task_data.feature_selection = feature_selection;
        task_data.load_vector_tile_data_task = {};
        task_data.load_in_memory_vector_data_task = {};
        task_data.request_client_data_task = {};
        task_data.extract_vector_tile_data_task = {};
        task_data.load_feature_ids_task = {};
        task_data.feature_ids = FeatureIdListRef{};
        task_data.geometry = TileGeometryRef{};
        task_data.attribution = {};
        new_task.params = std::move(task_data);
        new_task.hash = hash;

        tasks_by_hash.insert({new_task.hash, new_task_ref.make_weak_ref()});

        activate_task(new_task_ref.make_weak_ref());

        return new_task_ref;
    }

    TaskRef get_or_create_load_vector_tile_data_task(
        const LayerModelRef& layer_model,
        uint32_t data_source,
        const FeatureSelection& feature_selection)
    {
        uint64_t hash = hrz::hash_value(TaskType::LoadVectorTileData);
        hash = hrz::hash_mix<uint64_t>(hash, layer_model.get_handle().hash());
        hash = hrz::hash_mix<uint64_t>(hash, hrz::hash_value(data_source));
        hash = hrz::hash_mix<uint64_t>(hash, feature_selection.hash());

        auto it = tasks_by_hash.find(hash);
        if (it != tasks_by_hash.end() && it->second.is_valid())
        {
            const auto& task = it->second.value();
            if (task.type == TaskType::LoadVectorTileData)
            {
                const auto& task_data = task.load_vector_tile_data();
                if (task_data.layer_model == layer_model && task_data.data_source == data_source
                    && task_data.feature_selection == feature_selection)
                {
                    return it->second;
                }
            }
        }

        auto new_task_ref = tasks.alloc();
        Task& new_task = new_task_ref.value();
        new_task.type = TaskType::LoadVectorTileData;
        new_task.loader = this;
        new_task.status = TaskStatus::New;
        new_task.version = 0;
        new_task.data_use_count = 0;
        new_task.is_active = false;
        Task::LoadVectorTileData task_data;
        task_data.layer_model = layer_model;
        task_data.data_source = data_source;
        task_data.feature_selection = feature_selection;
        task_data.load_vector_data_url_package_task = {};
        task_data.load_vector_data_pmtiles_package_task = {};
        task_data.feature_ids = FeatureIdListRef{};
        task_data.geometry = TileGeometryRef{};
        task_data.attribution = {};
        new_task.params = std::move(task_data);
        new_task.hash = hash;

        tasks_by_hash.insert({new_task.hash, new_task_ref.make_weak_ref()});

        activate_task(new_task_ref.make_weak_ref());

        return new_task_ref;
    }

    TaskRef get_or_create_load_vector_data_url_package_task(
        const std::string_view& url,
        const hrz::HttpHeaders& headers,
        hrz::assets_loader::Queue queue,
        uint32_t priority,
        hrz_proto::VectorDataFormat format,
        const monitoring::ResourceOwner& resource_owner,
        const metrics::MetricDesc& request_count_metric)
    {
        // Don’t check on the queue and priority. We want to reuse
        // the task even if only the URL is the same.
        // If a task is reused, the metrics and resource ownership information
        // may become incomplete, as they would only mention one data source
        // on one vector data layer, but at least the actual work isn't
        // duplicated.

        uint64_t hash = hrz::hash_value(TaskType::LoadVectorDataUrlPackage);
        hash = hrz::hash_mix<uint64_t>(hash, hrz::murmur3_x64_64(url));
        hash = hrz::hash_mix<uint64_t>(hash, headers.hash_content());
        hash = hrz::hash_mix<uint64_t>(hash, hrz::hash_value(format));

        auto it = tasks_by_hash.find(hash);
        if (it != tasks_by_hash.end() && it->second.is_valid())
        {
            auto& task = it->second.value();

            if (task.type == TaskType::LoadVectorDataUrlPackage)
            {
                auto& task_data = task.load_vector_data_url_package();
                if (task_data.url == url
                    && task_data.headers.hash_content() == headers.hash_content()
                    && task_data.format == format)
                {
                    bump_task_priority(task, task_data, queue, priority);

                    return it->second;
                }
            }
        }

        auto new_task_ref = tasks.alloc();
        Task& new_task = new_task_ref.value();
        new_task.type = TaskType::LoadVectorDataUrlPackage;
        new_task.loader = this;
        new_task.status = TaskStatus::New;
        new_task.version = 0;
        new_task.data_use_count = 0;
        new_task.is_active = false;
        Task::LoadVectorDataUrlPackage task_data;
        task_data.url = {url.data(), url.size()};
        task_data.format = format;
        task_data.headers = headers;
        task_data.queue = queue;
        task_data.priority = priority;
        task_data.resource_owner = resource_owner;
        task_data.request_count_metric = request_count_metric;
        task_data.package = std::nullopt;
        new_task.params = std::move(task_data);
        new_task.hash = hash;

        tasks_by_hash.insert({new_task.hash, new_task_ref.make_weak_ref()});

        activate_task(new_task_ref.make_weak_ref());

        return new_task_ref;
    }

    TaskRef get_or_create_load_vector_data_pmtiles_package_task(
        const std::string_view& url,
        const hrz::HttpHeaders& headers,
        const TileCoords& tile_coords,
        hrz::assets_loader::Queue queue,
        uint32_t priority,
        const monitoring::ResourceOwner& resource_owner,
        const metrics::MetricDesc& request_count_metric)
    {
        uint64_t hash = hrz::hash_values(
            TaskType::LoadVectorDataPmTilesPackage, hrz::murmur3_x64_64(url),
            headers.hash_content(), tile_coords);

        auto it = tasks_by_hash.find(hash);
        if (it != tasks_by_hash.end() && it->second.is_valid())
        {
            auto& task = it->second.value();

            if (task.type == TaskType::LoadVectorDataPmTilesPackage)
            {
                auto& task_data = task.load_vector_data_pmtiles_package();
                if (task_data.url == url
                    && task_data.headers.hash_content() == headers.hash_content()
                    && task_data.tile_coords == tile_coords)
                {
                    bump_task_priority(task, task_data, queue, priority);

                    return it->second;
                }
            }
        }

        auto new_task_ref = tasks.alloc();
        Task& new_task = new_task_ref.value();
        new_task.type = TaskType::LoadVectorDataPmTilesPackage;
        new_task.loader = this;
        new_task.status = TaskStatus::New;
        new_task.version = 0;
        new_task.data_use_count = 0;
        new_task.is_active = false;
        Task::LoadVectorDataPmTilesPackage task_data;
        task_data.url = url;
        task_data.tile_coords = tile_coords;
        task_data.headers = headers;
        task_data.queue = queue;
        task_data.priority = priority;
        task_data.resource_owner = resource_owner;
        task_data.request_count_metric = request_count_metric;
        task_data.load_pmtiles_task = {};
        task_data.tile_query = std::nullopt;
        task_data.package = std::nullopt;
        new_task.params = std::move(task_data);
        new_task.hash = hash;

        tasks_by_hash.insert({new_task.hash, new_task_ref.make_weak_ref()});

        activate_task(new_task_ref.make_weak_ref());

        return new_task_ref;
    }

    TaskRef get_or_create_load_untiled_vector_data_task(
        const LayerModelRef& layer_model,
        uint32_t data_source)
    {
        uint64_t hash = hrz::hash_value(TaskType::LoadUntiledVectorData);
        hash = hrz::hash_mix<uint64_t>(hash, layer_model.get_handle().hash());
        hash = hrz::hash_mix<uint64_t>(hash, hrz::hash_value(data_source));

        auto it = tasks_by_hash.find(hash);
        if (it != tasks_by_hash.end() && it->second.is_valid())
        {
            const auto& task = it->second.value();
            if (task.type == TaskType::LoadUntiledVectorData)
            {
                const auto& task_data = task.load_untiled_vector_data();
                if (task_data.layer_model == layer_model && task_data.data_source == data_source)
                {
                    return it->second;
                }
            }
        }

        auto new_task_ref = tasks.alloc();
        Task& new_task = new_task_ref.value();
        new_task.type = TaskType::LoadUntiledVectorData;
        new_task.loader = this;
        new_task.status = TaskStatus::New;
        new_task.version = 0;
        new_task.data_use_count = 0;
        new_task.is_active = false;
        Task::LoadUntiledVectorData task_data;
        task_data.layer_model = layer_model;
        task_data.data_source = data_source;
        task_data.load_vector_tile_data_task = {};
        new_task.params = std::move(task_data);
        new_task.hash = hash;

        tasks_by_hash.insert({new_task.hash, new_task_ref.make_weak_ref()});

        activate_task(new_task_ref.make_weak_ref());

        return new_task_ref;
    }

    TaskRef get_or_create_extract_vector_tile_data_task(
        const LayerModelRef& layer_model,
        uint32_t data_source,
        TileCoords tile_coords)
    {
        uint64_t hash = hrz::hash_value(TaskType::ExtractVectorTileData);
        hash = hrz::hash_mix<uint64_t>(hash, layer_model.get_handle().hash());
        hash = hrz::hash_mix<uint64_t>(hash, hrz::hash_value(data_source));
        hash = hrz::hash_mix<uint64_t>(hash, hrz::hash_value(tile_coords));

        auto it = tasks_by_hash.find(hash);
        if (it != tasks_by_hash.end() && it->second.is_valid())
        {
            const auto& task = it->second.value();
            if (task.type == TaskType::ExtractVectorTileData)
            {
                const auto& task_data = task.extract_vector_tile_data();
                if (task_data.layer_model == layer_model && task_data.data_source == data_source
                    && task_data.tile_coords == tile_coords)
                {
                    return it->second;
                }
            }
        }

        auto new_task_ref = tasks.alloc();
        Task& new_task = new_task_ref.value();
        new_task.type = TaskType::ExtractVectorTileData;
        new_task.loader = this;
        new_task.status = TaskStatus::New;
        new_task.version = 0;
        new_task.data_use_count = 0;
        new_task.is_active = false;
        Task::ExtractVectorTileData task_data;
        task_data.layer_model = layer_model;
        task_data.data_source = data_source;
        task_data.tile_coords = tile_coords;
        task_data.load_untiled_vector_tile_data_task = {};
        new_task.params = std::move(task_data);
        new_task.hash = hash;

        tasks_by_hash.insert({new_task.hash, new_task_ref.make_weak_ref()});

        activate_task(new_task_ref.make_weak_ref());

        return new_task_ref;
    }

    TaskRef get_or_create_load_in_memory_vector_data_task(
        const LayerModelRef& layer_model,
        uint32_t data_source,
        const FeatureSelection& feature_selection)
    {
        uint64_t hash = hrz::hash_value(TaskType::LoadInMemoryVectorData);
        hash = hrz::hash_mix<uint64_t>(hash, layer_model.get_handle().hash());
        hash = hrz::hash_mix<uint64_t>(hash, hrz::hash_value(data_source));
        hash = hrz::hash_mix<uint64_t>(hash, feature_selection.hash());

        auto it = tasks_by_hash.find(hash);
        if (it != tasks_by_hash.end() && it->second.is_valid())
        {
            const auto& task = it->second.value();
            if (task.type == TaskType::LoadInMemoryVectorData)
            {
                const auto& task_data = task.load_in_memory_vector_data();
                if (task_data.layer_model == layer_model && task_data.data_source == data_source
                    && task_data.feature_selection == feature_selection)
                {
                    return it->second;
                }
            }
        }

        auto new_task_ref = tasks.alloc();
        Task& new_task = new_task_ref.value();
        new_task.type = TaskType::LoadInMemoryVectorData;
        new_task.loader = this;
        new_task.status = TaskStatus::New;
        new_task.version = 0;
        new_task.data_use_count = 0;
        new_task.is_active = false;
        Task::LoadInMemoryVectorData task_data;
        task_data.layer_model = layer_model;
        task_data.data_source = data_source;
        task_data.feature_selection = feature_selection;
        task_data.load_feature_ids_task = {};
        task_data.load_feature_ids_task_version = std::nullopt;
        task_data.in_memory_vector_data_request_id = std::nullopt;
        task_data.feature_ids = FeatureIdListRef{};
        task_data.geometry = TileGeometryRef{};
        task_data.attribution = {};
        new_task.params = std::move(task_data);
        new_task.hash = hash;

        tasks_by_hash.insert({new_task.hash, new_task_ref.make_weak_ref()});

        activate_task(new_task_ref.make_weak_ref());

        return new_task_ref;
    }

    TaskRef get_or_create_load_url_data_task(
        const std::string_view& url,
        const hrz::HttpHeaders& headers,
        hrz::assets_loader::Queue queue,
        uint32_t priority,
        const monitoring::ResourceOwner& resource_owner,
        const metrics::MetricDesc& request_count_metric)
    {
        // Don’t check on the queue and priority. We want to reuse
        // the task even if only the URL is the same.
        // If a task is reused, the metrics and resource ownership information
        // may become incomplete, as they would only mention one data source
        // on one vector data layer, but at least the actual work isn't
        // duplicated.

        uint64_t hash = hrz::hash_value(TaskType::LoadUrlData);
        hash = hrz::hash_mix<uint64_t>(hash, hrz::murmur3_x64_64(url));
        hash = hrz::hash_mix<uint64_t>(hash, headers.hash_content());

        auto it = tasks_by_hash.find(hash);
        if (it != tasks_by_hash.end())
        {
            if (it->second.is_valid())
            {
                auto& task = it->second.value();

                if (task.type == TaskType::LoadUrlData)
                {
                    auto& task_data = task.load_url_data();
                    if (task_data.url == url
                        && task_data.headers.hash_content() == headers.hash_content())
                    {
                        bump_task_priority(task, task_data, queue, priority);

                        return it->second;
                    }
                }
            }
        }

        auto new_task_ref = tasks.alloc();
        Task& new_task = new_task_ref.value();
        new_task.type = TaskType::LoadUrlData;
        new_task.loader = this;
        new_task.status = TaskStatus::New;
        new_task.version = 0;
        new_task.data_use_count = 0;
        new_task.is_active = false;
        Task::LoadUrlData task_data;
        task_data.url = {url.data(), url.size()};
        task_data.headers = headers;
        task_data.queue = queue;
        task_data.priority = priority;
        task_data.resource_owner = resource_owner;
        task_data.request_count_metric = request_count_metric;
        task_data.download_request_id = std::nullopt;
        new_task.params = std::move(task_data);
        new_task.hash = hash;

        tasks_by_hash.insert({new_task.hash, new_task_ref.make_weak_ref()});

        activate_task(new_task_ref.make_weak_ref());

        return new_task_ref;
    }

    TaskRef get_or_create_request_client_data_task(
        const LayerModelRef& layer_model,
        uint32_t data_source,
        const FeatureSelection& feature_selection)
    {
        uint64_t hash = hrz::hash_value(TaskType::RequestClientData);
        hash = hrz::hash_mix<uint64_t>(hash, layer_model.get_handle().hash());
        hash = hrz::hash_mix<uint64_t>(hash, hrz::hash_value(data_source));
        hash = hrz::hash_mix<uint64_t>(hash, feature_selection.hash());

        auto it = tasks_by_hash.find(hash);
        if (it != tasks_by_hash.end() && it->second.is_valid())
        {
            const auto& task = it->second.value();
            if (task.type == TaskType::RequestClientData)
            {
                const auto& task_data = task.request_client_data();
                if (task_data.layer_model == layer_model && task_data.data_source == data_source
                    && task_data.feature_selection == feature_selection)
                {
                    return it->second;
                }
            }
        }

        auto new_task_ref = tasks.alloc();
        Task& new_task = new_task_ref.value();
        new_task.type = TaskType::RequestClientData;
        new_task.loader = this;
        new_task.status = TaskStatus::New;
        new_task.version = 0;
        new_task.data_use_count = 0;
        new_task.is_active = false;
        Task::RequestClientData task_data;
        task_data.layer_model = layer_model;
        task_data.data_source = data_source;
        task_data.feature_selection = feature_selection;
        task_data.load_feature_ids_task = {};
        task_data.client_ticket = NO_CLIENT_TICKET;
        task_data.geometry = TileGeometryRef{};
        task_data.attribution = {};
        task_data.feature_ids = FeatureIdListRef{};
        new_task.params = std::move(task_data);
        new_task.hash = hash;

        tasks_by_hash.insert({new_task.hash, new_task_ref.make_weak_ref()});

        activate_task(new_task_ref.make_weak_ref());

        return new_task_ref;
    }

    TaskRef get_or_create_load_tilejson_task(
        const std::string_view& url,
        const hrz::HttpHeaders& headers,
        hrz::assets_loader::Queue queue,
        uint32_t priority,
        bool preserve_query_parameters,
        const monitoring::ResourceOwner& resource_owner,
        const metrics::MetricDesc& request_count_metric)
    {
        // Don’t check on the queue and priority. We want to reuse
        // the task even if only the URL is the same.
        // If a task is reused, the metrics and resource ownership information
        // may become incomplete, as they would only mention one data source
        // on one vector data layer, but at least the actual work isn't
        // duplicated.

        uint64_t hash = hrz::hash_value(TaskType::LoadTileJson);
        hash = hrz::hash_mix<uint64_t>(hash, hrz::murmur3_x64_64(url));
        hash = hrz::hash_mix<uint64_t>(hash, headers.hash_content());
        hash = hrz::hash_mix<uint64_t>(hash, preserve_query_parameters);

        auto it = tasks_by_hash.find(hash);
        if (it != tasks_by_hash.end() && it->second.is_valid())
        {
            auto& task = it->second.value();

            if (task.type == TaskType::LoadTileJson)
            {
                auto& task_data = task.load_tilejson();
                if (task_data.url == url
                    && task_data.headers.hash_content() == headers.hash_content()
                    && task_data.preserve_query_parameters == preserve_query_parameters)
                {
                    bump_task_priority(task, task_data, queue, priority);

                    return it->second;
                }
            }
        }

        auto new_task_ref = tasks.alloc();
        Task& new_task = new_task_ref.value();
        new_task.type = TaskType::LoadTileJson;
        new_task.loader = this;
        new_task.status = TaskStatus::New;
        new_task.version = 0;
        new_task.data_use_count = 0;
        new_task.is_active = false;
        Task::LoadTileJson task_data;
        task_data.url = {url.data(), url.size()};
        task_data.headers = headers;
        task_data.queue = queue;
        task_data.priority = priority;
        task_data.preserve_query_parameters = preserve_query_parameters;
        task_data.load_url_data_task = {};
        task_data.resource_owner = resource_owner;
        task_data.request_count_metric = request_count_metric;
        new_task.params = std::move(task_data);
        new_task.hash = hash;

        tasks_by_hash.insert({new_task.hash, new_task_ref.make_weak_ref()});

        activate_task(new_task_ref.make_weak_ref());

        return new_task_ref;
    }

    TaskRef get_or_create_load_pmtiles_task(
        const std::string_view& url,
        const hrz::HttpHeaders& headers,
        hrz::assets_loader::Queue queue,
        uint32_t priority,
        const monitoring::ResourceOwner& resource_owner,
        const metrics::MetricDesc& request_count_metric)
    {
        // Don’t check on the queue and priority. We want to reuse
        // the task even if only the URL is the same.
        // If a task is reused, the metrics and resource ownership information
        // may become incomplete, as they would only mention one data source
        // on one vector data layer, but at least the actual work isn't
        // duplicated.

        uint64_t hash = hrz::hash_value(TaskType::LoadPmTiles);
        hash = hrz::hash_mix<uint64_t>(hash, hrz::murmur3_x64_64(url));
        hash = hrz::hash_mix<uint64_t>(hash, headers.hash_content());

        auto it = tasks_by_hash.find(hash);
        if (it != tasks_by_hash.end() && it->second.is_valid())
        {
            auto& task = it->second.value();

            if (task.type == TaskType::LoadPmTiles)
            {
                auto& task_data = task.load_pmtiles();
                if (task_data.url == url
                    && task_data.headers.hash_content() == headers.hash_content())
                {
                    bump_task_priority(task, task_data, queue, priority);
                    return it->second;
                }
            }
        }

        auto new_task_ref = tasks.alloc();
        Task& new_task = new_task_ref.value();
        new_task.type = TaskType::LoadPmTiles;
        new_task.loader = this;
        new_task.status = TaskStatus::New;
        new_task.version = 0;
        new_task.data_use_count = 0;
        new_task.is_active = false;
        Task::LoadPmTiles task_data;
        task_data.url = {url.data(), url.size()};
        task_data.headers = headers;
        task_data.queue = queue;
        task_data.priority = priority;
        task_data.resource_owner = resource_owner;
        task_data.request_count_metric = request_count_metric;
        task_data.asset_loader_channel_request_id = std::nullopt;
        task_data.pmtiles = nullptr;
        new_task.params = std::move(task_data);
        new_task.hash = hash;

        tasks_by_hash.insert({new_task.hash, new_task_ref.make_weak_ref()});

        activate_task(new_task_ref.make_weak_ref());

        return new_task_ref;
    }

    TaskRef get_or_create_load_layer_model_task(uint32_t layer_id)
    {
        uint64_t hash = hrz::hash_value(TaskType::LoadLayerModel);
        hash = hrz::hash_mix<uint64_t>(hash, hrz::hash_value(layer_id));

        auto it = tasks_by_hash.find(hash);
        if (it != tasks_by_hash.end() && it->second.is_valid())
        {
            const auto& task = it->second.value();
            if (task.type == TaskType::LoadLayerModel)
            {
                const auto& task_data = task.load_layer_model();
                if (task_data.layer_id == layer_id)
                {
                    return it->second;
                }
            }
        }

        auto new_task_ref = tasks.alloc();
        Task& new_task = new_task_ref.value();
        new_task.type = TaskType::LoadLayerModel;
        new_task.loader = this;
        new_task.status = TaskStatus::New;
        new_task.version = 0;
        new_task.data_use_count = 0;
        new_task.is_active = false;
        Task::LoadLayerModel task_data;
        task_data.layer_id = layer_id;
        new_task.params = std::move(task_data);
        new_task.hash = hash;

        tasks_by_hash.insert({new_task.hash, new_task_ref.make_weak_ref()});

        activate_task(new_task_ref.make_weak_ref());

        return new_task_ref;
    }

    TaskRef get_or_create_load_source_model_task(
        const LayerModelRef& layer_model,
        uint32_t data_source)
    {
        uint64_t hash = hrz::hash_value(TaskType::LoadSourceModel);
        hash = hrz::hash_mix<uint64_t>(hash, layer_model.get_handle().hash());
        hash = hrz::hash_mix<uint64_t>(hash, hrz::hash_value(data_source));

        auto it = tasks_by_hash.find(hash);
        if (it != tasks_by_hash.end() && it->second.is_valid())
        {
            const auto& task = it->second.value();
            if (task.type == TaskType::LoadSourceModel)
            {
                const auto& task_data = task.load_source_model();
                if (task_data.layer_model == layer_model && task_data.data_source == data_source)
                {
                    return it->second;
                }
            }
        }

        auto new_task_ref = tasks.alloc();
        Task& new_task = new_task_ref.value();
        new_task.type = TaskType::LoadSourceModel;
        new_task.loader = this;
        new_task.status = TaskStatus::New;
        new_task.version = 0;
        new_task.data_use_count = 0;
        new_task.is_active = false;
        Task::LoadSourceModel task_data;
        task_data.layer_model = layer_model;
        task_data.data_source = data_source;
        task_data.load_tilejson_task = {};
        task_data.load_pmtiles_task = {};
        task_data.load_untiled_vector_data_task = {};
        new_task.params = std::move(task_data);
        new_task.hash = hash;

        tasks_by_hash.insert({new_task.hash, new_task_ref.make_weak_ref()});

        activate_task(new_task_ref.make_weak_ref());

        return new_task_ref;
    }

    DataRequest create_data_request(
        RequestId request_id,
        RequestId layer_loader_request_id,
        FeatureSelection feature_selection,
        hrz::vector_data::DataKind data_kind)
    {
        auto layer_model_ref = get_layer_model_for_loaded_layer(layer_loader_request_id);
        if (!layer_model_ref.has_value())
        {
            return make_data_request(
                request_id, layer_loader_request_id, std::numeric_limits<uint32_t>::max(),
                std::move(feature_selection), data_kind);
        }

        auto& layer_model = layer_model_ref.value();

        DataRequest request = make_data_request(
            request_id, layer_loader_request_id, layer_model.id, std::move(feature_selection),
            data_kind);

        if (request.feature_selection.has_tile_coords())
        {
            const auto& tile_coords = request.feature_selection.tile_coords();
            auto tile_bounds = hrz::mercator_tile_bounds(tile_coords);

            auto layer_bounds = get_bounds(layer_model);
            auto layer_min_lod = get_min_lod(layer_model);
            auto layer_max_lod = get_max_lod(layer_model);

            if (!hrz::intersect(tile_bounds, layer_bounds) || tile_coords.lod < layer_min_lod
                || tile_coords.lod > layer_max_lod)
            {
                HRZ_LOG_WARNING(
                    "Out-of-bounds request for vector data layer {}: {}", layer_model.id,
                    tile_coords);
                return request;
            }
        }

        TaskRef task_ref;

        switch (data_kind)
        {
            case hrz::vector_data::DataKind::AttributeValues:
                task_ref = get_or_create_load_all_attributes_task(
                    layer_model_ref, request.feature_selection);
                break;
            case hrz::vector_data::DataKind::Geometry:
                task_ref =
                    get_or_create_load_geometry_task(layer_model_ref, request.feature_selection);
                break;
            case hrz::vector_data::DataKind::FeatureIds:
                task_ref =
                    get_or_create_load_feature_ids_task(layer_model_ref, request.feature_selection);
                break;
            default:
                assert(false && "Unhandled case");
                task_ref = TaskRef{};
                break;
        }

        request.task = TaskDependency::on_task(task_ref);
        request.task.retain_data();
        request.data_version = task_ref.value().version;

        return request;
    }

    void destroy(JobScheduler* js)
    {
        request_ids_to_layer_loaders.clear();
        request_ids_to_data_requests.clear();
        tasks_to_data_request_ids.clear();

        for (auto ref : tasks)
        {
            clear_task(ref.value(), js);
        }

        asset_loader_channel.close();
        in_memory_vector_data_channel.close();
    }

    void register_layer(SceneModel* scene_model, uint64_t layer_handle)
    {
        assert(scene_model);
        HRZ_SCOPED_LOCK(model_mutex);

        if (layer_handles_to_layer_ids.find(layer_handle) != layer_handles_to_layer_ids.end())
        {
            // Layer already registered.
            return;
        }

        hrz_proto::PathRoot root;
        root.mutable_vector_data_layer()->set_opaque(layer_handle);
        scene_model::register_element(scene_model, root);

        // Default data
        uint32_t layer_id = 0;
        hrz_proto::VectorDataLayer layer;
        layer.set_id(layer_id);

        hrz::SceneModelAccessor accessor(scene_model);
        hrz_proto::VectorDataLayerPathBuilder<hrz::SceneModelAccessor> builder(
            accessor, root.vector_data_layer());
        builder.set(layer);

        layer_handles_to_layer_ids.insert({layer_handle, layer_id});
        created_model_layers.insert(layer_handle);
    }

    void unregister_layer(uint64_t layer_handle)
    {
        HRZ_SCOPED_LOCK(model_mutex);

        if (layer_handles_to_layer_ids.find(layer_handle) == layer_handles_to_layer_ids.end())
        {
            return;
        }

        destroyed_model_layers.insert(layer_handle);
        updated_model_layers.erase(layer_handle);
        updated_headers_model_layers.erase(layer_handle);
    }

    void notify_update(
        uint64_t layer_handle,
        scene_model::UpdateType,
        const scene_model::VectorDataLayerPath& path)
    {
        HRZ_SCOPED_LOCK(model_mutex);

        if (layer_handles_to_layer_ids.find(layer_handle) == layer_handles_to_layer_ids.end())
        {
            return;
        }

        bool must_update_all = true;

        if (path.is_sources() && path.has_sources_index())
        {
            auto source = path.clone().sources();
            if ((source.is_tiled_data_provider() && source.tiled_data_provider().is_http_headers())
                || (source.is_tilejson_data_provider()
                    && source.tilejson_data_provider().is_http_headers()))
            {
                updated_headers_model_layers.insert({layer_handle, path.sources_index()});
                must_update_all = false;
            }
        }

        if (must_update_all)
        {
            updated_model_layers.insert(layer_handle);
            updated_headers_model_layers.erase(layer_handle);
        }
    }

private:
    void load_layer(RequestId request_id, uint32_t layer_id)
    {
        auto it = request_ids_to_layer_loaders.find(request_id);
        if (it != request_ids_to_layer_loaders.end())
        {
            HRZ_LOG_ERROR("Duplicate request ID for load layer request");
            return;
        }

        LayerLoader loader;
        loader.request_id = request_id;
        loader.layer_id = layer_id;
        loader.load_model_task = {};
        loader.has_full_model_update = false;
        loader.has_max_lod_update = false;
        loader.data_version = 0;

        loader.load_model_task =
            TaskDependency::on_task(get_or_create_load_layer_model_task(layer_id));
        loader.load_model_task.retain_data();

        if (loader.load_model_task.get_task().status == TaskStatus::Loaded)
        {
            // The model is already loaded, signal this to the requester.
            loader.has_full_model_update = true;
            send_layer_model_message(
                loader, loader.load_model_task.get_task().load_layer_model().layer_model.value());
        }

        request_ids_to_layer_loaders.insert({request_id, std::move(loader)});
        layer_ids_to_request_ids.insert({layer_id, request_id});
    }

    void release_layer_loader(RequestId request_id)
    {
        auto it_loader = request_ids_to_layer_loaders.find(request_id);
        if (it_loader == request_ids_to_layer_loaders.end()) return;

        auto iterpair = layer_ids_to_request_ids.equal_range(it_loader->second.layer_id);
        for (auto it_request = iterpair.first; it_request != iterpair.second;)
        {
            if (it_request->second == request_id)
            {
                it_request = layer_ids_to_request_ids.erase(it_request);
            }
            else
            {
                ++it_request;
            }
        }

        request_ids_to_layer_loaders.erase(it_loader);
    }

    void send_layer_model_message(LayerLoader& loader, const LayerModel& layer_model)
    {
        auto it = channels.find(loader.request_id.channel_id);
        if (it != channels.end())
        {
            auto& channel = it->second;
            channel.send(vector_data::messages::LayerModelUpdate{
                loader.request_id.request_id, get_min_lod(layer_model), get_max_lod(layer_model),
                get_bounds(layer_model), get_attribute_ids(layer_model)});
        }
    }

    void send_layer_model_error_message(LayerLoader& loader)
    {
        auto it = channels.find(loader.request_id.channel_id);
        if (it != channels.end())
        {
            auto& channel = it->second;
            channel.send(vector_data::messages::LayerModelError{loader.request_id.request_id});
        }
    }

    void send_layer_has_new_data_message(LayerLoader& loader)
    {
        auto it = channels.find(loader.request_id.channel_id);
        if (it != channels.end())
        {
            auto& channel = it->second;
            channel.send(vector_data::messages::LayerNewData{loader.request_id.request_id});
        }
    }

    void send_data_message(
        DataRequest& request,
        std::variant<
            std::pair<TileGeometry, AttributionHandle>,
            hrz::InlinedVector<vector_data::AttributeValues, 16>,
            vector_data::FeatureIds> data)
    {
        auto it = channels.find(request.request_id.channel_id);
        if (it != channels.end())
        {
            auto& channel = it->second;
            channel.send(vector_data::messages::DataUpdate{
                request.request_id.request_id,
                {std::move(data)}});
        }
    }

    void send_geometry_message(WeakTaskRef& task_ref, Task& task)
    {
        assert(task.type == TaskType::LoadGeometry);
        assert(task.status == TaskStatus::Loaded);

        auto& task_data = task.load_geometry();

        auto iterpair = tasks_to_data_request_ids.equal_range(task_ref);
        for (auto it = iterpair.first; it != iterpair.second; ++it)
        {
            auto& request = request_ids_to_data_requests.at(it->second);
            send_data_message(
                request,
                std::pair<TileGeometry, AttributionHandle>{
                    task_data.geometry.value(), task_data.attribution});
        }
    }

    void send_attribute_values_message(WeakTaskRef& task_ref, Task& task)
    {
        assert(task.type == TaskType::LoadAllAttributeValues);
        assert(task.status == TaskStatus::Loaded);

        auto& task_data = task.load_all_attribute_values();

        auto iterpair = tasks_to_data_request_ids.equal_range(task_ref);
        for (auto it = iterpair.first; it != iterpair.second; ++it)
        {
            auto& request = request_ids_to_data_requests.at(it->second);

            hrz::InlinedVector<vector_data::AttributeValues, 16> attribute_values_to_send;
            attribute_values_to_send.reserve(task_data.attribute_tasks.size());

            for (auto& attribute_task_ref : task_data.attribute_tasks)
            {
                auto& attribute_task = attribute_task_ref.get_task();
                assert(attribute_task.status == TaskStatus::Loaded);
                attribute_values_to_send.push_back(
                    attribute_task.load_attribute_values().attribute_values.value());
            }

            send_data_message(request, std::move(attribute_values_to_send));
        }
    }

    void send_feature_ids_message(WeakTaskRef& task_ref, Task& task)
    {
        assert(task.type == TaskType::LoadFeatureIds);
        assert(task.status == TaskStatus::Loaded);

        auto& task_data = task.load_feature_ids();

        auto iterpair = tasks_to_data_request_ids.equal_range(task_ref);
        for (auto it = iterpair.first; it != iterpair.second; ++it)
        {
            auto& request = request_ids_to_data_requests.at(it->second);
            send_data_message(request, task_data.feature_ids.value());
        }
    }

    void send_data_error_message(DataRequest& request)
    {
        auto it = channels.find(request.request_id.channel_id);
        if (it != channels.end())
        {
            auto& channel = it->second;
            channel.send(vector_data::messages::DataError{request.request_id.request_id});
        }
    }

    LayerModelRef get_layer_model_for_loaded_layer(LayerLoader& layer_loader)
    {
        const auto& load_layer_model_task = layer_loader.load_model_task.get_task();

        if (load_layer_model_task.status != TaskStatus::Loaded)
        {
            HRZ_LOG_ERROR("Layer {} not loaded", layer_loader.layer_id);
            return {};
        }

        assert(load_layer_model_task.type == TaskType::LoadLayerModel);
        const auto& task_data = load_layer_model_task.load_layer_model();

        if (!task_data.layer_model.has_value())
        {
            assert(false);
            HRZ_LOG_ERROR("Model for layer {} not found", layer_loader.layer_id);
            return {};
        }

        return task_data.layer_model;
    }

    LayerModelRef get_layer_model_for_loaded_layer(RequestId layer_loader_request_id)
    {
        auto loader_it = request_ids_to_layer_loaders.find(layer_loader_request_id);
        if (loader_it == request_ids_to_layer_loaders.end())
        {
            HRZ_LOG_ERROR(
                "Invalid layer loader request ID: {}-{}", layer_loader_request_id.channel_id,
                layer_loader_request_id.request_id);
            return {};
        }

        return get_layer_model_for_loaded_layer(loader_it->second);
    }

    uint32_t get_min_lod(const LayerModel& layer_model)
    {
        uint32_t min_lod = 0;

        for (const auto& data_source : layer_model.data_sources)
        {
            if (data_source.min_lod.has_value())
            {
                min_lod = std::max(min_lod, data_source.min_lod.value());
            }
        }

        return min_lod;
    }

    uint32_t get_max_lod(const LayerModel& layer_model)
    {
        uint32_t max_lod = std::numeric_limits<uint8_t>::max();

        for (const auto& data_source : layer_model.data_sources)
        {
            if (data_source.max_lod.has_value())
            {
                max_lod = std::min(max_lod, data_source.max_lod.value());
            }
        }

        return max_lod;
    }

    static hrz::InlinedVector<uint32_t, 16> get_attribute_ids(const LayerModel& layer_model)
    {
        hrz::InlinedVector<uint32_t, 16> ids;
        for (const auto& attrib : layer_model.attributes)
        {
            ids.push_back(attrib.first);
        }

        return ids;
    }

    GeoBounds get_bounds(const LayerModel& layer_model)
    {
        auto bounds = GeoBounds::empty();

        for (const auto& data_source : layer_model.data_sources)
        {
            if (data_source.bounds.has_value())
            {
                if (bounds.is_empty())
                {
                    bounds = data_source.bounds.value();
                }
                else
                {
                    bounds = hrz::intersection(bounds, data_source.bounds.value());
                }
            }
        }

        return bounds;
    }

    void request_data_for_selection(
        RequestId request_id,
        RequestId layer_loader_request_id,
        FeatureSelection feature_selection,
        DataKind data_kind)
    {
        auto request = create_data_request(
            request_id, layer_loader_request_id, std::move(feature_selection), data_kind);

        if (!request.task.has_task())
        {
            send_data_error_message(request);
            request_ids_to_data_requests.insert({request_id, std::move(request)});
            return;
        }

        auto task_ref = request.task.get_task_ref().make_weak_ref();
        auto& task = request.task.get_task();

        tasks_to_data_request_ids.insert({request.task.get_task_ref().make_weak_ref(), request_id});

        if (task.status == TaskStatus::DataError || task.status == TaskStatus::ModelError)
        {
            send_data_error_message(request);
        }

        request_ids_to_data_requests.insert({request_id, std::move(request)});

        if (task.status == TaskStatus::Loaded)
        {
            switch (data_kind)
            {
                case DataKind::Geometry:
                {
                    send_geometry_message(task_ref, task);
                    break;
                }
                case DataKind::AttributeValues:
                {
                    send_attribute_values_message(task_ref, task);
                    break;
                }
                case DataKind::FeatureIds:
                {
                    send_feature_ids_message(task_ref, task);
                    break;
                }
                default: assert(false && "Unhandled case"); break;
            }
        }
    }

    void request_data(
        RequestId request_id,
        RequestId layer_loader_request_id,
        TileCoords tile_coords,
        DataKind data_kind)
    {
        request_data_for_selection(
            request_id, layer_loader_request_id, FeatureSelection{tile_coords}, data_kind);
    }

    void request_data(
        RequestId request_id,
        RequestId layer_loader_request_id,
        const hrz::vector_data::FeatureIds& feature_ids,
        DataKind data_kind)
    {
        auto feature_id_list_ref = feature_id_lists.alloc();
        feature_id_list_ref.value() = feature_ids;

        request_data_for_selection(
            request_id, layer_loader_request_id, FeatureSelection{feature_id_list_ref}, data_kind);
    }

    void release_data(RequestId request_id)
    {
        auto request_it = request_ids_to_data_requests.find(request_id);
        if (request_it == request_ids_to_data_requests.end())
        {
            HRZ_LOG_ERROR(
                "Invalid data request ID: {}-{}", request_id.channel_id, request_id.request_id);
            return;
        }

        auto& request = request_it->second;

        if (!request.task.is_data_retained()) return;

        auto& task_ref = request.task;

        if (!task_ref.has_task())
        {
            HRZ_LOG_ERROR(
                "No data to release for request ID {}-{}", request_id.channel_id,
                request_id.request_id);
            return;
        }

        task_ref.release_data();
    }

    void retain_data(RequestId request_id)
    {
        auto request_it = request_ids_to_data_requests.find(request_id);
        if (request_it == request_ids_to_data_requests.end())
        {
            HRZ_LOG_ERROR(
                "Invalid data request ID: {}-{}", request_id.channel_id, request_id.request_id);
            return;
        }

        auto& request = request_it->second;

        if (request.task.is_data_retained()) return;

        auto& task_ref = request.task;

        if (!task_ref.has_task())
        {
            HRZ_LOG_ERROR(
                "No data to reload for request ID {}-{}", request_id.channel_id,
                request_id.request_id);
            return;
        }

        task_ref.retain_data();
    }

    void release_request(RequestId request_id)
    {
        auto request_it = request_ids_to_data_requests.find(request_id);
        if (request_it == request_ids_to_data_requests.end())
        {
            return;
        }

        auto& request = request_it->second;

        auto iterpair =
            tasks_to_data_request_ids.equal_range(request.task.get_task_ref().make_weak_ref());
        for (auto it = iterpair.first; it != iterpair.second;)
        {
            if (it->second == request_id)
            {
                it = tasks_to_data_request_ids.erase(it);
            }
            else
            {
                ++it;
            }
        }

        request_ids_to_data_requests.erase(request_it);
    }

    void visit_load_layer_models_for_layer_id(
        uint32_t layer_id,
        const std::function<void(LayerModel&)>& callback)
    {
        auto iterpair = layer_ids_to_request_ids.equal_range(layer_id);
        for (auto it = iterpair.first; it != iterpair.second; ++it)
        {
            auto request_id = it->second;
            auto loader_it = request_ids_to_layer_loaders.find(request_id);
            if (loader_it != request_ids_to_layer_loaders.end())
            {
                auto& loader = loader_it->second;
                auto& load_layer_task_data = loader.load_model_task.get_task().load_layer_model();
                callback(*load_layer_task_data.layer_model);
            }
        }
    }

    // Returns whether content negotiation headers have changed.
    bool update_source_headers(
        uint64_t layer_handle,
        uint32_t layer_id,
        uint32_t source_index,
        SceneModel* scene_model)
    {
        bool content_negotiation_changed = false;
        auto iterpair = layer_ids_to_request_ids.equal_range(layer_id);
        for (auto it = iterpair.first; it != iterpair.second; ++it)
        {
            auto request_id = it->second;
            auto loader_it = request_ids_to_layer_loaders.find(request_id);
            if (loader_it == request_ids_to_layer_loaders.end()) continue;

            auto& loader = loader_it->second;

            auto& model_opt = loader.load_model_task.get_task().load_layer_model().layer_model;
            if (!model_opt.has_value()) continue;

            auto& model = model_opt.value();

            hrz::SceneModelAccessor accessor(scene_model);
            hrz_proto::LayerHandle handle;
            handle.set_opaque(layer_handle);
            hrz_proto::VectorDataLayerPathBuilder<hrz::SceneModelAccessor> builder(
                accessor, handle);

            auto& data_source = model.data_sources.at(source_index);
            if (data_source.has_tiled_data_provider())
            {
                auto new_headers_proto = builder.clone()
                                             .sources(source_index)
                                             .tiled_data_provider()
                                             .http_headers()
                                             .get();
                auto new_headers = hrz::assets_loader::from_proto(new_headers_proto);
                content_negotiation_changed =
                    data_source.tiled_data_provider().headers.hash_content()
                        != new_headers.hash_content()
                    || content_negotiation_changed;
                data_source.tiled_data_provider().headers = new_headers;

                visit_load_layer_models_for_layer_id(
                    model.id,
                    [&](LayerModel& load_layer_model)
                    {
                        auto& source = load_layer_model.data_sources.at(source_index);
                        if (source.has_tiled_data_provider())
                        {
                            source.tiled_data_provider().headers = new_headers;
                        }
                    });
            }
            else if (data_source.has_tilejson_data_provider())
            {
                auto new_headers_proto = builder.clone()
                                             .sources(source_index)
                                             .tilejson_data_provider()
                                             .http_headers()
                                             .get();
                auto new_headers = hrz::assets_loader::from_proto(new_headers_proto);
                content_negotiation_changed =
                    data_source.tilejson_data_provider().headers.hash_content()
                        != new_headers.hash_content()
                    || content_negotiation_changed;
                data_source.tilejson_data_provider().headers = new_headers;

                visit_load_layer_models_for_layer_id(
                    model.id,
                    [&](LayerModel& load_layer_model)
                    {
                        auto& source = load_layer_model.data_sources.at(source_index);
                        if (source.has_tilejson_data_provider())
                        {
                            source.tilejson_data_provider().headers = new_headers;
                        }
                    });
            }
            else if (data_source.has_pmtiles_data_provider())
            {
                auto new_headers_proto = builder.clone()
                                             .sources(source_index)
                                             .pmtiles_data_provider()
                                             .http_headers()
                                             .get();
                auto& provider = data_source.pmtiles_data_provider();

                auto new_headers = hrz::assets_loader::from_proto(new_headers_proto);

                content_negotiation_changed =
                    provider.headers.hash_content() != new_headers.hash_content()
                    || content_negotiation_changed;
                if (provider.load_pmtiles_task.has_task())
                {
                    auto& pmtiles_task = provider.load_pmtiles_task.get_task().load_pmtiles();
                    pmtiles_task.headers = new_headers;
                    if (pmtiles_task.pmtiles)
                    {
                        content_negotiation_changed =
                            pmtiles_task.pmtiles->set_http_headers(new_headers)
                            || content_negotiation_changed;
                    }
                }

                provider.headers = new_headers;

                visit_load_layer_models_for_layer_id(
                    model.id,
                    [&](LayerModel& load_layer_model)
                    {
                        auto& source = load_layer_model.data_sources.at(source_index);
                        if (source.has_pmtiles_data_provider())
                        {
                            auto& provider = source.pmtiles_data_provider();
                            provider.headers = new_headers;
                            if (provider.load_pmtiles_task.has_task())
                            {
                                auto& pmtiles_task =
                                    provider.load_pmtiles_task.get_task().load_pmtiles();
                                pmtiles_task.headers = new_headers;
                                if (pmtiles_task.pmtiles)
                                {
                                    pmtiles_task.pmtiles->set_http_headers(new_headers);
                                }
                            }
                        }
                    });
            }
        }
        return content_negotiation_changed;
    }

    void update_layers_from_model(SceneModel* scene_model)
    {
        HRZ_SCOPED_SAMPLE_A("update layers from model");

        HRZ_SCOPED_LOCK(model_mutex);

        for (uint64_t layer_handle : created_model_layers)
        {
            auto it = layer_handles_to_layer_ids.find(layer_handle);
            if (it == layer_handles_to_layer_ids.end()) continue;

            uint32_t layer_id = it->second;

            if (layer_ids_to_active_layer_handles.find(layer_id)
                == layer_ids_to_active_layer_handles.end())
            {
                // No layer with this id is active.
                layer_ids_to_active_layer_handles.insert({layer_id, layer_handle});
                updated_model_layers.insert(layer_handle);
            }
        }

        // Activates a new one if a another model with the same id exists.
        auto deactivate_model_layer_at_id = [&](uint64_t model_layer_handle, uint32_t layer_id)
        {
            if (layer_ids_to_active_layer_handles.at(layer_id) == model_layer_handle)
            {
                // The model layer was active, find another one.
                bool new_active_layer_found = false;

                for (auto& it : layer_handles_to_layer_ids)
                {
                    uint64_t replacement_layer_handle = it.first;
                    uint32_t replacement_layer_id = it.second;
                    if (replacement_layer_id == layer_id)
                    {
                        new_active_layer_found = true;
                        layer_ids_to_active_layer_handles[layer_id] = replacement_layer_handle;
                        break;
                    }
                }

                if (!new_active_layer_found)
                {
                    layer_ids_to_active_layer_handles.erase(layer_id);
                }

                updated_layers.insert(layer_id);
            }
        };

        for (uint64_t layer_handle : destroyed_model_layers)
        {
            auto it = layer_handles_to_layer_ids.find(layer_handle);
            if (it == layer_handles_to_layer_ids.end()) continue;

            uint32_t layer_id = it->second;

            layer_handles_to_layer_ids.erase(it);

            deactivate_model_layer_at_id(layer_handle, layer_id);
        }

        for (auto update_headers_it : updated_headers_model_layers)
        {
            uint64_t layer_handle = update_headers_it.first;

            auto layer_it = layer_handles_to_layer_ids.find(layer_handle);
            if (layer_it == layer_handles_to_layer_ids.end()) continue;

            uint32_t layer_id = layer_it->second;

            auto active_layer_it = layer_ids_to_active_layer_handles.find(layer_id);
            if (active_layer_it == layer_ids_to_active_layer_handles.end()) continue;

            uint32_t source_index = update_headers_it.second;

            if (update_source_headers(layer_handle, layer_id, source_index, scene_model))
            {
                updated_model_layers.insert(layer_handle);
            }
        }

        for (uint64_t layer_handle : updated_model_layers)
        {
            auto layer_it = layer_handles_to_layer_ids.find(layer_handle);
            if (layer_it == layer_handles_to_layer_ids.end()) continue;

            uint32_t previous_layer_id = layer_it->second;

            hrz::SceneModelAccessor accessor(scene_model);
            hrz_proto::LayerHandle handle;
            handle.set_opaque(layer_handle);
            hrz_proto::VectorDataLayerPathBuilder<hrz::SceneModelAccessor> builder(
                accessor, handle);
            uint32_t new_layer_id = builder.id().get();

            if (new_layer_id == previous_layer_id)
            {
                auto layer_id = previous_layer_id;

                if (layer_ids_to_active_layer_handles.at(layer_id) == layer_handle)
                {
                    // The layer was, and it still is, active.
                    updated_layers.insert(layer_id);
                }
            }
            else
            {
                layer_handles_to_layer_ids[layer_handle] = new_layer_id;

                deactivate_model_layer_at_id(layer_handle, previous_layer_id);

                if (layer_ids_to_active_layer_handles.find(new_layer_id)
                    == layer_ids_to_active_layer_handles.end())
                {
                    // No active layer for the new id, so activate the updated model layer.
                    layer_ids_to_active_layer_handles.insert({new_layer_id, layer_handle});
                    updated_layers.insert(new_layer_id);
                }
            }
        }

        created_model_layers.clear();
        destroyed_model_layers.clear();
        updated_model_layers.clear();
        updated_headers_model_layers.clear();

        for (uint32_t layer_id : updated_layers)
        {
            for (auto& it : request_ids_to_data_requests)
            {
                const auto& request_id = it.first;
                auto& request = it.second;

                if (request.layer_id == layer_id)
                {
                    auto iterpair = tasks_to_data_request_ids.equal_range(
                        request.task.get_task_ref().make_weak_ref());
                    for (auto it = iterpair.first; it != iterpair.second;)
                    {
                        if (it->second == request_id)
                        {
                            it = tasks_to_data_request_ids.erase(it);
                        }
                        else
                        {
                            ++it;
                        }
                    }

                    request.task.release();

                    request = create_data_request(
                        request.request_id, request.layer_loader_request_id,
                        std::move(request.feature_selection), request.data_kind);
                    tasks_to_data_request_ids.insert(
                        {request.task.get_task_ref().make_weak_ref(), request_id});
                }
            }
        }

        if (!updated_layers.empty())
        {
            hrz::flat_hash_set<RequestId> updated_loaders;

            for (auto& it : request_ids_to_layer_loaders)
            {
                auto request_id = it.first;
                auto& loader = it.second;

                if (updated_layers.find(loader.layer_id) != updated_layers.end())
                {
                    // Load layer model tasks must be removed from the hash-to-task
                    // map, so that when new tasks are created, the old ones are not
                    // reused, even though they are for the same layer IDs.

                    tasks_by_hash.erase(loader.load_model_task.get_task().hash);

                    updated_loaders.insert(request_id);
                }
            }

            for (auto request_id : updated_loaders)
            {
                auto& loader = request_ids_to_layer_loaders.at(request_id);

                // It isn't possible to simply restart the existing task,
                // because the model has changed, and the sub-tasks that
                // are needed may have changed.
                // On top of that, creating sub-tasks is only done when
                // the task is in the New state, and restarting a task
                // doesn't bring it back to that state.

                loader.load_model_task =
                    TaskDependency::on_task(get_or_create_load_layer_model_task(loader.layer_id));
                loader.load_model_task.retain_data();

                loader.has_full_model_update = true;
            }
        }

        updated_layers.clear();
    }

    void collect_garbage(JobScheduler* js)
    {
        HRZ_SCOPED_SAMPLE_A("collect garbage");

        layer_models.collect_garbage();
        feature_id_lists.collect_garbage();
        attribute_values.collect_garbage();
        tile_geometries.collect_garbage();
        tasks.collect_garbage([this, js](Task& task) { clear_task(task, js); });
    }

    void activate_task(WeakTaskRef task_ref)
    {
        if (task_ref.is_valid())
        {
            auto& task = task_ref.value();
            if (!task.is_active)
            {
                active_tasks.push_back(task_ref);
                task.is_active = true;
            }
        }
    }

    void set_task_status(WeakTaskRef& task_ref, Task& task, TaskStatus status)
    {
        auto activate_if_needed =
            [this](WeakTaskRef& task_ref, Task& task, TaskStatus status_before)
        {
            if (task.status != status_before
                && (task.status == TaskStatus::New || task.status == TaskStatus::Loading))
            {
                activate_task(task_ref);
            }
        };

        auto status_before = task.status;

        task.status = status;

        if (status != TaskStatus::Blocked)
        {
            for (auto dependent_task_ref : task.dependents)
            {
                auto& dependent_task = dependent_task_ref.value();
                if (dependent_task.status == TaskStatus::Blocked)
                {
                    // If A is blocked by B, and B is blocked by C, A is unblocked
                    // when B is loaded. When C is finished loading (and changes
                    // state, and we are in this function), B is unblocked, but
                    // isn’t finished loading yet. By unlocking B, we allow it to
                    // finish its work later, change state on its own, and then
                    // unlock A.
                    // All in all, we don't want this function to be recursive.
                    auto dependent_task_status_before = dependent_task.status;
                    dependent_task.status = TaskStatus::Loading;
                    activate_if_needed(
                        dependent_task_ref, dependent_task, dependent_task_status_before);
                }
            }
        }

        activate_if_needed(task_ref, task, status_before);
    }

    void clear_task(Task& task, JobScheduler* js)
    {
        cancel_task_jobs(task, js);

        if (task.type == TaskType::LoadInMemoryVectorData)
        {
            auto& task_data = task.load_in_memory_vector_data();

            if (task_data.in_memory_vector_data_request_id.has_value())
            {
                in_memory_vector_data_channel.send(in_memory::messages::ReleaseDataRequest{
                    task_data.in_memory_vector_data_request_id.value()});
            }
        }
        else if (task.type == TaskType::LoadPmTiles)
        {
            auto& task_data = task.load_pmtiles();
            if (task_data.pmtiles)
            {
                task_data.pmtiles->destroy();
                task_data.pmtiles.reset();
            }
        }

        tasks_by_hash.erase(task.hash);
    }

    void cancel_task_jobs(Task& task, JobScheduler* js)
    {
        if (!is_loading(task.status)) return;

        switch (task.type)
        {
            case TaskType::LoadGeometry:
            case TaskType::LoadAllAttributeValues:
            case TaskType::LoadAttributeValues:
            case TaskType::LoadFeatureIds: break;
            case TaskType::LoadVectorData:
            {
                auto& task_data = task.load_vector_data();
                hrz_jobs::cancel_job(js, task_data.sort_ticket);
            }
            break;
            case TaskType::LoadVectorTileData:
            {
                auto& task_data = task.load_vector_tile_data();
                hrz_jobs::cancel_job(js, task_data.decode_ticket);
            }
            break;
            case TaskType::LoadVectorDataUrlPackage:
            {
                const auto& task_data = task.load_vector_data_url_package();
                hrz_jobs::cancel_job(js, task_data.parse_ticket);
            }
            break;
            case TaskType::LoadVectorDataPmTilesPackage:
            {
                auto& task_data = task.load_vector_data_pmtiles_package();

                if (task_data.load_pmtiles_task.has_task()
                    && task_data.load_pmtiles_task.get_task().status == TaskStatus::Loaded)
                {
                    auto& pmtiles_task = task_data.load_pmtiles_task.get_task().load_pmtiles();

                    if (pmtiles_task.pmtiles && task_data.tile_query.has_value())
                    {
                        pmtiles_task.pmtiles->cancel(task_data.tile_query.value(), js);
                        task_data.tile_query = std::nullopt;
                    }
                    else
                    {
                        assert(!task_data.tile_query.has_value());
                    }
                }
                hrz_jobs::cancel_job(js, task_data.parse_ticket);
            }
            break;
            case TaskType::LoadUntiledVectorData:
            {
                auto& task_data = task.load_untiled_vector_data();
                hrz_jobs::cancel_job(js, task_data.build_aabb_tree_ticket);
            }
            break;
            case TaskType::ExtractVectorTileData:
            {
                auto& task_data = task.extract_vector_tile_data();
                hrz_jobs::cancel_job(js, task_data.extract_ticket);
            }
            break;
            case TaskType::LoadInMemoryVectorData: break;
            case TaskType::LoadUrlData:
            {
                auto& task_data = task.load_url_data();
                if (task_data.download_request_id.has_value())
                {
                    asset_loader_channel.send(assets_loader::messages::CancelRequest{
                        task_data.download_request_id.value()});
                    tasks_waiting_for_asset_loader_message.erase(
                        task_data.download_request_id.value());
                    task_data.download_request_id = std::nullopt;
                }
                task_data.blob = {};
            }
            break;
            case TaskType::RequestClientData:
            {
                auto& task_data = task.request_client_data();
                if (task_data.client_ticket != NO_CLIENT_TICKET)
                {
                    auto& history_entry =
                        client_request_history.entries[task_data.request_history_index];
                    if (history_entry.ticket == task_data.client_ticket)
                    {
                        history_entry.status = ClientRequestHistory::RequestStatus::Canceled;
                    }

                    client_tickets_to_tasks.erase(task_data.client_ticket);
                    task_data.client_ticket = NO_CLIENT_TICKET;
                }
                hrz_jobs::cancel_job(js, task_data.move_to_blobs_ticket);
            }
            break;
            case TaskType::LoadPmTiles:
            {
                auto& task_data = task.load_pmtiles();
                if (task_data.pmtiles)
                {
                    task_data.pmtiles->destroy();
                    task_data.pmtiles.reset();
                }
            }
            break;
            case TaskType::LoadTileJson:
            case TaskType::LoadLayerModel:
            case TaskType::LoadSourceModel: break;
            default: assert(false && "Unhandled case");
        }
    }

    void unload_task_data(Task& task, bool release_dependent_task_data, JobScheduler* js)
    {
        // @Todo A pretty large part of this is duplicated from cancel_task_jobs.
        // Find a way to deduplicate that.

        if (task.type == TaskType::LoadGeometry)
        {
            auto& task_data = task.load_geometry();
            task_data.geometry.release();
            if (release_dependent_task_data)
            {
                task_data.load_vector_data_task.release_data();
            }
        }
        else if (task.type == TaskType::LoadAllAttributeValues)
        {
            auto& task_data = task.load_all_attribute_values();
            if (release_dependent_task_data)
            {
                for (auto& attribute_task_ref : task_data.attribute_tasks)
                {
                    attribute_task_ref.release_data();
                }
            }
        }
        else if (task.type == TaskType::LoadAttributeValues)
        {
            auto& task_data = task.load_attribute_values();
            task_data.attribute_values.release();
            if (release_dependent_task_data)
            {
                task_data.load_vector_data_task.release_data();
            }
        }
        else if (task.type == TaskType::LoadFeatureIds)
        {
            auto& task_data = task.load_feature_ids();
            task_data.feature_ids.release();
            if (release_dependent_task_data)
            {
                task_data.load_vector_data_task.release_data();
            }
        }
        else if (task.type == TaskType::LoadVectorData)
        {
            auto& task_data = task.load_vector_data();

            if (hrz_jobs::is_job_valid(js, task_data.sort_ticket))
            {
                hrz_jobs::cancel_job(js, task_data.sort_ticket);
            }

            task_data.feature_ids.release();
            task_data.geometry.release();

            for (auto& it : task_data.attribute_ids_to_values)
            {
                it.second.release();
            }
            task_data.attribute_ids_to_values.clear();

            if (release_dependent_task_data)
            {
                task_data.load_vector_tile_data_task.release_data();
                task_data.load_in_memory_vector_data_task.release_data();
                task_data.request_client_data_task.release_data();
                task_data.extract_vector_tile_data_task.release_data();
                task_data.load_feature_ids_task.release_data();
            }
        }
        else if (task.type == TaskType::LoadVectorTileData)
        {
            auto& task_data = task.load_vector_tile_data();

            if (hrz_jobs::is_job_valid(js, task_data.decode_ticket))
            {
                hrz_jobs::cancel_job(js, task_data.decode_ticket);
            }

            task_data.feature_ids.release();
            task_data.geometry.release();

            for (auto& it : task_data.attribute_ids_to_values)
            {
                it.second.release();
            }
            task_data.attribute_ids_to_values.clear();

            if (release_dependent_task_data)
            {
                task_data.load_vector_data_url_package_task.release_data();
                task_data.load_vector_data_pmtiles_package_task.release_data();
            }
        }
        else if (task.type == TaskType::LoadVectorDataUrlPackage)
        {
            auto& task_data = task.load_vector_data_url_package();

            if (hrz_jobs::is_job_valid(js, task_data.parse_ticket))
            {
                hrz_jobs::cancel_job(js, task_data.parse_ticket);
            }

            task_data.package = std::nullopt;

            if (release_dependent_task_data)
            {
                task_data.load_url_data_task.release_data();
            }
        }
        else if (task.type == TaskType::LoadVectorDataPmTilesPackage)
        {
            auto& task_data = task.load_vector_data_pmtiles_package();

            if (task_data.load_pmtiles_task.has_task()
                && task_data.load_pmtiles_task.get_task().status == TaskStatus::Loaded)
            {
                auto& pmtiles_task = task_data.load_pmtiles_task.get_task().load_pmtiles();

                if (pmtiles_task.pmtiles && task_data.tile_query.has_value())
                {
                    pmtiles_task.pmtiles->cancel(task_data.tile_query.value(), js);
                    task_data.tile_query = std::nullopt;
                }
                else
                {
                    assert(!task_data.tile_query.has_value());
                }
            }
            hrz_jobs::cancel_job(js, task_data.parse_ticket);

            task_data.package = std::nullopt;

            if (release_dependent_task_data)
            {
                task_data.load_pmtiles_task.release_data();
            }
        }
        else if (task.type == TaskType::LoadUntiledVectorData)
        {
            auto& task_data = task.load_untiled_vector_data();

            if (hrz_jobs::is_job_valid(js, task_data.build_aabb_tree_ticket))
            {
                hrz_jobs::cancel_job(js, task_data.build_aabb_tree_ticket);
            }

            task_data.aabb_tree = {};
            task_data.feature_ids.release();
            task_data.geometry.release();

            for (auto& it : task_data.attribute_ids_to_values)
            {
                it.second.release();
            }
            task_data.attribute_ids_to_values.clear();

            if (release_dependent_task_data)
            {
                task_data.load_vector_tile_data_task.release_data();
            }
        }
        else if (task.type == TaskType::ExtractVectorTileData)
        {
            auto& task_data = task.extract_vector_tile_data();

            if (hrz_jobs::is_job_valid(js, task_data.extract_ticket))
            {
                hrz_jobs::cancel_job(js, task_data.extract_ticket);
            }

            task_data.feature_ids.release();
            task_data.geometry.release();

            for (auto& it : task_data.attribute_ids_to_values)
            {
                it.second.release();
            }
            task_data.attribute_ids_to_values.clear();

            if (release_dependent_task_data)
            {
                task_data.load_untiled_vector_tile_data_task.release_data();
            }
        }
        else if (task.type == TaskType::LoadInMemoryVectorData)
        {
            auto& task_data = task.load_in_memory_vector_data();

            task_data.feature_ids.release();
            task_data.geometry.release();

            for (auto& it : task_data.attribute_ids_to_values)
            {
                it.second.release();
            }
            task_data.attribute_ids_to_values.clear();

            if (release_dependent_task_data)
            {
                task_data.load_feature_ids_task.release_data();
            }
        }
        else if (task.type == TaskType::LoadUrlData)
        {
            auto& task_data = task.load_url_data();

            if (task_data.download_request_id.has_value())
            {
                asset_loader_channel.send(
                    assets_loader::messages::CancelRequest{task_data.download_request_id.value()});
                tasks_waiting_for_asset_loader_message.erase(task_data.download_request_id.value());
                task_data.download_request_id = std::nullopt;
            }

            task_data.blob = {};
        }
        else if (task.type == TaskType::RequestClientData)
        {
            auto& task_data = task.request_client_data();

            if (task_data.client_ticket != NO_CLIENT_TICKET)
            {
                auto& history_entry =
                    client_request_history.entries[task_data.request_history_index];
                if (history_entry.ticket == task_data.client_ticket)
                {
                    history_entry.status = ClientRequestHistory::RequestStatus::Canceled;
                }

                client_tickets_to_tasks.erase(task_data.client_ticket);
                task_data.client_ticket = NO_CLIENT_TICKET;
            }

            task_data.feature_ids.release();
            task_data.geometry.release();

            for (auto& it : task_data.attribute_ids_to_values)
            {
                it.second.release();
            }
            task_data.attribute_ids_to_values.clear();

            task_data.client_response = std::nullopt;

            if (hrz_jobs::is_job_valid(js, task_data.move_to_blobs_ticket))
            {
                hrz_jobs::cancel_job(js, task_data.move_to_blobs_ticket);
            }

            // Do not release features IDs from the load feature IDs task.
            // Otherwise, client invalidations by feature IDs cannot be
            // checked, and validating values accessed by tile when this
            // source isn't the geometry source, after they have been
            // invalidated, must obtain feature IDs from the geometry
            // source and likely download vector data again.
        }
        else if (task.type == TaskType::LoadTileJson)
        {
            auto& task_data = task.load_tilejson();

            if (release_dependent_task_data)
            {
                task_data.load_url_data_task.release_data();
            }
        }
        else if (task.type == TaskType::LoadPmTiles)
        {
            auto& task_data = task.load_pmtiles();
            if (task_data.pmtiles)
            {
                task_data.pmtiles->destroy();
                task_data.pmtiles.reset();
            }
        }
        else if (task.type == TaskType::LoadLayerModel)
        {
            auto& task_data = task.load_layer_model();

            task_data.layer_model.release();

            if (release_dependent_task_data)
            {
                for (auto& task : task_data.load_source_model_tasks)
                {
                    task.release_data();
                }
            }
        }
        else if (task.type == TaskType::LoadSourceModel)
        {
            auto& task_data = task.load_source_model();

            if (release_dependent_task_data)
            {
                task_data.load_tilejson_task.release_data();
                task_data.load_pmtiles_task.release_data();
                task_data.load_untiled_vector_data_task.release_data();
            }
        }
        else
        {
            assert(false && "Unhandled case");
        }
    }

    void restart_task(WeakTaskRef& task_ref, Task& task, hrz::JobScheduler* js)
    {
        if (task.status == TaskStatus::New || task.status == TaskStatus::ModelError) return;

        if (is_loading(task.status) || task.status == TaskStatus::Loaded
            || task.status == TaskStatus::Unloaded || task.status == TaskStatus::DataError)
        {
            cancel_task_jobs(task, js);
            unload_task_data(task, false, js);

            task.version += 1;

            set_task_status(task_ref, task, TaskStatus::Unloaded);
            activate_task(task_ref);

            auto send_new_data_message = [&](const LayerModel& layer_model)
            {
                auto iterpair = layer_ids_to_request_ids.equal_range(layer_model.id);
                for (auto it = iterpair.first; it != iterpair.second; ++it)
                {
                    auto request_id = it->second;
                    auto loader_it = request_ids_to_layer_loaders.find(request_id);
                    if (loader_it != request_ids_to_layer_loaders.end())
                    {
                        send_layer_has_new_data_message(loader_it->second);
                    }
                }
            };

            auto restart_tasks_from_requests = [&]()
            {
                auto iterpair = tasks_to_data_request_ids.equal_range(task_ref);
                for (auto it = iterpair.first; it != iterpair.second; ++it)
                {
                    auto& request = request_ids_to_data_requests.at(it->second);
                    request.task.retain_data();
                }
            };

            // These three task types are the ones that are created when requesting data.
            // There is no need to increment the layer model data version in the other
            // cases as they always have at least one task of one of these types among
            // their (transitive) dependents.
            // If a data request from a message depends on the task, we retain the data
            // so that it gets reloaded and then sent to the requester in a data message.
            switch (task.type)
            {
                case TaskType::LoadGeometry:
                {
                    auto& layer_model = task.load_geometry().layer_model.value();
                    layer_model.data_version += 1;
                    send_new_data_message(layer_model);
                    restart_tasks_from_requests();
                    break;
                }
                case TaskType::LoadAllAttributeValues:
                {
                    auto& layer_model = task.load_all_attribute_values().layer_model.value();
                    layer_model.data_version += 1;
                    send_new_data_message(layer_model);
                    restart_tasks_from_requests();
                    break;
                }
                case TaskType::LoadFeatureIds:
                {
                    auto& layer_model = task.load_feature_ids().layer_model.value();
                    layer_model.data_version += 1;
                    send_new_data_message(layer_model);
                    restart_tasks_from_requests();
                    break;
                }
                default: break;
            }
        }

        for (auto dependent_task_ref : task.dependents)
        {
            restart_task(dependent_task_ref, dependent_task_ref.value(), js);
        }
    }

    void work_new_task(
        WeakTaskRef& task_ref,
        Task& task,
        SceneModel* scene_model,
        AttributionRegistry* attributions)
    {
        HRZ_SCOPED_SAMPLE_A("work new task");

        assert(task.status == TaskStatus::New);

        if (task.type == TaskType::LoadGeometry)
        {
            auto& task_data = task.load_geometry();
            const auto& layer_model = task_data.layer_model.value();

            if (layer_model.geometry_source != LayerModel::NO_SOURCE)
            {
                task_data.load_vector_data_task = TaskDependency::between_tasks(
                    get_or_create_load_vector_data_task(
                        task_data.layer_model, layer_model.geometry_source,
                        task_data.feature_selection),
                    task_ref);
                set_task_status(task_ref, task, TaskStatus::Unloaded);
            }
            else
            {
                HRZ_LOG_ERROR("No geometry source provided.");
                set_task_status(task_ref, task, TaskStatus::ModelError);
            }
        }
        else if (task.type == TaskType::LoadAllAttributeValues)
        {
            auto& task_data = task.load_all_attribute_values();
            const auto& layer_model = task_data.layer_model.value();
            for (auto& it : layer_model.attributes)
            {
                const auto& attribute = it.second;
                auto sub_task = TaskDependency::between_tasks(
                    get_or_create_load_attribute_values_task(
                        task_data.layer_model, attribute.id, task_data.feature_selection),
                    task_ref);
                task_data.attribute_tasks.push_back(std::move(sub_task));
            }

            set_task_status(task_ref, task, TaskStatus::Unloaded);
        }
        else if (task.type == TaskType::LoadAttributeValues)
        {
            auto& task_data = task.load_attribute_values();
            const auto& layer_model = task_data.layer_model.value();
            const auto& attribute = layer_model.attributes.at(task_data.attribute_id);

            task_data.load_vector_data_task = TaskDependency::between_tasks(
                get_or_create_load_vector_data_task(
                    task_data.layer_model, attribute.data_source, task_data.feature_selection),
                task_ref);
            set_task_status(task_ref, task, TaskStatus::Unloaded);
        }
        else if (task.type == TaskType::LoadFeatureIds)
        {
            auto& task_data = task.load_feature_ids();
            const auto& layer_model = task_data.layer_model.value();

            if (task_data.feature_selection.has_feature_ids())
            {
                task_data.feature_ids = task_data.feature_selection.feature_ids();
                set_task_status(task_ref, task, TaskStatus::Loaded);
            }
            else if (
                layer_model.geometry_source != LayerModel::NO_SOURCE
                || layer_model.feature_id_source != LayerModel::NO_SOURCE)
            {
                if (task_data.feature_selection.has_tile_coords())
                {
                    // Geometry source has priority over feature ID source.
                    auto data_source = layer_model.geometry_source != LayerModel::NO_SOURCE
                        ? layer_model.geometry_source
                        : layer_model.feature_id_source;

                    task_data.load_vector_data_task = TaskDependency::between_tasks(
                        get_or_create_load_vector_data_task(
                            task_data.layer_model, data_source, task_data.feature_selection),
                        task_ref);
                    set_task_status(task_ref, task, TaskStatus::Unloaded);
                }
                else
                {
                    assert(false && "Unhandled case");
                    set_task_status(task_ref, task, TaskStatus::ModelError);
                }
            }
            else
            {
                HRZ_LOG_ERROR("No source for feature IDs provided.");
                set_task_status(task_ref, task, TaskStatus::ModelError);
            }
        }
        else if (task.type == TaskType::LoadVectorData)
        {
            auto& task_data = task.load_vector_data();
            const auto& layer_model = task_data.layer_model.value();
            const auto& data_source = layer_model.data_sources.at(task_data.data_source);

            auto load_feature_ids_if_needed = [&]()
            {
                // If the data source is not the one authoritative for the order of
                // features in a tile (i.e. is not the source for geometry), and is
                // accessed by tile, the vector data will need to be reordered to
                // conform to the authoritative order.
                // Features IDs are loaded as they will be in the right order and
                // serve as parameter to the reordering job.
                if (task_data.data_source != layer_model.geometry_source
                    && !task_data.feature_selection.has_feature_ids())
                {
                    task_data.load_feature_ids_task = TaskDependency::between_tasks(
                        get_or_create_load_feature_ids_task(
                            task_data.layer_model, task_data.feature_selection),
                        task_ref);
                }
            };

            if (data_source.has_tiled_data_provider() || data_source.has_tilejson_data_provider()
                || data_source.has_pmtiles_data_provider())
            {
                if (task_data.feature_selection.has_tile_coords())
                {
                    task_data.load_vector_tile_data_task = TaskDependency::between_tasks(
                        get_or_create_load_vector_tile_data_task(
                            task_data.layer_model, task_data.data_source,
                            task_data.feature_selection),
                        task_ref);

                    load_feature_ids_if_needed();

                    set_task_status(task_ref, task, TaskStatus::Unloaded);
                }
                else
                {
                    HRZ_LOG_ERROR("No tile coords provided for tiled data access.");
                    set_task_status(task_ref, task, TaskStatus::ModelError);
                }
            }
            else if (data_source.has_in_memory_data_provider())
            {
                task_data.load_in_memory_vector_data_task = TaskDependency::between_tasks(
                    get_or_create_load_in_memory_vector_data_task(
                        task_data.layer_model, task_data.data_source, task_data.feature_selection),
                    task_ref);

                load_feature_ids_if_needed();

                set_task_status(task_ref, task, TaskStatus::Unloaded);
            }
            else if (data_source.has_client_data_provider())
            {
                task_data.request_client_data_task = TaskDependency::between_tasks(
                    get_or_create_request_client_data_task(
                        task_data.layer_model, task_data.data_source, task_data.feature_selection),
                    task_ref);

                // Client data is expected to be already sorted.
                // There is no need to load the feature IDs as the client task handles it.

                set_task_status(task_ref, task, TaskStatus::Unloaded);
            }
            else if (data_source.has_untiled_data_provider())
            {
                if (task_data.feature_selection.has_tile_coords())
                {
                    task_data.extract_vector_tile_data_task = TaskDependency::between_tasks(
                        get_or_create_extract_vector_tile_data_task(
                            task_data.layer_model, task_data.data_source,
                            task_data.feature_selection.tile_coords()),
                        task_ref);

                    load_feature_ids_if_needed();

                    set_task_status(task_ref, task, TaskStatus::Unloaded);
                }
                else
                {
                    HRZ_LOG_ERROR("No tile coords provided for untiled data access.");
                    set_task_status(task_ref, task, TaskStatus::ModelError);
                }
            }
            else
            {
                assert(false && "Unhandled case");
                set_task_status(task_ref, task, TaskStatus::ModelError);
            }
        }
        else if (task.type == TaskType::LoadVectorTileData)
        {
            auto& task_data = task.load_vector_tile_data();
            const auto& layer_model = task_data.layer_model.value();
            const auto& data_source = layer_model.data_sources.at(task_data.data_source);

            assert(task_data.feature_selection.has_tile_coords());

            if (data_source.has_tiled_data_provider())
            {
                const auto& provider = data_source.tiled_data_provider();

                task_data.load_vector_data_url_package_task = TaskDependency::between_tasks(
                    get_or_create_load_vector_data_url_package_task(
                        make_url(
                            provider.tile_url_generator, task_data.feature_selection.tile_coords()),
                        provider.headers, get_load_queue(layer_model),
                        compute_tile_loading_priority(
                            layer_model, task_data.feature_selection.tile_coords()),
                        provider.format,
                        hrz::monitoring::ResourceOwner(
                            hrz::monitoring::systems::VectorDataLoader, layer_model.layer_handle),
                        data_source.request_count_metric),
                    task_ref);

                set_task_status(task_ref, task, TaskStatus::Unloaded);
            }
            else if (data_source.has_untiled_data_provider())
            {
                const auto& provider = data_source.untiled_data_provider();

                task_data.load_vector_data_url_package_task = TaskDependency::between_tasks(
                    get_or_create_load_vector_data_url_package_task(
                        provider.url, provider.headers, get_load_queue(layer_model),
                        compute_tile_loading_priority(
                            layer_model, task_data.feature_selection.tile_coords()),
                        provider.format,
                        hrz::monitoring::ResourceOwner(
                            hrz::monitoring::systems::VectorDataLoader, layer_model.layer_handle),
                        data_source.request_count_metric),
                    task_ref);

                set_task_status(task_ref, task, TaskStatus::Unloaded);
            }
            else if (data_source.has_tilejson_data_provider())
            {
                const auto& provider = data_source.tilejson_data_provider();

                task_data.load_vector_data_url_package_task = TaskDependency::between_tasks(
                    get_or_create_load_vector_data_url_package_task(
                        make_url(
                            provider.load_tilejson_task.get_task()
                                .load_tilejson()
                                .tile_url_generator,
                            task_data.feature_selection.tile_coords()),
                        provider.headers, get_load_queue(layer_model),
                        compute_tile_loading_priority(
                            layer_model, task_data.feature_selection.tile_coords()),
                        hrz_proto::VectorDataFormat::MVT_VECTOR_DATA,
                        hrz::monitoring::ResourceOwner(
                            hrz::monitoring::systems::VectorDataLoader, layer_model.layer_handle),
                        data_source.request_count_metric),
                    task_ref);

                set_task_status(task_ref, task, TaskStatus::Unloaded);
            }
            else if (data_source.has_pmtiles_data_provider())
            {
                const auto& provider = data_source.pmtiles_data_provider();

                task_data.load_vector_data_pmtiles_package_task = TaskDependency::between_tasks(
                    get_or_create_load_vector_data_pmtiles_package_task(
                        provider.url, provider.headers, task_data.feature_selection.tile_coords(),
                        get_load_queue(layer_model),
                        compute_tile_loading_priority(
                            layer_model, task_data.feature_selection.tile_coords()),
                        hrz::monitoring::ResourceOwner(
                            hrz::monitoring::systems::VectorDataLoader, layer_model.layer_handle),
                        data_source.request_count_metric),
                    task_ref);

                set_task_status(task_ref, task, TaskStatus::Unloaded);
            }
            else
            {
                assert(false && "Unhandled case");
                set_task_status(task_ref, task, TaskStatus::ModelError);
            }
        }
        else if (task.type == TaskType::LoadVectorDataUrlPackage)
        {
            auto& task_data = task.load_vector_data_url_package();

            task_data.load_url_data_task = TaskDependency::between_tasks(
                get_or_create_load_url_data_task(
                    task_data.url, task_data.headers, task_data.queue, task_data.priority,
                    task_data.resource_owner, task_data.request_count_metric),
                task_ref);

            set_task_status(task_ref, task, TaskStatus::Unloaded);
        }
        else if (task.type == TaskType::LoadVectorDataPmTilesPackage)
        {
            auto& task_data = task.load_vector_data_pmtiles_package();

            task_data.load_pmtiles_task = TaskDependency::between_tasks(
                get_or_create_load_pmtiles_task(
                    task_data.url, task_data.headers, task_data.queue, task_data.priority,
                    task_data.resource_owner, task_data.request_count_metric),
                task_ref);

            set_task_status(task_ref, task, TaskStatus::Unloaded);
        }
        else if (task.type == TaskType::LoadUntiledVectorData)
        {
            auto& task_data = task.load_untiled_vector_data();
            const auto& layer_model = task_data.layer_model.value();
            const auto& data_source = layer_model.data_sources.at(task_data.data_source);
            const auto& provider = data_source.untiled_data_provider();

            if (provider.format != hrz_proto::GEOJSON_VECTOR_DATA
                && provider.format != hrz_proto::GEOBUF_VECTOR_DATA)
            {
                HRZ_LOG_ERROR(
                    "Untiled vector data providers only support the GeoJSON and Geobuf formats.");
                set_task_status(task_ref, task, TaskStatus::ModelError);
            }
            else
            {
                task_data.load_vector_tile_data_task = TaskDependency::between_tasks(
                    get_or_create_load_vector_tile_data_task(
                        task_data.layer_model, task_data.data_source,
                        FeatureSelection{TileCoords{}}),
                    task_ref);

                set_task_status(task_ref, task, TaskStatus::Unloaded);
            }
        }
        else if (task.type == TaskType::ExtractVectorTileData)
        {
            auto& task_data = task.extract_vector_tile_data();
            const auto& layer_model = task_data.layer_model.value();
            const auto& data_source = layer_model.data_sources.at(task_data.data_source);
            const auto& provider = data_source.untiled_data_provider();

            if (provider.format != hrz_proto::GEOJSON_VECTOR_DATA
                && provider.format != hrz_proto::GEOBUF_VECTOR_DATA)
            {
                HRZ_LOG_ERROR(
                    "Untiled vector data providers only support the GeoJSON and Geobuf formats.");
                set_task_status(task_ref, task, TaskStatus::ModelError);
            }
            else
            {
                task_data.load_untiled_vector_tile_data_task = TaskDependency::between_tasks(
                    get_or_create_load_untiled_vector_data_task(
                        task_data.layer_model, task_data.data_source),
                    task_ref);

                set_task_status(task_ref, task, TaskStatus::Unloaded);
            }
        }
        else if (task.type == TaskType::LoadInMemoryVectorData)
        {
            auto& task_data = task.load_in_memory_vector_data();
            const auto& layer_model = task_data.layer_model.value();

            if (task_data.data_source == layer_model.geometry_source
                && !task_data.feature_selection.has_tile_coords())
            {
                HRZ_LOG_ERROR(
                    "No tile coords provided for retrieving geometries from in-memory vector data "
                    "layer.");
                set_task_status(task_ref, task, TaskStatus::ModelError);
            }
            else
            {
                if (task_data.data_source != layer_model.geometry_source
                    && !task_data.feature_selection.has_feature_ids())
                {
                    task_data.load_feature_ids_task = TaskDependency::between_tasks(
                        get_or_create_load_feature_ids_task(
                            task_data.layer_model, task_data.feature_selection),
                        task_ref);
                }

                set_task_status(task_ref, task, TaskStatus::Unloaded);
            }
        }
        else if (task.type == TaskType::LoadUrlData)
        {
            set_task_status(task_ref, task, TaskStatus::Unloaded);
        }
        else if (task.type == TaskType::RequestClientData)
        {
            auto& task_data = task.request_client_data();
            const auto& layer_model = task_data.layer_model.value();
            const auto& data_source = layer_model.data_sources.at(task_data.data_source);
            const auto access = data_source.client_data_provider().access;

            if (access == hrz_proto::VectorDataSourceAccess::ACCESS_BY_TILE
                && !task_data.feature_selection.has_tile_coords())
            {
                HRZ_LOG_ERROR(
                    "Cannot request client data using feature IDs when the client data "
                    "source uses an access by tile");
                set_task_status(task_ref, task, TaskStatus::ModelError);
            }
            else if (
                access == hrz_proto::VectorDataSourceAccess::ACCESS_BY_FEATURE_ID
                && layer_model.geometry_source == task_data.data_source)
            {
                HRZ_LOG_ERROR("Cannot request client vector geometry using feature IDs");
                set_task_status(task_ref, task, TaskStatus::ModelError);
            }
            else if (layer_model.geometry_source != task_data.data_source)
            {
                // Even if the provider's access has been set to an access by tile and not by
                // feature ids, we still require the feature ids so that we match the client values
                // with the features of the tile.
                task_data.load_feature_ids_task = TaskDependency::between_tasks(
                    get_or_create_load_feature_ids_task(
                        task_data.layer_model, task_data.feature_selection),
                    task_ref);

                set_task_status(task_ref, task, TaskStatus::Unloaded);
            }
            else
            {
                set_task_status(task_ref, task, TaskStatus::Unloaded);
            }
        }
        else if (task.type == TaskType::LoadTileJson)
        {
            auto& task_data = task.load_tilejson();

            task_data.load_url_data_task = TaskDependency::between_tasks(
                get_or_create_load_url_data_task(
                    task_data.url, task_data.headers, task_data.queue,
                    hrz::combine_loading_priorities(
                        task_data.priority, std::numeric_limits<uint16_t>::max()),
                    task_data.resource_owner, task_data.request_count_metric),
                task_ref);

            set_task_status(task_ref, task, TaskStatus::Unloaded);
        }
        else if (task.type == TaskType::LoadPmTiles)
        {
            set_task_status(task_ref, task, TaskStatus::Unloaded);
        }
        else if (task.type == TaskType::LoadLayerModel)
        {
            auto& task_data = task.load_layer_model();

            auto layer_handle_it = layer_ids_to_active_layer_handles.find(task_data.layer_id);
            if (layer_handle_it != layer_ids_to_active_layer_handles.end())
            {
                uint64_t layer_handle = layer_handle_it->second;

                hrz::SceneModelAccessor accessor(scene_model);
                hrz_proto::LayerHandle handle;
                handle.set_opaque(layer_handle);
                hrz_proto::VectorDataLayerPathBuilder<hrz::SceneModelAccessor> builder(
                    accessor, handle);

                task_data.layer_model = layer_models.alloc();
                auto& layer_model = task_data.layer_model.value();
                layer_model = make_model(attributions, builder.get(), layer_handle);

                for (uint32_t s = 0; s < layer_model.data_sources.size(); ++s)
                {
                    task_data.load_source_model_tasks.push_back(TaskDependency::between_tasks(
                        get_or_create_load_source_model_task(task_data.layer_model, s), task_ref));
                }

                set_task_status(task_ref, task, TaskStatus::Unloaded);
            }
            else
            {
                set_task_status(task_ref, task, TaskStatus::ModelError);

                auto iterpair = layer_ids_to_request_ids.equal_range(task_data.layer_id);
                for (auto it = iterpair.first; it != iterpair.second; ++it)
                {
                    auto request_id = it->second;
                    auto loader_it = request_ids_to_layer_loaders.find(request_id);
                    if (loader_it != request_ids_to_layer_loaders.end())
                    {
                        auto& loader = loader_it->second;
                        loader.has_full_model_update = true;
                        send_layer_model_error_message(loader);
                    }
                }
            }
        }
        else if (task.type == TaskType::LoadSourceModel)
        {
            auto& task_data = task.load_source_model();
            auto& layer_model = task_data.layer_model.value();
            auto& data_source = layer_model.data_sources.at(task_data.data_source);

            if (data_source.has_tiled_data_provider())
            {
                const auto& provider = data_source.tiled_data_provider();

                data_source.min_lod = {provider.min_lod};
                data_source.max_lod = {provider.max_lod};
                data_source.bounds = {provider.bounds};

                set_task_status(task_ref, task, TaskStatus::Loaded);
            }
            else if (data_source.has_client_data_provider())
            {
                const auto& provider = data_source.client_data_provider();

                if (provider.access == hrz_proto::VectorDataSourceAccess::ACCESS_BY_TILE)
                {
                    data_source.min_lod = {provider.min_lod};
                    data_source.max_lod = {provider.max_lod};
                    data_source.bounds = {provider.bounds};
                }

                set_task_status(task_ref, task, TaskStatus::Loaded);
            }
            else if (data_source.has_in_memory_data_provider())
            {
                data_source.min_lod = {0};
                data_source.max_lod = {24};
                data_source.bounds = hrz::GeoBounds::full();

                set_task_status(task_ref, task, TaskStatus::Loaded);
            }
            else if (data_source.has_tilejson_data_provider())
            {
                const auto& provider = data_source.tilejson_data_provider();

                task_data.load_tilejson_task = TaskDependency::between_tasks(
                    get_or_create_load_tilejson_task(
                        provider.url, provider.headers, get_load_queue(layer_model),
                        hrz::combine_loading_priorities(
                            layer_model.loading_priority, std::numeric_limits<uint16_t>::max()),
                        provider.preserve_query_parameters,
                        hrz::monitoring::ResourceOwner(
                            hrz::monitoring::systems::VectorDataLoader, layer_model.layer_handle),
                        data_source.request_count_metric),
                    task_ref);
                set_task_status(task_ref, task, TaskStatus::Unloaded);
            }
            else if (data_source.has_pmtiles_data_provider())
            {
                const auto& provider = data_source.pmtiles_data_provider();

                task_data.load_pmtiles_task = TaskDependency::between_tasks(
                    get_or_create_load_pmtiles_task(
                        provider.url, provider.headers, get_load_queue(layer_model),
                        hrz::combine_loading_priorities(
                            layer_model.loading_priority, std::numeric_limits<uint16_t>::max()),
                        hrz::monitoring::ResourceOwner(
                            hrz::monitoring::systems::VectorDataLoader, layer_model.layer_handle),
                        data_source.request_count_metric),
                    task_ref);
                set_task_status(task_ref, task, TaskStatus::Unloaded);
            }
            else if (data_source.has_untiled_data_provider())
            {
                const auto& provider = data_source.untiled_data_provider();

                data_source.min_lod = {provider.min_lod};
                data_source.max_lod = {provider.max_lod};
                data_source.bounds = {provider.bounds};

                task_data.load_untiled_vector_data_task = TaskDependency::between_tasks(
                    get_or_create_load_untiled_vector_data_task(
                        task_data.layer_model, task_data.data_source),
                    task_ref);
                set_task_status(task_ref, task, TaskStatus::Unloaded);
            }
            else
            {
                assert(false && "Unhandled case");
                set_task_status(task_ref, task, TaskStatus::ModelError);
            }
        }
        else
        {
            assert(false && "Unhandled case");
        }
    }

    void work_unloaded_task(WeakTaskRef& task_ref, Task& task)
    {
        HRZ_SCOPED_SAMPLE_A("work unloaded task");

        assert(task.status == TaskStatus::Unloaded);

        if (task.data_use_count == 0) return;

        if (task.type == TaskType::LoadGeometry)
        {
            auto& task_data = task.load_geometry();
            task_data.load_vector_data_task.retain_data();
        }
        else if (task.type == TaskType::LoadAllAttributeValues)
        {
            auto& task_data = task.load_all_attribute_values();
            for (auto& attribute_task_ref : task_data.attribute_tasks)
            {
                attribute_task_ref.retain_data();
            }
        }
        else if (task.type == TaskType::LoadAttributeValues)
        {
            auto& task_data = task.load_attribute_values();
            task_data.load_vector_data_task.retain_data();
        }
        else if (task.type == TaskType::LoadFeatureIds)
        {
            auto& task_data = task.load_feature_ids();
            task_data.load_vector_data_task.retain_data();
        }
        else if (task.type == TaskType::LoadVectorData)
        {
            auto& task_data = task.load_vector_data();
            task_data.load_vector_tile_data_task.retain_data();
            task_data.load_in_memory_vector_data_task.retain_data();
            task_data.request_client_data_task.retain_data();
            task_data.extract_vector_tile_data_task.retain_data();
            task_data.load_feature_ids_task.retain_data();
        }
        else if (task.type == TaskType::LoadVectorTileData)
        {
            auto& task_data = task.load_vector_tile_data();
            task_data.load_vector_data_url_package_task.retain_data();
            task_data.load_vector_data_pmtiles_package_task.retain_data();
        }
        else if (task.type == TaskType::LoadVectorDataUrlPackage)
        {
            auto& task_data = task.load_vector_data_url_package();
            task_data.load_url_data_task.retain_data();
        }
        else if (task.type == TaskType::LoadVectorDataPmTilesPackage)
        {
            auto& task_data = task.load_vector_data_pmtiles_package();
            task_data.load_pmtiles_task.retain_data();
        }
        else if (task.type == TaskType::LoadUntiledVectorData)
        {
            auto& task_data = task.load_untiled_vector_data();
            task_data.load_vector_tile_data_task.retain_data();
        }
        else if (task.type == TaskType::ExtractVectorTileData)
        {
            auto& task_data = task.extract_vector_tile_data();
            task_data.load_untiled_vector_tile_data_task.retain_data();
        }
        else if (task.type == TaskType::LoadInMemoryVectorData)
        {
            auto& task_data = task.load_in_memory_vector_data();
            task_data.load_feature_ids_task.retain_data();
        }
        else if (task.type == TaskType::LoadUrlData)
        {
            // No-op
        }
        else if (task.type == TaskType::RequestClientData)
        {
            auto& task_data = task.request_client_data();
            task_data.load_feature_ids_task.retain_data();
        }
        else if (task.type == TaskType::LoadTileJson)
        {
            auto& task_data = task.load_tilejson();
            task_data.load_url_data_task.retain_data();
        }
        else if (task.type == TaskType::LoadPmTiles)
        {
            // No-op
        }
        else if (task.type == TaskType::LoadLayerModel)
        {
            auto& task_data = task.load_layer_model();
            for (auto& task : task_data.load_source_model_tasks)
            {
                task.retain_data();
            }
        }
        else if (task.type == TaskType::LoadSourceModel)
        {
            auto& task_data = task.load_source_model();
            task_data.load_tilejson_task.retain_data();
            task_data.load_pmtiles_task.retain_data();
            task_data.load_untiled_vector_data_task.retain_data();
        }
        else
        {
            assert(false && "Unhandled case");
        }

        set_task_status(task_ref, task, TaskStatus::Loading);
    }

    void work_loading_task(
        WeakTaskRef& task_ref,
        Task& task,
        JobScheduler* js,
        BlobAllocator* ba,
        ClientMessageQueue* mq,
        AttributionRegistry* attributions)
    {
        HRZ_SCOPED_SAMPLE_A("work loading task");

        assert(task.status == TaskStatus::Loading);

        if (task.type == TaskType::LoadGeometry)
        {
            auto& task_data = task.load_geometry();
            if (task_data.load_vector_data_task.has_task())
            {
                auto& load_vector_data_task = task_data.load_vector_data_task.get_task();
                if (load_vector_data_task.status == TaskStatus::Loaded)
                {
                    task_data.geometry = load_vector_data_task.load_vector_data().geometry;
                    task_data.attribution = load_vector_data_task.load_vector_data().attribution;

                    task_data.load_vector_data_task.release_data();
                    set_task_status(task_ref, task, TaskStatus::Loaded);

                    send_geometry_message(task_ref, task);
                }
                else if (is_error(load_vector_data_task.status))
                {
                    task_data.load_vector_data_task.release_data();
                    set_task_status(task_ref, task, load_vector_data_task.status);

                    auto iterpair = tasks_to_data_request_ids.equal_range(task_ref);
                    for (auto it = iterpair.first; it != iterpair.second; ++it)
                    {
                        auto& request = request_ids_to_data_requests.at(it->second);
                        send_data_error_message(request);
                        request.task.release_data();
                    }
                }
                else
                {
                    set_task_status(task_ref, task, TaskStatus::Blocked);
                }
            }
            else
            {
                set_task_status(task_ref, task, TaskStatus::Blocked);
            }
        }
        else if (task.type == TaskType::LoadAllAttributeValues)
        {
            auto& task_data = task.load_all_attribute_values();
            bool all_attributes_loaded = true;
            std::optional<TaskStatus> error = std::nullopt;

            for (auto& attribute_task_ref : task_data.attribute_tasks)
            {
                auto& attribute_task = attribute_task_ref.get_task();

                if (is_error(attribute_task.status))
                {
                    attribute_task_ref.release_data();
                    error = attribute_task.status;
                    break;
                }
                if (attribute_task.status != TaskStatus::Loaded)
                {
                    all_attributes_loaded = false;
                    break;
                }
            }

            if (error.has_value())
            {
                set_task_status(task_ref, task, error.value());

                auto iterpair = tasks_to_data_request_ids.equal_range(task_ref);
                for (auto it = iterpair.first; it != iterpair.second; ++it)
                {
                    auto& request = request_ids_to_data_requests.at(it->second);
                    send_data_error_message(request);
                    request.task.release_data();
                }
            }
            else if (all_attributes_loaded)
            {
                set_task_status(task_ref, task, TaskStatus::Loaded);
                send_attribute_values_message(task_ref, task);
            }
            else
            {
                set_task_status(task_ref, task, TaskStatus::Blocked);
            }
        }
        else if (task.type == TaskType::LoadAttributeValues)
        {
            auto& task_data = task.load_attribute_values();
            if (task_data.load_vector_data_task.has_task())
            {
                auto& load_vector_data_task = task_data.load_vector_data_task.get_task();
                if (load_vector_data_task.status == TaskStatus::Loaded)
                {
                    auto it = load_vector_data_task.load_vector_data().attribute_ids_to_values.find(
                        task_data.attribute_id);
                    if (it
                        != load_vector_data_task.load_vector_data().attribute_ids_to_values.end())
                    {
                        // Attribute values have been retrieved.
                        const auto& values = it->second;
                        task_data.attribute_values = values;
                        set_task_status(task_ref, task, TaskStatus::Loaded);
                    }
                    else
                    {
                        HRZ_LOG_ERROR(
                            "Could not load values for attribute {}", task_data.attribute_id);
                        set_task_status(task_ref, task, TaskStatus::DataError);
                    }

                    task_data.load_vector_data_task.release_data();
                }
                else if (is_error(load_vector_data_task.status))
                {
                    task_data.load_vector_data_task.release_data();
                    set_task_status(task_ref, task, load_vector_data_task.status);
                }
                else
                {
                    set_task_status(task_ref, task, TaskStatus::Blocked);
                }
            }
        }
        else if (task.type == TaskType::LoadFeatureIds)
        {
            auto& task_data = task.load_feature_ids();
            const auto& layer_model = task_data.layer_model.value();

            if (task_data.feature_selection.has_feature_ids())
            {
                task_data.feature_ids = task_data.feature_selection.feature_ids();
                set_task_status(task_ref, task, TaskStatus::Loaded);
            }
            else if (
                layer_model.geometry_source != LayerModel::NO_SOURCE
                || layer_model.feature_id_source != LayerModel::NO_SOURCE)
            {
                if (task_data.load_vector_data_task.has_task())
                {
                    auto& load_vector_data_task = task_data.load_vector_data_task.get_task();
                    if (load_vector_data_task.status == TaskStatus::Loaded)
                    {
                        task_data.feature_ids =
                            load_vector_data_task.load_vector_data().feature_ids;
                        task_data.load_vector_data_task.release_data();
                        set_task_status(task_ref, task, TaskStatus::Loaded);
                        send_feature_ids_message(task_ref, task);
                    }
                    else if (is_error(load_vector_data_task.status))
                    {
                        task_data.load_vector_data_task.release_data();
                        set_task_status(task_ref, task, load_vector_data_task.status);

                        auto iterpair = tasks_to_data_request_ids.equal_range(task_ref);
                        for (auto it = iterpair.first; it != iterpair.second; ++it)
                        {
                            auto& request = request_ids_to_data_requests.at(it->second);
                            send_data_error_message(request);
                        }
                    }
                    else
                    {
                        set_task_status(task_ref, task, TaskStatus::Blocked);
                    }
                }
            }
            else
            {
                assert(false && "Unhandled case");
                set_task_status(task_ref, task, TaskStatus::ModelError);
            }
        }
        else if (task.type == TaskType::LoadVectorData)
        {
            auto& task_data = task.load_vector_data();
            const auto& layer_model = task_data.layer_model.value();

            // If the data comes from the source that provides the geometry,
            // then its values are in the correct order, and the data can be
            // considered to be loaded.
            // Otherwise, the data must be sorted according to the order of
            // the geometry.
            auto check_for_sorted_data = [&]()
            {
                if (task_data.data_source == layer_model.geometry_source
                    || task_data.feature_selection.has_feature_ids())
                {
                    set_task_status(task_ref, task, TaskStatus::Loaded);
                }
                else
                {
                    assert(task_data.load_feature_ids_task.has_task());
                }
            };

            if (hrz_jobs::is_job_valid(js, task_data.sort_ticket))
            {
                if (hrz_jobs::is_job_finished(js, task_data.sort_ticket))
                {
                    if (hrz_jobs::get_job_status(js, task_data.sort_ticket)
                        == hrz::job_scheduler::JobStatus::Finished_Success)
                    {
                        vector_data::SortedVectorData data;
                        hrz_jobs::get_job_response(js, task_data.sort_ticket, data);
                        size_t feature_count = task_data.load_feature_ids_task.get_task()
                                                   .load_feature_ids()
                                                   .feature_ids->size();

                        task_data.feature_ids = task_data.load_feature_ids_task.get_task()
                                                    .load_feature_ids()
                                                    .feature_ids;

                        task_data.geometry = tile_geometries.alloc();
                        task_data.geometry.value() = std::move(data.geometry.value());

                        load_attribute_data_into_map(
                            data.attributes, task_data.attribute_ids_to_values, layer_model,
                            feature_count);

                        set_task_status(task_ref, task, TaskStatus::Loaded);
                    }
                    else
                    {
                        HRZ_LOG_ERROR(
                            "Could not sort data of tile of vector data layer {}", layer_model.id);
                        hrz_jobs::cancel_job(js, task_data.sort_ticket);
                        set_task_status(task_ref, task, TaskStatus::DataError);
                    }
                }
            }
            else if (
                task_data.load_feature_ids_task.has_task() && task_data.feature_ids.has_value())
            {
                // The task needs its data to be sorted.

                auto& load_feature_ids_task = task_data.load_feature_ids_task.get_task();
                if (load_feature_ids_task.status == TaskStatus::Loaded)
                {
                    vector_data::UnsortedVectorData params;
                    params.reference_feature_ids =
                        load_feature_ids_task.load_feature_ids().feature_ids.value();
                    params.feature_ids = task_data.feature_ids.value();
                    params.geometry = {task_data.geometry.value()};
                    for (const auto& it : task_data.attribute_ids_to_values)
                    {
                        params.attributes.push_back(it.second.value());
                    }

                    task_data.sort_ticket = hrz_jobs::add_job_sort_vector_data(
                        js, params,
                        {monitoring::systems::VectorDataLoader, layer_model.layer_handle});
                }
            }
            else if (task_data.load_vector_tile_data_task.has_task())
            {
                auto& load_vector_tile_data_task = task_data.load_vector_tile_data_task.get_task();
                if (load_vector_tile_data_task.status == TaskStatus::Loaded)
                {
                    const auto& data = load_vector_tile_data_task.load_vector_tile_data();
                    task_data.feature_ids = data.feature_ids;
                    task_data.geometry = data.geometry;
                    task_data.attribution = data.attribution;
                    task_data.attribute_ids_to_values = data.attribute_ids_to_values;

                    task_data.load_vector_tile_data_task.release_data();

                    check_for_sorted_data();
                }
                else if (is_error(load_vector_tile_data_task.status))
                {
                    task_data.load_vector_tile_data_task.release_data();
                    set_task_status(task_ref, task, load_vector_tile_data_task.status);
                }
                else
                {
                    set_task_status(task_ref, task, TaskStatus::Blocked);
                }
            }
            else if (task_data.load_in_memory_vector_data_task.has_task())
            {
                auto& load_in_memory_vector_data_task =
                    task_data.load_in_memory_vector_data_task.get_task();
                if (load_in_memory_vector_data_task.status == TaskStatus::Loaded)
                {
                    const auto& data = load_in_memory_vector_data_task.load_in_memory_vector_data();
                    task_data.feature_ids = data.feature_ids;
                    task_data.geometry = data.geometry;
                    task_data.attribution = data.attribution;
                    task_data.attribute_ids_to_values = data.attribute_ids_to_values;

                    task_data.load_in_memory_vector_data_task.release_data();

                    check_for_sorted_data();
                }
                else if (is_error(load_in_memory_vector_data_task.status))
                {
                    task_data.load_in_memory_vector_data_task.release_data();
                    set_task_status(task_ref, task, load_in_memory_vector_data_task.status);
                }
                else
                {
                    set_task_status(task_ref, task, TaskStatus::Blocked);
                }
            }
            else if (task_data.request_client_data_task.has_task())
            {
                auto& request_client_data_task = task_data.request_client_data_task.get_task();
                if (request_client_data_task.status == TaskStatus::Loaded)
                {
                    const auto& data = request_client_data_task.request_client_data();
                    task_data.feature_ids = data.feature_ids;
                    task_data.geometry = data.geometry;
                    task_data.attribution = data.attribution;
                    task_data.attribute_ids_to_values = data.attribute_ids_to_values;

                    task_data.request_client_data_task.release_data();

                    // There is no need to sort the received data, as client data is expected
                    // to be already sorted.
                    set_task_status(task_ref, task, TaskStatus::Loaded);
                }
                else if (is_error(request_client_data_task.status))
                {
                    task_data.request_client_data_task.release_data();
                    set_task_status(task_ref, task, request_client_data_task.status);
                }
                else
                {
                    set_task_status(task_ref, task, TaskStatus::Blocked);
                }
            }
            else if (task_data.extract_vector_tile_data_task.has_task())
            {
                auto& extract_vector_tile_data_task =
                    task_data.extract_vector_tile_data_task.get_task();
                if (extract_vector_tile_data_task.status == TaskStatus::Loaded)
                {
                    const auto& data = extract_vector_tile_data_task.extract_vector_tile_data();
                    task_data.feature_ids = data.feature_ids;
                    task_data.geometry = data.geometry;
                    task_data.attribution = data.attribution;
                    task_data.attribute_ids_to_values = data.attribute_ids_to_values;

                    task_data.load_in_memory_vector_data_task.release_data();

                    check_for_sorted_data();
                }
                else if (is_error(extract_vector_tile_data_task.status))
                {
                    task_data.extract_vector_tile_data_task.release_data();
                    set_task_status(task_ref, task, extract_vector_tile_data_task.status);
                }
                else
                {
                    set_task_status(task_ref, task, TaskStatus::Blocked);
                }
            }
        }
        else if (task.type == TaskType::LoadVectorTileData)
        {
            auto& task_data = task.load_vector_tile_data();
            const auto& layer_model = task_data.layer_model.value();
            const auto& data_source = layer_model.data_sources.at(task_data.data_source);

            auto decode_data = [&](TileCoords tile_coords, std::string_view layer_name,
                                   AttributionHandle attribution,
                                   const vector_data::VectorDataPackage& package)
            {
                vector_data::EncodedVectorTile job_params;
                job_params.coords = tile_coords;
                job_params.data = package;
                job_params.layer_name = layer_name;
                job_params.source_feature_id_attribute = std::nullopt;

                for (const auto& it : layer_model.attributes)
                {
                    auto attribute_id = it.first;
                    const auto& attribute = it.second;

                    if (attribute.data_source != task_data.data_source) continue;

                    vector_data::AttributeModel param_attribute;
                    param_attribute.id = attribute_id;
                    param_attribute.name = attribute.name_in_source;
                    param_attribute.is_feature_id = attribute.is_feature_id;
                    param_attribute.transform = attribute.transform;

                    job_params.attributes.push_back(param_attribute);

                    if (attribute_id == data_source.source_feature_id_attribute)
                    {
                        job_params.source_feature_id_attribute = {job_params.attributes.size() - 1};
                    }
                }

                task_data.attribution = attribution;

                task_data.decode_ticket = hrz_jobs::add_job_decode_vector_tile(
                    js, job_params,
                    {monitoring::systems::VectorDataLoader, task_data.layer_model->layer_handle});
            };

            auto decode_url_data_if_loaded = [&](TileCoords tile_coords,
                                                 const std::string& layer_name,
                                                 AttributionHandle attribution)
            {
                auto& load_vector_data_url_package_task =
                    task_data.load_vector_data_url_package_task.get_task();
                if (load_vector_data_url_package_task.status == TaskStatus::Loaded)
                {
                    const auto& load_vector_data_url_package_task_data =
                        load_vector_data_url_package_task.load_vector_data_url_package();
                    decode_data(
                        tile_coords, layer_name, attribution,
                        load_vector_data_url_package_task_data.package.value());
                }
                else if (is_error(load_vector_data_url_package_task.status))
                {
                    task_data.load_vector_data_url_package_task.release_data();
                    set_task_status(task_ref, task, load_vector_data_url_package_task.status);
                }
                else
                {
                    set_task_status(task_ref, task, TaskStatus::Blocked);
                }
            };

            if (hrz_jobs::is_job_valid(js, task_data.decode_ticket))
            {
                if (hrz_jobs::is_job_finished(js, task_data.decode_ticket))
                {
                    if (hrz_jobs::get_job_status(js, task_data.decode_ticket)
                        == hrz::job_scheduler::JobStatus::Finished_Success)
                    {
                        vector_data::DecodedVectorTile data;
                        hrz_jobs::get_job_response(js, task_data.decode_ticket, data);

                        assert(data.feature_ids.size() == data.geometry.features.size());
                        task_data.feature_ids = feature_id_lists.alloc();
                        task_data.feature_ids.value() = std::move(data.feature_ids);

                        task_data.geometry = tile_geometries.alloc();
                        task_data.geometry.value() = std::move(data.geometry);

                        load_attribute_data_into_map(
                            data.attributes, task_data.attribute_ids_to_values, layer_model,
                            data.geometry.features.size());

                        set_task_status(task_ref, task, TaskStatus::Loaded);
                    }
                    else
                    {
                        const auto tile_coords = task_data.feature_selection.tile_coords();
                        HRZ_LOG_ERROR(
                            "Could not decode tile {} of vector data layer {}", tile_coords,
                            layer_model.id);
                        hrz_jobs::cancel_job(js, task_data.decode_ticket);
                        set_task_status(task_ref, task, TaskStatus::DataError);
                    }
                }
            }
            else if (data_source.has_tiled_data_provider())
            {
                const auto& provider = data_source.tiled_data_provider();

                assert(task_data.feature_selection.has_tile_coords());
                decode_url_data_if_loaded(
                    task_data.feature_selection.tile_coords(), provider.layer_name,
                    provider.attribution);
            }
            else if (data_source.has_untiled_data_provider())
            {
                const auto& provider = data_source.untiled_data_provider();

                assert(task_data.feature_selection.has_tile_coords());
                decode_url_data_if_loaded(
                    task_data.feature_selection.tile_coords(), "", provider.attribution);
            }
            else if (data_source.has_tilejson_data_provider())
            {
                const auto& provider = data_source.tilejson_data_provider();

                assert(task_data.feature_selection.has_tile_coords());
                decode_url_data_if_loaded(
                    task_data.feature_selection.tile_coords(), provider.layer_name,
                    provider.attribution);
            }
            else if (data_source.has_pmtiles_data_provider())
            {
                const auto& provider = data_source.pmtiles_data_provider();

                assert(task_data.feature_selection.has_tile_coords());

                auto& load_vector_data_pmtiles_package_task =
                    task_data.load_vector_data_pmtiles_package_task.get_task();
                if (load_vector_data_pmtiles_package_task.status == TaskStatus::Loaded)
                {
                    const auto& load_vector_data_pmtiles_package_task_data =
                        load_vector_data_pmtiles_package_task.load_vector_data_pmtiles_package();
                    decode_data(
                        task_data.feature_selection.tile_coords(), provider.layer_name,
                        provider.attribution,
                        load_vector_data_pmtiles_package_task_data.package.value());
                }
                else if (is_error(load_vector_data_pmtiles_package_task.status))
                {
                    task_data.load_vector_data_pmtiles_package_task.release_data();
                    set_task_status(task_ref, task, load_vector_data_pmtiles_package_task.status);
                }
                else
                {
                    set_task_status(task_ref, task, TaskStatus::Blocked);
                }
            }
            else
            {
                assert(false && "Unhandled case");
                set_task_status(task_ref, task, TaskStatus::ModelError);
            }
        }
        else if (task.type == TaskType::LoadVectorDataUrlPackage)
        {
            auto& task_data = task.load_vector_data_url_package();

            if (hrz_jobs::is_job_valid(js, task_data.parse_ticket))
            {
                if (hrz_jobs::is_job_finished(js, task_data.parse_ticket))
                {
                    if (hrz_jobs::get_job_status(js, task_data.parse_ticket)
                        == hrz::job_scheduler::JobStatus::Finished_Success)
                    {
                        hrz::vector_data::ParsedMvt parsed_mvt;
                        hrz_jobs::get_job_response(js, task_data.parse_ticket, parsed_mvt);

                        task_data.package = {
                            {std::move(parsed_mvt)},
                            hrz_proto::VectorDataFormat::MVT_VECTOR_DATA};

                        set_task_status(task_ref, task, TaskStatus::Loaded);
                    }
                    else
                    {
                        HRZ_LOG_ERROR("Could not parse MVT data");
                        hrz_jobs::cancel_job(js, task_data.parse_ticket);
                        set_task_status(task_ref, task, TaskStatus::DataError);
                    }
                }
            }
            else
            {
                const auto& load_url_data_task = task_data.load_url_data_task.get_task();
                if (load_url_data_task.status == TaskStatus::Loaded)
                {
                    if (task_data.format == hrz_proto::VectorDataFormat::MVT_VECTOR_DATA)
                    {
                        // A copy of the blob is made, because `add_job_*` functions
                        // move out their parameters, and we don't want to modify
                        // the load URL data task.
                        auto mvt_blob = load_url_data_task.load_url_data().blob;
                        task_data.parse_ticket = hrz_jobs::add_job_parse_mvt(
                            js, mvt_blob, {monitoring::systems::VectorDataLoader});
                    }
                    else
                    {
                        task_data.package = {
                            {load_url_data_task.load_url_data().blob, task_data.format}};

                        set_task_status(task_ref, task, TaskStatus::Loaded);
                    }
                }
                else if (is_error(load_url_data_task.status))
                {
                    set_task_status(task_ref, task, load_url_data_task.status);
                }
                else
                {
                    set_task_status(task_ref, task, TaskStatus::Blocked);
                }
            }
        }
        else if (task.type == TaskType::LoadVectorDataPmTilesPackage)
        {
            auto& task_data = task.load_vector_data_pmtiles_package();

            if (hrz_jobs::is_job_valid(js, task_data.parse_ticket))
            {
                if (hrz_jobs::is_job_finished(js, task_data.parse_ticket))
                {
                    if (hrz_jobs::get_job_status(js, task_data.parse_ticket)
                        == hrz::job_scheduler::JobStatus::Finished_Success)
                    {
                        hrz::vector_data::ParsedMvt parsed_mvt;
                        hrz_jobs::get_job_response(js, task_data.parse_ticket, parsed_mvt);

                        task_data.package = {
                            {std::move(parsed_mvt)},
                            hrz_proto::VectorDataFormat::MVT_VECTOR_DATA};

                        set_task_status(task_ref, task, TaskStatus::Loaded);
                    }
                    else
                    {
                        HRZ_LOG_ERROR("Could not parse MVT data");
                        hrz_jobs::cancel_job(js, task_data.parse_ticket);
                        set_task_status(task_ref, task, TaskStatus::DataError);
                    }
                }
            }
            else
            {
                const auto& load_pmtiles_task = task_data.load_pmtiles_task.get_task();
                if (load_pmtiles_task.status == TaskStatus::Loaded)
                {
                    assert(load_pmtiles_task.load_pmtiles().pmtiles);
                    auto& pmtiles = *load_pmtiles_task.load_pmtiles().pmtiles;

                    pmtiles.work(js, ba);

                    if (!task_data.tile_query.has_value())
                    {
                        task_data.tile_query = pmtiles.request_tile(
                            task_data.tile_coords, task_data.queue, task_data.priority,
                            task_data.resource_owner);
                        metrics::increment_counter(&task_data.request_count_metric);
                    }
                    else if (pmtiles.is_finished(task_data.tile_query.value()))
                    {
                        if (pmtiles.is_success(task_data.tile_query.value()))
                        {
                            auto mvt_blob = pmtiles.retrieve_blob(task_data.tile_query.value());
                            task_data.parse_ticket = hrz_jobs::add_job_parse_mvt(
                                js, mvt_blob, {monitoring::systems::VectorDataLoader});
                        }
                        else
                        {
                            set_task_status(task_ref, task, TaskStatus::DataError);
                        }

                        pmtiles.cancel(task_data.tile_query.value(), js);
                        task_data.tile_query = std::nullopt;
                    }
                }
                else if (is_error(load_pmtiles_task.status))
                {
                    set_task_status(task_ref, task, load_pmtiles_task.status);
                }
                else
                {
                    set_task_status(task_ref, task, TaskStatus::Blocked);
                }
            }
        }
        else if (task.type == TaskType::LoadUntiledVectorData)
        {
            auto& task_data = task.load_untiled_vector_data();
            auto& layer_model = task_data.layer_model.value();
            auto& data_source = layer_model.data_sources.at(task_data.data_source);

            assert(data_source.has_untiled_data_provider());

            if (hrz_jobs::is_job_valid(js, task_data.build_aabb_tree_ticket))
            {
                if (hrz_jobs::is_job_finished(js, task_data.build_aabb_tree_ticket))
                {
                    if (hrz_jobs::get_job_status(js, task_data.build_aabb_tree_ticket)
                        == hrz::job_scheduler::JobStatus::Finished_Success)
                    {
                        vector_data::AabbTree aabb_tree;
                        hrz_jobs::get_job_response(js, task_data.build_aabb_tree_ticket, aabb_tree);

                        task_data.aabb_tree = std::move(aabb_tree);
                        task_data.attribution = data_source.untiled_data_provider().attribution;

                        set_task_status(task_ref, task, TaskStatus::Loaded);
                    }
                    else
                    {
                        HRZ_LOG_ERROR(
                            "Could not load untiled vector data of layer {}", layer_model.id);
                        hrz_jobs::cancel_job(js, task_data.build_aabb_tree_ticket);
                        set_task_status(task_ref, task, TaskStatus::DataError);
                    }
                }
            }
            else if (task_data.load_vector_tile_data_task.has_task())
            {
                auto& load_vector_tile_data_task = task_data.load_vector_tile_data_task.get_task();
                if (load_vector_tile_data_task.status == TaskStatus::Loaded)
                {
                    const auto& load_data = load_vector_tile_data_task.load_vector_tile_data();

                    task_data.feature_ids = load_data.feature_ids;
                    task_data.geometry = load_data.geometry;
                    task_data.attribute_ids_to_values = load_data.attribute_ids_to_values;
                    task_data.attribution = load_data.attribution;

                    vector_data::VectorTileGeometry geometry = task_data.geometry.value();
                    task_data.build_aabb_tree_ticket = hrz_jobs::add_job_build_aabb_tree(
                        js, geometry, {monitoring::systems::VectorDataLoader});

                    task_data.load_vector_tile_data_task.release_data();
                }
                else if (is_error(load_vector_tile_data_task.status))
                {
                    task_data.load_vector_tile_data_task.release_data();
                    set_task_status(task_ref, task, load_vector_tile_data_task.status);
                }
                else
                {
                    set_task_status(task_ref, task, TaskStatus::Blocked);
                }
            }
        }
        else if (task.type == TaskType::ExtractVectorTileData)
        {
            auto& task_data = task.extract_vector_tile_data();
            auto& layer_model = task_data.layer_model.value();

            if (hrz_jobs::is_job_valid(js, task_data.extract_ticket))
            {
                if (hrz_jobs::is_job_finished(js, task_data.extract_ticket))
                {
                    if (hrz_jobs::get_job_status(js, task_data.extract_ticket)
                        == hrz::job_scheduler::JobStatus::Finished_Success)
                    {
                        vector_data::DecodedVectorTile extracted_tile;
                        hrz_jobs::get_job_response(js, task_data.extract_ticket, extracted_tile);

                        assert(
                            extracted_tile.feature_ids.size()
                            == extracted_tile.geometry.features.size());
                        task_data.feature_ids = feature_id_lists.alloc();
                        task_data.feature_ids.value() = std::move(extracted_tile.feature_ids);

                        task_data.geometry = tile_geometries.alloc();
                        task_data.geometry.value() = std::move(extracted_tile.geometry);

                        load_attribute_data_into_map(
                            extracted_tile.attributes, task_data.attribute_ids_to_values,
                            layer_model, extracted_tile.geometry.features.size());

                        set_task_status(task_ref, task, TaskStatus::Loaded);
                    }
                    else
                    {
                        HRZ_LOG_ERROR(
                            "Could not extract vector tile data of layer {}", layer_model.id);
                        hrz_jobs::cancel_job(js, task_data.extract_ticket);
                        set_task_status(task_ref, task, TaskStatus::DataError);
                    }
                }
            }
            else if (task_data.load_untiled_vector_tile_data_task.has_task())
            {
                auto& load_untiled_vector_tile_data_task =
                    task_data.load_untiled_vector_tile_data_task.get_task();
                if (load_untiled_vector_tile_data_task.status == TaskStatus::Loaded)
                {
                    const auto& data_source = layer_model.data_sources.at(task_data.data_source);
                    const auto& provider = data_source.untiled_data_provider();

                    const auto& load_data =
                        load_untiled_vector_tile_data_task.load_untiled_vector_data();

                    vector_data::VectorTileExtractionParams params{};
                    params.source_data.coords = TileCoords{0, 0, 0};
                    params.source_data.geometry = load_data.geometry.value();
                    params.source_data.feature_ids = load_data.feature_ids.value();
                    for (const auto& pair : load_data.attribute_ids_to_values)
                    {
                        params.source_data.attributes.push_back(pair.second.value());
                    }
                    params.aabb_tree = load_data.aabb_tree;
                    params.coords = task_data.tile_coords;
                    params.tolerance = data_source.untiled_data_provider().tolerance;
                    params.include_clip_margin = data_source.untiled_data_provider().clip_margin;
                    params.bounds = provider.bounds;

                    task_data.extract_ticket = hrz_jobs::add_job_extract_vector_tile(
                        js, params,
                        hrz::monitoring::ResourceOwner(
                            hrz::monitoring::systems::VectorDataLoader, layer_model.layer_handle));

                    task_data.attribution = load_data.attribution;

                    task_data.load_untiled_vector_tile_data_task.release_data();
                }
                else if (is_error(load_untiled_vector_tile_data_task.status))
                {
                    task_data.load_untiled_vector_tile_data_task.release_data();
                    set_task_status(task_ref, task, load_untiled_vector_tile_data_task.status);
                }
                else
                {
                    set_task_status(task_ref, task, TaskStatus::Blocked);
                }
            }
        }
        else if (task.type == TaskType::LoadInMemoryVectorData)
        {
            auto& task_data = task.load_in_memory_vector_data();
            const auto& layer_model = task_data.layer_model.value();
            const auto& data_source = layer_model.data_sources.at(task_data.data_source);
            const auto& provider = data_source.in_memory_data_provider();

            if (task_data.load_feature_ids_task.has_task()
                && task_data.load_feature_ids_task_version
                    != task_data.load_feature_ids_task.get_version()
                && task_data.in_memory_vector_data_request_id.has_value())
            {
                // Recreate the in-memory vector data request if the data is requested
                // by feature IDs and the task that provides them has been updated.
                in_memory_vector_data_channel.send(in_memory::messages::ReleaseDataRequest{
                    task_data.in_memory_vector_data_request_id.value()});
                tasks_waiting_for_in_memory_vector_data_message.erase(
                    task_data.in_memory_vector_data_request_id.value());
                task_data.in_memory_vector_data_request_id = std::nullopt;
            }

            if (!task_data.in_memory_vector_data_request_id.has_value())
            {
                auto set_request_id = [&]()
                {
                    task_data.in_memory_vector_data_request_id =
                        next_in_memory_vector_data_request_id;
                    next_in_memory_vector_data_request_id += 1;
                    return task_data.in_memory_vector_data_request_id.value();
                };

                if (task_data.load_feature_ids_task.has_task())
                {
                    auto& load_feature_ids_task = task_data.load_feature_ids_task.get_task();
                    if (load_feature_ids_task.status == TaskStatus::Loaded)
                    {
                        const auto& feature_ids =
                            load_feature_ids_task.load_feature_ids().feature_ids.value();
                        task_data.load_feature_ids_task_version = {load_feature_ids_task.version};
                        in_memory_vector_data_channel.send(in_memory::messages::FeatureDataRequest{
                            set_request_id(), provider.in_memory_layer_id, feature_ids, true});
                        tasks_waiting_for_in_memory_vector_data_message.insert(
                            {task_data.in_memory_vector_data_request_id.value(), task_ref});
                        set_task_status(task_ref, task, TaskStatus::Blocked);
                    }
                    else if (is_error(load_feature_ids_task.status))
                    {
                        task_data.load_feature_ids_task.release_data();
                        set_task_status(task_ref, task, load_feature_ids_task.status);
                    }
                    else
                    {
                        set_task_status(task_ref, task, TaskStatus::Blocked);
                    }
                }
                else
                {
                    if (task_data.feature_selection.has_tile_coords())
                    {
                        in_memory_vector_data_channel.send(in_memory::messages::TileDataRequest{
                            set_request_id(), provider.in_memory_layer_id,
                            task_data.feature_selection.tile_coords(), true});
                        tasks_waiting_for_in_memory_vector_data_message.insert(
                            {task_data.in_memory_vector_data_request_id.value(), task_ref});
                        set_task_status(task_ref, task, TaskStatus::Blocked);
                    }
                    else if (task_data.feature_selection.has_feature_ids())
                    {
                        const auto& feature_ids = task_data.feature_selection.feature_ids().value();
                        in_memory_vector_data_channel.send(in_memory::messages::FeatureDataRequest{
                            set_request_id(), provider.in_memory_layer_id, feature_ids, true});
                        tasks_waiting_for_in_memory_vector_data_message.insert(
                            {task_data.in_memory_vector_data_request_id.value(), task_ref});
                        set_task_status(task_ref, task, TaskStatus::Blocked);
                    }
                    else
                    {
                        assert(false && "Unhandled case");
                    }
                }
            }
        }
        else if (task.type == TaskType::LoadUrlData)
        {
            auto& task_data = task.load_url_data();

            if (!task_data.download_request_id.has_value())
            {
                task_data.download_request_id = next_download_request_id;
                next_download_request_id += 1;

                asset_loader_channel.send(assets_loader::messages::LoadRequest{
                    task_data.download_request_id.value(), task_data.url, 0, 0, task_data.headers,
                    task_data.queue, task_data.priority, task_data.resource_owner});
                tasks_waiting_for_asset_loader_message.insert(
                    {task_data.download_request_id.value(), task_ref});

                metrics::increment_counter(&task_data.request_count_metric);

                set_task_status(task_ref, task, TaskStatus::Blocked);
            }
        }
        else if (task.type == TaskType::RequestClientData)
        {
            auto& task_data = task.request_client_data();
            const auto& layer_model = task_data.layer_model.value();

            if (hrz_jobs::is_job_valid(js, task_data.move_to_blobs_ticket))
            {
                if (hrz_jobs::is_job_finished(js, task_data.move_to_blobs_ticket))
                {
                    if (hrz_jobs::get_job_status(js, task_data.move_to_blobs_ticket)
                        == hrz::job_scheduler::JobStatus::Finished_Success)
                    {
                        vector_data::DecodedVectorTile tile_data;
                        hrz_jobs::get_job_response(js, task_data.move_to_blobs_ticket, tile_data);

                        task_data.geometry = tile_geometries.alloc();
                        task_data.geometry.value() = std::move(tile_data.geometry);

                        task_data.feature_ids = feature_id_lists.alloc();
                        task_data.feature_ids.value() = std::move(tile_data.feature_ids);

                        for (auto& values : tile_data.attributes)
                        {
                            auto attribute_id = values.attribute_id;

                            auto ref = attribute_values.alloc();
                            ref.value() = std::move(values);
                            task_data.attribute_ids_to_values.insert({attribute_id, ref});
                        }

                        set_task_status(task_ref, task, TaskStatus::Loaded);
                    }
                    else
                    {
                        HRZ_LOG_ERROR(
                            "Could not allocate memory for the client-provided vector tile data of "
                            "layer {}.",
                            layer_model.id);
                        set_task_status(task_ref, task, TaskStatus::DataError);
                    }
                }
            }
            else if (task_data.client_response.has_value())
            {
                vector_data::RawClientVectorData params;
                params.attributes = std::move(task_data.attributes);
                params.expects_geometry =
                    layer_model.data_sources.at(task_data.data_source).has_geometry;
                params.client_data = std::move(task_data.client_response.value());

                task_data.attribution = attribution::register_attribution(
                    attributions, {params.client_data.attribution(), {}});
                task_data.move_to_blobs_ticket = hrz_jobs::add_job_move_client_vector_data_to_blobs(
                    js, params, {monitoring::systems::VectorDataLoader, layer_model.layer_handle});
            }
            else if (
                task_data.client_ticket == NO_CLIENT_TICKET
                && (!task_data.load_feature_ids_task.has_task()
                    || (task_data.load_feature_ids_task.has_task()
                        && task_data.load_feature_ids_task.get_task().status
                            == TaskStatus::Loaded)))
            {
                auto make_request_client_message = [&]() -> hrz_proto::VectorDataRequestMessage
                {
                    task_data.client_ticket = client_ticket_generator.generate();

                    hrz_proto::VectorDataRequestMessage message;
                    message.set_ticket(task_data.client_ticket);
                    message.set_vector_data_layer_id(task_data.layer_model->id);
                    message.set_vector_data_source_index(task_data.data_source);
                    message.set_expects_geometry(
                        task_data.layer_model->geometry_source == task_data.data_source);

                    for (const auto& pair : layer_model.attributes)
                    {
                        // We only want to request attribute values for the ones that have been
                        // defined in the same data source.
                        if (pair.second.data_source == task_data.data_source)
                        {
                            message.add_attribute_ids(pair.first);
                            task_data.attributes.push_back(
                                layer_model.attributes.at(pair.first).to_attribute_model());
                        }
                    }

                    return message;
                };

                auto enqueue_request_client_message =
                    [&](hrz_proto::VectorDataRequestMessage&& message) {
                        hrz::client_message_queue::enqueue_vector_data_request_message(
                            mq, std::move(message));
                    };

                const auto& data_source =
                    layer_model.data_sources.at(task_data.data_source).client_data_provider();

                auto start_request = [&]()
                {
                    client_tickets_to_tasks.insert({task_data.client_ticket, task_ref});

                    if (data_source.timeout_duration.has_value())
                    {
                        client_tickets_timeouts.push(
                            {task_data.client_ticket,
                             hrz::now_frame_ms() + data_source.timeout_duration.value()});
                    }

                    set_task_status(task_ref, task, TaskStatus::Blocked);
                };

                if (data_source.access == hrz_proto::VectorDataSourceAccess::ACCESS_BY_TILE)
                {
                    assert(task_data.feature_selection.has_tile_coords());

                    auto message = make_request_client_message();
                    message.mutable_tile_selection()->CopyFrom(
                        hrz::to_proto(task_data.feature_selection.tile_coords()));

                    start_request();

                    std::optional<size_t> feature_count = std::nullopt;
                    if (task_data.load_feature_ids_task.has_task())
                    {
                        const auto& feature_ids = task_data.load_feature_ids_task.get_task()
                                                      .load_feature_ids()
                                                      .feature_ids;
                        if (feature_ids.has_value())
                        {
                            feature_count = feature_ids.value().size();
                        }
                    }

                    ClientRequestHistory::Entry entry = {};
                    entry.ticket = task_data.client_ticket;
                    entry.layer_id = message.vector_data_layer_id();
                    entry.expects_geometry = message.expects_geometry();
                    entry.tile_coords = task_data.feature_selection.tile_coords();
                    entry.feature_count = feature_count;
                    for (auto attribute_id : message.attribute_ids())
                    {
                        entry.attribute_ids.push_back(attribute_id);
                    }
                    entry.status = ClientRequestHistory::RequestStatus::Loading;
                    task_data.request_history_index =
                        client_request_history.append(std::move(entry));

                    enqueue_request_client_message(std::move(message));
                }
                else
                {
                    assert(task_data.load_feature_ids_task.has_task());
                    const auto& feature_ids = task_data.load_feature_ids_task.get_task()
                                                  .load_feature_ids()
                                                  .feature_ids.value();

                    if (!feature_ids.empty())
                    {
                        auto message = make_request_client_message();
                        feature_ids.to_proto(
                            message.mutable_feature_id_selection()->mutable_feature_ids());

                        start_request();

                        ClientRequestHistory::Entry entry = {};
                        entry.ticket = task_data.client_ticket;
                        entry.layer_id = message.vector_data_layer_id();
                        entry.expects_geometry = message.expects_geometry();
                        entry.feature_count = feature_ids.size();
                        for (auto attribute_id : message.attribute_ids())
                        {
                            entry.attribute_ids.push_back(attribute_id);
                        }
                        entry.status = ClientRequestHistory::RequestStatus::Loading;
                        task_data.request_history_index =
                            client_request_history.append(std::move(entry));

                        enqueue_request_client_message(std::move(message));
                    }
                    else
                    {
                        // No need to make an empty request to the client.
                        for (const auto& pair : layer_model.attributes)
                        {
                            if (pair.second.data_source != task_data.data_source)
                            {
                                continue;
                            }

                            auto values_ref = attribute_values.alloc();
                            auto& values = values_ref.value();
                            values.attribute_id = pair.first;

                            task_data.attribute_ids_to_values.insert({pair.first, values_ref});
                        }

                        set_task_status(task_ref, task, TaskStatus::Loaded);
                    }
                }
            }
            else if (task_data.load_feature_ids_task.has_task())
            {
                auto& load_feature_ids_task = task_data.load_feature_ids_task.get_task();

                if (load_feature_ids_task.status != TaskStatus::Loaded)
                {
                    if (is_error(load_feature_ids_task.status))
                    {
                        task_data.load_feature_ids_task.release_data();
                        set_task_status(task_ref, task, load_feature_ids_task.status);
                    }
                    else
                    {
                        set_task_status(task_ref, task, TaskStatus::Blocked);
                    }
                }
            }
        }
        else if (task.type == TaskType::LoadTileJson)
        {
            auto& task_data = task.load_tilejson();

            auto& load_url_data_task = task_data.load_url_data_task.get_task();
            if (load_url_data_task.status == TaskStatus::Loaded)
            {
                auto raw_data = load_url_data_task.load_url_data().blob.get_data();
                auto base_url = BaseUrl(task_data.url, task_data.preserve_query_parameters);
                auto tilejson = tilejson::parse_tilejson(raw_data, base_url);

                if (tilejson.has_value())
                {
                    task_data.tile_url_generator =
                        hrz::MultiPatternTileUrlGenerator({tilejson->url_patterns}, 1);
                    task_data.min_level = tilejson->min_level;
                    task_data.max_level = tilejson->max_level;
                    task_data.bounds = tilejson->bounds;
                    task_data.attribution = attribution::register_attribution(
                        attributions, {tilejson->attribution, {}});
                    set_task_status(task_ref, task, TaskStatus::Loaded);
                }
                else
                {
                    HRZ_LOG_ERROR("Could not decode TileJSON file at {}", task_data.url);
                    set_task_status(task_ref, task, TaskStatus::DataError);
                }

                task_data.load_url_data_task.release_data();
            }
            else if (is_error(load_url_data_task.status))
            {
                task_data.load_url_data_task.release_data();
                set_task_status(task_ref, task, load_url_data_task.status);
            }
            else
            {
                set_task_status(task_ref, task, TaskStatus::Blocked);
            }
        }
        else if (task.type == TaskType::LoadPmTiles)
        {
            auto& task_data = task.load_pmtiles();

            if (!task_data.asset_loader_channel_request_id.has_value())
            {
                task_data.asset_loader_channel_request_id = next_download_request_id;
                next_download_request_id += 1;
                asset_loader_channel.send(assets_loader::messages::CreateChannel{
                    task_data.asset_loader_channel_request_id.value()});
                tasks_waiting_for_asset_loader_message.insert(
                    {task_data.asset_loader_channel_request_id.value(), task_ref});

                set_task_status(task_ref, task, TaskStatus::Blocked);
            }
            else if (task_data.pmtiles)
            {
                task_data.pmtiles->work(js, ba);

                if (task_data.pmtiles->get_status() == PmTiles::kReady)
                {
                    set_task_status(task_ref, task, TaskStatus::Loaded);
                }
                else if (task_data.pmtiles->get_status() == PmTiles::kError)
                {
                    set_task_status(task_ref, task, TaskStatus::DataError);
                }
            }
        }
        else if (task.type == TaskType::LoadLayerModel)
        {
            auto& task_data = task.load_layer_model();

            bool all_loaded = true;
            for (auto& task : task_data.load_source_model_tasks)
            {
                if (task.get_task().status != TaskStatus::Loaded)
                {
                    all_loaded = false;
                    break;
                }
            }

            if (all_loaded)
            {
                auto& layer_model = task_data.layer_model.value();

                auto iterpair = layer_ids_to_request_ids.equal_range(layer_model.id);
                for (auto it = iterpair.first; it != iterpair.second; ++it)
                {
                    auto request_id = it->second;
                    auto loader_it = request_ids_to_layer_loaders.find(request_id);
                    if (loader_it != request_ids_to_layer_loaders.end())
                    {
                        auto& loader = loader_it->second;
                        loader.has_full_model_update = true;
                        send_layer_model_message(loader, layer_model);
                    }
                }

                set_task_status(task_ref, task, TaskStatus::Loaded);
            }
            else
            {
                set_task_status(task_ref, task, TaskStatus::Blocked);
            }
        }
        else if (task.type == TaskType::LoadSourceModel)
        {
            auto& task_data = task.load_source_model();

            if (task_data.load_tilejson_task.has_task())
            {
                auto& load_tilejson_task = task_data.load_tilejson_task.get_task();
                if (load_tilejson_task.status == TaskStatus::Loaded)
                {
                    const auto& load_tilejson_task_data = load_tilejson_task.load_tilejson();

                    auto& layer_model = task_data.layer_model.value();
                    auto& data_source = layer_model.data_sources.at(task_data.data_source);

                    assert(data_source.has_tilejson_data_provider());
                    auto& provider = data_source.tilejson_data_provider();

                    data_source.min_lod = {load_tilejson_task_data.min_level};
                    data_source.max_lod = {load_tilejson_task_data.max_level};
                    data_source.bounds = {load_tilejson_task_data.bounds};

                    std::array<AttributionHandle, 2> attribution_group = {
                        provider.attribution, load_tilejson_task_data.attribution};
                    provider.attribution =
                        attribution::register_attribution_group(attributions, attribution_group);

                    auto iterpair = layer_ids_to_request_ids.equal_range(layer_model.id);
                    for (auto it = iterpair.first; it != iterpair.second; ++it)
                    {
                        auto request_id = it->second;
                        auto loader_it = request_ids_to_layer_loaders.find(request_id);
                        if (loader_it != request_ids_to_layer_loaders.end())
                        {
                            auto& loader = loader_it->second;
                            loader.has_full_model_update = true;
                            send_layer_model_message(loader, layer_model);
                        }
                    }

                    set_task_status(task_ref, task, TaskStatus::Loaded);
                }
                else if (is_error(load_tilejson_task.status))
                {
                    task_data.load_tilejson_task.release_data();
                    set_task_status(task_ref, task, load_tilejson_task.status);
                }
                else
                {
                    set_task_status(task_ref, task, TaskStatus::Blocked);
                }
            }
            else if (task_data.load_pmtiles_task.has_task())
            {
                auto& load_pmtiles_task = task_data.load_pmtiles_task.get_task();
                if (load_pmtiles_task.status == TaskStatus::Loaded)
                {
                    const auto& load_pmtiles_task_data = load_pmtiles_task.load_pmtiles();

                    auto& layer_model = task_data.layer_model.value();
                    auto& data_source = layer_model.data_sources.at(task_data.data_source);

                    assert(data_source.has_pmtiles_data_provider());
                    auto& provider = data_source.pmtiles_data_provider();

                    const auto& geometry = load_pmtiles_task_data.pmtiles->get_geometry();

                    data_source.min_lod = geometry.tiling_scheme.global_tiling().min_level();
                    data_source.max_lod = geometry.tiling_scheme.global_tiling().max_level();

                    data_source.bounds = hrz::web_mercator_bounds_to_geo(geometry.bounds);

                    auto new_attribution = attribution::register_attribution(
                        attributions, {load_pmtiles_task_data.pmtiles->get_attribution(), ""});
                    std::array<AttributionHandle, 2> attribution_group = {
                        provider.attribution, new_attribution};
                    provider.attribution =
                        attribution::register_attribution_group(attributions, attribution_group);

                    auto iterpair = layer_ids_to_request_ids.equal_range(layer_model.id);
                    for (auto it = iterpair.first; it != iterpair.second; ++it)
                    {
                        auto request_id = it->second;
                        auto loader_it = request_ids_to_layer_loaders.find(request_id);
                        if (loader_it != request_ids_to_layer_loaders.end())
                        {
                            auto& loader = loader_it->second;
                            loader.has_full_model_update = true;
                            send_layer_model_message(loader, layer_model);
                        }
                    }

                    set_task_status(task_ref, task, TaskStatus::Loaded);
                }
                else if (is_error(load_pmtiles_task.status))
                {
                    task_data.load_pmtiles_task.release_data();
                    set_task_status(task_ref, task, load_pmtiles_task.status);
                }
                else
                {
                    set_task_status(task_ref, task, TaskStatus::Blocked);
                }
            }
            else if (task_data.load_untiled_vector_data_task.has_task())
            {
                auto& load_untiled_vector_data_task =
                    task_data.load_untiled_vector_data_task.get_task();
                if (load_untiled_vector_data_task.status == TaskStatus::Loaded)
                {
                    const auto& aabb_tree =
                        load_untiled_vector_data_task.load_untiled_vector_data().aabb_tree;

                    auto& layer_model = task_data.layer_model.value();
                    auto& data_source = layer_model.data_sources.at(task_data.data_source);

                    assert(data_source.has_untiled_data_provider());
                    auto& provider = data_source.untiled_data_provider();

                    data_source.bounds = {hrz::intersection(provider.bounds, aabb_tree.bounds)};
                    if (data_source.bounds != provider.bounds)
                    {
                        auto iterpair = layer_ids_to_request_ids.equal_range(layer_model.id);
                        for (auto it = iterpair.first; it != iterpair.second; ++it)
                        {
                            auto request_id = it->second;
                            auto loader_it = request_ids_to_layer_loaders.find(request_id);
                            if (loader_it != request_ids_to_layer_loaders.end())
                            {
                                auto& loader = loader_it->second;
                                loader.has_full_model_update = true;
                                send_layer_model_message(loader, layer_model);
                            }
                        }
                    }

                    task_data.load_untiled_vector_data_task.release_data();
                    set_task_status(task_ref, task, TaskStatus::Loaded);
                }
                else if (is_error(load_untiled_vector_data_task.status))
                {
                    task_data.load_untiled_vector_data_task.release_data();
                    set_task_status(task_ref, task, load_untiled_vector_data_task.status);
                }
                else
                {
                    set_task_status(task_ref, task, TaskStatus::Blocked);
                }
            }
            else
            {
                set_task_status(task_ref, task, TaskStatus::Loaded);
            }
        }
        else
        {
            assert(false && "Unhandled case");
        }
    }

    void unload_task_data_if_not_needed(WeakTaskRef& task_ref, Task& task, JobScheduler* js)
    {
        HRZ_SCOPED_SAMPLE_A("unload task data if not needed");

        if (task.data_use_count > 0
            || !(is_loading(task.status) || task.status == TaskStatus::Loaded))
        {
            return;
        }

        // We don't want to unload untiled vector data even when it's not used because
        // it could lead to the source data file being reloaded multiple times, and
        // the AABB tree recomputed as many times.
        // It should be unloaded when the provider disappears or is invalidated.
        if (task.type == TaskType::LoadUntiledVectorData)
        {
            return;
        }

        // Do not release in-meomry vector data, otherwise we would have to request
        // the data from the in-memory vector data system explicitly after  an update
        // instead of being able to rely on receiving the data automatically when a
        // new version comes out.
        if (task.type == TaskType::LoadInMemoryVectorData)
        {
            return;
        }

        cancel_task_jobs(task, js);
        unload_task_data(task, true, js);

        set_task_status(task_ref, task, TaskStatus::Unloaded);
    }

    void check_for_invalidated_data_for_task(WeakTaskRef& task_ref, Task& task, JobScheduler* js)
    {
        HRZ_SCOPED_SAMPLE_A("check for updated data for task");

        if (!(is_loading(task.status) || task.status == TaskStatus::Loaded
              || task.status == TaskStatus::Unloaded || task.status == TaskStatus::DataError))
        {
            return;
        }

        if (task.type == TaskType::LoadVectorTileData)
        {
            auto& task_data = task.load_vector_tile_data();
            const auto& layer_model = task_data.layer_model.value();

            for (const auto& invalidation : invalidations)
            {
                if (!invalidation.applies_to_source(layer_model.id, task_data.data_source))
                {
                    continue;
                }

                if (invalidation.invalidates_everything()
                    || (task_data.feature_selection.has_tile_coords()
                        && invalidation.invalidates_tile(
                            task_data.feature_selection.tile_coords())))
                {
                    restart_task(task_ref, task, js);
                    return;
                }
            }
        }
        else if (task.type == TaskType::LoadInMemoryVectorData)
        {
            auto& task_data = task.load_in_memory_vector_data();
            const auto& layer_model = task_data.layer_model.value();

            for (const auto& invalidation : invalidations)
            {
                if (!invalidation.applies_to_source(layer_model.id, task_data.data_source))
                {
                    continue;
                }

                if (invalidation.invalidates_everything()
                    || (task_data.feature_selection.has_tile_coords()
                        && invalidation.invalidates_tile(
                            task_data.feature_selection.tile_coords())))
                {
                    restart_task(task_ref, task, js);
                    return;
                }
            }
        }
        else if (task.type == TaskType::RequestClientData)
        {
            auto& task_data = task.request_client_data();
            const auto& layer_model = task_data.layer_model.value();
            const auto& data_source = layer_model.data_sources.at(task_data.data_source);
            const auto access = data_source.client_data_provider().access;

            for (const auto& invalidation : invalidations)
            {
                if (!invalidation.applies_to_source(layer_model.id, task_data.data_source))
                {
                    continue;
                }

                if (invalidation.invalidates_everything())
                {
                    restart_task(task_ref, task, js);
                    return;
                }

                if (access == hrz_proto::VectorDataSourceAccess::ACCESS_BY_TILE
                    && task_data.feature_selection.has_tile_coords()
                    && invalidation.invalidates_tile(task_data.feature_selection.tile_coords()))
                {
                    restart_task(task_ref, task, js);
                    return;
                }

                if (access == hrz_proto::VectorDataSourceAccess::ACCESS_BY_FEATURE_ID)
                {
                    const auto& feature_ids = task_data.load_feature_ids_task.get_task()
                                                  .load_feature_ids()
                                                  .feature_ids.value();
                    if (invalidation.invalidates_feature_ids(feature_ids))
                    {
                        restart_task(task_ref, task, js);
                        return;
                    }
                }
            }
        }
        else if (task.type == TaskType::LoadUntiledVectorData)
        {
            auto& task_data = task.load_untiled_vector_data();
            const auto& layer_model = task_data.layer_model.value();

            for (const auto& invalidation : invalidations)
            {
                if (!invalidation.applies_to_source(layer_model.id, task_data.data_source))
                {
                    continue;
                }

                if (invalidation.invalidates_everything())
                {
                    restart_task(task_ref, task, js);
                    return;
                }

                if (invalidation.proto.has_tile_coords())
                {
                    restart_task(task_ref, task, js);
                    return;
                }
            }
        }
    }

    void work_messages(SceneModel* scene_model, JobScheduler* js)
    {
        HRZ_SCOPED_SAMPLE("work messages");

        for (auto& message : asset_loader_channel.receive())
        {
            std::visit(
                [&](auto& message)
                {
                    using MessageType = std::decay_t<decltype(message)>;
                    if constexpr (std::is_same_v<MessageType, assets_loader::messages::LoadedData>)
                    {
                        auto it = tasks_waiting_for_asset_loader_message.find(message.request_id);
                        if (it != tasks_waiting_for_asset_loader_message.end()
                            && it->second.has_value())
                        {
                            auto& task_ref = it->second;
                            auto& task = task_ref.value();
                            assert(task.type == TaskType::LoadUrlData);
                            auto& task_data = task.load_url_data();

                            if (task.status == TaskStatus::Blocked)
                            {
                                task_data.blob = std::move(message.data);
                                set_task_status(task_ref, task, TaskStatus::Loaded);
                            }
                        }
                    }
                    else if constexpr (std::is_same_v<
                                           MessageType, assets_loader::messages::LoadFailure>)
                    {
                        auto it = tasks_waiting_for_asset_loader_message.find(message.request_id);
                        if (it != tasks_waiting_for_asset_loader_message.end()
                            && it->second.has_value())
                        {
                            auto& task_ref = it->second;
                            auto& task = task_ref.value();
                            assert(task.type == TaskType::LoadUrlData);

                            if (task.status == TaskStatus::Blocked)
                            {
                                set_task_status(task_ref, task, TaskStatus::DataError);
                            }
                        }
                    }
                    else if constexpr (std::is_same_v<
                                           MessageType, assets_loader::messages::NewChannel>)
                    {
                        auto it = tasks_waiting_for_asset_loader_message.find(message.request_id);
                        if (it != tasks_waiting_for_asset_loader_message.end()
                            && it->second.has_value())
                        {
                            auto& task_ref = it->second;
                            auto& task = task_ref.value();
                            assert(task.type == TaskType::LoadPmTiles);
                            auto& task_data = task.load_pmtiles();

                            if (task.status == TaskStatus::Blocked)
                            {
                                task_data.pmtiles = PmTiles::create(
                                    task_data.url, task_data.headers, task_data.queue,
                                    std::move(message.channel));
                                set_task_status(task_ref, task, TaskStatus::Loading);
                            }
                        }
                    }
                    else
                    {
                        static_assert(hrz::always_false<MessageType>, "Unhandled case");
                    }
                },
                message);
        }

        for (auto& message : in_memory_vector_data_channel.receive())
        {
            std::visit(
                [&](auto& message)
                {
                    using MessageType = std::decay_t<decltype(message)>;
                    if constexpr (std::is_same_v<MessageType, in_memory::messages::VectorData>)
                    {
                        auto it = tasks_waiting_for_in_memory_vector_data_message.find(
                            message.request_id);
                        if (it != tasks_waiting_for_in_memory_vector_data_message.end()
                            && it->second.has_value())
                        {
                            auto& task_ref = it->second;
                            auto& task = task_ref.value();
                            assert(task.type == TaskType::LoadInMemoryVectorData);
                            auto& task_data = task.load_in_memory_vector_data();
                            const auto& layer_model = task_data.layer_model.value();

                            if (task.status == TaskStatus::Blocked
                                || task.status == TaskStatus::Loaded
                                || task.status == TaskStatus::Unloaded)
                            {
                                auto& data = message.data;

                                assert(data.feature_ids.size() == data.geometry.features.size());
                                task_data.feature_ids = feature_id_lists.alloc();
                                task_data.feature_ids.value() = std::move(data.feature_ids);

                                task_data.geometry = tile_geometries.alloc();
                                task_data.geometry.value() = std::move(data.geometry);

                                task_data.attribution = message.attribution;

                                load_attribute_data_into_map(
                                    data.attributes, task_data.attribute_ids_to_values, layer_model,
                                    data.geometry.features.size());

                                set_task_status(task_ref, task, TaskStatus::Loaded);

                                if (task.status == TaskStatus::Loaded
                                    || task.status == TaskStatus::Unloaded)
                                {
                                    task.version += 1;

                                    for (auto dependent_task_ref : task.dependents)
                                    {
                                        restart_task(
                                            dependent_task_ref, dependent_task_ref.value(), js);
                                    }
                                }
                            }
                        }
                    }
                    else if constexpr (std::is_same_v<MessageType, in_memory::messages::Error>)
                    {
                        auto it = tasks_waiting_for_in_memory_vector_data_message.find(
                            message.request_id);
                        if (it != tasks_waiting_for_in_memory_vector_data_message.end()
                            && it->second.has_value())
                        {
                            auto& task_ref = it->second;
                            auto& task = task_ref.value();

                            if (task.status == TaskStatus::Blocked
                                || task.status == TaskStatus::Loaded
                                || task.status == TaskStatus::Unloaded)
                            {
                                set_task_status(task_ref, task, TaskStatus::DataError);
                            }
                        }
                    }
                    else
                    {
                        static_assert(hrz::always_false<MessageType>, "Unhandled case");
                    }
                },
                message);
        }

        channels.work();

        for (auto& it : channels)
        {
            auto channel_id = it.first;
            auto& channel = it.second;

            for (auto& message : channel.receive())
            {
                std::visit(
                    [&](auto& message)
                    {
                        using MessageType = std::decay_t<decltype(message)>;
                        if constexpr (std::is_same_v<MessageType, vector_data::messages::LoadLayer>)
                        {
                            RequestId request_id{channel_id, message.request_id};
                            load_layer(request_id, message.layer_id);
                        }
                        else if constexpr (std::is_same_v<
                                               MessageType,
                                               vector_data::messages::ReleaseLayerLoader>)
                        {
                            RequestId request_id{channel_id, message.request_id};
                            release_layer_loader(request_id);
                        }
                        else if constexpr (std::is_same_v<
                                               MessageType, vector_data::messages::RequestData>)
                        {
                            RequestId request_id{channel_id, message.request_id};
                            RequestId layer_request_id{channel_id, message.load_layer_request_id};
                            auto it_data_request = request_ids_to_data_requests.find(request_id);
                            if (it_data_request == request_ids_to_data_requests.end())
                            {
                                if (std::holds_alternative<TileCoords>(message.feature_selection))
                                {
                                    request_data(
                                        request_id, layer_request_id,
                                        std::get<TileCoords>(message.feature_selection),
                                        message.data_kind);
                                }
                                else if (std::holds_alternative<vector_data::FeatureIds>(
                                             message.feature_selection))
                                {
                                    request_data(
                                        request_id, layer_request_id,
                                        std::get<vector_data::FeatureIds>(
                                            message.feature_selection),
                                        message.data_kind);
                                }
                                else
                                {
                                    assert(false && "Unhandled case");
                                }
                            }
                            else
                            {
                                retain_data(request_id);
                            }
                        }
                        else if constexpr (std::is_same_v<
                                               MessageType, vector_data::messages::ReleaseData>)
                        {
                            RequestId request_id{channel_id, message.request_id};
                            release_data(request_id);
                        }
                        else if constexpr (std::is_same_v<
                                               MessageType, vector_data::messages::RetainData>)
                        {
                            RequestId request_id{channel_id, message.request_id};
                            retain_data(request_id);
                        }
                        else if constexpr (std::is_same_v<
                                               MessageType,
                                               vector_data::messages::ReleaseDataRequest>)
                        {
                            RequestId request_id{channel_id, message.request_id};
                            release_request(request_id);
                        }
                        else if constexpr (std::is_same_v<
                                               MessageType, hrz_proto::VectorDataRequestResponse>)
                        {
                            provide_client_data(message);
                        }
                        else if constexpr (std::is_same_v<
                                               MessageType, hrz_proto::VectorDataInvalidation>)
                        {
                            invalidate_client_data(message);
                        }
                        else
                        {
                            static_assert(hrz::always_false<MessageType>, "Unhandled case");
                        }
                    },
                    message);
            }
        }
    }

    void work_tasks(
        SceneModel* scene_model,
        JobScheduler* js,
        BlobAllocator* ba,
        ClientMessageQueue* mq,
        AttributionRegistry* attributions)
    {
        HRZ_SCOPED_SAMPLE("work tasks");

        if (active_tasks.empty() && invalidations.empty()) return;

        // Here we go through the list of active tasks and work on them.
        // Some tasks may remain in an active state after having been worked
        // on. In this case, they remain in the active_tasks array.
        //
        // * We want tasks that are being activated during the loop to be
        //   worked on if they are activated.
        // * We do not want tasks that remain active to be worked on multiple
        //   times.
        // * The next time this function is called, we want tasks that have
        //   activated this time to be worked on before tasks that remained
        //   active. (Otherwise if there are too many tasks remaining active,
        //   they could take up all the slots and prevent other tasks from
        //   being worked on, potentially soft-locking the loader.)
        // * When we exit this function, we want tasks to be packed at the
        //   front of the active task array.
        //
        // The strategy is to go through as many unique active tasks as we
        // are allowed by looping through the array. Tasks that remain active
        // are packed at the front. Newly activated tasks are pushed at the
        // back. Then after the loop, tasks that remained active are in turn
        // pushed at the back, and finally all tasks are move to the front.

        static constexpr size_t max_tasks_to_update = std::numeric_limits<size_t>::max();
        size_t write_index = 0;

        for (size_t read_index = 0;
             read_index < active_tasks.size() && read_index < max_tasks_to_update; ++read_index)
        {
            // Cannot take a reference here, as `active_tasks` may be resized,
            // and its data moved, if new tasks are activated during this loop.
            auto task_ref = active_tasks[read_index];

            if (!task_ref.is_valid()) continue;

            auto& task = task_ref.value();

            if (task.status == TaskStatus::New)
            {
                work_new_task(task_ref, task, scene_model, attributions);
            }
            if (task.status == TaskStatus::Unloaded)
            {
                work_unloaded_task(task_ref, task);
            }
            if (task.status == TaskStatus::Loading)
            {
                work_loading_task(task_ref, task, js, ba, mq, attributions);
            }

            unload_task_data_if_not_needed(task_ref, task, js);

            if (task.status == TaskStatus::New || task.status == TaskStatus::Loading)
            {
                // Keep the task active.
                // Tasks that remain active are accumulated at the front of the array.

                if (write_index != read_index)
                {
                    active_tasks[write_index] = task_ref;
                }

                write_index += 1;
            }
            else
            {
                task.is_active = false;
            }
        }

        if (active_tasks.size() > max_tasks_to_update)
        {
            // Move tasks that remained active from the front of the array to the back.
            for (size_t i = 0; i < write_index; ++i)
            {
                active_tasks.push_back(active_tasks[i]);
            }

            // Move all tasks to the front of the array.
            for (size_t i = max_tasks_to_update; i < active_tasks.size(); ++i)
            {
                active_tasks[i - max_tasks_to_update] = active_tasks[i];
            }

            // Tasks are now tidily packed at the front of the array, so the array
            // can be resized.
            active_tasks.resize(active_tasks.size() - max_tasks_to_update);
        }
        else
        {
            assert(active_tasks.size() >= write_index);
            if (active_tasks.size() > write_index)
            {
                // The end of the array is now unused, as references of tasks that
                // remained active have been pushed to the front, and tasks that have
                // been made active during the loop have themselves been worked on.
                active_tasks.resize(write_index);
            }
        }

        if (!invalidations.empty())
        {
            // We don't have a nice way to check if a given invalidation applies to a
            // given task without explicitly testing one against the other. So we have
            // to loop through all tasks. But invalidations are infrequent, so it should
            // be okay.

            for (auto task_ref : tasks)
            {
                auto& task = task_ref.value();
                check_for_invalidated_data_for_task(task_ref, task, js);
            }

            invalidations.clear();
        }
    }

    void work_client_request_timeouts()
    {
        while (!client_tickets_timeouts.empty())
        {
            auto& request = client_tickets_timeouts.top();
            if (hrz::now_frame_ms() >= request.timeout_date)
            {
                auto it = client_tickets_to_tasks.find(request.ticket);
                if (it != client_tickets_to_tasks.end())
                {
                    auto& task_ref = it->second;
                    auto& task = task_ref.value();
                    auto& task_data = task.request_client_data();

                    HRZ_LOG_WARNING(
                        "Client did not respond to vector data request {}",
                        task_data.client_ticket);

                    auto& history_entry =
                        client_request_history.entries[task_data.request_history_index];
                    if (history_entry.ticket == task_data.client_ticket)
                    {
                        history_entry.status = ClientRequestHistory::RequestStatus::Timeout;
                    }

                    client_tickets_to_tasks.erase(task_data.client_ticket);
                    task_data.client_ticket = NO_CLIENT_TICKET;

                    set_task_status(task_ref, task, TaskStatus::DataError);
                }

                client_tickets_timeouts.pop();
            }
            else
            {
                break;
            }
        }
    }

public:
    void work(
        SceneModel* scene_model,
        JobScheduler* js,
        BlobAllocator* ba,
        ClientMessageQueue* mq,
        AttributionRegistry* attributions)
    {
        HRZ_SCOPED_SAMPLE_A("vector data loader work");

        assert(js && ba && mq);

        HRZ_SCOPED_LOCK(dev_ui_mutex);

        update_layers_from_model(scene_model);
        collect_garbage(js);
        work_messages(scene_model, js);
        work_tasks(scene_model, js, ba, mq, attributions);
        work_client_request_timeouts();
    }

    void provide_client_data(const hrz_proto::VectorDataRequestResponse& response)
    {
        HRZ_SCOPED_SAMPLE_A("vector data loader add client data");

        auto task_ref_it = client_tickets_to_tasks.find(response.ticket());
        if (task_ref_it == client_tickets_to_tasks.end())
        {
            // It is probably data for a tile that has been removed since the request
            // was made.
            return;
        }

        auto& task_ref = task_ref_it->second;
        auto& task = task_ref.value();

        if (task.type != TaskType::RequestClientData || !is_loading(task.status))
        {
            HRZ_LOG_WARNING("Unexpected client vector data for ticket {}", response.ticket());
            return;
        }

        auto& task_data = task.request_client_data();

        auto set_history_entry_status = [&](ClientRequestHistory::RequestStatus status)
        {
            auto& history_entry = client_request_history.entries[task_data.request_history_index];
            if (history_entry.ticket == task_data.client_ticket)
            {
                history_entry.status = status;
            }
        };

        std::optional<uint32_t> expected_feature_count = std::nullopt;

        if (task_data.load_feature_ids_task.has_task())
        {
            const auto& feature_ids =
                task_data.load_feature_ids_task.get_task().load_feature_ids().feature_ids;
            assert(feature_ids.has_value());
            if (feature_ids.has_value())
            {
                expected_feature_count = {(uint32_t)feature_ids.value().size()};
            }
        }

        if (expected_feature_count.has_value()
            && expected_feature_count.value() != (size_t)response.features_size())
        {
            HRZ_LOG_WARNING(
                "Incorrect amount of features in client vector data for ticket {}: got {}, "
                "expected {}",
                response.ticket(), response.features_size(), expected_feature_count.value());

            set_history_entry_status(ClientRequestHistory::RequestStatus::Error);
            set_task_status(task_ref, task, TaskStatus::DataError);
        }
        else
        {
            task_data.client_response = {std::move(response)};

            set_history_entry_status(ClientRequestHistory::RequestStatus::Received);
            set_task_status(task_ref, task, TaskStatus::Loading);
        }

        client_tickets_to_tasks.erase(response.ticket());
        task_data.client_ticket = NO_CLIENT_TICKET;
    }

    void invalidate_client_data(const hrz_proto::VectorDataInvalidation& invalidation)
    {
        invalidations.push_back({invalidation});
    }

    vector_data::VectorDataLoaderChannel create_channel()
    {
        return channels.create_channel().second;
    }
};

namespace vector_data
{
VectorDataLoader* create_loader(AssetsLoader* al, InMemoryVectorDataBase* in_memory_database)
{
    assert(al && in_memory_database);

    auto loader = new VectorDataLoader();
    loader->asset_loader_channel = assets_loader::create_channel(al);
    loader->in_memory_vector_data_channel = in_memory::create_channel(in_memory_database);

    return loader;
}

void destroy_loader(VectorDataLoader* loader, JobScheduler* js)
{
    assert(loader);
    loader->destroy(js);
    delete loader;
}

void register_layer(VectorDataLoader* loader, SceneModel* scene_model, uint64_t layer_id)
{
    assert(loader);
    loader->register_layer(scene_model, layer_id);
}

void unregister_layer(VectorDataLoader* loader, uint64_t layer_id)
{
    assert(loader);
    loader->unregister_layer(layer_id);
}

void notify_update(
    VectorDataLoader* loader,
    uint64_t layer_id,
    scene_model::UpdateType update_type,
    const scene_model::VectorDataLayerPath& path)
{
    assert(loader);
    loader->notify_update(layer_id, update_type, path);
}

void work(
    VectorDataLoader* loader,
    SceneModel* scene_model,
    JobScheduler* js,
    BlobAllocator* ba,
    ClientMessageQueue* mq,
    AttributionRegistry* attributions)
{
    assert(loader);
    loader->work(scene_model, js, ba, mq, attributions);
}

VectorDataLoaderChannel create_channel(VectorDataLoader* loader)
{
    assert(loader);
    return loader->create_channel();
}

namespace
{
const char* data_kind_str(DataKind data_kind)
{
    switch (data_kind)
    {
        case DataKind::Geometry: return "Geometry";
        case DataKind::AttributeValues: return "Attribute values";
        case DataKind::FeatureIds: return "Feature IDs";
        default: return "Unknown";
    }
}

const char* task_type_str(const VectorDataLoader::Task& task)
{
    switch (task.type)
    {
        case VectorDataLoader::TaskType::LoadGeometry: return "Load geometry";
        case VectorDataLoader::TaskType::LoadAllAttributeValues: return "Load all attribute values";
        case VectorDataLoader::TaskType::LoadAttributeValues: return "Load single attribute values";
        case VectorDataLoader::TaskType::LoadFeatureIds: return "Load feature IDs";
        case VectorDataLoader::TaskType::LoadVectorData: return "Load vector data";
        case VectorDataLoader::TaskType::LoadVectorTileData: return "Load vector tile data";
        case VectorDataLoader::TaskType::LoadVectorDataUrlPackage:
            return "Load vector data package from URL";
        case VectorDataLoader::TaskType::LoadVectorDataPmTilesPackage:
            return "Load vector data package from PMTiles";
        case VectorDataLoader::TaskType::LoadUntiledVectorData: return "Load untiled vector data";
        case VectorDataLoader::TaskType::ExtractVectorTileData: return "Extract vector tile data";
        case VectorDataLoader::TaskType::LoadInMemoryVectorData:
            return "Load in-memory vector data";
        case VectorDataLoader::TaskType::LoadUrlData: return "Load URL data";
        case VectorDataLoader::TaskType::RequestClientData: return "Request client data";
        case VectorDataLoader::TaskType::LoadTileJson: return "Load TileJSON";
        case VectorDataLoader::TaskType::LoadPmTiles: return "Load PMTiles";
        case VectorDataLoader::TaskType::LoadLayerModel: return "Load layer model";
        case VectorDataLoader::TaskType::LoadSourceModel: return "Load source model";
        default: return "Unknown";
    }
}

const char* task_status_str(const VectorDataLoader::Task& task)
{
    switch (task.status)
    {
        case VectorDataLoader::TaskStatus::New: return "Initializing";
        case VectorDataLoader::TaskStatus::Loading: return "Loading";
        case VectorDataLoader::TaskStatus::Blocked: return "Blocked";
        case VectorDataLoader::TaskStatus::Loaded: return "Complete";
        case VectorDataLoader::TaskStatus::Unloaded: return "Unloaded";
        case VectorDataLoader::TaskStatus::DataError: return "Data error";
        case VectorDataLoader::TaskStatus::ModelError: return "Model error";
        default: return "Unknown";
    }
}

const char* status_to_str(VectorDataLoader::ClientRequestHistory::RequestStatus status)
{
    using RequestStatus = VectorDataLoader::ClientRequestHistory::RequestStatus;

    switch (status)
    {
        case RequestStatus::Loading: return "(Loading)";
        case RequestStatus::Received: return "(Received)";
        case RequestStatus::Canceled: return "(Canceled)";
        case RequestStatus::Timeout: return "(Timeout)";
        case RequestStatus::Error: return "(Error)";
        default:
            assert(false && "Unhandled case");
            return "(???"
                   ")";
    }
}

mu_Color status_to_text_color(VectorDataLoader::ClientRequestHistory::RequestStatus status)
{
    using RequestStatus = VectorDataLoader::ClientRequestHistory::RequestStatus;

    switch (status)
    {
        case RequestStatus::Loading:
        case RequestStatus::Canceled: return {160, 160, 160, 255};
        case RequestStatus::Received: return {97, 204, 38, 255};
        case RequestStatus::Timeout:
        case RequestStatus::Error: return {255, 32, 32, 255};
        default: assert(false && "Unhandled case"); return {255, 255, 255, 255};
    }
}
} // namespace

void dev_ui(VectorDataLoader* loader, mu_Context* ctx, const char* window_name)
{
    HRZ_SCOPED_LOCK(loader->dev_ui_mutex);

    if (mu_begin_window_ex(ctx, window_name, mu_rect(300, 100, 530, 500), MU_OPT_CLOSED))
    {
        auto get_spinner_str = [&]() -> const char*
        {
            size_t frame = (((size_t)ctx->frame) / 30) % 4;
            switch (frame)
            {
                case 0: return "-";
                case 1: return "\\";
                case 2: return "|";
                case 3: return "/";
                default: return "";
            }
        };

        fmt::memory_buffer buffer;

        int window_width = mu_get_current_container(ctx)->body.w - 16;

        mu_layout_row(ctx, 1, &window_width, 0);

        if (mu_header_ex(
                ctx, "Requests",
                hrz::format_to_buffer(
                    buffer, "{} requests", loader->request_ids_to_data_requests.size()),
                0))
        {
            std::vector<VectorDataLoader::RequestId> request_ids(
                loader->request_ids_to_data_requests.size());
            {
                uint32_t index = 0;
                for (const auto& request_it : loader->request_ids_to_data_requests)
                {
                    request_ids[index] = request_it.first;
                    index += 1;
                }
                std::sort(
                    request_ids.begin(), request_ids.end(),
                    [](const VectorDataLoader::RequestId& a, const VectorDataLoader::RequestId& b)
                    {
                        if (a.channel_id == b.channel_id)
                        {
                            return a.request_id < b.request_id;
                        }
                        return a.channel_id < b.channel_id;
                    });
            }

            static int layout[] = {120, 120, 40, 110, 40, -1};
            mu_layout_row(ctx, 6, layout, 0);

            mu_text(ctx, "#");
            mu_text(ctx, "Type");
            mu_text(ctx, "Layer #");
            mu_text(ctx, "Feature selection");
            mu_text(ctx, "Task #");
            mu_text(ctx, "Status");

            for (auto request_id : request_ids)
            {
                const auto& request = loader->request_ids_to_data_requests.at(request_id);

                mu_text(
                    ctx,
                    hrz::format_to_buffer(
                        buffer, "{}-{}", request_id.channel_id, request_id.request_id));

                mu_Rect tooltip_rect = mu_layout_next(ctx);
                mu_layout_set_next(ctx, tooltip_rect, 0);

                mu_text(ctx, data_kind_str(request.data_kind));

                mu_text(ctx, hrz::format_to_buffer(buffer, "{}", request.layer_id));

                if (request.feature_selection.has_tile_coords())
                {
                    const auto& tile_coords = request.feature_selection.tile_coords();
                    mu_text(ctx, hrz::format_to_buffer(buffer, "Tile {}", tile_coords));
                }
                else if (request.feature_selection.has_feature_ids())
                {
                    const auto& feature_ids = request.feature_selection.feature_ids();
                    mu_text(
                        ctx, hrz::format_to_buffer(buffer, "{} feature IDs", feature_ids->size()));
                }

                if (request.task.has_task())
                {
                    const auto& task_ref = request.task.get_task_ref();
                    const auto& task = request.task.get_task();

                    mu_text(
                        ctx, hrz::format_to_buffer(buffer, "{}", task_ref.get_handle().to_int()));
                    mu_text(ctx, task_status_str(task));
                }
                else
                {
                    mu_text(ctx, "None");
                    mu_text(ctx, "");
                    mu_text(ctx, "");
                }
            }
        }

        if (mu_header_ex(
                ctx, "Tasks",
                hrz::format_to_buffer(
                    buffer, "{} tasks ({} active)", loader->tasks.size(),
                    loader->active_tasks.size()),
                0))
        {
            static int layout[] = {40, 160, 50, 50, 50, 50, 60, -1};
            mu_layout_row(ctx, 8, layout, 0);

            mu_text(ctx, "#");
            mu_text(ctx, "Type");
            mu_text(ctx, "Refs");
            mu_text(ctx, "Dep tasks");
            mu_text(ctx, "Data uses");
            mu_text(ctx, "Version");
            mu_text(ctx, "Status");
            mu_text(ctx, "");

            for (const auto& ref : loader->tasks)
            {
                const auto& handle = ref.get_handle();
                const auto& task = ref.value();

                mu_text(ctx, hrz::format_to_buffer(buffer, "{}", handle.to_int()));
                mu_text(ctx, task_type_str(task));
                mu_text(ctx, hrz::format_to_buffer(buffer, "{}", ref.ref_count()));
                mu_text(ctx, hrz::format_to_buffer(buffer, "{}", task.dependents.size()));
                mu_text(ctx, hrz::format_to_buffer(buffer, "{}", task.data_use_count));
                mu_text(ctx, hrz::format_to_buffer(buffer, "{}", task.version));
                mu_text(ctx, task_status_str(task));

                if (task.is_active)
                {
                    mu_text(ctx, get_spinner_str());
                }
                else
                {
                    mu_text(ctx, "");
                }
            }
        }

        if (mu_header_ex(
                ctx, "Layer models",
                hrz::format_to_buffer(buffer, "{} layer models", loader->layer_models.size()), 0))
        {
            static int layout[] = {40, 60, 60, -1};
            mu_layout_row(ctx, 4, layout, 0);

            mu_text(ctx, "#");
            mu_text(ctx, "Refs");
            mu_text(ctx, "Layer ID");
            mu_text(ctx, "Layer handle");

            for (const auto& ref : loader->layer_models)
            {
                mu_text(ctx, hrz::format_to_buffer(buffer, "{}", ref.get_handle().to_int()));
                mu_text(ctx, hrz::format_to_buffer(buffer, "{}", (uint32_t)ref.ref_count()));

                if (ref.has_value())
                {
                    const auto& layer_model = ref.value();

                    mu_text(ctx, hrz::format_to_buffer(buffer, "{}", layer_model.id));
                    mu_text(ctx, hrz::format_to_buffer(buffer, "{}", layer_model.layer_handle));
                }
                else
                {
                    mu_text(ctx, "");
                    mu_text(ctx, "");
                }
            }
        }

        if (mu_header_ex(
                ctx, "Feature ID lists",
                hrz::format_to_buffer(
                    buffer, "{} feature ID lists", loader->feature_id_lists.size()),
                0))
        {
            static int layout[] = {40, 60, -1};
            mu_layout_row(ctx, 3, layout, 0);

            mu_text(ctx, "#");
            mu_text(ctx, "Refs");
            mu_text(ctx, "Features");

            for (const auto& ref : loader->feature_id_lists)
            {
                mu_text(ctx, hrz::format_to_buffer(buffer, "{}", ref.get_handle().to_int()));
                mu_text(ctx, hrz::format_to_buffer(buffer, "{}", (uint32_t)ref.ref_count()));

                if (ref.has_value())
                {
                    const auto& feature_id_list = ref.value();

                    mu_text(ctx, hrz::format_to_buffer(buffer, "{}", feature_id_list.size()));
                }
                else
                {
                    mu_text(ctx, "");
                }
            }
        }

        if (mu_header_ex(
                ctx, "Attribute value lists",
                hrz::format_to_buffer(
                    buffer, "{} attribute value lists", loader->attribute_values.size()),
                0))
        {
            static int layout[] = {40, 60, 60, -1};
            mu_layout_row(ctx, 4, layout, 0);

            mu_text(ctx, "#");
            mu_text(ctx, "Refs");
            mu_text(ctx, "Attribute ID");
            mu_text(ctx, "Values");

            for (const auto& ref : loader->attribute_values)
            {
                mu_text(ctx, hrz::format_to_buffer(buffer, "{}", ref.get_handle().to_int()));
                mu_text(ctx, hrz::format_to_buffer(buffer, "{}", (uint32_t)ref.ref_count()));

                if (ref.has_value())
                {
                    const auto& attribute_values = ref.value();

                    mu_text(
                        ctx, hrz::format_to_buffer(buffer, "{}", attribute_values.attribute_id));
                    mu_text(
                        ctx, hrz::format_to_buffer(buffer, "{}", attribute_values.values.size()));
                }
                else
                {
                    mu_text(ctx, "");
                    mu_text(ctx, "");
                }
            }
        }

        if (mu_header_ex(
                ctx, "Tile geometries",
                hrz::format_to_buffer(buffer, "{} tile geometries", loader->tile_geometries.size()),
                0))
        {
            static int layout[] = {40, 60, 60, 60, -1};
            mu_layout_row(ctx, 5, layout, 0);

            mu_text(ctx, "#");
            mu_text(ctx, "Refs");
            mu_text(ctx, "Points");
            mu_text(ctx, "Features");
            mu_text(ctx, "Bounds");

            for (const auto& ref : loader->tile_geometries)
            {
                mu_text(ctx, hrz::format_to_buffer(buffer, "{}", ref.get_handle().to_int()));
                mu_text(ctx, hrz::format_to_buffer(buffer, "{}", (uint32_t)ref.ref_count()));

                if (ref.has_value())
                {
                    const auto& geometry = ref.value();

                    mu_text(ctx, hrz::format_to_buffer(buffer, "{}", geometry.features.size()));
                    mu_text(ctx, hrz::format_to_buffer(buffer, "{}", geometry.features.size()));
                    mu_text(
                        ctx,
                        hrz::format_to_buffer(
                            buffer, "({:.0f}, {:.0f}), ({:.0f}, {:.0f})", geometry.bounds.min.x,
                            geometry.bounds.min.y, geometry.bounds.max.x, geometry.bounds.max.y));
                }
                else
                {
                    mu_text(ctx, "");
                    mu_text(ctx, "");
                    mu_text(ctx, "");
                }
            }
        }

        if (mu_header(ctx, "Client request history"))
        {
            {
                static const int layout[] = {100, 50, 50, -1};
                mu_layout_row(ctx, 4, layout, 0);

                buffer.clear();
                fmt::format_to(
                    std::back_inserter(buffer), "{} requests",
                    loader->client_request_history.count);
                buffer.push_back(0);
                mu_draw_control_text(
                    ctx, buffer.data(), mu_layout_next(ctx), MU_COLOR_TEXT, MU_OPT_ALIGNRIGHT);

                mu_text(ctx, "");

                if (mu_button(ctx, "Clear"))
                {
                    loader->client_request_history.clear();
                }
            }

            static int row_layout[] = {40, 50, 60, 120, -70, 65};
            mu_layout_row(ctx, 6, row_layout, 0);

            mu_text(ctx, "#");
            mu_text(ctx, "Layer ID");
            mu_text(ctx, "Geometry");
            mu_text(ctx, "Feature selection");
            mu_text(ctx, "Attribute IDs");
            mu_text(ctx, "Status");

            static const int panel_layout = -1;
            mu_layout_row(ctx, 1, &panel_layout, -1);
            static ui::StickyPanelState sticky_panel_state;
            begin_sticky_panel(ctx, &sticky_panel_state, "Client request panel");

            for (uint32_t i = 0; i < loader->client_request_history.count; i++)
            {
                uint32_t index = (loader->client_request_history.head + i)
                    % hrz::VectorDataLoader::ClientRequestHistory::CAPACITY;
                const auto& entry = loader->client_request_history.entries[index];

                mu_layout_row(ctx, 6, row_layout, 0);

                mu_text(ctx, hrz::format_to_buffer(buffer, "{}", entry.ticket));
                mu_text(ctx, hrz::format_to_buffer(buffer, "{}", entry.layer_id));
                mu_text(ctx, entry.expects_geometry ? "Yes" : "No");

                if (entry.tile_coords.has_value())
                {
                    buffer.clear();
                    fmt::format_to(
                        std::back_inserter(buffer), "Tile {}", entry.tile_coords.value());

                    if (entry.feature_count.has_value())
                    {
                        fmt::format_to(
                            std::back_inserter(buffer), " ({} features)",
                            entry.feature_count.value());
                    }

                    buffer.push_back(0);
                }
                else if (entry.feature_count.has_value())
                {
                    hrz::format_to_buffer(buffer, "{} features", entry.feature_count.value());
                }

                mu_text(ctx, buffer.data());

                buffer.clear();
                for (size_t i = 0; i < entry.attribute_ids.size(); i++)
                {
                    fmt::format_to(std::back_inserter(buffer), "{}", entry.attribute_ids[i]);
                    if (i < entry.attribute_ids.size() - 1)
                    {
                        fmt::format_to(std::back_inserter(buffer), ", ");
                    }
                }
                buffer.push_back(0);
                mu_text(ctx, buffer.data());

                mu_text_color(ctx, status_to_str(entry.status), status_to_text_color(entry.status));
            }

            end_sticky_panel(ctx, &sticky_panel_state);
        }

        if (mu_header(ctx, "Memory usage"))
        {
            size_t tasks_memory = loader->tasks.size() * sizeof(VectorDataLoader::Task);

            size_t feature_id_lists_memory = 0;
            for (const auto& ref : loader->feature_id_lists)
            {
                feature_id_lists_memory += ref->size_bytes();
            }

            size_t attribute_values_memory = 0;
            for (const auto& ref : loader->attribute_values)
            {
                attribute_values_memory += sizeof(vector_data::AttributeValues)
                    + ref->values.size_bytes() + ref->out_of_line_data.size_bytes();
            }

            size_t tile_geometries_memory = 0;
            for (const auto& ref : loader->tile_geometries)
            {
                tile_geometries_memory += sizeof(VectorTileGeometry) + ref->features.size_bytes()
                    + ref->points.size_bytes() + ref->linestring_sizes.size_bytes();
            }

            size_t total_memory = tasks_memory + feature_id_lists_memory + attribute_values_memory
                + tile_geometries_memory;

            static int layout[] = {120, -1};
            mu_layout_row(ctx, 2, layout, 0);

            fmt::memory_buffer buffer;

            mu_text(ctx, "Total memory:");
            mu_text(ctx, bytes_to_string(total_memory, buffer));
            mu_text(ctx, "Tasks:");
            mu_text(ctx, bytes_to_string(tasks_memory, buffer));
            mu_text(ctx, "Feature ID lists:");
            mu_text(ctx, bytes_to_string(feature_id_lists_memory, buffer));
            mu_text(ctx, "Attribute values:");
            mu_text(ctx, bytes_to_string(attribute_values_memory, buffer));
            mu_text(ctx, "Tile geometries:");
            mu_text(ctx, bytes_to_string(tile_geometries_memory, buffer));
        }

        mu_end_window(ctx);
    }
}
} // namespace vector_data
} // namespace hrz
