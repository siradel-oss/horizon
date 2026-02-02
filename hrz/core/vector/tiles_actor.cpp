#include "hrz/core/vector/tiles_actor.h"

#include "hrz/common/blob_allocator.h"
#include "hrz/common/blob_vector.h"
#include "hrz/common/profiling.h"
#include "hrz/common/proj.h"
#include "hrz/common/vector_tiles/picking.h"
#include "hrz/core/actor.h"
#include "hrz/core/actor_runner.h"
#include "hrz/core/clock.h"
#include "hrz/core/job_scheduler.h"
#include "hrz/core/jobs/jobs_tickets.h"
#include "hrz/core/jobs/styling.h"
#include "hrz/core/planet/elevation_query.h"
#include "hrz/core/planet/surface.h"
#include "hrz/core/vector/data_loader/data_loader.h"
#include "hrz/core/vector/repr.h"
#include "hrz/fnd/flat_hash_map.h"
#include "hrz/fnd/flat_hash_set.h"
#include "hrz/fnd/gen_index_pool.h"
#include "hrz/fnd/gen_object_pool.h"
#include "hrz/fnd/hash.h"
#include "hrz/fnd/mem.h"
#include "hrz/fnd/meta.h"
#include "hrz/fnd/time.h"

#include <array>
#include <cassert>

namespace hrz::vt
{
// The delay between the first time a tile's content was needed and the time
// it's actually loaded. This prevents loading tiles that are only requested for
// a very short amount of time, like when the camera is travelling.
constexpr double CONTENT_LOAD_DELAY_MS = 500.0;

// The delay between the last time a tile's content was needed and the time it's pruned.
constexpr double CONTENT_PRUNE_DELAY_MS = 2500.0;

// The delay between queries made by a tile to the planet system until its elevation has
// been retrieved from a DTM tile.
constexpr double TILE_ELEVATION_QUERY_DELAY_MS = 500.0;

constexpr double MAX_SCREEN_SPACE_ERROR = 1.33;
constexpr size_t MAX_TILE_NODE_COUNT = 10001;

constexpr double VISIBLITY_SET_RECOMPUTE_DELAY_MS = 150;

struct VisibilitySetBuilder
{
    struct State
    {
        bool is_valid;
        size_t repr_count;
        size_t tile_count;
    };

    uint64_t id;
    hrz::BlobVector<VisibilitySet::Repr> reprs;
    hrz::BlobVector<VisibilitySet::Tile> tiles;
    bool can_be_scheduled_immediately;
    bool include_debug_info;

    VisibilitySetBuilder(
        uint64_t id,
        bool include_debug_info,
        uint64_t layer_id,
        BlobAllocator* ba) :
        id(id),
        reprs({ba, 400}),
        tiles({ba, 200}),
        can_be_scheduled_immediately(true),
        include_debug_info(include_debug_info)
    {
        reprs.register_blob_owner({monitoring::systems::VectorTiles, layer_id});
        reprs.register_blob_metadata("contents", "visibility set reprs");
        tiles.register_blob_owner({monitoring::systems::VectorTiles, layer_id});
        tiles.register_blob_metadata("contents", "visibility set tiles");
    }

    void reset()
    {
        can_be_scheduled_immediately = true;
        tiles.clear();
    }

    // This retrieves the current stats of the visibility set.
    // This can be used to roll back any changes made to the visibility set.
    State current_state() const
    {
        State state{false, 0, 0};

        if (!reprs.is_valid() || !tiles.is_valid())
        {
            return state;
        }

        state.is_valid = true;
        state.repr_count = reprs.size().value();
        state.tile_count = tiles.size().value();
        return state;
    }

    // Erase the views from the starting state to the end of the visibility set.
    // If a tile now has an empty bitset, swap it with the end to delete it.
    void rollback(const State starting_state, SceneViewBitset in_views)
    {
        if (!starting_state.is_valid || !reprs.is_valid() || !tiles.is_valid())
        {
            return;
        }

        size_t cursor_it = starting_state.repr_count;
        size_t end_it = reprs.size().value();

        while (cursor_it < end_it)
        {
            auto repr = reprs.at(cursor_it);
            repr->in_views.reset(in_views);
            if (repr->in_views.none())
            {
                end_it -= 1;
                std::swap(*repr, *reprs.at(end_it));
            }
            else
            {
                cursor_it += 1;
            }
        }

        reprs.resize(end_it);

        tiles.resize(end_it);
    }

    void add_repr(hrz_proto::VectorReprType type, uint64_t repr_id, SceneViewBitset in_views)
    {
        reprs.push_back({type, repr_id, in_views});
    }

    void add_tile(
        SceneViewBitset in_views,
        std::array<AttributionHandle, 2> attribution,
        const std::optional<VisibilitySet::Tile::DebugInfo>& debug_info)
    {
        assert(include_debug_info == debug_info.has_value());
        tiles.push_back(VisibilitySet::Tile{in_views, attribution, debug_info});
    }

    std::optional<VisibilitySet> build() &&
    {
        auto reprs_array = reprs.to_blob_array();
        auto tiles_array = tiles.to_blob_array();

        if (!reprs_array.has_value() || !tiles_array.has_value())
        {
            return std::nullopt;
        }

        VisibilitySet visibility_set;
        visibility_set.id = id;
        visibility_set.reprs = std::move(reprs_array.value());
        visibility_set.tiles = std::move(tiles_array.value());
        visibility_set.can_be_scheduled_immediately = can_be_scheduled_immediately;

        return {visibility_set};
    }
};

struct Elevation
{
    double min = 0.0;
    double max = 0.0;
    ElevationSource source = ElevationSource::None;

    bool update_from_child(const Elevation& other)
    {
        if (source == ElevationSource::GroundTruth || other.source == ElevationSource::None
            || other.source == ElevationSource::InferredFromParent)
        {
            return false;
        }

        if (source == ElevationSource::InferredFromChild)
        {
            min = std::min(min, other.min);
            max = std::max(max, other.max);
            return true;
        }

        min = other.min;
        max = other.max;
        source = ElevationSource::InferredFromChild;
        return true;
    }

    bool update_from_parent(const Elevation& other)
    {
        if (source == ElevationSource::GroundTruth || other.source == ElevationSource::None)
        {
            return false;
        }

        if (source == ElevationSource::InferredFromChild)
        {
            min = std::min(min, other.min);
            max = std::max(max, other.max);
            return true;
        }

        min = other.min;
        max = other.max;
        source = ElevationSource::InferredFromParent;
        return true;
    }

    void clear()
    {
        min = 0.0;
        max = 0.0;
        source = ElevationSource::InferredFromParent;
    }
};

/**
 * Content of a tile.
 */
struct TileContent
{
    enum class LoadStatus
    {
        Loading,
        Baking,
        Ready,
        Error,
    };

    enum class DisplayStatus
    {
        Loading,
        Displayable,
        Error,
    };

    struct FeatureIds
    {
        enum class Status
        {
            Loading,
            Ready,
            Error,
        };

        Status status;
        uint64_t data_request_ticket = 0;
        hrz::vector_data::FeatureIds feature_ids;
    };

    struct Geometry
    {
        enum class Status
        {
            Loading,
            Clamping,
            Ready,
            Error,
        };

        Status status;
        uint64_t data_request_ticket = 0;
        hrz::vt::ReprGeometry repr;
        AttributionHandle attribution;
    };

    struct Attributes
    {
        enum class Status
        {
            Loading,
            AllocatingSpecialAttributes,
            GeneratingSpecialAttributes,
            Ready,
            Error,
        };

        Status status;
        uint64_t data_request_ticket = 0;
        // All attributes values must be in the same order as in fids.
        // When a value is not present, we just put 0.
        // This is done to ease passing those values to the styling job.
        hrz::InlinedVector<hrz::vector_data::AttributeValues, 16> attributes;
        AttributionHandle attribution;

        std::optional<hrz::BlobArrayAllocation<hrz::vector_data::PackedAttributeValue>>
            anchor_z_attribute_allocation;
        hrz::BlobArray<hrz::vector_data::PackedAttributeValue> anchor_z_attribute_values;
        std::optional<hrz::BlobArrayAllocation<hrz::vector_data::PackedAttributeValue>>
            anchor_angle_attribute_allocation;
        hrz::BlobArray<hrz::vector_data::PackedAttributeValue> anchor_angle_attribute_values;
        std::optional<hrz::BlobArrayAllocation<hrz::vector_data::PackedAttributeValue>>
            feature_type_attribute_allocation;
        hrz::BlobArray<hrz::vector_data::PackedAttributeValue> feature_type_attribute_values;
    };

    struct StyleJob
    {
        enum class Status
        {
            Idle,
            Styling,
            Ready,
            Error,
        };

        Status status;
        hrz_jobs::StyleFeaturesTicket ticket;
        hrz::flat_hash_set<uint32_t> for_reprs;

        hrz::style::StyledFeatures result_repr;
        hrz::flat_hash_set<uint32_t> result_unique_reprs;
    };

    TileCoords coords;

    LoadStatus load_status;
    DisplayStatus display_status;

    FeatureIds feature_ids;
    Geometry geometry;
    Attributes attributes;

    hrz::flat_hash_set<uint32_t> reprs_to_style;
    StyleJob style_job;

    struct ReprSlot
    {
        struct Repr
        {
            hrz_proto::VectorReprType type;
            uint64_t id;
        };

        struct DisplayedRepr
        {
            Repr repr;
            std::optional<uint64_t> last_used_in_visibility_set;
        };

        // To avoid too much flickering when the tile's data is updated, displayed
        // and baking representations are stored concurrently, as long as they
        // correspond to the same representations in the model.

        // Newest, currently baking
        std::optional<Repr> baking;

        // Representation currently being displayed.
        std::optional<DisplayedRepr> displayed;
    };

    // This maps the index of the repr in _reprs_configs to the tile handle
    // after the tile has been added to the representation.
    // It contains optionals because not all tiles have features with every
    // representation.
    std::vector<ReprSlot> reprs;

    uint32_t next_repr_id = 1;

    constexpr bool should_start_new_style_job() const
    {
        return style_job.status == TileContent::StyleJob::Status::Idle && !reprs_to_style.empty();
    }

    void reset_style_job()
    {
        style_job.for_reprs.clear();
        style_job.result_repr = {};
        style_job.result_unique_reprs.clear();
        style_job.status = StyleJob::Status::Idle;
    }

    void cancel_styling(std::vector<hrz_jobs::StyleFeaturesTicket>& to_cancel, uint32_t repr_id)
    {
        reprs_to_style.erase(repr_id);
        style_job.for_reprs.erase(repr_id);

        if (style_job.status == StyleJob::Status::Styling && style_job.for_reprs.empty())
        {
            to_cancel.push_back(style_job.ticket);
        }

        style_job.status = StyleJob::Status::Idle;
        load_status = LoadStatus::Loading;
    }

    void reschedule_styling(
        std::vector<hrz_jobs::StyleFeaturesTicket>& to_cancel,
        uint32_t old_repr_id,
        uint32_t new_repr_id)
    {
        cancel_styling(to_cancel, old_repr_id);
        reprs_to_style.insert(new_repr_id);
        load_status = LoadStatus::Loading;
    }

    void schedule_styling(std::vector<hrz_jobs::StyleFeaturesTicket>& to_cancel, uint32_t repr_id)
    {
        cancel_styling(to_cancel, repr_id);
        reprs_to_style.insert(repr_id);
        load_status = LoadStatus::Loading;
    }
};

uint64_t make_data_request_id(TileId id, vector_data::DataKind data_kind)
{
    return (uint64_t)id | ((uint64_t)data_kind << 32);
}

struct TileIdAndDataKind
{
    TileId tile_id;
    vector_data::DataKind data_kind;
};

TileIdAndDataKind extract_tile_id_from_request_id(uint64_t id)
{
    return {(uint32_t)(id & 0xFFFFFFFF), (vector_data::DataKind)(id >> 32)};
}

uint64_t make_repr_id(TileId id, uint32_t repr_id)
{
    return (uint64_t)id | ((uint64_t)repr_id << 32);
}

TileId extract_tile_id_from_repr_id(uint64_t id)
{
    return (uint32_t)(id & 0xFFFFFFFF);
}

/**
 * Quadtree based node.
 */
struct TileNode
{
    enum class DrawState
    {
        None,
        This,
        Children,
    };

