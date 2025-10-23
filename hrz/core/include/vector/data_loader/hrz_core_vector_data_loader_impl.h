#pragma once

#include "assets_loader/hrz_core_assets_loader.h"
#include "hrz_core_attribution.h"
#include "hrz_core_channel_group.h"
#include "hrz_core_pmtiles.h"
#include "hrz_core_scene_path.h"
#include "hrz_core_tile_url_generator.h"
#include "hrz_jobs_tickets.h"
#include "vector/data_loader/hrz_core_vector_data_loader.h"
#include "vector/hrz_core_vector_in_memory.h"

#include <hrz_common_metrics.h>
#include <hrz_common_tickets.h>
#include <hrz_common_tile_coords.h>
#include <hrz_fnd_class.h>
#include <hrz_fnd_flat_hash_set.h>
#include <hrz_fnd_hash.h>
#include <hrz_fnd_http.h>
#include <hrz_fnd_log.h>
#include <hrz_fnd_shared_object_pool.h>

extern "C"
{
#include <microui/microui.h>
}

#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <queue>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

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
private:
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
        static constexpr uint32_t PRIMARY_SOURCE = 0;
        static constexpr uint32_t NO_SOURCE = std::numeric_limits<uint32_t>::max();

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

        enum class JoinType
        {
            // Take the vector data as is.
            // This is for the primary source.
            None,

            // Keep the features in the order they are in the source,
            // but make sure there are as many features as there are
            // in the primary source, by either deleting features if
            // there are too many, or adding empty geometries and
            // default attribute values if there are too few.
            MatchFeatureCount,

            // Reorder the features so that they match the order they
            // are in in the primary source. This respects duplicated
            // feature IDs. Empty geometries and default attribute
            // values are inserted if the secondary source has no
            // matching feature for a feature in the primary source.
            // This can only be used if both the primary source
            // and the secondary source have feature IDs.
            SortByFeatureIds,
        };

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
            hrz::flat_hash_map<uint32_t, Attribute> attributes;
            std::optional<uint32_t> source_feature_id_attribute;
            JoinType join_type;
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

        uint64_t layer_handle;
        uint32_t id;
        uint32_t data_version;
        int8_t loading_priority;
        std::vector<Source> data_sources;

        // attribute ID -> source index
        // Then use the map in `Source` to go from the attribute ID to the attribute definition.
        hrz::flat_hash_map<uint32_t, uint32_t> attributes;

        uint32_t geometry_source;
        bool has_feature_ids; // If true, the IDs are provided by the first source.

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

        constexpr bool operator==(const FeatureSelection& other) const = default;

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

        constexpr bool operator==(const RequestId& other) const = default;

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
        TaskStatus status;
        uint8_t version;
        hrz::flat_hash_set<WeakTaskRef, TaskRef::Hasher> dependents;
        uint32_t data_use_count;

        bool is_active;
        uint64_t hash;

        VectorDataLoader* loader;

        // Loads the geometry for a given feature selection (tile coords or list of feature IDs),
        // for one vector data layer.
        struct LoadGeometry
        {
            LayerModelRef layer_model;
            FeatureSelection feature_selection;
            TaskDependency load_vector_data_task;
            TileGeometryRef geometry;
            AttributionHandle attribution;
        };

        // Loads all attribute values for a given feature selection.
        struct LoadAllAttributeValues
        {
            LayerModelRef layer_model;
            FeatureSelection feature_selection;
            std::vector<TaskDependency> attribute_tasks;
            AttributionHandle attribution;
        };

        // Loads values of one attribute for a given feature selection.
        struct LoadAttributeValues
        {
            LayerModelRef layer_model;
            uint32_t attribute_id;
            FeatureSelection feature_selection;
            TaskDependency load_vector_data_task;
            AttributeValueListRef attribute_values;
            AttributionHandle attribution;
        };

        // Loads feature IDs for a given feature selection. The IDs are in the canonical order
        // for the layer, i.e. the one of the geometry source, if it exists. There may duplicated
        // feature IDs.
        struct LoadFeatureIds
        {
            LayerModelRef layer_model;
            FeatureSelection feature_selection;
            TaskDependency load_vector_data_task;
            FeatureIdListRef feature_ids;
        };

        // Loads geometry, attribute values, and feature IDs for a given feature selection.
        // If the data is not from the primary source, it is joined. If the join is by
        // feature IDs, the geometry and attribute values are adjusted to match the feature
        // order of the primary source. If the join is a count match, the geometry and
        // attribute value counts are adjusted to match the primary source.
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
            FeatureIdListRef reference_feature_ids;
            hrz_jobs::JoinVectorDataTicket join_ticket;
            FeatureIdListRef feature_ids;
            TileGeometryRef geometry;
            AttributionHandle attribution;
            hrz::flat_hash_map<uint32_t, AttributeValueListRef> attribute_ids_to_values;
        };

        // Loads geometry, attribute values, and feature IDs from a vector tile file for a given
        // tile coordinate, and a layer if the format is MVT.
        // The data is not joined (i.e. sorted or made to have the expected number of features).
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

        // Loads a vector data file at the given URL, and parses it if the format is MVT.
        // All layers are retained.
        // This helps avoiding parsing the same Protobuf buffer several times when multiple
        // data sources load data from different layers inside the MVT.
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

        // Loads a vector data package from a PMTiles dataset, and parses it from MVT.
        // All layers are retained.
        // This helps avoiding parsing the same Protobuf buffer several times when multiple
        // data sources load data from different layers inside the MVT.
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

        // Loads geometry, attribute values, and feature IDs from an untiled vector data source
        // (GeoJSON or Geobuf).
        // Computes a tree of feature bounding boxes, to allow fast intersection tests with tile
        // bounding boxes.
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

        // Loads geometry, attribute values, and feature IDs from untiled vector data source.
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

        // Loads geometry, attribute values, and feature IDs from an in-memory vector data layer
        // for a given feature selection.
        // The data is not joined (i.e. sorted or made to have the expected number of features).
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

        // Loads a file at the given URL.
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

        // Requests geometry, attribute values, and feature IDs from the client.
        // The data is not joined (i.e. sorted or made to have the expected number of features).
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

        // Loads a TileJSON file and parses it.
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

        // Loads a PMTiles system.
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

        // Loads the model for the given layer. The model may not be usable if such a task has
        // not been executed. (For example, a provider may need to download a descriptor.)
        struct LoadLayerModel
        {
            uint32_t layer_id;
            LayerModelRef layer_model;
            std::vector<TaskDependency> load_source_model_tasks;
        };

        // Loads the model for a given source.
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
            data;

        bool is_load_geometry() const { return std::holds_alternative<LoadGeometry>(data); }

        bool is_load_all_attribute_values() const
        {
            return std::holds_alternative<LoadAllAttributeValues>(data);
        }

        bool is_load_attribute_values() const
        {
            return std::holds_alternative<LoadAttributeValues>(data);
        }

        bool is_load_feature_ids() const { return std::holds_alternative<LoadFeatureIds>(data); }

        bool is_load_vector_data() const { return std::holds_alternative<LoadVectorData>(data); }

        bool is_load_vector_tile_data() const
        {
            return std::holds_alternative<LoadVectorTileData>(data);
        }

        bool is_load_vector_data_url_package() const
        {
            return std::holds_alternative<LoadVectorDataUrlPackage>(data);
        }

        bool is_load_vector_data_pmtiles_package() const
        {
            return std::holds_alternative<LoadVectorDataPmTilesPackage>(data);
        }

        bool is_load_untiled_vector_data() const
        {
            return std::holds_alternative<LoadUntiledVectorData>(data);
        }

        bool is_extract_vector_tile_data() const
        {
            return std::holds_alternative<ExtractVectorTileData>(data);
        }

        bool is_load_in_memory_vector_data() const
        {
            return std::holds_alternative<LoadInMemoryVectorData>(data);
        }

        bool is_load_url_data() const { return std::holds_alternative<LoadUrlData>(data); }

        bool is_request_client_data() const
        {
            return std::holds_alternative<RequestClientData>(data);
        }

        bool is_load_tilejson() const { return std::holds_alternative<LoadTileJson>(data); }

        bool is_load_pmtiles() const { return std::holds_alternative<LoadPmTiles>(data); }

        bool is_load_layer_model() const { return std::holds_alternative<LoadLayerModel>(data); }

        bool is_load_source_model() const { return std::holds_alternative<LoadSourceModel>(data); }

        LoadGeometry& load_geometry() { return std::get<LoadGeometry>(data); }

        LoadAllAttributeValues& load_all_attribute_values()
        {
            return std::get<LoadAllAttributeValues>(data);
        }

        LoadAttributeValues& load_attribute_values() { return std::get<LoadAttributeValues>(data); }

        LoadFeatureIds& load_feature_ids() { return std::get<LoadFeatureIds>(data); }

        LoadVectorData& load_vector_data() { return std::get<LoadVectorData>(data); }

        LoadVectorTileData& load_vector_tile_data() { return std::get<LoadVectorTileData>(data); }

        LoadVectorDataUrlPackage& load_vector_data_url_package()
        {
            return std::get<LoadVectorDataUrlPackage>(data);
        }

        LoadVectorDataPmTilesPackage& load_vector_data_pmtiles_package()
        {
            return std::get<LoadVectorDataPmTilesPackage>(data);
        }

        LoadUntiledVectorData& load_untiled_vector_data()
        {
            return std::get<LoadUntiledVectorData>(data);
        }

        ExtractVectorTileData& extract_vector_tile_data()
        {
            return std::get<ExtractVectorTileData>(data);
        }

        LoadInMemoryVectorData& load_in_memory_vector_data()
        {
            return std::get<LoadInMemoryVectorData>(data);
        }

        LoadUrlData& load_url_data() { return std::get<LoadUrlData>(data); }

        RequestClientData& request_client_data() { return std::get<RequestClientData>(data); }

        LoadTileJson& load_tilejson() { return std::get<LoadTileJson>(data); }

        LoadPmTiles& load_pmtiles() { return std::get<LoadPmTiles>(data); }

        LoadLayerModel& load_layer_model() { return std::get<LoadLayerModel>(data); }

        LoadSourceModel& load_source_model() { return std::get<LoadSourceModel>(data); }

        const LoadGeometry& load_geometry() const { return std::get<LoadGeometry>(data); }

        const LoadAllAttributeValues& load_all_attribute_values() const
        {
            return std::get<LoadAllAttributeValues>(data);
        }

        const LoadAttributeValues& load_attribute_values() const
        {
            return std::get<LoadAttributeValues>(data);
        }

        const LoadFeatureIds& load_feature_ids() const { return std::get<LoadFeatureIds>(data); }

        const LoadVectorData& load_vector_data() const { return std::get<LoadVectorData>(data); }

        const LoadVectorTileData& load_vector_tile_data() const
        {
            return std::get<LoadVectorTileData>(data);
        }

        const LoadVectorDataUrlPackage& load_vector_data_url_package() const
        {
            return std::get<LoadVectorDataUrlPackage>(data);
        }

        const LoadVectorDataPmTilesPackage& load_vector_data_pmtiles_package() const
        {
            return std::get<LoadVectorDataPmTilesPackage>(data);
        }

        const LoadUntiledVectorData& load_untiled_vector_data() const
        {
            return std::get<LoadUntiledVectorData>(data);
        }

        const ExtractVectorTileData& extract_vector_tile_data() const
        {
            return std::get<ExtractVectorTileData>(data);
        }

        const LoadInMemoryVectorData& load_in_memory_vector_data() const
        {
            return std::get<LoadInMemoryVectorData>(data);
        }

        const LoadUrlData& load_url_data() const { return std::get<LoadUrlData>(data); }

        const RequestClientData& request_client_data() const
        {
            return std::get<RequestClientData>(data);
        }

        const LoadTileJson& load_tilejson() const { return std::get<LoadTileJson>(data); }

        const LoadPmTiles& load_pmtiles() const { return std::get<LoadPmTiles>(data); }

        const LoadLayerModel& load_layer_model() const { return std::get<LoadLayerModel>(data); }

        const LoadSourceModel& load_source_model() const { return std::get<LoadSourceModel>(data); }
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

    VectorDataLoader() = default;

