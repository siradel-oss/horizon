#include "hrz/core/vector/in_memory.h"

#include "hrz/common/blob_allocator.h"
#include "hrz/common/crs_database.h"
#include "hrz/common/geo.h"
#include "hrz/common/profiling.h"
#include "hrz/common/proj.h"
#include "hrz/common/vector_data/attribute_type_in_memory.h" // IWYU pragma: keep
#include "hrz/common/vector_data/attributes_ops.h"
#include "hrz/common/vector_data/geometry_utils.h"
#include "hrz/core/channel_group.h"
#include "hrz/core/scene_path/layer/in_memory_vector_source_layer_paths.h"
#include "hrz/fnd/flat_hash_map.h"
#include "hrz/fnd/flat_hash_set.h"
#include "hrz/fnd/gen_object_pool.h"
#include "hrz/fnd/log.h"
#include "hrz/fnd/node_hash_map.h"
#include "hrz/fnd/thread.h"
#include "hrz/protocol/path_builder/layer/in_memory_vector_source_layer.h"

#include <lin_maths.h>

#include <algorithm>
#include <cassert>
#include <functional>
#include <limits>
#include <mutex>
#include <variant>
#include <vector>

namespace hrz
{
namespace vector_data::in_memory
{
namespace
{
static constexpr TileCoords ROOT_TILE_COORDS = {
    std::numeric_limits<uint32_t>::max(), std::numeric_limits<uint32_t>::max(), 0xff};

lm::dbbox2 flatten_bbox(const lm::dbbox3& bbox)
{
    return lm::dbbox2(bbox.min.xy, bbox.max.xy);
}

using FeatureH = uint64_t;
using TileH = uint64_t;
using LayerPoolH = uint64_t;

struct Feature
{
    FeatureId id;
    hrz_proto::VectorGeometryType type;
    lm::dbbox3 bbox;
    lm::dvec3 anchor;
    float anchor_angle;
    std::vector<OwnedAttributeValue> attribute_values;
    size_t out_of_line_data_size;
    bool geometry_updated;
    bool attribute_values_updated;
};

struct Tile
{
    static constexpr FeatureH EMPTY_FEATURE = 0;

    struct Feature
    {
        FeatureH handle;
        FeatureId id;
        hrz_proto::VectorGeometryType type;
        size_t first_point;
        size_t point_count;
        size_t first_linestring_size;
        size_t linestring_count;
        lm::dvec3 anchor;
        float anchor_angle;
    };

    lm::dbbox2 bbox;
    lm::dbbox2 data_bbox;
    uint32_t use_count;
    size_t out_of_line_data_size = 0;
    std::vector<Feature> features;
    std::vector<lm::dvec3> positions; // In web-Mercator metres
    std::vector<uint32_t> sizes;
};

struct UpdatedAttributeValueIndex
{
    uint32_t attribute_index;
    uint32_t value_index;
};

struct FeatureIdList
{
    using Hash = FeatureIds::Hash;

    static FeatureIdList from_ids(FeatureIds ids)
    {
        FeatureIdList list;
        list.feature_ids = std::move(ids);
        list.feature_ids_hash = list.feature_ids.compute_hash();
        list.use_count_value = 0;

        return list;
    }

    Hash hash() const { return feature_ids_hash; }

    bool contains(const FeatureId& feature_id) const { return feature_ids.contains(feature_id); }

    FeatureIds ids() const { return feature_ids; }

    uint32_t use_count() const { return use_count_value; }

    void retain() { use_count_value += 1; }

    void release()
    {
        assert(use_count_value > 0);
        use_count_value -= 1;
    }

private:
    FeatureIds feature_ids;
    Hash feature_ids_hash;
    uint32_t use_count_value;
};

struct AttributeDefinition
{
    uint32_t id;
    hrz_proto::AttributeTransform transform;
    bool is_feature_id;
};

struct Layer
{
    uint32_t id;
    uint64_t layer_model_handle;
    pl_Transform transform;
    AttributionHandle attribution{};
    std::vector<FeatureH> features;
    std::vector<AttributeDefinition> attributes;
    hrz::flat_hash_map<hrz::TileCoords, TileH> tiles_by_coords;
    hrz::flat_hash_map<FeatureIds::Hash, TileH> tiles_by_feature_ids;

    bool id_updated = false;
    bool projection_updated = false;
    bool attribution_updated = false;
    bool all_features_updated = false;
    bool features_updated = false;
    bool attribute_definitions_updated = false;
    std::vector<FeatureH> deleted_features;
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

struct DataTracker
{
    enum class Status
    {
        GatheringData,
        AllocatingBlobs,
        Loaded,
        Error,
    };

    struct FeatureIdListSelection
    {
        FeatureIdList::Hash hash;
    };

    RequestId request_id;
    Status status{};
    uint32_t layer_id{};
    std::variant<hrz::TileCoords, FeatureIdListSelection> feature_selection;
    bool one_shot = false;
    bool tile_data_has_changed = false;

    struct GeometryBlobs
    {
        BlobArrayAllocation<VectorTileGeometry::Feature> features;
        BlobArrayAllocation<lm::dvec3> points;
        BlobArrayAllocation<uint32_t> sizes;
    };

    std::optional<BlobArrayAllocation<FeatureIdHash>> feature_id_hash_blob;
    std::optional<GeometryBlobs> geometry_blobs;
    std::vector<BlobArrayAllocation<PackedAttributeValue>> attribute_blobs;
    std::optional<BlobArrayAllocation<char>> out_of_line_attributes_data;

    DecodedVectorTile tile_data;
    AttributionHandle attribution;

    bool has_feature_ids() const
    {
        return std::holds_alternative<FeatureIdListSelection>(feature_selection);
    }

    const FeatureIdList::Hash& feature_ids() const
    {
        assert(std::holds_alternative<FeatureIdListSelection>(feature_selection));
        return std::get<FeatureIdListSelection>(feature_selection).hash;
    }

    bool has_tile_coords() const
    {
        return std::holds_alternative<hrz::TileCoords>(feature_selection);
    }

    const hrz::TileCoords& tile_coords() const
    {
        assert(std::holds_alternative<hrz::TileCoords>(feature_selection));
        return std::get<hrz::TileCoords>(feature_selection);
    }
};
} // namespace
} // namespace vector_data::in_memory

struct InMemoryVectorDataBase
{
    using LayerIndexPool = GenIndexPool<vector_data::in_memory::LayerPoolH, 32, 32>;
    using LayerPool = GenObjectPool<vector_data::in_memory::Layer, LayerIndexPool, 64>;

    using FeatureIndexPool = hrz::GenIndexPool<vector_data::in_memory::FeatureH, 32, 32>;
    using FeaturePool = hrz::GenObjectPool<vector_data::in_memory::Feature, FeatureIndexPool, 64>;

    using TileIndexPool = hrz::GenIndexPool<vector_data::in_memory::TileH, 32, 32>;
    using TilePool = hrz::GenObjectPool<vector_data::in_memory::Tile, TileIndexPool, 64>;

    LayerPool layer_pool;
    hrz::flat_hash_map<uint64_t, vector_data::in_memory::LayerPoolH> layer_model_to_pool;
    hrz::flat_hash_map<uint32_t, vector_data::in_memory::LayerPoolH> active_layers;

    std::vector<uint64_t> unregistered_layers;

    std::mutex model_mutex;

    // Feature ID lists are created and destroyed according to data request
    // tracker creation and destruction.
    // `Layer::tiles_by_feature_ids` refers to the same objects, but because
    // tiles are also created and destroyed along with trackers, and as the
    // contents of the lists are only accessed through the trackers, there
    // is no need to retain and release feature ID lists for tiles.
    hrz::node_hash_map<
        vector_data::in_memory::FeatureIdList::Hash,
        vector_data::in_memory::FeatureIdList>
        feature_id_lists;

    FeaturePool feature_pool;
    TilePool tile_pool;

    hrz::flat_hash_map<vector_data::in_memory::RequestId, vector_data::in_memory::DataTracker>
        request_ids_to_trackers;