    TileNode(hrz::TileCoords tile_coords, const Elevation& parent_elevation) :
        coords(tile_coords), elevation(parent_elevation)
    {
        bounds = GeoVolumeBounds(mercator_tile_bounds(coords), 0.0, 0.0);

        if (elevation.source != ElevationSource::None)
        {
            elevation.source = ElevationSource::InferredFromParent;
        }

        update_bounds();

        double center_to_z_axis_length =
            hrz::geo_to_ecef(GeoPosition2((bounds.south + bounds.north) * 0.5, 0)).x;
        width = (center_to_z_axis_length * 2.0 * lm::PI) / (double)((uint64_t)1 << coords.lod);

        last_time_content_not_needed_ms = 0;
        last_time_content_needed_ms = 0;
        last_time_children_visited_ms = 0;
        last_time_elevation_queried_ms = 0;
    }

    hrz::TileCoords coords;
    GeoVolumeBounds bounds;
    double width;

    Elevation elevation;
    // This is the sphere used for culling.
    hrz::BSphere<double> bsphere_ecef;
    // This is the point used for the horizon culling.
    // Another point is used when the content of the tile is loaded (in the `Tile` structure)
    // for a more accurate check that accounts for representation geometry.
    std::optional<lm::dvec3> horizon_occlusion_point;

    std::optional<std::array<TileId, 4>> children = std::nullopt;

    double last_time_content_not_needed_ms;
    double last_time_content_needed_ms;
    double last_time_children_visited_ms;
    double last_time_elevation_queried_ms;

    std::optional<TileContent> content;

    std::optional<uint64_t> last_used_in_visibility_set = std::nullopt;

    void update_bounds()
    {
        // Leave 200 metres of vertical margin for representations. This means that tiles
        // with representations that occupy more vertical space can be culled too early during
        // the traversal of the node tree.
        //
        // @Todo(1264) This could be improved by having each representation return the maximum
        // height of baked tiles, and using the highest amount all representations as a margin.
        static constexpr double max_elevation_margin = 200.0;

        bounds.min_height = elevation.min;
        bounds.max_height = elevation.max + max_elevation_margin;

        lm::dvec3 points[18];
        points[0] = {bounds.west, bounds.south, elevation.min};
        points[1] = {bounds.east, bounds.south, elevation.min};
        points[2] = {bounds.west, bounds.north, elevation.min};
        points[3] = {bounds.east, bounds.north, elevation.min};
        points[4] = (points[0] + points[1]) * 0.5;
        points[5] = (points[2] + points[3]) * 0.5;
        points[6] = (points[0] + points[2]) * 0.5;
        points[7] = (points[1] + points[3]) * 0.5;
        points[8] = (points[0] + points[3]) * 0.5;

        for (size_t i = 0; i < 9; ++i)
        {
            points[i + 9] =
                points[i] + lm::dvec3(0, 0, elevation.max - elevation.min + max_elevation_margin);
        }

        pl_transform_in_place_canonical(
            &hrz_proj::lonlat_rad_to_ecef, HRZ_ARRAY_COUNT(points), &points[0].x);

        bsphere_ecef = hrz::compute_bounding_sphere(std::span<const lm::dvec3>(points));

        horizon_occlusion_point = hrz::horizon_culling::compute_occlusion_point(bounds);
    }
};

struct VectorTilesActor : public Actor
{
    uint64_t _global_layer_id{};
    uint32_t _vector_data_layer{};
    hrz_proto::MissingTilePolicy _missing_tile_policy;
    bool _static_tiles;
    uint8_t _layer_min_lod{};
    uint8_t _layer_max_lod{};
    uint8_t _data_min_lod{};
    uint8_t _data_max_lod{};
    GeoBounds _layer_bounds = GeoBounds::full();
    GeoBounds _data_bounds = GeoBounds::full();
    float _max_screen_space_error{};
    uint32_t _object_reference_layer_id_partial{};
    hrz_proto::VectorClamping _clamping;
    SceneViewBitset _visible_in;

    // There is never more than MAX_TILE_NODE_COUNT + 4 nodes alive
    // at a time, so using 20 bits for the index is enough.
    // Because some bits are needed to identify the layer for picking,
    // tile IDs must not exceed 24 bits.
    using TileIdPool = hrz::GenIndexPool<TileId, 4, 20>;
    using TileNodePool = hrz::GenObjectPool<TileNode, TileIdPool>;
    TileNodePool _tile_node_pool;
    size_t _tile_node_count{};

    bool _any_tile_content_waiting_load_delay{};
    bool _any_node_lacking_elevation{};

    TileId _root_tile_id;

    // This map only contains entries for tiles that have content,
    // as otherwise there is no need for getting a node by coords.
    hrz::flat_hash_map<hrz::TileCoords, TileId> _tiles_by_coords;
    hrz::flat_hash_set<TileId> _loading_tiles;
    std::vector<TileId> _nodes_to_start_loading;

    struct StylingAttributeRef
    {
        std::string styling_name;
        uint32_t vector_data_id;
        int style_id;
    };

    struct StylingReprRef
    {
        hrz_proto::VectorReprType type;
        std::optional<uint64_t> config_id;
        std::string name;
        uint32_t id;
        bool registered;
    };

    std::vector<hrz::Palette> _palettes;
    std::vector<StylingAttributeRef> _attributes;
    hrz::flat_hash_map<uint32_t, int> _attribute_id_to_style;
    hrz::flat_hash_map<uint64_t, hrz::vector_data::OwnedAttributeValue> prp_id_to_default_value;

    std::optional<std::string> _anchor_z_attribute_name;
    std::optional<std::string> _anchor_angle_attribute_name;
    std::optional<std::string> _feature_type_attribute_name;
    std::optional<int> _anchor_z_style_attribute_id;
    std::optional<int> _anchor_angle_style_attribute_id;
    std::optional<int> _feature_type_style_attribute_id;

    std::vector<StylingReprRef> _reprs_configs;

    enum class ModelStatus
    {
        Unloaded,
        Loading,
        Loaded,
        Error,
    };

    ModelStatus _model_status = ModelStatus::Unloaded;

    std::string _script;
    bool _needs_to_reload_attributes = true;
    hrz::flat_hash_set<uint32_t> _attribute_ids;

    enum class ScriptCompilationStatus
    {
        MustRecompile,
        WaitingForStyles,
        WaitingForProperties,
        PropertiesRegistered,
        Compiled,
    };

    ScriptCompilationStatus _script_compilation_status{ScriptCompilationStatus::MustRecompile};
    std::unique_ptr<style::Parser> _parser;
    std::shared_ptr<hrz::style::FlatAst> _ast = std::make_shared<hrz::style::FlatAst>();
    bool _script_is_valid = false;

    std::vector<hrz_jobs::StyleFeaturesTicket> _styling_jobs_to_cancel;

    uint32_t _tile_z_uniform_id = -1;

    bool _selected_features_changed{false};
    hrz::flat_hash_set<vector_data::FeatureIdHash> _selected_features;

    bool _appearance_changed = false;
    int8_t _clip_id = -1;
    render::LightingSettings _lighting_settings;
    uint64_t _rng_seed = 0;

    bool _planet_elevation_version_has_changed = false;
    uint64_t _planet_elevation_version = 0;

    bool _has_warned_about_too_many_tiles = false;

    std::optional<hrz::StaticVector<VectorTilesCuller, SCENE_VIEW_COUNT>> _cullers;

    bool _should_schedule_flat_overlays = false;

    double _last_visibility_set_creation_time_ms{0};
    uint64_t _last_destroyed_visibility_set_id{0};
    uint64_t _next_visibility_set_id{1};

    bool _generate_new_visibility_set = true;
    bool _include_debug_info_in_visibility_set = false;

    hrz::flat_hash_set<hrz_proto::VectorReprType> _reprs_not_scheduling_instantly;
    hrz::flat_hash_set<hrz_proto::VectorReprType> _reprs_using_z_coordinates;

    // Because the creation of visibility sets and their usage are separated by
    // an asynchronous communication boundary, the representations that are not
    // needed any longer on this side could still be used on the other side for
    // display.
    // The ID of the most recently discarded visibility set is communicated back
    // here (and stored in `_last_destroyed_visibility_set_id`).
    // When a representation has last been used in this visibility set, or an
    // earlier one, it can be safely destroyed.
    std::deque<TileContent::ReprSlot::DisplayedRepr> _reprs_in_queued_visibility_sets;

    planet::SurfaceChannel _planet_channel;
    planet::ElevationQuery::Channel _elevation_query_channel;

    vector_data::VectorDataLoaderChannel _vector_data_channel;
    uint64_t _next_data_request_id = 1;

    hrz::flat_hash_map<hrz_proto::VectorReprType, ReprSystem::Channel> _repr_channels;
    uint64_t _next_style_id = 1;

    Channel<FromActorMessage, ToActorMessage> _channel;

    VectorTilesActor(
        uint64_t layer_id,
        uint32_t vector_data_layer,
        hrz_proto::MissingTilePolicy missing_tile_policy,
        bool static_tiles,
        uint32_t object_reference_layer_id_partial,
        hrz::Channel<FromActorMessage, ToActorMessage> channel,
        PlanetSurface* planet,
        VectorDataLoader* vdl) :
        _global_layer_id(layer_id),
        _vector_data_layer(vector_data_layer),
        _missing_tile_policy(missing_tile_policy),
        _static_tiles(static_tiles),
        _object_reference_layer_id_partial(object_reference_layer_id_partial),
        _planet_channel(planet::create_surface_channel(planet)),
        _elevation_query_channel(planet::get_elevation_query(planet)->create_channel()),
        _vector_data_channel(vector_data::create_channel(vdl)),
        _channel(std::move(channel))
    {
        _root_tile_id = _tile_node_pool.alloc(TileNode({0, 0, 0}, {0, 0.0, ElevationSource::None}));
        _tile_node_count = 1;
        _any_node_lacking_elevation = true;

        _planet_channel.send(planet::surface::messages::SubscribeToTerrainVersionUpdates{0});
    }

    enum DestroyRepr
    {
        DestroyRepr_Baking = 1,
        DestroyRepr_Displayed = 2,

        DestroyRepr_All = DestroyRepr_Baking | DestroyRepr_Displayed,
    };

    friend constexpr DestroyRepr operator|(DestroyRepr a, DestroyRepr b)
    {
        return (DestroyRepr)((int)a | (int)b);
    }

    // If destroy_now is true, the representation gets destroyed even if it is still
    // in use in a visibility set.
    void destroy_repr(TileContent::ReprSlot& repr, DestroyRepr what, bool destroy_now = false)
    {
        auto send_remove_message = [this](const TileContent::ReprSlot::Repr& repr)
        { _repr_channels[repr.type].send(repr::messages::RemoveTile{repr.id}); };

        if ((what & DestroyRepr_Displayed) && repr.displayed.has_value())
        {
            const auto& current_repr = repr.displayed.value();

            if (!destroy_now && repr.displayed->last_used_in_visibility_set.has_value()
                && repr.displayed->last_used_in_visibility_set.value()
                    > _last_destroyed_visibility_set_id)
            {
                _reprs_in_queued_visibility_sets.push_back(repr.displayed.value());
            }
            else
            {
                send_remove_message(current_repr.repr);
            }

            repr.displayed = std::nullopt;
        }

        if ((what & DestroyRepr_Baking) && repr.baking.has_value())
        {
            const auto& baking_repr = repr.baking.value();
            send_remove_message(baking_repr);
            repr.baking = std::nullopt;
        }
    }

    void destroy_reprs(TileContent& tile_content, DestroyRepr what, bool destroy_now = false)
    {
        for (auto& repr : tile_content.reprs)
        {
            destroy_repr(repr, what, destroy_now);
        }
    }

    void clean_tree(BlobAllocator* ba, JobScheduler* js)
    {
        TileNode* root_node = _tile_node_pool.get_object(_root_tile_id);
        assert(root_node);
        clean_tile_node(_root_tile_id, *root_node, ba, js, true, true, true);
        assert(_tile_node_count == 1);
        assert(_tiles_by_coords.empty());
        _nodes_to_start_loading.clear();
        _loading_tiles.clear();

        for (const auto& it : _reprs_in_queued_visibility_sets)
        {
            const auto& repr = it.repr;
            _repr_channels[repr.type].send(repr::messages::RemoveTile{repr.id});
        }
        _reprs_in_queued_visibility_sets.clear();

        _generate_new_visibility_set = true;
    }