public:
    static VectorDataLoader* create(AssetsLoader* al, InMemoryVectorDataBase* in_memory_database);

    void destroy(JobScheduler* js);

    void register_layer(SceneModel* scene_model, uint64_t layer_handle);

    void unregister_layer(uint64_t layer_handle);

    void notify_update(
        uint64_t layer_handle,
        scene_model::UpdateType,
        const scene_model::VectorDataLayerPath& path);

private:
    std::string make_url(
        const hrz::PatternTileUrlGenerator& tile_url_generator,
        hrz::TileCoords tile_coords);

    std::string make_url(
        const hrz::MultiPatternTileUrlGenerator& tile_url_generator,
        hrz::TileCoords tile_coords);

    hrz::assets_loader::Queue get_load_queue(const LayerModel& model);

    uint32_t compute_tile_loading_priority(const LayerModel& model, hrz::TileCoords tile_coords);

    const char* get_vector_data_provider_name(hrz_proto::VectorDataProviderType provider_type);

    LayerModel make_model(
        AttributionRegistry* attributions,
        const hrz_proto::VectorDataLayer& layer,
        uint64_t layer_handle);

    void load_attribute_data_into_map(
        std::vector<vector_data::AttributeValues>& attributes,
        hrz::flat_hash_map<uint32_t, AttributeValueListRef>& attribute_ids_to_values,
        const LayerModel::Source& data_source,
        size_t expected_value_count);

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
        const FeatureSelection& feature_selection);

    TaskRef get_or_create_load_all_attributes_task(
        const LayerModelRef& layer_model,
        const FeatureSelection& feature_selection);

    TaskRef get_or_create_load_attribute_values_task(
        const LayerModelRef& layer_model,
        uint32_t attribute_id,
        const FeatureSelection& feature_selection);

    TaskRef get_or_create_load_feature_ids_task(
        const LayerModelRef& layer_model,
        const FeatureSelection& feature_selection);

    TaskRef get_or_create_load_vector_data_task(
        const LayerModelRef& layer_model,
        uint32_t data_source,
        const FeatureSelection& feature_selection);

    TaskRef get_or_create_load_vector_tile_data_task(
        const LayerModelRef& layer_model,
        uint32_t data_source,
        const FeatureSelection& feature_selection);

    TaskRef get_or_create_load_vector_data_url_package_task(
        const std::string_view& url,
        const hrz::HttpHeaders& headers,
        hrz::assets_loader::Queue queue,
        uint32_t priority,
        hrz_proto::VectorDataFormat format,
        const monitoring::ResourceOwner& resource_owner,
        const metrics::MetricDesc& request_count_metric);

    TaskRef get_or_create_load_vector_data_pmtiles_package_task(
        const std::string_view& url,
        const hrz::HttpHeaders& headers,
        const TileCoords& tile_coords,
        hrz::assets_loader::Queue queue,
        uint32_t priority,
        const monitoring::ResourceOwner& resource_owner,
        const metrics::MetricDesc& request_count_metric);

    TaskRef get_or_create_load_untiled_vector_data_task(
        const LayerModelRef& layer_model,
        uint32_t data_source);

    TaskRef get_or_create_extract_vector_tile_data_task(
        const LayerModelRef& layer_model,
        uint32_t data_source,
        TileCoords tile_coords);

    TaskRef get_or_create_load_in_memory_vector_data_task(
        const LayerModelRef& layer_model,
        uint32_t data_source,
        const FeatureSelection& feature_selection);

    TaskRef get_or_create_load_url_data_task(
        const std::string_view& url,
        const hrz::HttpHeaders& headers,
        hrz::assets_loader::Queue queue,
        uint32_t priority,
        const monitoring::ResourceOwner& resource_owner,
        const metrics::MetricDesc& request_count_metric);

    TaskRef get_or_create_request_client_data_task(
        const LayerModelRef& layer_model,
        uint32_t data_source,
        const FeatureSelection& feature_selection);

    TaskRef get_or_create_load_tilejson_task(
        const std::string_view& url,
        const hrz::HttpHeaders& headers,
        hrz::assets_loader::Queue queue,
        uint32_t priority,
        bool preserve_query_parameters,
        const monitoring::ResourceOwner& resource_owner,
        const metrics::MetricDesc& request_count_metric);

    TaskRef get_or_create_load_pmtiles_task(
        const std::string_view& url,
        const hrz::HttpHeaders& headers,
        hrz::assets_loader::Queue queue,
        uint32_t priority,
        const monitoring::ResourceOwner& resource_owner,
        const metrics::MetricDesc& request_count_metric);

    TaskRef get_or_create_load_layer_model_task(uint32_t layer_id);

    TaskRef get_or_create_load_source_model_task(
        const LayerModelRef& layer_model,
        uint32_t data_source);

    DataRequest create_data_request(
        RequestId request_id,
        RequestId layer_loader_request_id,
        FeatureSelection feature_selection,
        hrz::vector_data::DataKind data_kind);

