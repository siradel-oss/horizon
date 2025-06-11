#pragma once

#include "hrz_core_actor.h"
#include "hrz_core_attribution.h"
#include "hrz_core_channel.h"
#include "hrz_core_render.h"
#include "vector/hrz_core_vector_repr.h"

#include <hrz_common_blob_allocator.h>
#include <hrz_common_blob_array.h>
#include <hrz_common_blob_array_view.h>
#include <hrz_common_geo.h>
#include <hrz_common_horizon_culling.h>
#include <hrz_common_vector_data.h>
#include <hrz_fnd_flat_hash_set.h>
#include <hrz_fnd_inlined_vector.h>
#include <hrz_fnd_static_vector.h>
#include <hrz_protocol_all.h>

#include <lin_maths.h>

#include <optional>
#include <utility>
#include <variant>

namespace hrz
{
struct ActorRunner;
struct BlobAllocator;
struct JobScheduler;
struct PlanetSurface;
struct VectorDataLoader;

namespace style
{
struct Parser;
}

namespace vt
{
enum class ElevationSource
{
    None,
    InferredFromParent,
    InferredFromChild,
    GroundTruth
};

struct VectorTilesCuller
{
    lm::dvec3 cam_pos;
    hrz_proto::SceneViewIndex view;
    render::ScreenSpaceError sse;
    my::FrustumCuller frustum_culler;
    HorizonCuller horizon_culler;

    // See `hrz_core_render.h` for details.
    double compute_tile_screen_space_error(
        double tile_radius,
        double distance_from_camera,
        double max_screen_space_error) const
    {
        double geometric_error = (tile_radius * lm::SQRT2) / 256.0;
        return sse.compute_screen_space_error(
            geometric_error, distance_from_camera, max_screen_space_error);
    }

    bool operator==(const VectorTilesCuller& other) const
    {
        return cam_pos == other.cam_pos && view == other.view && sse == other.sse
            && frustum_culler == other.frustum_culler && horizon_culler == other.horizon_culler;
    }

    bool operator!=(const VectorTilesCuller& other) const { return !(*this == other); }
};

using TileId = uint32_t;

struct VisibilitySet
{
    struct Repr
    {
        hrz_proto::VectorReprType type;
        uint64_t id;
        SceneViewBitset in_views;
    };

    struct Tile
    {
        struct DebugInfo
        {
            TileCoords coords;
            double min_elevation;
            double max_elevation;
            ElevationSource elevation_source;
            std::optional<lm::dvec3> horizon_occlusion_point;
        };

        SceneViewBitset in_views;
        // This is for geometry and attributes attribution.
        std::array<AttributionHandle, 2> attribution;
        std::optional<DebugInfo> debug_info;
    };

    uint64_t id;
    hrz::BlobArray<Repr> reprs;
    hrz::BlobArray<Tile> tiles;
    bool can_be_scheduled_immediately;
};

namespace to_actor
{
struct SetBounds
{
    std::optional<hrz::GeoBounds> bounds;
    std::optional<uint8_t> min_lod;
    std::optional<uint8_t> max_lod;
};

struct SetClamping
{
    hrz_proto::VectorClamping clamping;
};

struct SetAttributes
{
    hrz::InlinedVector<hrz_proto::StylingAttributeRef, 16> attributes;
};

struct SetMaxScreenSpaceError
{
    unsigned int max_screen_space_error;
};

struct SetPalettes
{
    hrz::InlinedVector<hrz_proto::Palette, 4> palettes;
};

struct SetStyleScript
{
    std::string script;
};

struct SetRepresentations
{
    hrz::InlinedVector<hrz_proto::VectorRepr, 8> reprs;
};

struct AddRepresentation
{
    hrz_proto::VectorRepr repr;
};

struct UpdateRepresentation
{
    size_t index;
    hrz_proto::VectorRepr repr;
};

struct RemoveRepresentation
{
    size_t index;
};

struct ReprChannel
{
    hrz_proto::VectorReprType type;
    ReprSystem::Channel channel;
    bool schedules_instantly;
    bool uses_z_coordinates;
};

struct SetSpecialAttributes
{
    std::string anchor_z_attribute_name;
    std::string anchor_angle_attribute_name;
    std::string feature_type_attribute_name;
};

struct SetClipId
{
    int32_t clip_id;
};

struct SetLighting
{
    render::LightingSettings lighting_settings;
};

struct SetRngSeed
{
    uint64_t rng_seed;
};

struct SetVisibility
{
    SceneViewBitset visible_in;
};

struct UpdateSelection
{
    hrz::flat_hash_set<vector_data::FeatureIdHash> selected_features;
};

struct SignalPropertiesRegistered
{
    style::Parser* parser;
};

struct GenerateNewVisibilitySet
{
    hrz::StaticVector<VectorTilesCuller, SCENE_VIEW_COUNT> cullers;
    bool include_debug_info;
};

struct SignalVisibilitySetDestroyed
{
    uint64_t visibility_set_id;
};
} // namespace to_actor

using ToActorMessage = std::variant<
    to_actor::SetBounds,
    to_actor::SetClamping,
    to_actor::SetAttributes,
    to_actor::SetMaxScreenSpaceError,
    to_actor::SetPalettes,
    to_actor::SetStyleScript,
    to_actor::SetRepresentations,
    to_actor::AddRepresentation,
    to_actor::UpdateRepresentation,
    to_actor::RemoveRepresentation,
    to_actor::ReprChannel,
    to_actor::SetSpecialAttributes,
    to_actor::SetClipId,
    to_actor::SetLighting,
    to_actor::SetRngSeed,
    to_actor::SetVisibility,
    to_actor::UpdateSelection,
    to_actor::SignalPropertiesRegistered,
    to_actor::GenerateNewVisibilitySet,
    to_actor::SignalVisibilitySetDestroyed>;

namespace from_actor
{
struct RegisterProperties
{
    style::Parser* parser;
};

struct TileCoords
{
    TileId tile_id;
    hrz::TileCoords tile_coords;
};

struct TileFeatureIds
{
    TileId tile_id;
    hrz::vector_data::FeatureIds feature_ids;
};

struct TileAttributes
{
    TileId tile_id;
    hrz::vector_data::AttributeValues attribute_values;
};

struct TileFeatureAnchors
{
    TileId tile_id;
    hrz::BlobArrayView<lm::dvec3> feature_anchors;
};

struct DiscardTile
{
    TileId tile_id;
    hrz::TileCoords tile_coords;
};

struct RenderRequest
{
    hrz::RenderRequest render_request;
};

struct NewVisibilitySet
{
    VisibilitySet visibility_set;
};
} // namespace from_actor

using FromActorMessage = std::variant<
    from_actor::RegisterProperties,
    from_actor::TileCoords,
    from_actor::TileFeatureIds,
    from_actor::TileAttributes,
    from_actor::TileFeatureAnchors,
    from_actor::DiscardTile,
    from_actor::RenderRequest,
    from_actor::NewVisibilitySet>;

struct VectorTilesActor;
using VectorTilesActorChannel = Channel<ToActorMessage, FromActorMessage>;

VectorTilesActorChannel spawn_vector_tiles_actor(
    uint64_t layer_id,
    uint32_t vector_data_layer,
    hrz_proto::MissingTilePolicy missing_tile_policy,
    bool static_tiles,
    uint32_t picking_id,
    PlanetSurface* planet,
    VectorDataLoader* vdl,
    ActorRunner* ar);
} // namespace vt
} // namespace hrz