    ActorStatus work_async(BlobAllocator* ba, JobScheduler* js) override
    {
        HRZ_SCOPED_SAMPLE("vector tiles actor work async");

        RenderRequest render_request;

        for (auto& generic_message : _channel.receive())
        {
            std::visit(
                hrz::overload{
                    [&](const to_actor::SetBounds& message)
                    {
                        _layer_bounds = message.bounds.value_or(GeoBounds::full());
                        _layer_min_lod = message.min_lod.value_or(0);
                        _layer_max_lod = message.max_lod.value_or(32);
                    },
                    [&](const to_actor::SetClamping& message) { _clamping = message.clamping; },
                    [&](const to_actor::SetAttributes& message)
                    {
                        _attributes.clear();
                        for (auto& attrib : message.attributes)
                        {
                            StylingAttributeRef attrib_inner;
                            attrib_inner.vector_data_id = attrib.vector_data_attr_id();
                            attrib_inner.styling_name = attrib.styling_name();
                            _attributes.push_back(attrib_inner);
                        }

                        _needs_to_reload_attributes = true;
                        _script_compilation_status = ScriptCompilationStatus::MustRecompile;
                    },
                    [&](const to_actor::SetMaxScreenSpaceError& message)
                    { _max_screen_space_error = message.max_screen_space_error; },
                    [&](const to_actor::SetPalettes& message)
                    {
                        _palettes.clear();
                        for (auto& palette : message.palettes)
                        {
                            _palettes.push_back(hrz::palette::from_proto(palette));
                        }
                        _script_compilation_status = ScriptCompilationStatus::MustRecompile;
                    },
                    [&](const to_actor::SetStyleScript& message)
                    {
                        _script = message.script;
                        _script_compilation_status = ScriptCompilationStatus::MustRecompile;
                    },
                    [&](const to_actor::SetRepresentations& message)
                    {
                        auto& new_reprs = message.reprs;

                        std::vector<uint32_t> old_reprs_ids;
                        for (const auto& repr : _reprs_configs)
                        {
                            old_reprs_ids.push_back(repr.id);
                        }

                        unregister_all_representations();

                        prp_id_to_default_value.clear();

                        for (auto& new_repr : new_reprs)
                        {
                            auto type = new_repr.type();

                            StylingReprRef ref;
                            ref.type = type;
                            ref.config_id = {_next_style_id++};
                            ref.id = new_repr.id();
                            ref.name = new_repr.name();
                            ref.registered = false;
                            _reprs_configs.push_back(ref);

                            assert(_repr_channels.contains(type));
                            _repr_channels[type].send(repr::messages::RegisterStyle{
                                ref.config_id.value(), new_repr, _global_layer_id});
                        }

                        assert(_reprs_configs.size() == new_reprs.size());

                        for (const auto& it : _tiles_by_coords)
                        {
                            TileId tile_id = it.second;
                            auto* node = _tile_node_pool.get_object(tile_id);
                            if (!node->content.has_value()) continue;

                            auto& content = node->content.value();

                            if (content.geometry.repr.clamping.method()
                                == hrz_proto::VectorClampMode::NO_CLAMPING)
                            {
                                query_elevations_for_clamping(tile_id, content.geometry);
                            }

                            destroy_reprs(content, DestroyRepr_All);
                            content.reprs.clear();
                            content.reprs.resize(new_reprs.size());

                            for (uint32_t old_repr_id : old_reprs_ids)
                            {
                                content.cancel_styling(_styling_jobs_to_cancel, old_repr_id);
                            }
                            assert(content.style_job.status == TileContent::StyleJob::Status::Idle);

                            for (const auto& repr : _reprs_configs)
                            {
                                content.schedule_styling(_styling_jobs_to_cancel, repr.id);
                            }

                            _loading_tiles.insert(tile_id);
                        }
                    },
                    [&](const to_actor::AddRepresentation& message)
                    {
                        auto& new_repr = message.repr;
                        auto type = new_repr.type();
                        auto id = new_repr.id();
                        const auto& name = new_repr.name();

                        StylingReprRef ref;
                        ref.type = type;
                        ref.config_id = {_next_style_id++};
                        ref.id = id;
                        ref.name = name;
                        ref.registered = false;
                        _reprs_configs.push_back(ref);

                        assert(_repr_channels.contains(type));
                        _repr_channels[type].send(repr::messages::RegisterStyle{
                            ref.config_id.value(), new_repr, _global_layer_id});

                        for (const auto& it : _tiles_by_coords)
                        {
                            TileId tile_id = it.second;
                            auto* node = _tile_node_pool.get_object(tile_id);
                            if (!node->content.has_value()) continue;

                            auto& content = node->content.value();

                            if (content.geometry.repr.clamping.method()
                                == hrz_proto::VectorClampMode::NO_CLAMPING)
                            {
                                query_elevations_for_clamping(tile_id, content.geometry);
                            }

                            content.reprs.emplace_back();
                            content.schedule_styling(_styling_jobs_to_cancel, id);

                            _loading_tiles.insert(tile_id);
                        }
                    },
                    [&](const to_actor::UpdateRepresentation& message)
                    {
                        auto& index = message.index;
                        auto& repr = message.repr;

                        assert(index < _reprs_configs.size());

                        uint32_t old_repr_id;
                        uint32_t new_repr_id;

                        {
                            auto& repr_config = _reprs_configs[index];
                            if (repr_config.config_id.has_value())
                            {
                                _repr_channels[repr_config.type].send(
                                    repr::messages::UnregisterStyle{
                                        repr_config.config_id.value(), _global_layer_id});
                            }

                            auto type = repr.type();
                            auto id = repr.id();
                            const auto& name = repr.name();

                            old_repr_id = repr_config.id;
                            new_repr_id = id;

                            repr_config.name = name;
                            repr_config.id = id;
                            repr_config.type = type;
                            repr_config.config_id = {_next_style_id++};
                            repr_config.registered = false;

                            assert(_repr_channels.contains(type));
                            _repr_channels[type].send(repr::messages::RegisterStyle{
                                repr_config.config_id.value(), repr, _global_layer_id});

                            _script_compilation_status = ScriptCompilationStatus::MustRecompile;
                        }

                        for (const auto& it : _tiles_by_coords)
                        {
                            TileId tile_id = it.second;
                            auto* node = _tile_node_pool.get_object(tile_id);
                            if (!node->content.has_value()) continue;

                            auto& content = node->content.value();

                            if (content.geometry.repr.clamping.method()
                                == hrz_proto::VectorClampMode::NO_CLAMPING)
                            {
                                query_elevations_for_clamping(tile_id, content.geometry);
                            }

                            destroy_repr(content.reprs[index], DestroyRepr_Baking);

                            content.reschedule_styling(
                                _styling_jobs_to_cancel, old_repr_id, new_repr_id);

                            _loading_tiles.insert(tile_id);
                        }
                    },
                    [&](const to_actor::RemoveRepresentation& message)
                    {
                        auto& index = message.index;

                        assert(index < _reprs_configs.size());

                        uint32_t repr_id;

                        {
                            auto& repr_config = _reprs_configs[index];
                            repr_id = repr_config.id;

                            if (repr_config.config_id.has_value())
                            {
                                _repr_channels[repr_config.type].send(
                                    repr::messages::UnregisterStyle{
                                        repr_config.config_id.value(), _global_layer_id});
                            }

                            _reprs_configs.erase(_reprs_configs.begin() + index);
                        }

                        if (_reprs_configs.empty())
                        {
                            prp_id_to_default_value.clear();
                        }

                        for (const auto& it : _tiles_by_coords)
                        {
                            auto* node = _tile_node_pool.get_object(it.second);
                            if (!node->content.has_value()) continue;

                            auto& content = node->content.value();

                            destroy_repr(content.reprs[index], DestroyRepr_All);
                            content.reprs.erase(content.reprs.begin() + index);

                            content.cancel_styling(_styling_jobs_to_cancel, repr_id);

                            _loading_tiles.insert(it.second);
                        }
                    },
                    [&](to_actor::ReprChannel& message)
                    {
                        _repr_channels.insert_or_assign(message.type, std::move(message.channel));
                        if (!message.schedules_instantly)
                        {
                            _reprs_not_scheduling_instantly.insert(message.type);
                        }
                        if (message.uses_z_coordinates)
                        {
                            _reprs_using_z_coordinates.insert(message.type);
                        }
                    },
                    [&](const to_actor::SetSpecialAttributes& message)
                    {
                        if (!message.anchor_z_attribute_name.empty())
                        {
                            _anchor_z_attribute_name = std::string(
                                message.anchor_z_attribute_name.begin(),
                                message.anchor_z_attribute_name.end());
                        }
                        else
                        {
                            _anchor_z_attribute_name = std::nullopt;
                        }

                        if (!message.anchor_angle_attribute_name.empty())
                        {
                            _anchor_angle_attribute_name = std::string(
                                message.anchor_angle_attribute_name.begin(),
                                message.anchor_angle_attribute_name.end());
                        }
                        else
                        {
                            _anchor_angle_attribute_name = std::nullopt;
                        }

                        if (!message.feature_type_attribute_name.empty())
                        {
                            _feature_type_attribute_name = std::string(
                                message.feature_type_attribute_name.begin(),
                                message.feature_type_attribute_name.end());
                        }
                        else
                        {
                            _feature_type_attribute_name = std::nullopt;
                        }

                        _script_compilation_status = ScriptCompilationStatus::MustRecompile;
                    },
                    [&](const to_actor::SetClipId& message)
                    {
                        _clip_id = message.clip_id;
                        _appearance_changed = true;
                    },
                    [&](const to_actor::SetLighting& message)
                    {
                        _lighting_settings = message.lighting_settings;
                        _appearance_changed = true;
                    },
                    [&](const to_actor::SetRngSeed& message)
                    {
                        _rng_seed = message.rng_seed;
                        _script_compilation_status = ScriptCompilationStatus::MustRecompile;
                    },
                    [&](const to_actor::SetVisibility& message)
                    { _visible_in = message.visible_in; },
                    [&](to_actor::UpdateSelection& message)
                    {
                        _selected_features = std::move(message.selected_features);
                        _selected_features_changed = true;
                    },
                    [&](const to_actor::SignalPropertiesRegistered& message)
                    {
                        if (_script_compilation_status
                                == ScriptCompilationStatus::WaitingForProperties
                            && message.parser == _parser.get())
                        {
                            _script_compilation_status =
                                ScriptCompilationStatus::PropertiesRegistered;
                        }
                    },
                    [&](to_actor::GenerateNewVisibilitySet& message)
                    {
                        _cullers = {std::move(message.cullers)};
                        _generate_new_visibility_set = true;
                        _include_debug_info_in_visibility_set = message.include_debug_info;
                    },
                    [&](const to_actor::SignalVisibilitySetDestroyed& message)
                    { _last_destroyed_visibility_set_id = message.visibility_set_id; }},
                generic_message);
        }

        if (_model_status == ModelStatus::Unloaded)
        {
            _vector_data_channel.send(vector_data::messages::LoadLayer{0, _vector_data_layer});
            _model_status = ModelStatus::Loading;
        }

        auto get_tile_content_for_tile_id = [&](TileId tile_id) -> TileContent*
        {
            auto* node = _tile_node_pool.get_object(tile_id);
            if (node == nullptr || !node->content.has_value())
            {
                // The tile has been deleted since the request was sent.
                return nullptr;
            }

            return &node->content.value();
        };

        for (auto& generic_message : _vector_data_channel.receive())
        {
            std::visit(
                hrz::overload{
                    [&](const vector_data::messages::LayerModelUpdate& message)
                    {
                        assert(message.request_id == 0);

                        _data_min_lod = message.min_lod;
                        _data_max_lod = message.max_lod;
                        _data_bounds = message.bounds;

                        _attribute_ids.clear();
                        for (auto attribute_id : message.attribute_ids)
                        {
                            _attribute_ids.insert(attribute_id);
                        }

                        clean_tree(ba, js);
                        _needs_to_reload_attributes = false; // `clean_tree()` deals with it.
                        _script_compilation_status =
                            ScriptCompilationStatus::MustRecompile; // Attribute types may have
                                                                    // changed.

                        _model_status = ModelStatus::Loaded;

                        render_request.request_visual_render();
                    },
                    [&](const vector_data::messages::LayerModelError& message)
                    {
                        assert(message.request_id == 0);
                        clean_tree(ba, js);
                        _needs_to_reload_attributes = false; // `clean_tree()` deals with it.
                        _script_compilation_status =
                            ScriptCompilationStatus::MustRecompile; // Attribute types may have
                                                                    // changed.
                        _model_status = ModelStatus::Error;
                    },
                    [&](const vector_data::messages::LayerNewData&)
                    {
                        // No-op
                    },
                    [&](vector_data::messages::DataUpdate& message)
                    {
                        auto tile_id_and_data_kind =
                            extract_tile_id_from_request_id(message.request_id);
                        auto* content = get_tile_content_for_tile_id(tile_id_and_data_kind.tile_id);
                        if (content == nullptr) return;

                        std::visit(
                            hrz::overload{
                                [&](vector_data::FeatureIds&& feature_ids)
                                {
                                    assert(
                                        tile_id_and_data_kind.data_kind
                                        == vector_data::DataKind::FeatureIds);
                                    content->feature_ids.feature_ids = std::move(feature_ids);
                                    content->feature_ids.status =
                                        TileContent::FeatureIds::Status::Ready;

                                    if (_static_tiles)
                                    {
                                        _vector_data_channel.send(
                                            vector_data::messages::ReleaseDataRequest{
                                                message.request_id});
                                    }

                                    if (content->feature_ids.feature_ids.empty())
                                    {
                                        if (_static_tiles)
                                        {
                                            _vector_data_channel.send(
                                                vector_data::messages::ReleaseDataRequest{
                                                    make_data_request_id(
                                                        tile_id_and_data_kind.tile_id,
                                                        vector_data::DataKind::AttributeValues)});
                                        }

                                        for (auto& repr : _reprs_configs)
                                        {
                                            content->reschedule_styling(
                                                _styling_jobs_to_cancel, repr.id, repr.id);
                                        }
                                        content->style_job.status =
                                            TileContent::StyleJob::Status::Ready;
                                    }
                                },
                                [&](vector_data::VectorTileGeometry&& geometry_source)
                                {
                                    assert(
                                        tile_id_and_data_kind.data_kind
                                        == vector_data::DataKind::Geometry);

                                    if (content->attributes.status
                                        > TileContent::Attributes::Status::Loading)
                                    {
                                        content->attributes.status = TileContent::Attributes::
                                            Status::AllocatingSpecialAttributes;
                                    }

                                    content->geometry.repr.geometry = std::move(geometry_source);
                                    content->geometry.attribution = std::move(message.attribution);

                                    content->geometry.status =
                                        TileContent::Geometry::Status::Clamping;
                                    query_elevations_for_clamping(
                                        tile_id_and_data_kind.tile_id, content->geometry);

                                    if (_static_tiles)
                                    {
                                        _vector_data_channel.send(
                                            vector_data::messages::ReleaseDataRequest{
                                                message.request_id});
                                    }
                                    else
                                    {
                                        _vector_data_channel.send(
                                            vector_data::messages::ReleaseData{message.request_id});
                                    }
                                },
                                [&](hrz::InlinedVector<vector_data::AttributeValues, 16>&&
                                        attribute_values)
                                {
                                    assert(
                                        tile_id_and_data_kind.data_kind
                                        == vector_data::DataKind::AttributeValues);
                                    content->attributes.attributes = std::move(attribute_values);
                                    content->attributes.attribution = message.attribution;

                                    if (content->attributes.status
                                        == TileContent::Attributes::Status::Loading)
                                    {
                                        content->attributes.status = TileContent::Attributes::
                                            Status::AllocatingSpecialAttributes;
                                    }

                                    if (_static_tiles)
                                    {
                                        _vector_data_channel.send(
                                            vector_data::messages::ReleaseDataRequest{
                                                message.request_id});
                                    }
                                    else
                                    {
                                        _vector_data_channel.send(
                                            vector_data::messages::ReleaseData{message.request_id});
                                    }
                                },
                            },
                            std::move(message.data));

                        destroy_reprs(*content, DestroyRepr_Baking);
                        for (auto& repr : _reprs_configs)
                        {
                            content->reschedule_styling(_styling_jobs_to_cancel, repr.id, repr.id);
                        }
                        assert(content->style_job.status == TileContent::StyleJob::Status::Idle);

                        assert(content->load_status == TileContent::LoadStatus::Loading);
                        _loading_tiles.insert(tile_id_and_data_kind.tile_id);
                    },
                    [&](const vector_data::messages::DataError& message)
                    {
                        auto tile_id_and_data_kind =
                            extract_tile_id_from_request_id(message.request_id);
                        auto* content = get_tile_content_for_tile_id(tile_id_and_data_kind.tile_id);
                        if (content == nullptr) return;

                        if (content->load_status == TileContent::LoadStatus::Loading)
                        {
                            switch (tile_id_and_data_kind.data_kind)
                            {
                                case vector_data::DataKind::FeatureIds:
                                    if (content->feature_ids.status
                                        == TileContent::FeatureIds::Status::Loading)
                                    {
                                        content->feature_ids.status =
                                            TileContent::FeatureIds::Status::Error;
                                    }
                                    break;
                                case vector_data::DataKind::Geometry:
                                    if (content->geometry.status
                                        == TileContent::Geometry::Status::Loading)
                                    {
                                        content->geometry.status =
                                            TileContent::Geometry::Status::Error;
                                    }
                                    break;
                                case vector_data::DataKind::AttributeValues:
                                    if (content->attributes.status
                                        == TileContent::Attributes::Status::Loading)
                                    {
                                        content->attributes.status =
                                            TileContent::Attributes::Status::Error;
                                    }
                                    break;
                                default: assert(false && "Unhandled case");
                            }

                            render_request.request_visual_render();
                        }
                        else if (content->load_status == TileContent::LoadStatus::Ready)
                        {
                            content->load_status = TileContent::LoadStatus::Error;
                            content->display_status = TileContent::DisplayStatus::Error;
                            content->reset_style_job();

                            destroy_reprs(*content, DestroyRepr_All);

                            render_request.request_visual_render();
                        }
                    },
                },
                generic_message);
        }

        for (auto& generic_message : _elevation_query_channel.receive())
        {
            namespace messages = planet::elevation_query::messages;
            std::visit(
                hrz::overload{
                    [&](messages::ElevationQueryResult& message)
                    {
                        auto* content = get_tile_content_for_tile_id(message.query_id);
                        if (content == nullptr) return;

                        if (content->geometry.status == TileContent::Geometry::Status::Clamping)
                        {
                            if (message.elevations.has_value())
                            {
                                content->geometry.repr.clamps =
                                    std::move(message.elevations.value());
                                content->geometry.repr.clamping.CopyFrom(_clamping);
                            }
                            else
                            {
                                // No elevation values have been retrieved from the elevation
                                // query. This means that no DTMs are loaded, in which case we
                                // don't need to clamp.
                                content->geometry.repr.clamping.set_method(
                                    hrz_proto::VectorClampMode::NO_CLAMPING);
                                content->geometry.repr.clamping.set_use_z(_clamping.use_z());
                            }

                            content->geometry.status = TileContent::Geometry::Status::Ready;
                        }
                    },
                },
                generic_message);
        }

        for (auto& generic_message : _planet_channel.receive())
        {
            std::visit(
                hrz::overload{
                    [&](const planet::surface::messages::TerrainVersionUpdate& message)
                    {
                        _planet_elevation_version_has_changed =
                            message.terrain_version != _planet_elevation_version;
                        _planet_elevation_version = message.terrain_version;
                    },
                    [&](const planet::surface::messages::TileElevationBounds& message)
                    {
                        auto node = _tile_node_pool.get_object(message.request_id);
                        if (node != nullptr)
                        {
                            if (message.min_elevation.has_value()
                                && message.max_elevation.has_value())
                            {
                                const Elevation elevation{
                                    message.min_elevation.value(), message.max_elevation.value(),
                                    ElevationSource::GroundTruth};
                                if (update_tile_elevation(*node, elevation))
                                {
                                    render_request.request_visual_render();
                                }
                            }
                            else if (node->elevation.source == ElevationSource::GroundTruth)
                            {
                                const Elevation elevation{0.0, 0.0, ElevationSource::None};
                                if (update_tile_elevation(*node, elevation))
                                {
                                    render_request.request_visual_render();
                                }
                            }
                        }
                    },
                },
                generic_message);
        }

        for (auto& it : _repr_channels)
        {
            auto& channel = it.second;

            for (auto& generic_message : channel.receive())
            {
                std::visit(
                    hrz::overload{
                        [&](const repr::messages::StyleRegistrationResult& message)
                        {
                            for (const auto& prp : message.registered_properties)
                            {
                                register_property(prp.prp_id, prp.default_value);
                            }

                            for (auto& repr : _reprs_configs)
                            {
                                if (repr.config_id == message.style_id)
                                {
                                    repr.registered = true;
                                    break;
                                }
                            }
                        },
                        [&](const repr::messages::TileStatusUpdate& message)
                        {
                            auto repr_id = message.tile_id;
                            auto tile_id = extract_tile_id_from_repr_id(repr_id);
                            auto* content = get_tile_content_for_tile_id(tile_id);
                            if (content == nullptr) return;

                            for (auto& repr_slot : content->reprs)
                            {
                                if (repr_slot.baking.has_value()
                                    && repr_slot.baking.value().id == repr_id)
                                {
                                    destroy_repr(repr_slot, DestroyRepr_Displayed);

                                    repr_slot.displayed = TileContent::ReprSlot::DisplayedRepr{
                                        std::exchange(repr_slot.baking, std::nullopt).value(),
                                        std::nullopt};

                                    // We need to update the selection for tiles that are about
                                    // to be displayed because selected features may be present
                                    // in new tiles and we want the selection to be preserved
                                    // when loading new tiles.
                                    render_request |= update_repr(repr_slot.displayed->repr, true);
                                    break;
                                }
                            }

                            render_request.request_visual_render();
                        },
                    },
                    generic_message);
            }
        }

        if (_model_status == ModelStatus::Loaded && _needs_to_reload_attributes)
        {
            TileNode* root_node = _tile_node_pool.get_object(_root_tile_id);
            assert(root_node);
            reload_attribute_values(_root_tile_id, *root_node, js, true);
            _needs_to_reload_attributes = false;
        }

        if (_model_status == ModelStatus::Loaded)
        {
            // Script compilation is split into two steps to allow a synchronous
            // call to the representation registry, that registers the properties
            // in the parser.
            if (_script_compilation_status == ScriptCompilationStatus::MustRecompile)
            {
                prepare_script_for_compilation();
            }
            else if (_script_compilation_status == ScriptCompilationStatus::WaitingForStyles)
            {
                bool all_configs_registered = true;
                for (const auto& repr : _reprs_configs)
                {
                    if (!repr.registered)
                    {
                        all_configs_registered = false;
                        break;
                    }
                }

                if (all_configs_registered)
                {
                    _script_compilation_status = ScriptCompilationStatus::WaitingForProperties;
                    _channel.send(from_actor::RegisterProperties{_parser.get()});
                }
            }
            else if (_script_compilation_status == ScriptCompilationStatus::PropertiesRegistered)
            {
                compile_style_script();
            }
        }

        if (_model_status == ModelStatus::Loaded)
        {
            {
                HRZ_SCOPED_SAMPLE("create tile content and start loading");
                for (TileId tile_id : _nodes_to_start_loading)
                {
                    TileNode* node = _tile_node_pool.get_object(tile_id);
                    assert(node);
                    create_tile_content_and_start_loading(tile_id, *node);
                }
                _nodes_to_start_loading.clear();
            }

            {
                HRZ_SCOPED_SAMPLE("advance tile work");
                for (auto it = _loading_tiles.begin(); it != _loading_tiles.end();)
                {
                    auto res = advance_tile_work(*it, ba, js);

                    render_request |= res.second;

                    if (res.first)
                    {
                        _loading_tiles.erase(it++);
                    }
                    else
                    {
                        ++it;
                    }
                }
            }

            render_request |= work_node_elevations();
            work_tile_clamping();
            render_request |= work_tile_reprs();
        }

        if (render_request.is_render_requested(RenderRequest::Type::Visual))
        {
            _generate_new_visibility_set = true;
        }

        _appearance_changed = false;

        for (hrz_jobs::StyleFeaturesTicket ticket : _styling_jobs_to_cancel)
        {
            hrz_jobs::cancel_job(js, ticket);
        }
        _styling_jobs_to_cancel.clear();

        if (_model_status == ModelStatus::Loaded
            && _script_compilation_status == ScriptCompilationStatus::Compiled
            && _cullers.has_value()
            && (_generate_new_visibility_set || _any_tile_content_waiting_load_delay
                || _planet_elevation_version_has_changed)
            && _visible_in.any() && _next_visibility_set_id - _last_destroyed_visibility_set_id <= 2
            && hrz::now_ms()
                >= _last_visibility_set_creation_time_ms + VISIBLITY_SET_RECOMPUTE_DELAY_MS)
        {
            _generate_new_visibility_set = false;
            _any_tile_content_waiting_load_delay = false;

            auto visibility_set_builder = VisibilitySetBuilder(
                _next_visibility_set_id++, _include_debug_info_in_visibility_set, _global_layer_id,
                ba);

            traverse_tile_node_for_visibility_set(
                _root_tile_id, _cullers.value(), visibility_set_builder, _visible_in, _visible_in,
                false, false, ba, js);

            auto visibility_set = std::move(visibility_set_builder).build();

            if (visibility_set.has_value())
            {
                _channel.send(from_actor::NewVisibilitySet{std::move(visibility_set.value())});
                _last_visibility_set_creation_time_ms = hrz::now_ms();
            }
            else
            {
                _generate_new_visibility_set = true;
            }
        }

        while (!_reprs_in_queued_visibility_sets.empty()
               && _reprs_in_queued_visibility_sets.front().last_used_in_visibility_set.value_or(0)
                   <= _last_destroyed_visibility_set_id)
        {
            const auto& repr = _reprs_in_queued_visibility_sets.front().repr;
            _repr_channels[repr.type].send(repr::messages::RemoveTile{repr.id});

            _reprs_in_queued_visibility_sets.pop_front();
        }

        _planet_elevation_version_has_changed = false;

        _channel.send(from_actor::RenderRequest{std::move(render_request)});

        auto compute_is_working = [&]()
        {
            if (_model_status == ModelStatus::Unloaded || _model_status == ModelStatus::Loading)
            {
                return true;
            }

            if (_any_tile_content_waiting_load_delay && _visible_in.any()) return true;

            // There is a one frame delay between the insertion in '_nodes_to_start_loading' and the
            // actual requests made to the vector data loader.
            if (!_nodes_to_start_loading.empty()) return true;

            if (!_loading_tiles.empty()) return true;

            return false;
        };

        bool is_working = compute_is_working();
        _selected_features_changed = false;

        if (_channel.is_closed())
        {
            _vector_data_channel.send(vector_data::messages::ReleaseLayerLoader{0});

            clean_tree(ba, js);
            _tile_node_pool.release(_root_tile_id);
            _tile_node_count = 0;

            unregister_all_representations();

            for (hrz_jobs::StyleFeaturesTicket ticket : _styling_jobs_to_cancel)
            {
                hrz_jobs::cancel_job(js, ticket);
            }
            _styling_jobs_to_cancel.clear();

            _planet_channel.send(
                planet::surface::messages::CancelTerrainVersionUpdatesSubscription{0});

            return ActorStatus::RELEASED;
        }

        return is_working ? ActorStatus::WORKING : ActorStatus::IDLE;
    }

