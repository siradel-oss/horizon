#pragma once

#include "hrz/common/blob_array.h"
#include "hrz/common/blob_array_view.h"
#include "hrz/common/geo.h"
#include "hrz/common/picking_types.h"
#include "hrz/core/channel.h"
#include "hrz/core/planet/raster_data_fetch_types.h"
#include "hrz/core/render_request.h"
#include "hrz/core/scene_model.h"
#include "hrz/protocol/identification/object_reference.pb.h"

#include <mycelium/render_graph.h>

#include <optional>
#include <span>
#include <variant>

namespace hrz
{
struct AssetsLoader;
struct BlobAllocator;
struct JobScheduler;
struct PlanetSurface;
struct PlanetGeometry;
struct PlatformInfo;
struct PickingIdAllocator;
struct Render;
struct RenderViewInfo;
struct AttributionRegistry;

namespace scene_model
{
class DtmRasterLayerPath;
class ImageryRasterLayerPath;
class SceneSettingsPath;
} // namespace scene_model

namespace planet
{
struct GeometryResources;
class ElevationQuery;
using ElevationQueryTicket = uint64_t;

PlanetSurface* create_surface(
    uint32_t imagery_merge_group_count,
    uint32_t atlas_size,
    bool compress_atlas_textures,
    uint32_t default_tile_cache_size,
    const PlatformInfo&,
    const my::Instance::Info&,
    PickingIdAllocator*);

void destroy(
    PlanetSurface*,
    AssetsLoader*,
    BlobAllocator*,
    JobScheduler*,
    PickingIdAllocator*,
    Render*);

void initialize_rendering(PlanetSurface*, Render*);

void work(
    PlanetSurface*,
    AssetsLoader*,
    BlobAllocator*,
    JobScheduler*,
    SceneModel*,
    AttributionRegistry*,
    std::span<const RenderViewInfo> views_info,
    std::span<PlanetGeometry*> geometries);

RenderRequest work_gpu(PlanetSurface*, Render*, BlobAllocator*);

void fill_in_geometry_resources(PlanetSurface*, GeometryResources*, const my::Instance::Info&);
uint32_t get_imagery_raster_groups_bitset(PlanetSurface*, hrz_proto::SceneViewIndex);

void use_attributions(PlanetSurface*, AttributionRegistry*);

void register_layer(PlanetSurface*, SceneModel*, uint64_t layer_id, hrz_proto::LayerType);

void unregister_layer(PlanetSurface*, uint64_t layer_id, hrz_proto::LayerType);

void notify_model_update(
    PlanetSurface*,
    uint64_t layer_id,
    scene_model::UpdateType update_type,
    const scene_model::DtmRasterLayerPath& path);

void notify_model_update(
    PlanetSurface*,
    uint64_t layer_id,
    scene_model::UpdateType update_type,
    const scene_model::ImageryRasterLayerPath& path);

void notify_model_update(
    PlanetSurface*,
    scene_model::UpdateType update_type,
    const scene_model::SceneSettingsPath& path);

void pick(
    PlanetSurface*,
    const picking::ObjectReference&,
    const lm::dvec3& position,
    std::span<const hrz_proto::LayerHandle> included_rasters,
    hrz_proto::SceneViewIndex scene_view,
    hrz_proto::PickResults&);

RasterDataFetchTicket schedule_raster_data_fetch(
    PlanetSurface*,
    const GeoPosition2& position,
    std::span<const hrz_proto::LayerHandle> layers);

std::optional<RasterDataFetchResult> retrieve_raster_data_fetch_results(
    PlanetSurface*,
    RasterDataFetchTicket ticket);

std::pair<size_t, size_t> make_typed_object_references(
    PlanetSurface*,
    std::span<const picking::ObjectReference> objs,
    std::span<hrz_proto::TypedObjectReference> output);

/**
 * Returns true if the rasters' configs have been modified
 * since last call.
 */
bool layer_work(PlanetSurface*, SceneModel*, AssetsLoader*, BlobAllocator*, JobScheduler*);

// /!\ Points have to be projected in webmercator.
ElevationQueryTicket query_elevation(
    PlanetSurface*,
    const lm::dvec2& point,
    monitoring::ResourceOwner resource_owner);
ElevationQueryTicket query_elevation(
    PlanetSurface*,
    hrz::BlobArrayView<lm::dvec2> points,
    monitoring::ResourceOwner resource_owner);

void cancel_elevation_query(PlanetSurface*, ElevationQueryTicket);

bool is_elevation_query_ready(const PlanetSurface*, ElevationQueryTicket);

/**
 *  Returns an empty optional when no DTM rasters are defined in the scene.
 *  In this case, all elevations can considered to be 0.
 */
std::optional<hrz::BlobArray<float>> retrieve_elevation_query(PlanetSurface*, ElevationQueryTicket);

ElevationQuery* get_elevation_query(PlanetSurface*);

/**
 * Returns an id that is updated whenever changes are made to the
 * terrain, such as when a DTM layer is added, removed, updated, or
 * hidden.
 */
uint64_t get_terrain_version(const PlanetSurface*);

bool get_tile_elevation_bounds(
    const PlanetSurface*,
    const hrz::TileCoords& coords,
    double* min_elevation,
    double* max_elevation);

bool is_working(const PlanetSurface*, std::span<const PlanetGeometry*> geometries);

namespace surface::messages
{
struct SubscribeToTerrainVersionUpdates
{
    uint64_t subscription_id;
};

struct CancelTerrainVersionUpdatesSubscription
{
    uint64_t subscription_id;
};

struct RequestTileElevationBounds
{
    uint64_t request_id;
    hrz::TileCoords coords;
};
} // namespace surface::messages

using ToPlanetSurfaceMessage = std::variant<
    surface::messages::SubscribeToTerrainVersionUpdates,
    surface::messages::CancelTerrainVersionUpdatesSubscription,
    surface::messages::RequestTileElevationBounds>;

namespace surface::messages
{
struct TerrainVersionUpdate
{
    uint64_t subscription_id;
    uint64_t terrain_version;
};

struct TileElevationBounds
{
    uint64_t request_id;
    std::optional<double> min_elevation;
    std::optional<double> max_elevation;
};
} // namespace surface::messages

using FromPlanetSurfaceMessage =
    std::variant<surface::messages::TerrainVersionUpdate, surface::messages::TileElevationBounds>;

using SurfaceChannel = Channel<ToPlanetSurfaceMessage, FromPlanetSurfaceMessage>;

SurfaceChannel create_surface_channel(PlanetSurface*);
} // namespace planet
} // namespace hrz