    hrz::ChannelGroup<
        vector_data::in_memory::FromInMemoryMessages,
        vector_data::in_memory::ToInMemoryMessages>
        channels;
};

namespace vector_data::in_memory
{
namespace
{
Layer* _get_layer(InMemoryVectorDataBase* system, uint64_t layer_model_handle)
{
    auto it = system->layer_model_to_pool.find(layer_model_handle);
    if (it == system->layer_model_to_pool.end())
    {
        return nullptr;
    }
    return system->layer_pool.get_object(it->second);
}

void _compute_feature_id(Layer* layer, Feature* feature)
{
    FeatureId::Builder builder;

    for (size_t i = 0; i < layer->attributes.size(); ++i)
    {
        const auto& attribute = layer->attributes.at(i);

        if (attribute.is_feature_id)
        {
            builder.add_value(attribute.id, feature->attribute_values[i]);
        }
    }

    feature->id = builder.build();
}

void _project_positions(Layer* layer, std::span<lm::dvec3> positions)
{
    if (positions.empty()) return;

    pl_transform_in_place_canonical(&layer->transform, positions.size(), &positions[0].x);
}

TileCoords _parent_tile_coords(TileCoords tile_coords)
{
    assert(tile_coords != ROOT_TILE_COORDS);

    if (tile_coords.lod == 0) return ROOT_TILE_COORDS;

    return {tile_coords.x / 2, tile_coords.y / 2, (uint8_t)(tile_coords.lod - 1)};
}

void _update_tile_use_count(
    InMemoryVectorDataBase* system,
    Layer* layer,
    TileCoords tile_coords,
    int change)
{
    HRZ_SCOPED_SAMPLE_A("update use count");

    auto it = layer->tiles_by_coords.find(tile_coords);
    assert(it != layer->tiles_by_coords.end());

    auto& tile_handle = it->second;
    auto tile = system->tile_pool.get_object(tile_handle);
    if (change < 0) assert(tile->use_count >= (uint32_t)(-change));
    tile->use_count += change;

    if (tile->use_count == 0)
    {
        layer->tiles_by_coords.erase(tile_coords);
    }

    if (tile_coords.lod == 0) return;

    _update_tile_use_count(system, layer, _parent_tile_coords(tile_coords), change);
}

void _update_tile_use_count(
    InMemoryVectorDataBase* system,
    Layer* layer,
    const FeatureIdList& feature_id_list,
    int change)
{
    HRZ_SCOPED_SAMPLE_A("update use count");

    auto it = layer->tiles_by_feature_ids.find(feature_id_list.hash());
    assert(it != layer->tiles_by_feature_ids.end());

    auto& tile_handle = it->second;
    auto tile = system->tile_pool.get_object(tile_handle);
    if (change < 0) assert(tile->use_count >= (uint32_t)(-change));
    tile->use_count += change;

    if (tile->use_count == 0)
    {
        layer->tiles_by_feature_ids.erase(feature_id_list.hash());
    }
}

// The root tile is regenerated when features have been created, destroyed,
// or modified. It contains all the features, extracted from the model.
// Geographical tiles, as well as "tiles" that are built from a feature
// ID list or from reference attribute values, are generated as extracts
// from the root tile.
void _generate_root_tile(
    InMemoryVectorDataBase* system,
    Layer* layer,
    const hrz_proto::InMemoryVectorSourceLayerPathBuilder<hrz::SceneModelAccessor>& path_builder)
{
    HRZ_SCOPED_SAMPLE_A("generate root tile");

    TileCoords tile_coords = ROOT_TILE_COORDS;
    uint32_t use_count = 1;
    auto current_it = layer->tiles_by_coords.find(tile_coords);
    if (current_it != layer->tiles_by_coords.end())
    {
        TileH current_tile_handle = current_it->second;
        Tile* current_tile = system->tile_pool.get_object(current_tile_handle);
        use_count = current_tile->use_count;
        assert(use_count >= 1);
        system->tile_pool.release(current_tile_handle);
    }

    TileH tile_handle = system->tile_pool.alloc();
    Tile* tile = system->tile_pool.get_object(tile_handle);
    tile->bbox = hrz::mercator_tile_bbox_meters(tile_coords);
    tile->data_bbox = lm::dbbox2::invalid();
    tile->use_count = use_count;
    tile->out_of_line_data_size = 0;

    assert(layer->features.size() == path_builder.clone().features_count());

    for (unsigned int i = 0; i < layer->features.size(); ++i)
    {
        auto feature_handle = layer->features.at(i);
        const auto feature = system->feature_pool.get_object(feature_handle);

        Tile::Feature tile_feature;
        tile_feature.handle = feature_handle;
        tile_feature.id = feature->id;

        auto feature_geometry = path_builder.clone().features(i).geometry().get();

        unsigned int coord_count = feature_geometry.coords_size();

        if ((feature->type == hrz_proto::VectorGeometryType::POINT_GEOMETRY && coord_count < 3)
            || (feature->type == hrz_proto::VectorGeometryType::POLYLINE_GEOMETRY
                && coord_count < 6)
            || (feature->type == hrz_proto::VectorGeometryType::POLYGON_GEOMETRY
                && coord_count < 9))
        {
            // The geometry is invalid.
            tile_feature.type = hrz_proto::VectorGeometryType::POLYLINE_GEOMETRY;
            tile_feature.first_point = 0;
            tile_feature.point_count = 0;
            tile_feature.first_linestring_size = 0;
            tile_feature.linestring_count = 0;
            tile_feature.anchor = lm::dvec3(std::numeric_limits<double>::quiet_NaN());
            tile_feature.anchor_angle = 0;
            tile->features.push_back(tile_feature);
            tile->out_of_line_data_size += feature->out_of_line_data_size;
            continue;
        }

        tile_feature.type = feature->type;

        if (feature->type == hrz_proto::VectorGeometryType::POINT_GEOMETRY)
        {
            coord_count = 3;
        }

        tile_feature.first_point = tile->positions.size();
        tile_feature.point_count = coord_count / 3;

        for (unsigned int j = 0; j + 2 < coord_count; j += 3)
        {
            lm::dvec3 point = {
                feature_geometry.coords(j + 0), feature_geometry.coords(j + 1),
                feature_geometry.coords(j + 2)};

            tile->positions.push_back(point);
        }

        if (feature->type == hrz_proto::VectorGeometryType::POLYGON_GEOMETRY
            || feature->type == hrz_proto::VectorGeometryType::POLYLINE_GEOMETRY)
        {
            tile_feature.first_linestring_size = tile->sizes.size();

            unsigned int linestring_count = feature_geometry.linestring_sizes_size();

            if (linestring_count > 0)
            {
                uint32_t current_ring_start = 0;
                tile_feature.linestring_count = 0;

                for (unsigned int j = 0; j < linestring_count; ++j)
                {
                    uint32_t remaining_points = tile_feature.point_count - current_ring_start;
                    uint32_t linestring_size =
                        std::min(feature_geometry.linestring_sizes(j), remaining_points);

                    tile->sizes.push_back(linestring_size);

                    current_ring_start += linestring_size;
                    tile_feature.linestring_count += 1;

                    if (current_ring_start >= tile_feature.point_count) break;
                }
            }
            else
            {
                tile->sizes.push_back(tile_feature.point_count);
                tile_feature.linestring_count = 1;
            }
        }
        else
        {
            tile_feature.first_linestring_size = 0;
            tile_feature.linestring_count = 0;
        }

        tile->features.push_back(tile_feature);
        tile->out_of_line_data_size += feature->out_of_line_data_size;
        tile->data_bbox = lm::merge(tile->data_bbox, flatten_bbox(feature->bbox));
    }

    _project_positions(layer, tile->positions);

    for (unsigned int i = 0; i < layer->features.size(); ++i)
    {
        auto feature_handle = layer->features.at(i);
        const auto layer_feature = system->feature_pool.get_object(feature_handle);

        auto& feature = tile->features[i];

        if (feature.type == hrz_proto::VectorGeometryType::POLYGON_GEOMETRY)
        {
            uint32_t count = tile->sizes[feature.first_linestring_size];
            uint32_t first_point = feature.first_point;
            feature.anchor = vector_data::compute_ring_average(
                std::span<const lm::dvec3>(tile->positions).subspan(first_point, count));
            feature.anchor_angle = 0.0f;
        }
        else if (feature.type == hrz_proto::VectorGeometryType::POLYLINE_GEOMETRY)
        {
            auto points = std::span<const lm::dvec3>(tile->positions)
                              .subspan(feature.first_point, feature.point_count);
            auto linestring_sizes =
                std::span<const uint32_t>(tile->sizes)
                    .subspan(feature.first_linestring_size, feature.linestring_count);
            vector_data::compute_linestring_middle_and_angle(
                points, linestring_sizes, &feature.anchor, &feature.anchor_angle);
        }
        else if (feature.type == hrz_proto::VectorGeometryType::POINT_GEOMETRY)
        {
            feature.anchor = tile->positions[feature.first_point];
            feature.anchor_angle = 0.0f;
        }
        else
        {
            assert(!"Unhandled feature type");
        }

        layer_feature->anchor = feature.anchor;
        layer_feature->anchor_angle = feature.anchor_angle;
    }

    layer->tiles_by_coords[tile_coords] = tile_handle;
}

enum class FeatureInsertionDecision
{
    Skip,
    InsertAndContinue,
    InsertAndStop,
};

void _insert_features_from_parent_in_tile(
    InMemoryVectorDataBase* system,
    Tile* tile,
    const Tile* parent_tile,
    const std::function<FeatureInsertionDecision(const Feature*)>& filter_predicate)
{
    for (size_t feature_index = 0; feature_index < parent_tile->features.size(); ++feature_index)
    {
        const auto& parent_tile_feature = parent_tile->features.at(feature_index);
        auto feature_handle = parent_tile_feature.handle;
        const auto feature = system->feature_pool.get_object(feature_handle);

        auto decision = filter_predicate(feature);
        if (decision == FeatureInsertionDecision::Skip) continue;

        Tile::Feature tile_feature;
        tile_feature.handle = feature_handle;
        tile_feature.id = feature->id;
        tile_feature.type = feature->type;

        tile_feature.first_point = tile->positions.size();
        tile_feature.point_count = parent_tile_feature.point_count;

        for (unsigned int i = 0; i < tile_feature.point_count; ++i)
        {
            tile->positions.push_back(parent_tile->positions[parent_tile_feature.first_point + i]);
        }

        tile_feature.first_linestring_size = tile->sizes.size();
        tile_feature.linestring_count = parent_tile_feature.linestring_count;

        for (unsigned int i = 0; i < tile_feature.linestring_count; ++i)
        {
            tile->sizes.push_back(
                parent_tile->sizes[parent_tile_feature.first_linestring_size + i]);
        }

        tile_feature.anchor = feature->anchor;
        tile_feature.anchor_angle = feature->anchor_angle;

        tile->features.push_back(tile_feature);
        tile->out_of_line_data_size += feature->out_of_line_data_size;
        tile->data_bbox = lm::merge(tile->data_bbox, flatten_bbox(feature->bbox));

        if (decision == FeatureInsertionDecision::InsertAndStop) return;
    }
}

void _generate_tile_for_coords(InMemoryVectorDataBase* system, Layer* layer, TileCoords tile_coords)
{
    HRZ_SCOPED_SAMPLE_A("generate tile for coords");

    uint32_t use_count = 1;
    auto current_it = layer->tiles_by_coords.find(tile_coords);
    if (current_it != layer->tiles_by_coords.end())
    {
        TileH current_tile_handle = current_it->second;
        Tile* current_tile = system->tile_pool.get_object(current_tile_handle);
        use_count = current_tile->use_count;
        assert(use_count >= 1);
        system->tile_pool.release(current_tile_handle);
    }

    TileH tile_handle = system->tile_pool.alloc();
    Tile* tile = system->tile_pool.get_object(tile_handle);
    tile->bbox = hrz::mercator_tile_bbox_meters(tile_coords);
    tile->data_bbox = lm::dbbox2::invalid();
    tile->use_count = use_count;

    TileCoords parent_tile_coords = _parent_tile_coords(tile_coords);
    auto parent_it = layer->tiles_by_coords.find(parent_tile_coords);
    assert(parent_it != layer->tiles_by_coords.end());
    const auto parent_tile = system->tile_pool.get_object(parent_it->second);

    _insert_features_from_parent_in_tile(
        system, tile, parent_tile,
        [&](const Feature* feature)
        {
            return lm::intersect(flatten_bbox(feature->bbox), tile->bbox)
                ? FeatureInsertionDecision::InsertAndContinue
                : FeatureInsertionDecision::Skip;
        });

    layer->tiles_by_coords[tile_coords] = tile_handle;
}

void _generate_tiles_for_coords(
    InMemoryVectorDataBase* system,
    Layer* layer,
    TileCoords tile_coords)
{
    HRZ_SCOPED_SAMPLE_A("generate tiles for coords");

    if (tile_coords == ROOT_TILE_COORDS) return;

    auto it = layer->tiles_by_coords.find(tile_coords);

    if (it != layer->tiles_by_coords.end()) return;

    _generate_tiles_for_coords(system, layer, _parent_tile_coords(tile_coords));

    _generate_tile_for_coords(system, layer, tile_coords);
}

void _generate_tile_for_features(
    InMemoryVectorDataBase* system,
    Layer* layer,
    const FeatureIdList& feature_id_list)
{
    HRZ_SCOPED_SAMPLE_A("generate tile for feature ids");

    uint32_t use_count = 1;
    auto current_it = layer->tiles_by_feature_ids.find(feature_id_list.hash());
    if (current_it != layer->tiles_by_feature_ids.end())
    {
        TileH current_tile_handle = current_it->second;
        Tile* current_tile = system->tile_pool.get_object(current_tile_handle);
        use_count = current_tile->use_count;
        assert(use_count >= 1);
        system->tile_pool.release(current_tile_handle);
    }

    TileH tile_handle = system->tile_pool.alloc();
    Tile* tile = system->tile_pool.get_object(tile_handle);
    tile->bbox = lm::dbbox2::invalid();
    tile->data_bbox = lm::dbbox2::invalid();
    tile->use_count = use_count;

    auto root_it = layer->tiles_by_coords.find(ROOT_TILE_COORDS);
    assert(root_it != layer->tiles_by_coords.end());
    const auto root_tile = system->tile_pool.get_object(root_it->second);

    for (size_t i = 0; i < feature_id_list.ids().size(); ++i)
    {
        auto feature_id = feature_id_list.ids().at(i);

        bool found = false;
        _insert_features_from_parent_in_tile(
            system, tile, root_tile,
            [&](const Feature* feature)
            {
                if (feature->id == feature_id)
                {
                    found = true;
                    return FeatureInsertionDecision::InsertAndStop;
                }
                return FeatureInsertionDecision::Skip;
            });

        if (!found)
        {
            // Insert feature with empty geometry
            Tile::Feature tile_feature;
            tile_feature.handle = Tile::EMPTY_FEATURE;
            tile_feature.id = feature_id;
            tile_feature.first_point = 0;
            tile_feature.point_count = 0;
            tile_feature.first_linestring_size = 0;
            tile_feature.linestring_count = 0;
            tile_feature.type = hrz_proto::VectorGeometryType::POLYGON_GEOMETRY;
            tile_feature.anchor = lm::dvec3(std::numeric_limits<double>::quiet_NaN());
            tile_feature.anchor_angle = 0;

            tile->features.push_back(tile_feature);
        }
    }

    layer->tiles_by_feature_ids[feature_id_list.hash()] = tile_handle;
}

void _generate_tiles_for_all_requests(InMemoryVectorDataBase* system, Layer* layer)
{
    HRZ_SCOPED_SAMPLE_A("generate tiles for all requests");

    auto root_tile_handle = layer->tiles_by_coords.at(ROOT_TILE_COORDS);
    layer->tiles_by_coords.clear();
    layer->tiles_by_coords.insert({ROOT_TILE_COORDS, root_tile_handle});

    layer->tiles_by_feature_ids.clear();

    auto root_tile = system->tile_pool.get_object(root_tile_handle);
    root_tile->use_count = 1;

    for (auto& it : system->request_ids_to_trackers)
    {
        auto& tracker = it.second;

        if (tracker.layer_id == layer->id)
        {
            if (tracker.has_tile_coords())
            {
                _generate_tiles_for_coords(system, layer, tracker.tile_coords());
                _update_tile_use_count(system, layer, tracker.tile_coords(), 1);
            }
            else if (tracker.has_feature_ids())
            {
                const auto& feature_ids = system->feature_id_lists.at(tracker.feature_ids());
                _generate_tile_for_features(system, layer, feature_ids);
                _update_tile_use_count(system, layer, feature_ids, 1);
            }
            else
            {
                assert(false && "Unhandled case");
            }

            tracker.tile_data_has_changed = true;
            tracker.tile_data = {};
            tracker.attribution = {};
        }
    }
}

void _clear_tracker_data(DataTracker& tracker, BlobAllocator* ba)
{
    if (tracker.feature_id_hash_blob.has_value())
    {
        tracker.feature_id_hash_blob->cancel(ba);
        tracker.feature_id_hash_blob = {};
    }

    if (tracker.geometry_blobs.has_value())
    {
        tracker.geometry_blobs->features.cancel(ba);
        tracker.geometry_blobs->points.cancel(ba);
        tracker.geometry_blobs->sizes.cancel(ba);
        tracker.geometry_blobs = {};
    }

    if (tracker.out_of_line_attributes_data.has_value())
    {
        tracker.out_of_line_attributes_data->cancel(ba);
        tracker.out_of_line_attributes_data = {};
    }

    for (auto& attribute : tracker.attribute_blobs)
    {
        attribute.cancel(ba);
    }
    tracker.attribute_blobs.clear();

    tracker.tile_data = {};
    tracker.attribution = {};
}

void _restart_tracker(DataTracker& tracker, BlobAllocator* ba)
{
    _clear_tracker_data(tracker, ba);

    tracker.status = DataTracker::Status::GatheringData;
    tracker.tile_data_has_changed = true;
}

// The property `id` of the layer must be updated before calling this
// function, or the deactivated layer will be reactivated immediately.
void _deactivate_layer(
    InMemoryVectorDataBase* system,
    LayerPoolH layer_pool_handle,
    uint32_t layer_id,
    bool layer_is_being_deleted = false)
{
    HRZ_SCOPED_SAMPLE_A("deactivate layer");

    auto it_active = system->active_layers.find(layer_id);
    if (it_active != system->active_layers.end())
    {
        if (it_active->second == layer_pool_handle)
        {
            // The layer was active.
            system->active_layers.erase(it_active);

            // Signal that data has changed for the whole layer.
            for (auto& it : system->request_ids_to_trackers)
            {
                auto& tracker = it.second;

                if (tracker.layer_id == layer_id)
                {
                    if (!layer_is_being_deleted)
                    {
                        auto layer = system->layer_pool.get_object(layer_pool_handle);

                        if (tracker.has_tile_coords())
                        {
                            _update_tile_use_count(system, layer, tracker.tile_coords(), -1);
                        }
                        else if (tracker.has_feature_ids())
                        {
                            auto& feature_ids = system->feature_id_lists.at(tracker.feature_ids());
                            _update_tile_use_count(system, layer, feature_ids, -1);
                        }
                        else
                        {
                            assert(false && "Unhandled case");
                        }
                    }

                    tracker.tile_data_has_changed = true;
                }
            }

            // Try to find another layer with the same id.
            for (auto it_all : system->layer_model_to_pool)
            {
                auto other_layer = _get_layer(system, it_all.first);
                if (other_layer->id == layer_id && it_all.second != layer_pool_handle)
                {
                    // Found such a layer, activate it.
                    system->active_layers.insert({layer_id, it_all.second});

                    // And generate the tiles that are needed.
                    _generate_tiles_for_all_requests(system, other_layer);
                }
            }
        }
    }
}

void _unregister_layers(InMemoryVectorDataBase* system, SceneModel* model)
{
    HRZ_SCOPED_SAMPLE_A("unregister layers");

    for (auto layer_model_handle : system->unregistered_layers)
    {
        auto it = system->layer_model_to_pool.find(layer_model_handle);
        if (it != system->layer_model_to_pool.end())
        {
            hrz_proto::PathRoot root;
            root.mutable_in_memory_vector_source_layer()->set_opaque(layer_model_handle);
            scene_model::unregister_element(model, root);

            auto layer_pool_handle = it->second;
            auto layer = system->layer_pool.get_object(layer_pool_handle);
            uint32_t layer_id = layer->id;

            for (auto it : layer->tiles_by_coords)
            {
                system->tile_pool.release(it.second);
            }

            for (auto feature_handle : layer->features)
            {
                system->feature_pool.release(feature_handle);
            }
            for (auto feature_handle : layer->deleted_features)
            {
                system->feature_pool.release(feature_handle);
            }

            if (layer)
            {
                system->layer_pool.release(layer_pool_handle);
            }

            system->layer_model_to_pool.erase(it);

            _deactivate_layer(system, layer_pool_handle, layer_id, true);
        }
    }

    system->unregistered_layers.clear();
}
} // namespace

InMemoryVectorDataBase* create_system()
{
    return new InMemoryVectorDataBase();
}

void destroy_system(
    InMemoryVectorDataBase* system,
    SceneModel* scene_model,
    BlobAllocator* blob_allocator)
{
    assert(system);

    for (auto& it : system->request_ids_to_trackers)
    {
        auto& tracker = it.second;
        _clear_tracker_data(tracker, blob_allocator);
    }

    for (auto& it : system->layer_model_to_pool)
    {
        hrz_proto::PathRoot root;
        root.mutable_in_memory_vector_source_layer()->set_opaque(it.first);
        scene_model::unregister_element(scene_model, root);
    }

    delete system;
}

void register_layer(InMemoryVectorDataBase* system, SceneModel* model, uint64_t layer_model_handle)
{
    HRZ_SCOPED_SAMPLE_A("in memory vectors register layer");

    assert(system && model);

    HRZ_SCOPED_LOCK(system->model_mutex);

    if (system->layer_model_to_pool.count(layer_model_handle) == 0)
    {
        auto layer_pool_handle = system->layer_pool.alloc();
        auto layer = system->layer_pool.get_object(layer_pool_handle);
        layer->layer_model_handle = layer_model_handle;
        layer->id = 0;
        layer->transform = hrz_proj::lonlat_deg_to_wmerc;
        layer->id_updated = false;
        layer->all_features_updated = false;
        layer->attribute_definitions_updated = false;

        auto root_tile_handle = system->tile_pool.alloc();
        auto root_tile = system->tile_pool.get_object(root_tile_handle);
        root_tile->bbox = hrz::mercator_tile_bbox_meters({0, 0, 0});
        root_tile->use_count = 1; // Prevent the root tile from being deleted.
        layer->tiles_by_coords.insert({ROOT_TILE_COORDS, root_tile_handle});

        system->layer_model_to_pool.insert({layer_model_handle, layer_pool_handle});

        hrz_proto::PathRoot root;
        root.mutable_in_memory_vector_source_layer()->set_opaque(layer_model_handle);
        scene_model::register_element(model, root);

        // Default data
        hrz_proto::InMemoryVectorSourceLayer data;
        data.set_id(0);
        data.mutable_projection()->set_descriptor_type(
            hrz_proto::SrsDescriptorType::PROJ4_STRING_DESCRIPTOR);
        data.mutable_projection()->set_descriptor_(hrz_proj::lonlat_deg_proj_str);

        hrz_proto::InMemoryVectorSourceLayerPathBuilder<hrz::SceneModelAccessor>(
            model, root.in_memory_vector_source_layer())
            .set(data);

        // Activate layer if there no active layer with id 0.
        auto it_active = system->active_layers.find(0);
        if (it_active == system->active_layers.end())
        {
            system->active_layers.insert({0, layer_pool_handle});

            _generate_tiles_for_all_requests(system, layer);
        }
    }
}

void unregister_layer(InMemoryVectorDataBase* system, uint64_t layer_model_handle)
{
    assert(system);

    HRZ_SCOPED_LOCK(system->model_mutex);

    system->unregistered_layers.push_back(layer_model_handle);
}

void request_tile_data(
    InMemoryVectorDataBase* system,
    const RequestId& request_id,
    uint32_t layer_id,
    TileCoords tile_coords,
    bool subscribe_to_updates)
{
    assert(system);

    auto layer_it = system->active_layers.find(layer_id);

    if (layer_it != system->active_layers.end())
    {
        auto* layer = system->layer_pool.get_object(layer_it->second);

        _generate_tiles_for_coords(system, layer, tile_coords);
        _update_tile_use_count(system, layer, tile_coords, 1);
    }

    DataTracker tracker;
    tracker.request_id = request_id;
    tracker.status = DataTracker::Status::GatheringData;
    tracker.layer_id = layer_id;
    tracker.feature_selection = tile_coords;
    tracker.one_shot = !subscribe_to_updates;
    tracker.tile_data_has_changed = false;

    system->request_ids_to_trackers.insert({request_id, std::move(tracker)});
}

void request_feature_data(
    InMemoryVectorDataBase* system,
    const RequestId& request_id,
    uint32_t layer_id,
    const hrz::vector_data::FeatureIds& feature_ids,
    bool subscribe_to_updates)
{
    assert(system);

    auto& feature_id_list = [&]() -> FeatureIdList&
    {
        auto feature_id_list = FeatureIdList::from_ids(feature_ids);

        auto it = system->feature_id_lists.find(feature_id_list.hash());
        if (it != system->feature_id_lists.end())
        {
            return it->second;
        }
        else
        {
            auto hash = feature_id_list.hash();
            system->feature_id_lists.insert({hash, std::move(feature_id_list)});
            return system->feature_id_lists.find(hash)->second;
        }
    }();

    feature_id_list.retain();

    auto status = DataTracker::Status::GatheringData;

    auto layer_it = system->active_layers.find(layer_id);

    if (layer_it != system->active_layers.end())
    {
        auto* layer = system->layer_pool.get_object(layer_it->second);

        _generate_tile_for_features(system, layer, feature_id_list);
        _update_tile_use_count(system, layer, feature_id_list, 1);

        for (const auto& attribute : layer->attributes)
        {
            if (attribute.is_feature_id)
            {
                if (!feature_ids.has_attribute(attribute.id))
                {
                    HRZ_LOG_WARNING(
                        "Attributes in provided feature IDs do not match the definition of "
                        "in-memory layer {}",
                        layer_id);
                    break;
                }
            }
        }
    }

    DataTracker tracker;
    tracker.request_id = request_id;
    tracker.status = status;
    tracker.layer_id = layer_id;
    tracker.feature_selection = DataTracker::FeatureIdListSelection{feature_id_list.hash()};
    tracker.one_shot = !subscribe_to_updates;
    tracker.tile_data_has_changed = false;

    system->request_ids_to_trackers.insert({request_id, std::move(tracker)});
}

void release_data_request(InMemoryVectorDataBase* system, const RequestId& request_id)
{
    assert(system);

    auto tracker_it = system->request_ids_to_trackers.find(request_id);
    if (tracker_it == system->request_ids_to_trackers.end())
    {
        return;
    }

    auto& tracker = tracker_it->second;

    auto layer_it = system->active_layers.find(tracker.layer_id);

    if (layer_it != system->active_layers.end())
    {
        auto* layer = system->layer_pool.get_object(layer_it->second);

        if (tracker.has_tile_coords())
        {
            _update_tile_use_count(system, layer, tracker.tile_coords(), -1);
        }
        else if (tracker.has_feature_ids())
        {
            const auto& feature_ids = system->feature_id_lists.at(tracker.feature_ids());
            _update_tile_use_count(system, layer, feature_ids, -1);
        }
        else
        {
            assert(false && "Unhandled case");
        }
    }

    if (tracker.has_feature_ids())
    {
        auto& feature_ids = system->feature_id_lists.at(tracker.feature_ids());
        feature_ids.release();

        if (feature_ids.use_count() == 0)
        {
            system->feature_id_lists.erase(feature_ids.hash());
        }
    }

    system->request_ids_to_trackers.erase(tracker_it);
}

void notify_update(
    InMemoryVectorDataBase* system,
    uint64_t layer_model_handle,
    scene_model::UpdateType update_type,
    const scene_model::InMemoryVectorSourceLayerPath& path)
{
    HRZ_SCOPED_SAMPLE_A("in memory vectors notify update");

    assert(system);

    HRZ_SCOPED_LOCK(system->model_mutex);

    auto layer = _get_layer(system, layer_model_handle);
    if (!layer) return;

    layer->id_updated = path.leaf() || path.is_id();
    layer->projection_updated = path.leaf() || path.is_projection();
    layer->attribution_updated = path.leaf() || path.is_attribution();
    layer->all_features_updated = path.leaf() || path.is_projection() || path.is_attributes();
    layer->attribute_definitions_updated = path.leaf() || path.is_attributes();

    if (path.is_features())
    {
        auto features_path = path.clone().features();
        if (features_path.leaf())
        {
            if (update_type == scene_model::UpdateType::Add)
            {
                auto feature_handle = system->feature_pool.alloc();
                auto feature = system->feature_pool.get_object(feature_handle);
                feature->id = {};
                feature->type = hrz_proto::VectorGeometryType::POINT_GEOMETRY;
                feature->bbox = lm::dbbox3::invalid();
                feature->anchor = {};
                feature->anchor_angle = 0;
                feature->geometry_updated = true;
                feature->attribute_values_updated = true;
                layer->features.push_back(feature_handle);
            }
            else if (update_type == scene_model::UpdateType::Remove)
            {
                assert(path.has_features_index());
                if (path.features_index() < layer->features.size())
                {
                    layer->deleted_features.push_back(layer->features.at(path.features_index()));
                    layer->features.erase(layer->features.begin() + path.features_index());
                }
            }
            else if (update_type == scene_model::UpdateType::Set)
            {
                assert(path.has_features_index());
                if (path.features_index() < layer->features.size())
                {
                    auto feature_handle = layer->features.at(path.features_index());
                    auto feature = system->feature_pool.get_object(feature_handle);
                    feature->geometry_updated = true;
                    feature->attribute_values_updated = true;
                }
            }
        }
        else
        {
            assert(path.has_features_index());
            if (path.features_index() < layer->features.size())
            {
                auto feature_handle = layer->features.at(path.features_index());
                auto feature = system->feature_pool.get_object(feature_handle);

                if (features_path.is_attribute_values()
                    && features_path.attribute_values_index() < layer->attributes.size())
                {
                    feature->attribute_values_updated = true;
                }
                else
                {
                    feature->geometry_updated = true;
                }
            }
        }

        layer->features_updated = true;
    }
}

void work(
    InMemoryVectorDataBase* system,
    SceneModel* model,
    BlobAllocator* ba,
    AttributionRegistry* attributions)
{
    HRZ_SCOPED_SAMPLE_A("in memory vectors work");

    assert(system && model && ba);

    HRZ_SCOPED_LOCK(system->model_mutex);

    system->channels.work();

    for (auto& it : system->channels)
    {
        auto channel_id = it.first;
        auto& channel = it.second;

        for (auto& generic_message : channel.receive())
        {
            std::visit(
                hrz::overload{
                    [&](const messages::TileDataRequest& message)
                    {
                        auto request_id = RequestId{channel_id, message.request_id};

                        auto it = system->request_ids_to_trackers.find(request_id);
                        if (it != system->request_ids_to_trackers.end())
                        {
                            HRZ_LOG_WARNING(
                                "A request with ID {}-{} was already present, replacing it",
                                channel_id, message.request_id);
                            release_data_request(system, request_id);
                        }

                        request_tile_data(
                            system, request_id, message.in_memory_layer_id, message.tile_coords,
                            message.subscribe_to_updates);
                    },
                    [&](const messages::FeatureDataRequest& message)
                    {
                        auto request_id = RequestId{channel_id, message.request_id};

                        auto it = system->request_ids_to_trackers.find(request_id);
                        if (it != system->request_ids_to_trackers.end())
                        {
                            HRZ_LOG_WARNING(
                                "A request with ID {}-{} was already present, replacing it",
                                channel_id, message.request_id);
                            release_data_request(system, request_id);
                        }

                        request_feature_data(
                            system, request_id, message.in_memory_layer_id, message.feature_ids,
                            message.subscribe_to_updates);
                    },
                    [&](const messages::ReleaseDataRequest& message)
                    {
                        auto request_id = RequestId{channel_id, message.request_id};

                        auto it = system->request_ids_to_trackers.find(request_id);
                        if (it != system->request_ids_to_trackers.end())
                        {
                            release_data_request(system, request_id);
                        }
                        else
                        {
                            HRZ_LOG_WARNING(
                                "No request with ID {}-{}", channel_id, message.request_id);
                        }
                    }},
                generic_message);
        }
    }

    _unregister_layers(system, model);

    for (auto it : system->layer_model_to_pool)
    {
        uint64_t layer_model_handle = it.first;
        LayerPoolH layer_pool_handle = it.second;
        Layer* layer = _get_layer(system, layer_model_handle);

        hrz_proto::LayerHandle scene_model_handle;
        scene_model_handle.set_opaque(layer_model_handle);

        hrz_proto::InMemoryVectorSourceLayerPathBuilder<hrz::SceneModelAccessor> builder(
            model, scene_model_handle);

        bool restart_all_trackers = false;

        if (layer->id_updated)
        {
            uint32_t previous_id = layer->id;
            uint32_t new_id = builder.clone().id().get();

            layer->id = new_id;

            _deactivate_layer(system, layer_pool_handle, previous_id);

            auto it_active = system->active_layers.find(new_id);
            if (it_active == system->active_layers.end())
            {
                // No active layer for this id.
                // Activate the updated layer.
                system->active_layers.insert({new_id, layer_pool_handle});

                if (layer->all_features_updated || layer->features_updated)
                {
                    // Avoid generating tiles twice.
                    layer->all_features_updated = true;
                }
                else
                {
                    _generate_tiles_for_all_requests(system, layer);
                }
            }

            layer->id_updated = false;
        }

        if (layer->projection_updated)
        {
            assert(layer->all_features_updated);

            pl_Crs crs;
            bool convert_success = hrz::convert_crs(builder.clone().projection().get(), &crs);
            if (convert_success)
            {
                pl_bake_transform(&crs, &hrz_proj::wmerc, &layer->transform);
            }
            else
            {
                HRZ_LOG_WARNING(
                    "Couldn't convert raster projection string to pl_Crs. Defaulting to lat-long.");
                layer->transform = hrz_proj::lonlat_deg_to_wmerc;
            }

            layer->projection_updated = false;
        }

        if (layer->attribution_updated)
        {
            layer->attribution = attribution::register_attribution(
                attributions, {builder.clone().attribution().get(), ""});

            restart_all_trackers = true;
            layer->attribution_updated = false;
        }

        if (layer->attribute_definitions_updated)
        {
            // We need to update all features if the attribute definitions have changed.
            // This should have been set during the notify_update call, but we check it anyway.
            assert(layer->all_features_updated);

            layer->attributes.clear();

            uint32_t attribute_count = builder.clone().attributes_count();
            layer->attributes.reserve(attribute_count);

            for (uint32_t i = 0; i < attribute_count; ++i)
            {
                auto attribute = builder.clone().attributes(i).get();
                layer->attributes.push_back(
                    {attribute.id(), attribute.transform(), attribute.is_feature_id()});
            }

            layer->attribute_definitions_updated = false;
        }

        if (layer->all_features_updated)
        {
            for (auto feature_handle : layer->features)
            {
                system->feature_pool.release(feature_handle);
            }
            layer->features.clear();

            uint32_t feature_count = builder.clone().features_count();
            for (uint32_t i = 0; i < feature_count; ++i)
            {
                auto feature_proto = builder.clone().features(i).get();

                auto feature_handle = system->feature_pool.alloc();
                auto feature = system->feature_pool.get_object(feature_handle);
                feature->type = feature_proto.geometry().type();
                feature->bbox = lm::dbbox3::invalid();

                feature->attribute_values.clear();
                feature->out_of_line_data_size = 0;

                auto attribute_value_count = (size_t)feature_proto.attribute_values_size();
                for (size_t attr_i = 0;
                     attr_i < attribute_value_count && attr_i < layer->attributes.size(); ++attr_i)
                {
                    auto attr = attr_as_ref(feature_proto.attribute_values((int)attr_i));
                    feature->attribute_values.push_back(attr_transform<OwnedAttributeValue>(
                        layer->attributes[attr_i].transform, attr));
                    feature->out_of_line_data_size += get_packed_out_of_line_size(attr);
                }
                feature->attribute_values.resize(
                    layer->attributes.size(), attr_null<OwnedAttributeValue>());

                _compute_feature_id(layer, feature);

                feature->geometry_updated = false;
                feature->attribute_values_updated = false;

                auto bbox = lm::dbbox3::invalid();

                unsigned int point_count = feature_proto.geometry().coords_size();
                for (unsigned int pt_i = 0; pt_i + 2 < point_count; pt_i += 3)
                {
                    lm::dvec3 point = {
                        feature_proto.geometry().coords(pt_i + 0),
                        feature_proto.geometry().coords(pt_i + 1),
                        feature_proto.geometry().coords(pt_i + 2)};

                    _project_positions(layer, {&point, 1});

                    bbox = lm::expand(bbox, point);
                }

                feature->bbox = bbox;

                layer->features.push_back(feature_handle);
            }

            _generate_root_tile(system, layer, builder);
            _generate_tiles_for_all_requests(system, layer);

            restart_all_trackers = true;
        }
        else if (layer->features_updated)
        {
            std::vector<lm::dbbox2> updated_bboxes;
            hrz::flat_hash_set<FeatureId> updated_feature_ids;

            for (size_t i = 0; i < layer->features.size(); ++i)
            {
                auto feature_handle = layer->features.at(i);
                auto feature = system->feature_pool.get_object(feature_handle);

                if (!feature->geometry_updated && !feature->attribute_values_updated)
                {
                    continue;
                }

                auto feature_proto = builder.clone().features(i).get();

                if (feature->geometry_updated)
                {
                    // Previous bbox and id.
                    updated_bboxes.push_back(flatten_bbox(feature->bbox));

                    feature->type = feature_proto.geometry().type();

                    auto new_bbox = lm::dbbox3::invalid();

                    unsigned int coord_count = feature_proto.geometry().coords_size();

                    if (feature->type == hrz_proto::VectorGeometryType::POINT_GEOMETRY)
                    {
                        coord_count = coord_count > 3 ? 3 : coord_count;
                    }

                    for (unsigned int i = 0; i + 2 < coord_count; i += 3)
                    {
                        lm::dvec3 point = {
                            feature_proto.geometry().coords(i + 0),
                            feature_proto.geometry().coords(i + 1),
                            feature_proto.geometry().coords(i + 2)};

                        _project_positions(layer, {&point, 1});

                        new_bbox = lm::expand(new_bbox, point);
                    }

                    updated_bboxes.push_back(flatten_bbox(new_bbox));
                    updated_feature_ids.insert(feature->id);

                    feature->bbox = new_bbox;
                }

                if (feature->attribute_values_updated)
                {
                    auto default_value = attr_null<OwnedAttributeValue>();
                    feature->attribute_values.resize(layer->attributes.size(), default_value);

                    auto attribute_value_count = (size_t)feature_proto.attribute_values_size();
                    for (size_t attr_i = 0;
                         attr_i < attribute_value_count && attr_i < layer->attributes.size();
                         ++attr_i)
                    {
                        auto new_value = attr_transform<OwnedAttributeValue>(
                            layer->attributes[attr_i].transform,
                            attr_as_ref(feature_proto.attribute_values((int)attr_i)));
                        auto old_value = attr_as_ref(feature->attribute_values[attr_i]);

                        if (attr_as_ref(new_value) != old_value)
                        {
                            feature->out_of_line_data_size -=
                                get_packed_out_of_line_size(old_value);
                            feature->out_of_line_data_size +=
                                get_packed_out_of_line_size(attr_as_ref(new_value));

                            feature->attribute_values[attr_i] = std::move(new_value);
                            // @Safety old_value is not valid anymore after overwriting
                            // attribute_values[i]

                            updated_feature_ids.insert(feature->id);
                        }
                    }

                    for (size_t attr_i = attribute_value_count; attr_i < layer->attributes.size();
                         ++attr_i)
                    {
                        auto old_value = attr_as_ref(feature->attribute_values[attr_i]);
                        if (attr_as_ref(default_value) != old_value)
                        {
                            feature->out_of_line_data_size -=
                                get_packed_out_of_line_size(old_value);
                            feature->attribute_values[attr_i] = default_value;
                            // @Safety old_value is not valid anymore after overwriting
                            // attribute_values[i]
                            updated_feature_ids.insert(feature->id);
                        }
                    }

                    _compute_feature_id(layer, feature);

                    updated_bboxes.push_back(flatten_bbox(feature->bbox));
                }
            }

            for (auto feature_handle : layer->deleted_features)
            {
                auto feature = system->feature_pool.get_object(feature_handle);

                updated_bboxes.push_back(flatten_bbox(feature->bbox));
                updated_feature_ids.insert(feature->id);

                system->feature_pool.release(feature_handle);
            }

            // All other tiles are based on the root tile, so it must be
            // rebuilt any time a change happened.
            if (!updated_bboxes.empty() || !updated_feature_ids.empty())
            {
                _generate_root_tile(system, layer, builder);
            }

            // Ordered so that tiles with the lowest LODs come first.
            std::set<TileCoords, decltype(&tile_coords_ordering_lod_y_x)>
                tiles_to_regenerate_by_coords(&tile_coords_ordering_lod_y_x);

            for (const auto& it : layer->tiles_by_coords)
            {
                const auto& tile_coords = it.first;
                const auto& tile_handle = it.second;
                const auto tile = system->tile_pool.get_object(tile_handle);

                for (const auto& bbox : updated_bboxes)
                {
                    if (lm::intersect(bbox, tile->bbox))
                    {
                        tiles_to_regenerate_by_coords.insert(tile_coords);
                    }
                }
            }

            for (const auto& tile_coords : tiles_to_regenerate_by_coords)
            {
                assert(tile_coords != ROOT_TILE_COORDS);

                _generate_tile_for_coords(system, layer, tile_coords);

                for (auto& it : system->request_ids_to_trackers)
                {
                    auto& tracker = it.second;

                    if (tracker.layer_id == layer->id && tracker.has_tile_coords()
                        && tracker.tile_coords() == tile_coords)
                    {
                        _restart_tracker(tracker, ba);
                    }
                }
            }

            for (auto& it : system->request_ids_to_trackers)
            {
                auto& tracker = it.second;

                if (tracker.layer_id == layer->id && tracker.has_feature_ids())
                {
                    const auto& feature_ids = system->feature_id_lists.at(tracker.feature_ids());
                    for (const auto& updated_feature_id : updated_feature_ids)
                    {
                        if (feature_ids.contains(updated_feature_id))
                        {
                            _generate_tile_for_features(system, layer, feature_ids);
                            _restart_tracker(tracker, ba);
                        }
                    }
                }
            }
        }

        layer->all_features_updated = false;
        layer->features_updated = false;
        layer->deleted_features.clear();

        if (restart_all_trackers)
        {
            for (auto& it : system->request_ids_to_trackers)
            {
                auto& tracker = it.second;
                if (tracker.layer_id == layer->id)
                {
                    _restart_tracker(tracker, ba);
                }
            }
        }
    }

    auto push_load_error_message = [&](DataTracker& tracker)
    {
        auto it = system->channels.find(tracker.request_id.channel_id);
        if (it != system->channels.end())
        {
            auto& channel = it->second;
            channel.send(messages::Error{tracker.request_id.request_id});
        }
    };

    for (auto it = system->request_ids_to_trackers.begin();
         it != system->request_ids_to_trackers.end();)
    {
        auto& tracker = it->second;

        if (tracker.status != DataTracker::Status::GatheringData
            && tracker.status != DataTracker::Status::AllocatingBlobs)
        {
            ++it;
            continue;
        }

        auto layer_it = system->active_layers.find(tracker.layer_id);

        if (layer_it == system->active_layers.end())
        {
            ++it;
            continue;
        }

        bool erase = false;

        const auto* layer = system->layer_pool.get_object(layer_it->second);
        const Tile* tile = nullptr;

        if (tracker.has_tile_coords())
        {
            const auto& tile_coords = tracker.tile_coords();
            const auto& tile_handle = layer->tiles_by_coords.at(tile_coords);
            tile = system->tile_pool.get_object(tile_handle);
        }
        else if (tracker.has_feature_ids())
        {
            const auto& tile_handle = layer->tiles_by_feature_ids.at(tracker.feature_ids());
            tile = system->tile_pool.get_object(tile_handle);
        }
        else
        {
            assert(false && "Unhandled case");
        }

        if (tracker.status == DataTracker::Status::GatheringData)
        {
            tracker.feature_id_hash_blob = {
                BlobArrayAllocation<FeatureIdHash>::allocate(ba, tile->features.size())};
            tracker.geometry_blobs = {{
                BlobArrayAllocation<VectorTileGeometry::Feature>::allocate(
                    ba, tile->features.size()),
                BlobArrayAllocation<lm::dvec3>::allocate(ba, tile->positions.size()),
                BlobArrayAllocation<uint32_t>::allocate(ba, tile->sizes.size()),
            }};

            tracker.feature_id_hash_blob->register_blob_metadata(
                ba, "contents"_ss, "in-memory vector geometry feature IDs"_ss);
            tracker.geometry_blobs->features.register_blob_metadata(
                ba, "contents"_ss, "in-memory vector geometry features"_ss);
            tracker.geometry_blobs->points.register_blob_metadata(
                ba, "contents"_ss, "in-memory vector geometry points"_ss);
            tracker.geometry_blobs->sizes.register_blob_metadata(
                ba, "contents"_ss, "in-memory vector geometry linestring sizes"_ss);

            tracker.feature_id_hash_blob->register_blob_owner(
                ba, {monitoring::systems::InMemoryVectorData, layer->layer_model_handle});
            tracker.geometry_blobs->features.register_blob_owner(
                ba, {monitoring::systems::InMemoryVectorData, layer->layer_model_handle});
            tracker.geometry_blobs->points.register_blob_owner(
                ba, {monitoring::systems::InMemoryVectorData, layer->layer_model_handle});
            tracker.geometry_blobs->sizes.register_blob_owner(
                ba, {monitoring::systems::InMemoryVectorData, layer->layer_model_handle});

            for (size_t i = 0; i < layer->attributes.size(); ++i)
            {
                auto attribute_blobs =
                    BlobArrayAllocation<PackedAttributeValue>::allocate(ba, tile->features.size());
                attribute_blobs.register_blob_metadata(
                    ba, "contents"_ss, "in-memory attribute values"_ss);
                attribute_blobs.register_blob_owner(
                    ba, {monitoring::systems::InMemoryVectorData, layer->layer_model_handle});

                tracker.attribute_blobs.push_back(std::move(attribute_blobs));
            }

            tracker.out_of_line_attributes_data =
                BlobArrayAllocation<char>::allocate(ba, tile->out_of_line_data_size);
            tracker.out_of_line_attributes_data->register_blob_metadata(
                ba, "contents"_ss, "in-memory vector geometry out of line data"_ss);
            tracker.out_of_line_attributes_data->register_blob_owner(
                ba, {monitoring::systems::InMemoryVectorData, layer->layer_model_handle});

            tracker.status = DataTracker::Status::AllocatingBlobs;
        }

        if (tracker.status == DataTracker::Status::AllocatingBlobs)
        {
            auto allocation_state = BlobArrayAllocationState::Allocated;

            auto combine_state = [&](BlobArrayAllocationState state)
            {
                if (allocation_state == BlobArrayAllocationState::Error)
                {
                    return;
                }
                if (state == BlobArrayAllocationState::Error)
                {
                    allocation_state = BlobArrayAllocationState::Error;
                    return;
                }
                if (allocation_state == BlobArrayAllocationState::NotAllocated)
                {
                    return;
                }
                if (state == BlobArrayAllocationState::NotAllocated)
                {
                    allocation_state = BlobArrayAllocationState::NotAllocated;
                }
            };

            combine_state(tracker.feature_id_hash_blob->get_state(ba));
            combine_state(tracker.geometry_blobs->features.get_state(ba));
            combine_state(tracker.geometry_blobs->points.get_state(ba));
            combine_state(tracker.geometry_blobs->sizes.get_state(ba));
            for (const auto& attribute : tracker.attribute_blobs)
            {
                combine_state(attribute.get_state(ba));
            }
            combine_state(tracker.out_of_line_attributes_data->get_state(ba));

            if (allocation_state == BlobArrayAllocationState::Error)
            {
                _clear_tracker_data(tracker, ba);
                tracker.status = DataTracker::Status::Error;
                push_load_error_message(tracker);
                erase = true;
            }
            else if (allocation_state != BlobArrayAllocationState::Allocated)
            {
                ++it;
                continue;
            }

            // We now have all the data and the blob arrays to store it.
            //
            // All the returned data, including geometry, attribute values,
            // and feature IDs, must conform to the same order. That order
            // comes from either the model (in the case of a request by tile
            // coords) or the provided feature ID list (in the case of a
            // request by feature IDs).
            //
            // The loops below are not the most optimized thing there is,
            // but this system isn't supposed to work with millions of features
            // anyway.

            if (tracker.has_tile_coords())
            {
                tracker.tile_data.coords = tracker.tile_coords();
            }
            else if (tracker.has_feature_ids())
            {
                // There are no actual tile coords in this case.
                tracker.tile_data.coords = {
                    std::numeric_limits<uint32_t>::max(), std::numeric_limits<uint32_t>::max(),
                    std::numeric_limits<uint8_t>::max()};
            }
            else
            {
                assert(false && "Unhandled case");
            }

            tracker.tile_data.geometry.bounds = tile->data_bbox;

            auto feature_id_hashes = tracker.feature_id_hash_blob->to_array(ba);
            tracker.feature_id_hash_blob = std::nullopt;

            tracker.tile_data.geometry.features = tracker.geometry_blobs->features.to_array(ba);
            tracker.tile_data.geometry.points = tracker.geometry_blobs->points.to_array(ba);
            tracker.tile_data.geometry.linestring_sizes =
                tracker.geometry_blobs->sizes.to_array(ba);

            // Empty tiles and tiles that are small enough that numerical imprecision is
            // negligible can be considered to contain full detail.
            tracker.tile_data.geometry.has_full_detail =
                tile->features.empty() || tracker.tile_data.coords.lod >= 6;

            tracker.geometry_blobs = std::nullopt;
            tracker.attribution = layer->attribution;

            {
                auto feature_id_hashes_data = feature_id_hashes.get_mutable_data();
                auto features_data = tracker.tile_data.geometry.features.get_mutable_data();
                for (size_t i = 0; i < tile->features.size(); ++i)
                {
                    const auto& feature = tile->features.at(i);

                    hrz::vector_data::VectorTileGeometry::Feature tracker_feature;
                    tracker_feature.first_point = feature.first_point;
                    tracker_feature.point_count = feature.point_count;
                    tracker_feature.first_linestring_size = feature.first_linestring_size;
                    tracker_feature.linestring_count = feature.linestring_count;
                    tracker_feature.type = feature.type;
                    tracker_feature.anchor = feature.anchor;
                    tracker_feature.anchor_angle = feature.anchor_angle;

                    feature_id_hashes_data.at(i) = feature.id.hash();
                    features_data.at(i) = tracker_feature;
                }

                // @Safety the lifetime of the temporary blob array data object is extended until
                // the end of the copy call.
                std::ranges::copy(
                    tile->positions,
                    tracker.tile_data.geometry.points.get_mutable_data().unsafe_data());

                // @Safety the lifetime of the temporary blob array data object is extended until
                // the end of the copy call.
                std::ranges::copy(
                    tile->sizes,
                    tracker.tile_data.geometry.linestring_sizes.get_mutable_data().unsafe_data());
            }

            auto out_of_line_data_blob = tracker.out_of_line_attributes_data->to_array(ba);
            std::vector<BlobArray<PackedAttributeValue>> attribute_blobs;

            // We scope this so that the out of line data blob mutable handle is released before
            // copying it into each attribute.
            {
                auto out_of_line_data = out_of_line_data_blob.get_mutable_data();
                tracker.out_of_line_attributes_data = std::nullopt;

                CharSpanWriter out_of_line_writer(out_of_line_data.as_span());

                assert(tracker.attribute_blobs.size() == layer->attributes.size());
                for (size_t i = 0; i < layer->attributes.size(); ++i)
                {
                    auto values_blob = tracker.attribute_blobs.at(i).to_array(ba);

                    // Scope to release the mutable handle to the values blob.
                    {
                        auto values_data = values_blob.get_mutable_data();
                        size_t value_count = 0;

                        for (const auto& tile_feature : tile->features)
                        {
                            if (tile_feature.handle != Tile::EMPTY_FEATURE)
                            {
                                const auto feature =
                                    system->feature_pool.get_object(tile_feature.handle);

                                auto value = attr_from<
                                    PackedAttributeValue,
                                    PackedAttributeValueTraits<CharSpanWriter>>(
                                    attr_as_ref(feature->attribute_values.at(i)),
                                    out_of_line_writer);

                                values_data.at(value_count++) = value;
                            }
                            else
                            {
                                values_data.at(value_count++) = attr_null<PackedAttributeValue>();
                            }
                        }
                    }

                    attribute_blobs.push_back(values_blob);
                }
            }

            // Now that we released the mutable handle to the out of line data blob, we can copy it
            // into each attribute.
            assert(attribute_blobs.size() == layer->attributes.size());
            for (size_t i = 0; i < layer->attributes.size(); ++i)
            {
                tracker.tile_data.attributes.push_back({
                    layer->attributes.at(i).id,
                    attribute_blobs.at(i),
                    out_of_line_data_blob,
                });
            }

            bool feature_id_error = false;
            {
                hrz::InlinedVector<AttributeValues, 2> feature_id_values;
                for (size_t i = 0; i < layer->attributes.size(); ++i)
                {
                    const auto& attribute = layer->attributes.at(i);
                    if (attribute.is_feature_id)
                    {
                        feature_id_values.push_back(tracker.tile_data.attributes.at(i));
                    }
                }

                auto feature_ids =
                    FeatureIds::make(feature_id_values, std::move(feature_id_hashes));
                if (feature_ids.has_value())
                {
                    tracker.tile_data.feature_ids = std::move(feature_ids.value());
                }
                else
                {
                    feature_id_error = true;
                }
            }

            tracker.attribute_blobs.clear();

            tracker.status =
                feature_id_error ? DataTracker::Status::Error : DataTracker::Status::Loaded;

            if (tracker.status == DataTracker::Status::Loaded)
            {
                auto channel_it = system->channels.find(tracker.request_id.channel_id);
                if (channel_it != system->channels.end())
                {
                    auto& channel = channel_it->second;
                    channel.send(messages::VectorData{
                        tracker.request_id.request_id, tracker.tile_data, tracker.attribution});
                }

                if (tracker.one_shot)
                {
                    erase = true;
                }
            }
            else
            {
                assert(tracker.status == DataTracker::Status::Error);
                push_load_error_message(tracker);
                erase = true;
            }
        }

        if (erase)
        {
            system->request_ids_to_trackers.erase(it++);
        }
        else
        {
            ++it;
        }
    }
}

InMemoryChannel create_channel(InMemoryVectorDataBase* system)
{
    assert(system);

    return system->channels.create_channel().second;
}
} // namespace vector_data::in_memory
} // namespace hrz