    void unregister_all_representations()
    {
        for (const auto& repr : _reprs_configs)
        {
            if (repr.config_id.has_value())
            {
                _repr_channels[repr.type].send(
                    repr::messages::UnregisterStyle{repr.config_id.value(), _global_layer_id});
            }
        }
        _reprs_configs.clear();
    }

    void prepare_script_for_compilation()
    {
        _script_is_valid = false;

        _parser = style::Parser::create();

        //
        // Register all attributes
        //

        _attribute_id_to_style.clear();
        for (auto& attrib : _attributes)
        {
            auto it = _attribute_ids.find(attrib.vector_data_id);
            if (it == _attribute_ids.end())
            {
                HRZ_LOG_WARNING(
                    "Attribute with id {} (\"{}\") not found in data layer {}.",
                    attrib.vector_data_id, attrib.styling_name, _vector_data_layer);
            }

            attrib.style_id = _parser->add_attribute(attrib.styling_name);
            if (attrib.style_id != style::Parser::INSERT_ERROR)
            {
                _attribute_id_to_style.insert({attrib.vector_data_id, attrib.style_id});
            }
            else
            {
                HRZ_LOG_WARNING("Couldn't register attribute \"{}\".", attrib.styling_name);
            }
        }

        if (_anchor_z_attribute_name.has_value())
        {
            const auto& name = _anchor_z_attribute_name.value();
            int id = _parser->add_attribute(name);
            if (id != style::Parser::INSERT_ERROR)
            {
                _anchor_z_style_attribute_id = id;
            }
            else
            {
                HRZ_LOG_WARNING("Couldn't register attribute {}.", name);
                _anchor_z_style_attribute_id = std::nullopt;
            }
        }
        else
        {
            _anchor_z_style_attribute_id = std::nullopt;
        }

        if (_anchor_angle_attribute_name.has_value())
        {
            const auto& name = _anchor_angle_attribute_name.value();
            int id = _parser->add_attribute(name);
            if (id != style::Parser::INSERT_ERROR)
            {
                _anchor_angle_style_attribute_id = id;
            }
            else
            {
                HRZ_LOG_WARNING("Couldn't register attribute {}.", name);
                _anchor_angle_style_attribute_id = std::nullopt;
            }
        }
        else
        {
            _anchor_angle_style_attribute_id = std::nullopt;
        }

        if (_feature_type_attribute_name.has_value())
        {
            const auto& name = _feature_type_attribute_name.value();
            int id = _parser->add_attribute(name);
            if (id != style::Parser::INSERT_ERROR)
            {
                _feature_type_style_attribute_id = id;
            }
            else
            {
                HRZ_LOG_WARNING("Couldn't register attribute {}.", name);
                _feature_type_style_attribute_id = std::nullopt;
            }
        }
        else
        {
            _feature_type_style_attribute_id = std::nullopt;
        }

        //
        // Register uniforms
        //

        _tile_z_uniform_id = (uint32_t)_parser->add_uniform("tile_z");

        //
        // Register palettes.
        //

        for (const auto& palette : _palettes)
        {
            _parser->add_palette(palette.name.c_str());
        }

        _script_compilation_status = ScriptCompilationStatus::WaitingForStyles;
    }

