#pragma once

#include "hrz_core_render_request.h"
#include "hrz_core_scene_model.h"
#include "hrz_core_scene_path.h"

#include <hrz_common_planet.h>
#include <hrz_common_shader_defines.h>
#include <hrz_common_tile_coords.h>
#include <hrz_protocol_all.h>

#include <mycelium_backend.h>
#include <mycelium_render_graph.h>
#include <mycelium_renderer.h>

#include <span>
#include <utility>

namespace hrz
{
struct BlobAllocator;
struct JobScheduler;
struct PlanetGeometry;
struct PickingIdAllocator;
struct Render;
struct RenderView;
struct RenderViewInfo;
struct VectorFlatOverlaySystem;

namespace vtex
{
class ClipmapParams;
}

namespace planet
{
struct GeometryResources
{
    my::UboBinding planet_params;

    my::TextureBinding dtm_indirection;
    my::TextureBinding dtm_atlas;

    my::TextureBinding imagery_indirection[HRZ_S_MAX_IMAGERY_GROUP_COUNT];
    my::TextureBinding imagery_atlas[HRZ_S_MAX_IMAGERY_GROUP_COUNT];
};

struct RequestedTiles
{
    std::span<const planet::RequestedTileCoords> tiles;
    size_t hash;
    bool was_updated;
};

PlanetGeometry* create_geometry();
my::RenderPassId initialize_rendering(PlanetGeometry* geometry, RenderView* render);
void destroy(PlanetGeometry*, JobScheduler*, Render*);

void work(
    PlanetGeometry*,
    BlobAllocator*,
    JobScheduler*,
    SceneModel*,
    const vtex::ClipmapParams*,
    const std::pair<double, double>& dtm_min_max,
    const RenderViewInfo& camera_info,
    double mipmap_bias);

bool is_working(const PlanetGeometry*);

RenderRequest work_gpu(
    PlanetGeometry*,
    Render*,
    my::Renderer::ViewId main_view_id,
    double height_above_terrain);

void draw(PlanetGeometry*, Render*, const GeometryResources&, bool cast_shadows);

// @Todo This should be part of the scene model eventually.
void set_resolution(PlanetGeometry*, double resolution);

void notify_model_update(
    PlanetGeometry*,
    scene_model::UpdateType update_type,
    const scene_model::SceneViewSettingsPath& path);

void request_feedback_render(PlanetGeometry*);

RequestedTiles get_updated_requested_tiles(const PlanetGeometry*);

} // namespace planet
} // namespace hrz