private:
    void load_layer(RequestId request_id, uint32_t layer_id);

    void release_layer_loader(RequestId request_id);

    void send_layer_model_message(LayerLoader& loader, const LayerModel& layer_model);

    void send_layer_model_error_message(LayerLoader& loader);

    void send_layer_has_new_data_message(LayerLoader& loader);

    void send_data_message(
        DataRequest& request,
        std::variant<
            TileGeometry,
            hrz::InlinedVector<vector_data::AttributeValues, 16>,
            vector_data::FeatureIds> data,
        AttributionHandle attribution);

    void send_geometry_message(WeakTaskRef& task_ref, Task& task);

    void send_attribute_values_message(WeakTaskRef& task_ref, Task& task);

    void send_feature_ids_message(WeakTaskRef& task_ref, Task& task);

    void send_data_error_message(DataRequest& request);

    LayerModelRef get_layer_model_for_loaded_layer(LayerLoader& layer_loader);

    LayerModelRef get_layer_model_for_loaded_layer(RequestId layer_loader_request_id);

    uint32_t get_min_lod(const LayerModel& layer_model);

    uint32_t get_max_lod(const LayerModel& layer_model);

    static hrz::InlinedVector<uint32_t, 16> get_attribute_ids(const LayerModel& layer_model);

    GeoBounds get_bounds(const LayerModel& layer_model);

    void request_data_for_selection(
        RequestId request_id,
        RequestId layer_loader_request_id,
        FeatureSelection feature_selection,
        DataKind data_kind);

    void request_data(
        RequestId request_id,
        RequestId layer_loader_request_id,
        TileCoords tile_coords,
        DataKind data_kind);

    void request_data(
        RequestId request_id,
        RequestId layer_loader_request_id,
        const hrz::vector_data::FeatureIds& feature_ids,
        DataKind data_kind);

    void release_data(RequestId request_id);

    void retain_data(RequestId request_id);

    void release_request(RequestId request_id);

    void visit_load_layer_models_for_layer_id(
        uint32_t layer_id,
        const std::function<void(LayerModel&)>& callback);

    // Returns whether content negotiation headers have changed.
    bool update_source_headers(
        uint64_t layer_handle,
        uint32_t layer_id,
        uint32_t source_index,
        SceneModel* scene_model);

    void update_layers_from_model(SceneModel* scene_model);

    void collect_garbage(JobScheduler* js);

    void activate_task(WeakTaskRef task_ref);

    void set_task_status(WeakTaskRef& task_ref, Task& task, TaskStatus status);

    void clear_task(Task& task, JobScheduler* js);

    template<typename TaskDataType>
    void clear_task(Task& task, TaskDataType& task_data, JobScheduler* js);

    void cancel_task_jobs(Task& task, JobScheduler* js);

    template<typename TaskDataType>
    void cancel_task_jobs(Task& task, TaskDataType& task_data, JobScheduler* js);

    void unload_task_data(Task& task, bool release_dependent_task_data, JobScheduler* js);

    template<typename TaskDataType>
    void unload_task_data(
        Task& task,
        TaskDataType& task_data,
        bool release_dependent_task_data,
        JobScheduler* js);

    void restart_task(WeakTaskRef& task_ref, Task& task, hrz::JobScheduler* js);

    void work_new_task(
        WeakTaskRef& task_ref,
        Task& task,
        SceneModel* scene_model,
        AttributionRegistry* attributions);

    template<typename TaskDataType>
    void work_new_task(
        WeakTaskRef& task_ref,
        Task& task,
        TaskDataType& task_data,
        SceneModel* scene_model,
        AttributionRegistry* attributions);

    void work_unloaded_task(WeakTaskRef& task_ref, Task& task);

    template<typename TaskDataType>
    void work_unloaded_task(WeakTaskRef& task_ref, Task& task, TaskDataType& task_data);

    void work_loading_task(
        WeakTaskRef& task_ref,
        Task& task,
        JobScheduler* js,
        BlobAllocator* ba,
        ClientMessageQueue* mq,
        AttributionRegistry* attributions);

    template<typename TaskDataType>
    void work_loading_task(
        WeakTaskRef& task_ref,
        Task& task,
        TaskDataType& task_data,
        JobScheduler* js,
        BlobAllocator* ba,
        ClientMessageQueue* mq,
        AttributionRegistry* attributions);

    void unload_task_data_if_not_needed(WeakTaskRef& task_ref, Task& task, JobScheduler* js);

    template<typename TaskDataType>
    static bool unload_task_data_if_not_needed();

    void check_for_invalidated_data_for_task(WeakTaskRef& task_ref, Task& task, JobScheduler* js);

    template<typename TaskDataType>
    void check_for_invalidated_data_for_task(
        WeakTaskRef& task_ref,
        Task& task,
        TaskDataType& task_data,
        JobScheduler* js);

    void work_messages(SceneModel* scene_model, JobScheduler* js);

    void work_tasks(
        SceneModel* scene_model,
        JobScheduler* js,
        BlobAllocator* ba,
        ClientMessageQueue* mq,
        AttributionRegistry* attributions);

    void work_client_request_timeouts();

public:
    void work(
        SceneModel* scene_model,
        JobScheduler* js,
        BlobAllocator* ba,
        ClientMessageQueue* mq,
        AttributionRegistry* attributions);

    void provide_client_data(const hrz_proto::VectorDataRequestResponse& response);

    void invalidate_client_data(const hrz_proto::VectorDataInvalidation& invalidation);

    vector_data::VectorDataLoaderChannel create_channel();

private:
    static const char* data_kind_str(DataKind data_kind);

    static const char* task_type_str(const VectorDataLoader::Task& task);

    static const char* task_status_str(const VectorDataLoader::Task& task);

    static const char* status_to_str(VectorDataLoader::ClientRequestHistory::RequestStatus status);

    static mu_Color status_to_text_color(
        VectorDataLoader::ClientRequestHistory::RequestStatus status);

public:
    void dev_ui(mu_Context* ctx);
};
} // namespace hrz