    void compile_style_script()
    {
        if (_script_compilation_status != ScriptCompilationStatus::PropertiesRegistered)
        {
            return;
        }

        assert(_parser != nullptr);
        auto lexer = style::Lexer::create(_script);

        //
        // Compile the styling script.
        //

        _ast = std::make_shared<hrz::style::FlatAst>();
        hrz::style::Ast full_ast;
        auto result = _parser->parse(*lexer, full_ast);
        if (!result)
        {
            HRZ_LOG_ERROR(
                "Styling script parsing error {} ({}) at line {}", result.description(),
                fmt::underlying(result.type), result.line);
            HRZ_LOG_DEBUG("For script:\n{}", _script);
            _ast = nullptr;
        }
        else
        {
            //
            // Optimize the AST
            //

            auto optimizer = hrz::style::Optimizer::create(_palettes);
            if (optimizer->optimize(std::move(full_ast), _ast.get()))
            {
                _script_is_valid = true;
            }
            else
            {
                HRZ_LOG_ERROR("Styling script optimization error");
                _ast = nullptr;
            }
        }

        _parser = nullptr;

        //
        // Schedule styling jobs
        //

        for (const auto& it : _tiles_by_coords)
        {
            TileNode* node = _tile_node_pool.get_object(it.second);
            assert(node);
            if (!node->content.has_value()) continue;

            TileContent& content = node->content.value();

            destroy_reprs(content, DestroyRepr_Baking);
            for (const auto& repr : _reprs_configs)
            {
                content.reschedule_styling(_styling_jobs_to_cancel, repr.id, repr.id);
            }
            assert(content.style_job.status == TileContent::StyleJob::Status::Idle);

            _loading_tiles.insert(it.second);
        }

        _script_compilation_status = ScriptCompilationStatus::Compiled;
    }

    void register_property(
        uint64_t prp_id,
        const hrz::vector_data::OwnedAttributeValue& default_value)
    {
        prp_id_to_default_value[prp_id] = default_value;
    }

    void query_elevations_for_clamping(TileId tile_id, TileContent::Geometry& geometry)
    {
        if (geometry.status == TileContent::Geometry::Status::Loading
            || geometry.status == TileContent::Geometry::Status::Error)
        {
            // This is too early, we need the geometry to be ready
            // before we can start clamping.
            return;
        }

        if (geometry.status == TileContent::Geometry::Status::Clamping)
        {
            _elevation_query_channel.send(
                hrz::planet::elevation_query::messages::CancelElevationQuery{tile_id});
        }

        bool must_clamp = false;
        for (const auto& repr : _reprs_configs)
        {
            if (_reprs_using_z_coordinates.contains(repr.type))
            {
                must_clamp = true;
                break;
            }
        }

        if (!must_clamp)
        {
            geometry.repr.clamping.set_method(hrz_proto::VectorClampMode::NO_CLAMPING);
            geometry.repr.clamping.set_use_z(false);
            geometry.status = TileContent::Geometry::Status::Ready;
            return;
        }

        if (geometry.repr.geometry.features.empty())
        {
            geometry.status = TileContent::Geometry::Status::Ready;
            return;
        }

        if (_clamping.method() == hrz_proto::VectorClampMode::PER_VERTEX)
        {
            if (geometry.repr.geometry.points.empty())
            {
                geometry.status = TileContent::Geometry::Status::Ready;
            }
            else
            {
                auto point_view = BlobArrayView<lm::dvec2>::make_blob_array_view(
                    hrz::unsafe(
                        "Converting from a blob array of dvec3 to a blob array view of dvec2"),
                    geometry.repr.geometry.points.blob(), geometry.repr.geometry.points.size(), 0,
                    sizeof(lm::dvec3));
                _elevation_query_channel.send(
                    hrz::planet::elevation_query::messages::QueryElevation{
                        tile_id,
                        std::move(point_view),
                        {monitoring::systems::VectorTiles, _global_layer_id}});
                geometry.status = TileContent::Geometry::Status::Clamping;
            }
        }
        else if (_clamping.method() == hrz_proto::VectorClampMode::ANCHOR)
        {
            if (geometry.repr.geometry.features.empty())
            {
                geometry.status = TileContent::Geometry::Status::Ready;
            }
            else
            {
                auto point_view = BlobArrayView<lm::dvec2>::make_blob_array_view(
                    hrz::unsafe(
                        "Converting from a blob array of dvec3 to a blob array view of dvec2"),
                    geometry.repr.geometry.features.blob(), geometry.repr.geometry.features.size(),
                    offsetof(vector_data::VectorTileGeometry::Feature, anchor),
                    sizeof(vector_data::VectorTileGeometry::Feature));
                _elevation_query_channel.send(
                    hrz::planet::elevation_query::messages::QueryElevation{
                        tile_id,
                        std::move(point_view),
                        {monitoring::systems::VectorTiles, _global_layer_id}});
                geometry.status = TileContent::Geometry::Status::Clamping;
            }
        }
        else if (_clamping.method() == hrz_proto::VectorClampMode::NO_CLAMPING)
        {
            geometry.repr.clamping.CopyFrom(_clamping);
            geometry.status = TileContent::Geometry::Status::Ready;
        }
    }

    void advance_tile_work_loading(
        TileContent& content,
        const TileCoords& tile_coords,
        BlobAllocator* ba,
        JobScheduler* js)
    {
        if (content.attributes.status
            == TileContent::Attributes::Status::AllocatingSpecialAttributes)
        {
            if ((int)content.geometry.status > (int)TileContent::Geometry::Status::Loading)
            {
                if (!content.geometry.repr.geometry.features.empty()
                    && (_anchor_z_style_attribute_id.has_value()
                        || _anchor_angle_style_attribute_id.has_value()
                        || _feature_type_style_attribute_id.has_value()))
                {
                    size_t feature_count = content.geometry.repr.geometry.features.size();
                    bool allocations_are_ready = true;
                    bool allocations_have_errors = false;

                    auto maybe_restart_allocation_and_wait_for_ready =
                        [feature_count, ba, layer_id = _global_layer_id, &allocations_are_ready,
                         &allocations_have_errors](
                            const hrz::StaticString& name,
                            std::optional<
                                hrz::BlobArrayAllocation<hrz::vector_data::PackedAttributeValue>>&
                                allocation,
                            hrz::BlobArray<hrz::vector_data::PackedAttributeValue>& array)
                    {
                        bool array_is_valid =
                            array.blob().is_valid() && array.size() == feature_count;
                        bool allocation_is_valid =
                            allocation.has_value() && allocation->requested_size() == feature_count;

                        if (array_is_valid)
                        {
                            if (allocation.has_value())
                            {
                                allocation->cancel(ba);
                                allocation = std::nullopt;
                            }
                            return;
                        }

                        if (!allocation_is_valid)
                        {
                            if (allocation.has_value())
                            {
                                allocation->cancel(ba);
                                allocation = std::nullopt;
                            }
                            allocation =
                                BlobArrayAllocation<hrz::vector_data::PackedAttributeValue>::
                                    allocate(ba, feature_count);
                            allocation->register_blob_metadata(ba, "contents"_ss, name);
                            allocation->register_blob_owner(
                                ba, {monitoring::systems::VectorTiles, layer_id});
                            allocations_are_ready = false;
                        }
                        else if (allocation.has_value())
                        {
                            auto state = allocation->get_state(ba);
                            if (state == BlobArrayAllocationState::Error)
                            {
                                allocations_have_errors = true;
                            }
                            else if (state == BlobArrayAllocationState::Allocated)
                            {
                                array = allocation->to_array(ba);
                                allocation = std::nullopt;
                                assert(array.blob().is_valid() && array.size() == feature_count);
                            }
                            else
                            {
                                allocations_are_ready = false;
                            }
                        }
                    };

                    if (_anchor_z_style_attribute_id.has_value())
                    {
                        maybe_restart_allocation_and_wait_for_ready(
                            "anchor Z attribute values"_ss,
                            content.attributes.anchor_z_attribute_allocation,
                            content.attributes.anchor_z_attribute_values);
                    }
                    if (_anchor_angle_style_attribute_id.has_value())
                    {
                        maybe_restart_allocation_and_wait_for_ready(
                            "anchor angle attribute values"_ss,
                            content.attributes.anchor_angle_attribute_allocation,
                            content.attributes.anchor_angle_attribute_values);
                    }
                    if (_feature_type_style_attribute_id.has_value())
                    {
                        maybe_restart_allocation_and_wait_for_ready(
                            "feature type attribute values"_ss,
                            content.attributes.feature_type_attribute_allocation,
                            content.attributes.feature_type_attribute_values);
                    }

                    if (allocations_have_errors)
                    {
                        content.attributes.status = TileContent::Attributes::Status::Error;
                    }
                    else if (allocations_are_ready)
                    {
                        content.attributes.status =
                            TileContent::Attributes::Status::GeneratingSpecialAttributes;
                    }
                }
                else
                {
                    content.attributes.status = TileContent::Attributes::Status::Ready;
                }
            }
        }

        if (content.attributes.status
            == TileContent::Attributes::Status::GeneratingSpecialAttributes)
        {
            if (_anchor_z_style_attribute_id.has_value())
            {
                auto& array = content.attributes.anchor_z_attribute_values;

                auto features = content.geometry.repr.geometry.features.get_data();
                auto anchors = array.get_mutable_data();

                assert(anchors.size() == features.size());
                auto write_ptr = anchors.data();
                for (const auto& feature : features)
                {
                    *write_ptr++ =
                        vector_data::PackedAttributeValueTraits<>::from_number(feature.anchor.z);
                }
            }

            if (_anchor_angle_style_attribute_id.has_value())
            {
                auto& array = content.attributes.anchor_angle_attribute_values;

                auto features = content.geometry.repr.geometry.features.get_data();
                auto angles = array.get_mutable_data();

                assert(angles.size() == features.size());
                auto write_ptr = angles.data();
                for (const auto& feature : features)
                {
                    *write_ptr++ = vector_data::PackedAttributeValueTraits<>::from_number(
                        feature.anchor_angle);
                }
            }

            if (_feature_type_style_attribute_id.has_value())
            {
                auto& array = content.attributes.feature_type_attribute_values;

                auto features = content.geometry.repr.geometry.features.get_data();
                auto feature_types = array.get_mutable_data();

                static_assert(
                    hrz_proto::VectorGeometryType::POINT_GEOMETRY == 0, "Point geometry type 0");
                static_assert(
                    hrz_proto::VectorGeometryType::POLYLINE_GEOMETRY == 1,
                    "Polyline geometry type 1");
                static_assert(
                    hrz_proto::VectorGeometryType::POLYGON_GEOMETRY == 2,
                    "Polygon geometry type 2");

                assert(feature_types.size() == features.size());
                auto write_ptr = feature_types.data();
                for (const auto& feature : features)
                {
                    // @Safety: The enum values are safe integers for packed attribute values.
                    *write_ptr++ = vector_data::PackedAttributeValueTraits<>::from_number(
                        (double)feature.type);
                }
            }

            content.attributes.status = TileContent::Attributes::Status::Ready;
        }

        if (content.should_start_new_style_job()
            && content.attributes.status == TileContent::Attributes::Status::Ready
            && content.feature_ids.status == TileContent::FeatureIds::Status::Ready
            && !content.feature_ids.feature_ids.empty())
        {
            auto try_start_style_job = [&]()
            {
                if (!_script_is_valid) return false;

                hrz_jobs::FeaturesStylingData data;

                data.ast = _ast;

                data.properties_default_values = prp_id_to_default_value;

                for (const auto& repr_cfg : _reprs_configs)
                {
                    if (content.reprs_to_style.count(repr_cfg.id))
                    {
                        data.representations.push_back({repr_cfg.id, repr_cfg.name});
                        content.style_job.for_reprs.insert(repr_cfg.id);
                    }
                }

                content.reprs_to_style.clear();

                data.feature_count = content.feature_ids.feature_ids.size();

                data.attributes.reserve(content.attributes.attributes.size());
                for (const auto& src : content.attributes.attributes)
                {
                    auto it = _attribute_id_to_style.find(src.attribute_id);
                    if (it != _attribute_id_to_style.end())
                    {
                        if (src.values.size() != data.feature_count) return false;
                        data.attributes.emplace(it->second, src);
                    }
                }

                const bool has_feature_ids = content.feature_ids.feature_ids.has_any_attribute();
                if (!has_feature_ids)
                {
                    const size_t tile_coords_hash = hrz::hash_value(content.coords);
                    data.rng_seed = hrz::hash_mix<uint64_t>(tile_coords_hash, _rng_seed);
                }
                else
                {
                    data.rng_seed = _rng_seed;
                    data.feature_ids_hashes = content.feature_ids.feature_ids.hashes();
                }

                {
                    auto lod_uniform_value =
                        vector_data::attr_from<vector_data::OwnedAttributeValue>(tile_coords.lod);
                    data.uniforms.emplace(_tile_z_uniform_id, lod_uniform_value);
                }

                if (_anchor_z_style_attribute_id.has_value())
                {
                    vector_data::AttributeValues values;
                    values.values = content.attributes.anchor_z_attribute_values;
                    if (values.values.size() != data.feature_count) return false;
                    data.attributes.emplace(_anchor_z_style_attribute_id.value(), values);
                }

                if (_anchor_angle_style_attribute_id.has_value())
                {
                    vector_data::AttributeValues values;
                    values.values = content.attributes.anchor_angle_attribute_values;
                    if (values.values.size() != data.feature_count) return false;
                    data.attributes.emplace(_anchor_angle_style_attribute_id.value(), values);
                }

                if (_feature_type_style_attribute_id.has_value())
                {
                    vector_data::AttributeValues values;
                    values.values = content.attributes.feature_type_attribute_values;
                    if (values.values.size() != data.feature_count) return false;
                    data.attributes.emplace(_feature_type_style_attribute_id.value(), values);
                }

                for (const auto& palette : _palettes)
                {
                    data.palettes.push_back(palette);
                }

                content.style_job.ticket = hrz_jobs::add_job_style_features(
                    js, data, {hrz::monitoring::systems::Styling, _global_layer_id});

                return true;
            };

            if (try_start_style_job())
            {
                content.style_job.status = TileContent::StyleJob::Status::Styling;
            }
            else
            {
                // Either the source data is inconsistent, or we are in the middle of
                // data updates and the current state is transient.
                // Or the script isn't valid.
                content.style_job.status = TileContent::StyleJob::Status::Error;
            }
        }
        else if (
            content.style_job.status == TileContent::StyleJob::Status::Styling
            && hrz_jobs::is_job_valid(js, content.style_job.ticket)
            && hrz_jobs::is_job_finished(js, content.style_job.ticket))
        {
            if (hrz_jobs::get_job_status(js, content.style_job.ticket)
                == hrz::job_scheduler::JobStatus::Finished_Success)
            {
                hrz_jobs::StylingResult result;
                hrz_jobs::get_job_response(js, content.style_job.ticket, result);

                content.style_job.result_repr = std::move(result.features);
                content.style_job.result_unique_reprs = std::move(result.unique_reprs);

                content.style_job.status = TileContent::StyleJob::Status::Ready;
            }
            else
            {
                hrz_jobs::cancel_job(js, content.style_job.ticket);
                content.style_job.status = TileContent::StyleJob::Status::Error;
            }
        }
    }

    static inline bool repr_is_flat_overlay(hrz_proto::VectorReprType type)
    {
        switch (type)
        {
            case hrz_proto::VectorReprType::FLAT_OVERLAY_POINT_VECTOR_REPR:
            case hrz_proto::VectorReprType::FLAT_OVERLAY_POLYLINE_VECTOR_REPR:
            case hrz_proto::VectorReprType::FLAT_OVERLAY_POLYGON_VECTOR_REPR:
            case hrz_proto::VectorReprType::HEATMAP_VECTOR_REPR: return true;

            case hrz_proto::VectorReprType::CYLINDER_VECTOR_REPR:
            case hrz_proto::VectorReprType::EXTRUDED_GEOMETRY_VECTOR_REPR:
            case hrz_proto::VectorReprType::MODEL_VECTOR_REPR:
            case hrz_proto::VectorReprType::NULL_VECTOR_REPR:
            case hrz_proto::VectorReprType::SYMBOL_VECTOR_REPR: return false;

            default: assert(false && "Unhandled case"); return false;
        }
    }

    RenderRequest update_repr(const TileContent::ReprSlot::Repr& repr, bool force_all = false)
    {
        RenderRequest render_request;

        if (force_all || _selected_features_changed)
        {
            _repr_channels[repr.type].send(
                repr::messages::UpdateSelection{repr.id, _selected_features});

            render_request.request_visual_render();
            if (repr_is_flat_overlay(repr.type))
            {
                render_request.schedule_flat_overlay_render();
            }
        }

        if (force_all || _appearance_changed)
        {
            _repr_channels[repr.type].send(repr::messages::UpdateClipId{repr.id, _clip_id});
            _repr_channels[repr.type].send(
                repr::messages::UpdateLighting{repr.id, _lighting_settings});
            render_request.request_visual_render();
        }

        return render_request;
    }

    // The first value of the returned pair is true when the tile is loaded.
    std::pair<bool, RenderRequest> advance_tile_work(
        TileId tile_id,
        BlobAllocator* ba,
        JobScheduler* js)
    {
        RenderRequest render_request;

        TileNode* node = _tile_node_pool.get_object(tile_id);
        assert(node && node->content.has_value());
        TileContent& content = node->content.value();

        auto send_tile_data_message = [&]()
        {
            _channel.send(from_actor::TileCoords{tile_id, node->coords});
            _channel.send(from_actor::TileFeatureIds{tile_id, content.feature_ids.feature_ids});

            for (const auto& attributes : content.attributes.attributes)
            {
                _channel.send(from_actor::TileAttributes{tile_id, attributes});
            }

            if (content.geometry.repr.geometry.features.blob().is_valid())
            {
                auto anchors_view = BlobArrayView<lm::dvec3>::make_blob_array_view(
                    hrz::unsafe("Converting from a blob array of `Feature` to a blob array view of "
                                "a property of these instances"),
                    content.geometry.repr.geometry.features.blob(),
                    content.geometry.repr.geometry.features.size(),
                    offsetof(vector_data::VectorTileGeometry::Feature, anchor),
                    sizeof(vector_data::VectorTileGeometry::Feature));

                _channel.send(from_actor::TileFeatureAnchors{tile_id, anchors_view});
            }
        };

        if (_planet_elevation_version_has_changed
            && _clamping.method() != hrz_proto::VectorClampMode::NO_CLAMPING)
        {
            if (content.geometry.status == TileContent::Geometry::Status::Clamping)
            {
                _elevation_query_channel.send(
                    hrz::planet::elevation_query::messages::CancelElevationQuery{
                        (uint64_t)tile_id});
            }

            if (content.geometry.status == TileContent::Geometry::Status::Ready
                || content.geometry.status == TileContent::Geometry::Status::Clamping)
            {
                query_elevations_for_clamping(tile_id, content.geometry);
                for (size_t i = 0; i < _reprs_configs.size(); ++i)
                {
                    const auto& repr = _reprs_configs[i];

                    destroy_repr(content.reprs[i], DestroyRepr_Baking);
                    content.schedule_styling(_styling_jobs_to_cancel, repr.id);
                }
            }
        }

        if (content.load_status == TileContent::LoadStatus::Loading)
        {
            advance_tile_work_loading(content, content.coords, ba, js);

            if (content.geometry.status == TileContent::Geometry::Status::Ready
                && content.geometry.repr.geometry.features.empty())
            {
                content.reset_style_job();
                content.load_status = TileContent::LoadStatus::Ready;
                content.display_status = TileContent::DisplayStatus::Displayable;

                send_tile_data_message();

                // The tile may have had features before, so we need to destroy
                // the old representations.
                destroy_reprs(content, DestroyRepr_All);

                render_request.request_visual_render();
            }
            else if (
                content.feature_ids.status == TileContent::FeatureIds::Status::Ready
                && content.geometry.status == TileContent::Geometry::Status::Ready
                && content.attributes.status == TileContent::Attributes::Status::Ready
                && content.style_job.status == TileContent::StyleJob::Status::Ready)
            {
                assert(_reprs_configs.size() == content.reprs.size());

                // Send tiles to representations that have at least one representation instance.
                for (unsigned int i = 0; i < _reprs_configs.size(); ++i)
                {
                    const auto& cfg = _reprs_configs.at(i);

                    if (!cfg.config_id.has_value()
                        || content.style_job.for_reprs.count(cfg.id) == 0)
                    {
                        continue;
                    }

                    if (content.style_job.result_unique_reprs.count(cfg.id) == 0)
                    {
                        // There are no new instances for this representation so we
                        // won't be baking, but we need to destroy the old
                        // representation.
                        destroy_repr(content.reprs[i], DestroyRepr_All);
                        continue;
                    }

                    assert(!content.reprs[i].baking.has_value());

                    auto tile_object_ref =
                        make_object_reference(_object_reference_layer_id_partial, tile_id);
                    auto feature_ref = make_feature_reference(_object_reference_layer_id_partial);

                    TileContent::ReprSlot::Repr repr;
                    repr.type = cfg.type;
                    repr.id = make_repr_id(tile_id, content.next_repr_id++);
                    content.reprs[i].baking = repr;

                    _repr_channels[repr.type].send(repr::messages::AddTile{
                        repr.id, cfg.config_id.value(), content.coords, _global_layer_id,
                        tile_object_ref, feature_ref, content.feature_ids.feature_ids,
                        content.geometry.repr, content.style_job.result_repr, node->elevation.min,
                        node->elevation.max});
                }

                content.reset_style_job();
                content.load_status = TileContent::LoadStatus::Baking;
            }
            else if (
                content.geometry.status == TileContent::Geometry::Status::Error
                || content.attributes.status == TileContent::Attributes::Status::Error
                || content.style_job.status == TileContent::StyleJob::Status::Error)
            {
                content.load_status = TileContent::LoadStatus::Error;
                content.display_status = TileContent::DisplayStatus::Error;
                content.reset_style_job();

                // If the tile has been reloaded because its data was updated,
                // but had an error while reloading, its previous representations
                // are still displayed, but are outdated, with no new representations
                // in preparations. In that case we delete all representations.

                destroy_reprs(content, DestroyRepr_All);
            }
        }
        else if (content.load_status == TileContent::LoadStatus::Baking)
        {
            bool ready = true;

            for (const auto& repr_slot : content.reprs)
            {
                if (repr_slot.baking.has_value())
                {
                    ready = false;
                    break;
                }
            }

            if (ready)
            {
                // All tiles representations are ready.
                content.load_status = TileContent::LoadStatus::Ready;
                content.display_status = TileContent::DisplayStatus::Displayable;

                send_tile_data_message();

                render_request.request_visual_render();
            }
        }

        bool is_loaded = content.load_status == TileContent::LoadStatus::Ready
            || content.load_status == TileContent::LoadStatus::Error;

        return {is_loaded, render_request};
    }

    bool update_tile_elevation(
        TileNode& node,
        Elevation elevation,
        std::optional<ElevationSource> source_override = std::nullopt)
    {
        if (source_override.has_value())
        {
            elevation.source = source_override.value();
        }

        bool elevation_updated = false;

        switch (elevation.source)
        {
            case ElevationSource::None:
                if (node.elevation.source != ElevationSource::None)
                {
                    node.elevation.clear();
                    elevation_updated = true;
                }
                break;
            case ElevationSource::GroundTruth:
                node.elevation = elevation;
                elevation_updated = true;
                break;
            case ElevationSource::InferredFromParent:
                elevation_updated = node.elevation.update_from_parent(elevation);
                break;
            case ElevationSource::InferredFromChild:
                elevation_updated = node.elevation.update_from_child(elevation);
                break;
            default: assert(false && "Unhandled case"); return false;
        }

        if (!elevation_updated) return false;

        node.update_bounds();

        if (node.content.has_value())
        {
            TileContent& content = node.content.value();

            for (auto& repr : content.reprs)
            {
                if (repr.baking.has_value())
                {
                    _repr_channels[repr.baking->type].send(repr::messages::UpdateTileElevation{
                        repr.baking->id, node.elevation.min, node.elevation.max});
                }
                if (repr.displayed.has_value())
                {
                    _repr_channels[repr.displayed->repr.type].send(
                        repr::messages::UpdateTileElevation{
                            repr.displayed->repr.id, node.elevation.min, node.elevation.max});
                }
            }
        }

        if (elevation.source == ElevationSource::None)
        {
            // Elevations that were inferred from this tile are no longer valid.
            if (node.children.has_value())
            {
                for (auto child_tile_id : node.children.value())
                {
                    TileNode* child_node = _tile_node_pool.get_object(child_tile_id);
                    assert(child_node);

                    if (child_node->elevation.source == ElevationSource::InferredFromParent)
                    {
                        update_tile_elevation(*child_node, elevation);
                    }
                }
            }
        }

        return true;
    }

    RenderRequest traverse_tile_node_for_elevation(
        TileId tile_id,
        const std::optional<Elevation>& updated_parent_elevation,
        std::optional<Elevation>& updated_elevation)
    {
        RenderRequest render_request;

        assert(_tile_node_pool.is_valid(tile_id));
        TileNode& node = *_tile_node_pool.get_object(tile_id);

        if (_planet_elevation_version_has_changed)
        {
            node.elevation.clear();
            node.last_time_elevation_queried_ms = 0;
        }

        double now = hrz::clock::CurrentFrameRealTime.ms;
        if (node.elevation.source != ElevationSource::GroundTruth)
        {
            if ((now - node.last_time_elevation_queried_ms) > TILE_ELEVATION_QUERY_DELAY_MS)
            {
                _planet_channel.send(
                    planet::surface::messages::RequestTileElevationBounds{tile_id, node.coords});
                node.last_time_elevation_queried_ms = now;
            }

            if (!updated_elevation.has_value() && updated_parent_elevation.has_value())
            {
                if (update_tile_elevation(
                        node, updated_parent_elevation.value(),
                        ElevationSource::InferredFromParent))
                {
                    updated_elevation = node.elevation;
                }
            }

            _any_node_lacking_elevation = true;
        }

        if (node.children.has_value())
        {
            for (auto child_tile_id : node.children.value())
            {
                std::optional<Elevation> updated_child_elevation = std::nullopt;

                render_request |= traverse_tile_node_for_elevation(
                    child_tile_id, updated_elevation, updated_child_elevation);

                if (updated_child_elevation.has_value())
                {
                    if (update_tile_elevation(
                            node, updated_child_elevation.value(),
                            ElevationSource::InferredFromChild))
                    {
                        updated_elevation = node.elevation;
                    }
                }
            }
        }

        if (updated_elevation)
        {
            render_request.request_visual_render();
        }

        return render_request;
    }

    RenderRequest work_node_elevations()
    {
        HRZ_SCOPED_SAMPLE("work_node_elevations");

        if (!_any_node_lacking_elevation && !_planet_elevation_version_has_changed)
        {
            return {};
        }

        _any_node_lacking_elevation = false;

        std::optional<Elevation> updated_root_elevation = std::nullopt;
        return traverse_tile_node_for_elevation(
            _root_tile_id, std::nullopt, updated_root_elevation);
    }

    void work_tile_clamping()
    {
        HRZ_SCOPED_SAMPLE("work_tile_clamping");

        if (!_planet_elevation_version_has_changed) return;

        for (const auto& it : _tiles_by_coords)
        {
            TileId tile_id = it.second;
            TileNode* node = _tile_node_pool.get_object(tile_id);
            if (!node->content.has_value()) continue;

            TileContent& content = node->content.value();

            if (content.load_status != TileContent::LoadStatus::Ready)
            {
                continue;
            }

            assert(content.geometry.status == TileContent::Geometry::Status::Ready);

            if (_clamping.method() != hrz_proto::VectorClampMode::NO_CLAMPING)
            {
                query_elevations_for_clamping(tile_id, content.geometry);

                for (size_t i = 0; i < _reprs_configs.size(); ++i)
                {
                    const auto& repr = _reprs_configs[i];

                    destroy_repr(content.reprs[i], DestroyRepr_Baking);
                    content.schedule_styling(_styling_jobs_to_cancel, repr.id);
                }

                _loading_tiles.insert(tile_id);
            }
        }
    }

    RenderRequest work_tile_reprs()
    {
        HRZ_SCOPED_SAMPLE("work_tile_reprs");

        RenderRequest render_request;

        if (!(_selected_features_changed || _appearance_changed)) return render_request;

        for (const auto& it : _tiles_by_coords)
        {
            TileId tile_id = it.second;
            const TileNode* node = _tile_node_pool.get_object(tile_id);
            if (!node->content.has_value()) continue;

            const TileContent& content = node->content.value();

            if (content.display_status == TileContent::DisplayStatus::Displayable)
            {
                for (const auto& repr_slot : content.reprs)
                {
                    if (repr_slot.displayed.has_value())
                    {
                        render_request |= update_repr(repr_slot.displayed->repr);
                    }
                }
            }
        }

        return render_request;
    }

    void clean_tile_node(
        TileId tile_id,
        TileNode& node,
        BlobAllocator* ba,
        JobScheduler* js,
        bool prune_children,
        bool prune_content,
        bool prune_content_now)
    {
        if (node.content.has_value() && prune_content)
        {
            TileContent& content = node.content.value();

            _vector_data_channel.send(vector_data::messages::ReleaseDataRequest{
                make_data_request_id(tile_id, vector_data::DataKind::FeatureIds)});
            _vector_data_channel.send(vector_data::messages::ReleaseDataRequest{
                make_data_request_id(tile_id, vector_data::DataKind::Geometry)});

            if (content.geometry.status == TileContent::Geometry::Status::Clamping)
            {
                _elevation_query_channel.send(
                    hrz::planet::elevation_query::messages::CancelElevationQuery{
                        (uint64_t)tile_id});
            }

            _vector_data_channel.send(vector_data::messages::ReleaseDataRequest{
                make_data_request_id(tile_id, vector_data::DataKind::AttributeValues)});

            if (content.attributes.anchor_z_attribute_allocation.has_value())
            {
                content.attributes.anchor_z_attribute_allocation->cancel(ba);
            }
            if (content.attributes.anchor_angle_attribute_allocation.has_value())
            {
                content.attributes.anchor_angle_attribute_allocation->cancel(ba);
            }
            if (content.attributes.feature_type_attribute_allocation.has_value())
            {
                content.attributes.feature_type_attribute_allocation->cancel(ba);
            }

            destroy_reprs(content, DestroyRepr_All, prune_content_now);

            if (content.style_job.status == TileContent::StyleJob::Status::Styling)
            {
                hrz_jobs::cancel_job(js, content.style_job.ticket);
            }

            _tiles_by_coords.erase(node.coords);
            _loading_tiles.erase(tile_id);
            node.content = std::nullopt;

            _channel.send(from_actor::DiscardTile{tile_id, node.coords});
        }

        if (prune_children && node.children.has_value())
        {
            for (auto child_tile_id : node.children.value())
            {
                TileNode* child_node = _tile_node_pool.get_object(child_tile_id);
                assert(child_node);
                clean_tile_node(child_tile_id, *child_node, ba, js, true, true, false);

                // node is not valid anymore after this point.
                _tile_node_pool.release(child_tile_id);
                assert(_tile_node_count > 0);
                _tile_node_count -= 1;
            }

            node.children = std::nullopt;
        }
    }

    void reload_attribute_values(
        TileId tile_id,
        TileNode& node,
        JobScheduler* js,
        bool reload_children)
    {
        if (node.content.has_value())
        {
            TileContent& content = node.content.value();

            _vector_data_channel.send(vector_data::messages::RequestData{
                make_data_request_id(tile_id, vector_data::DataKind::AttributeValues), 0,
                content.coords, vector_data::DataKind::AttributeValues});

            for (const auto& repr : _reprs_configs)
            {
                content.reschedule_styling(_styling_jobs_to_cancel, repr.id, repr.id);
            }

            content.attributes.status = TileContent::Attributes::Status::Loading;

            _loading_tiles.insert(tile_id);
        }

        if (reload_children && node.children.has_value())
        {
            for (auto child_tile_id : node.children.value())
            {
                TileNode* child_node = _tile_node_pool.get_object(child_tile_id);
                assert(child_node);
                reload_attribute_values(child_tile_id, *child_node, js, true);
            }
        }
    }

    void create_tile_content_and_start_loading(TileId tile_id, TileNode& node)
    {
        if (node.content.has_value())
        {
            return;
        }

        node.content = {TileContent{}};
        TileContent& content = node.content.value();

        content.coords = node.coords;

        content.feature_ids.status = TileContent::FeatureIds::Status::Loading;
        _vector_data_channel.send(vector_data::messages::RequestData{
            make_data_request_id(tile_id, vector_data::DataKind::FeatureIds), 0, node.coords,
            vector_data::DataKind::FeatureIds});

        content.geometry.status = TileContent::Geometry::Status::Loading;
        _vector_data_channel.send(vector_data::messages::RequestData{
            make_data_request_id(tile_id, vector_data::DataKind::Geometry), 0, node.coords,
            vector_data::DataKind::Geometry});

        content.attributes.status = TileContent::Attributes::Status::Loading;
        _vector_data_channel.send(vector_data::messages::RequestData{
            make_data_request_id(tile_id, vector_data::DataKind::AttributeValues), 0, node.coords,
            vector_data::DataKind::AttributeValues});

        content.load_status = TileContent::LoadStatus::Loading;
        content.display_status = TileContent::DisplayStatus::Loading;

        content.reprs.resize(_reprs_configs.size());

        content.style_job.status = TileContent::StyleJob::Status::Idle;
        for (const auto& repr : _reprs_configs)
        {
            content.schedule_styling(_styling_jobs_to_cancel, repr.id);
        }

        _tiles_by_coords.insert(std::make_pair(node.coords, tile_id));
        _loading_tiles.insert(tile_id);
    }

    std::pair<SceneViewBitset, SceneViewBitset> traverse_tile_node_for_visibility_set(
        TileId tile_id,
        std::span<const VectorTilesCuller> view_cullers,
        VisibilitySetBuilder& visibility_set,
        SceneViewBitset visiting_in,
        SceneViewBitset refining_in,
        bool has_fallback,
        bool has_visible_fallback,
        BlobAllocator* ba,
        JobScheduler* js)
    {
        assert(visiting_in.any());
        assert((refining_in & visiting_in) == refining_in);

        assert(_tile_node_pool.is_valid(tile_id));
        TileNode& node = *_tile_node_pool.get_object(tile_id);

        const auto starting_visibility_set = visibility_set.current_state();
        SceneViewBitset visible_in = visiting_in;
        SceneViewBitset good_state_in;
        SceneViewBitset error_in;
        bool content_needed = false;
        SceneViewBitset draw_in;

        // Early discard of the tile if it doesn't intersect the dataset or the
        // frusta.

        if (_model_status != ModelStatus::Loaded
            || !hrz::intersect(_layer_bounds, node.bounds.to_flat())
            || !hrz::intersect(_data_bounds, node.bounds.to_flat())
            || node.coords.lod > _layer_max_lod || node.coords.lod > _data_max_lod)
        {
            good_state_in = visiting_in;
            visible_in.reset();
            refining_in.reset();
        }
        else
        {
            for (const auto& view_culler : view_cullers)
            {
                int view_index = (int)view_culler.view;
                if (!visible_in.is_set(view_index)) continue;

                bool occluded_by_horizon = false;
                if (node.horizon_occlusion_point.has_value())
                {
                    occluded_by_horizon = view_culler.horizon_culler.is_occluded(
                        node.horizon_occlusion_point.value());
                }

                if (occluded_by_horizon
                    || !view_culler.frustum_culler.intersects(
                        node.bsphere_ecef.center, node.bsphere_ecef.radius))
                {
                    good_state_in.set(view_index);
                    visible_in.reset(view_index);
                    refining_in.reset(view_index);
                }
            }
        }

        double now = hrz::clock::CurrentFrameRealTime.ms;

        // Stop refining the tiles that have reached their screen space error
        // threshold, or when we have reached the end of the dataset.
        for (const auto& view_culler : view_cullers)
        {
            int view_index = (int)view_culler.view;

            if (!refining_in.is_set(view_index)) continue;

            const double distance = hrz::distance_to_wgs84_bbox(view_culler.cam_pos, node.bounds);
            double screen_space_error = view_culler.compute_tile_screen_space_error(
                node.width, distance, _max_screen_space_error);

            bool should_not_refine_more = screen_space_error <= MAX_SCREEN_SPACE_ERROR;
            bool is_last_lod = node.coords.lod + 1 > std::min(_layer_max_lod, _data_max_lod);

            if ((should_not_refine_more || is_last_lod)
                && node.coords.lod >= std::max(_layer_min_lod, _data_min_lod))
            {
                // This is the LOD we want
                content_needed = true;

                // We may visit children to find fallback tiles, but we don't want
                // to start loading any tile below.
                refining_in.reset(view_index);
            }
        }

        if (node.content.has_value())
        {
            const TileContent& content = node.content.value();

            if ((content.geometry.status == TileContent::Geometry::Status::Clamping
                 || content.geometry.status == TileContent::Geometry::Status::Ready)
                && content.geometry.repr.geometry.has_full_detail)
            {
                // No need to go further down the tree.
                refining_in.reset();
            }
        }

        if (refining_in.any() && !node.children.has_value()
            && _tile_node_count < MAX_TILE_NODE_COUNT)
        {
            const hrz::TileCoords next = {
                2 * node.coords.x, 2 * node.coords.y, (uint8_t)(node.coords.lod + 1)};
            node.children = {std::array<TileId, 4>{
                _tile_node_pool.alloc(TileNode({next.x, next.y, next.lod}, node.elevation)),
                _tile_node_pool.alloc(TileNode({next.x + 1, next.y, next.lod}, node.elevation)),
                _tile_node_pool.alloc(TileNode({next.x, next.y + 1, next.lod}, node.elevation)),
                _tile_node_pool.alloc(TileNode({next.x + 1, next.y + 1, next.lod}, node.elevation)),
            }};
            _tile_node_count += 4;
            _any_node_lacking_elevation = true;

            if (!_has_warned_about_too_many_tiles && _tile_node_count >= MAX_TILE_NODE_COUNT)
            {
                HRZ_LOG_WARNING(
                    "Too many vector tiles loaded for layer {}. Blocking refinement. "
                    "This may be due to a too low max screen-space error value "
                    "(current value: {}).",
                    _global_layer_id, _max_screen_space_error);
                _has_warned_about_too_many_tiles = true;
            }
        }

        bool this_is_drawable = false;
        if (node.content.has_value())
        {
            const TileContent& content = node.content.value();

            if (content.display_status == TileContent::DisplayStatus::Displayable)
            {
                this_is_drawable = true;
            }
            else if (
                content.display_status == TileContent::DisplayStatus::Error
                && _missing_tile_policy == hrz_proto::MissingTilePolicy::USE_EMPTY_TILE)
            {
                this_is_drawable = true;
            }

            if (content.display_status == TileContent::DisplayStatus::Displayable
                || content.display_status == TileContent::DisplayStatus::Error)
            {
                has_fallback = true;
            }

            if (content.display_status == TileContent::DisplayStatus::Error)
            {
                error_in = visiting_in;
            }
        }

        // If we don't need to go deeper and we can draw this tile, draw it.
        if (this_is_drawable)
        {
            SceneViewBitset draw_this_in = (!refining_in) & visible_in;
            draw_in |= draw_this_in;
            good_state_in |= draw_this_in;
            has_visible_fallback = true;
        }

        // If we do need to go deeper or we have no parent tile to fall back on to,
        // look for drawable tiles in children.
        if (node.children.has_value())
        {
            SceneViewBitset should_go_deeper_in =
                (refining_in | SceneViewBitset(!has_fallback)) & visible_in;

            if (should_go_deeper_in.any())
            {
                SceneViewBitset children_are_ok_in = should_go_deeper_in;
                SceneViewBitset children_error_in;

                for (auto child_tile_id : node.children.value())
                {
                    auto res = traverse_tile_node_for_visibility_set(
                        child_tile_id, view_cullers, visibility_set, should_go_deeper_in,
                        refining_in & should_go_deeper_in, has_fallback, has_visible_fallback, ba,
                        js);
                    children_are_ok_in &= res.first;
                    children_error_in |= res.second;
                }

                node.last_time_children_visited_ms = now;

                // Draw the children if they are all drawable or we have no fallback in
                // parent tiles.
                good_state_in |= children_are_ok_in;

                if (children_error_in.any()
                    && _missing_tile_policy == hrz_proto::MissingTilePolicy::USE_LOWER_RESOLUTION
                    && visible_in.any()
                    && node.coords.lod >= std::max(_layer_min_lod, _data_min_lod))
                {
                    // We cannot display any of the children, but we can try to display this tile.
                    content_needed = true;
                    good_state_in &= !children_error_in;
                }
            }
        }

        // If we still have not found a good set of children tiles to draw and we
        // can draw this tile, draw it.
        SceneViewBitset fallback_in =
            SceneViewBitset(this_is_drawable) & (!good_state_in) & visiting_in;
        if (fallback_in.any())
        {
            // Erase the children from the draw list, in case they had been added.
            visibility_set.rollback(starting_visibility_set, fallback_in);
            draw_in |= fallback_in;
            good_state_in |= fallback_in;
        }

        if (draw_in.any())
        {
            TileContent& content = node.content.value();
            for (auto& repr : content.reprs)
            {
                if (repr.displayed.has_value())
                {
                    if (_reprs_not_scheduling_instantly.contains(repr.displayed->repr.type))
                    {
                        visibility_set.can_be_scheduled_immediately = false;
                    }

                    repr.displayed->last_used_in_visibility_set = {visibility_set.id};
                    visibility_set.add_repr(
                        repr.displayed->repr.type, repr.displayed->repr.id, draw_in);
                }
            }

            std::optional<VisibilitySet::Tile::DebugInfo> debug_info = std::nullopt;
            if (visibility_set.include_debug_info)
            {
                debug_info = {VisibilitySet::Tile::DebugInfo{}};
                debug_info->coords = node.coords;
                debug_info->min_elevation = node.elevation.min;
                debug_info->max_elevation = node.elevation.max;
                debug_info->elevation_source = node.elevation.source;
                debug_info->horizon_occlusion_point = node.horizon_occlusion_point;
            }

            visibility_set.add_tile(
                draw_in, {content.geometry.attribution, content.attributes.attribution},
                debug_info);
            node.last_used_in_visibility_set = visibility_set.id;
        }

        if (content_needed)
        {
            node.last_time_content_needed_ms = now;

            if (!node.content.has_value())
            {
                if ((now - node.last_time_content_not_needed_ms) > CONTENT_LOAD_DELAY_MS)
                {
                    _nodes_to_start_loading.push_back(tile_id);
                }
                else
                {
                    _any_tile_content_waiting_load_delay = true;
                }
            }
        }
        else
        {
            node.last_time_content_not_needed_ms = now;
        }

        {
            bool prune_children = false;
            bool prune_content = false;

            // Purge children nodes that are not needed anymore.
            if (node.children.has_value()
                && (now - node.last_time_children_visited_ms) > CONTENT_PRUNE_DELAY_MS)
            {
                prune_children = true;
            }

            // Purge this content if it's not being drawn
            if (draw_in.none() && node.content.has_value()
                && (now - node.last_time_content_needed_ms) > CONTENT_PRUNE_DELAY_MS)
            {
                prune_content = true;
            }

            if ((prune_children || prune_content)
                && node.last_used_in_visibility_set <= _last_destroyed_visibility_set_id)
            {
                clean_tile_node(tile_id, node, ba, js, prune_children, prune_content, false);
            }
        }

        return {good_state_in, error_in};
    }
};

VectorTilesActorChannel spawn_vector_tiles_actor(
    uint64_t global_layer_id,
    uint32_t vector_data_layer,
    hrz_proto::MissingTilePolicy missing_tile_policy,
    bool static_tiles,
    uint32_t object_reference_layer_id_partial,
    PlanetSurface* planet,
    VectorDataLoader* vdl,
    ActorRunner* ar)
{
    auto [from_actor_channel, to_actor_channel] =
        hrz::create_channel<FromActorMessage, ToActorMessage>();

    auto actor = std::make_unique<VectorTilesActor>(
        global_layer_id, vector_data_layer, missing_tile_policy, static_tiles,
        object_reference_layer_id_partial, std::move(from_actor_channel), planet, vdl);

    actor_runner::add_actor(ar, std::move(actor));

    return std::move(to_actor_channel);
}
} // namespace hrz::vt
