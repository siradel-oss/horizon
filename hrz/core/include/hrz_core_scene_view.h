#pragma once

#include "camera/hrz_core_camera_types.h"
#include "hrz_core_clipping_plane_layers.h"
#include "hrz_core_events.h"
#include "hrz_core_picking_types.h"
#include "hrz_core_render_request.h"
#include "hrz_core_scene_model.h"
#include "hrz_core_scene_path.h"

#include <hrz_protocol_all.h>

#include <mycelium_backend.h>
#include <mycelium_render_graph.h>
#include <mycelium_renderer.h>

#include <optional>
#include <vector>

namespace hrz
{
struct SceneView;
struct AssetsLoader;
struct JobScheduler;
struct Render;
struct CameraViewInfo;
struct SceneModel;
struct PickingSystem;
struct PlanetGeometry;
struct GizmoLayerSystem;
struct ShapeEditor;
struct PlanetSurface;
struct VectorFlatOverlaySystem;
struct Event;
struct HeatmapReprRegistry;

namespace camera
{
class Camera;
} // namespace camera

namespace planet
{
struct GeometryResources;
}

namespace scene
{

ViewportInfo compute_viewport_info(
    hrz_proto::SceneViewIndex view_index,
    SceneModel* model,
    float device_pixel_ratio,
    lm::uvec2 canvas_size);

SceneView* create_scene_view(
    SceneModel*,
    hrz_proto::SceneViewIndex,
    lm::uvec2 canvas_size,
    float device_pixel_ratio,
    uint32_t flat_overlay_cascade_count,
    uint32_t flat_overlay_texture_size,
    uint32_t shadow_map_cascade_count);

void destroy_scene_view(SceneView*, AssetsLoader*, JobScheduler*, Render*);

void initialize_rendering(SceneView* view, Render* render);

my::ResourceHandle get_color_output(SceneView* view);
lm::ibbox2 get_color_output_viewport_on_canvas(SceneView* view);
lm::ibbox2 get_event_viewport_on_canvas(SceneView* view);

void work_start_frame(SceneView* view, ShapeEditor* shape_editor, Render* render);

RenderRequest work(
    SceneView*,
    SceneModel*,
    const CameraViewInfo&,
    const lm::uvec2& canvas_size,
    float device_pixel_ratio,
    ClippingPlaneInfo[HRZ_S_MAX_CLIP_PLANES],
    PlanetSurface*,
    const HeatmapReprRegistry*,
    picking::FeatureReference quick_highlight_feature_id);

ViewportEvent make_viewport_event(SceneView* view, const Event& event);

void set_canvas_size(
    SceneView* view,
    SceneModel* model,
    lm::uvec2 canvas_size,
    float device_pixel_ratio);

void set_highlight_enabled(SceneView* view, bool enabled);

void register_aux_views(
    SceneView* view,
    Render* render,
    my::Renderer::ViewId main_view_id,
    std::vector<my::Renderer::ViewId>& created_views);

RenderRequest work_gpu(SceneView* view, Render* render);

void draw(
    SceneView* view,
    const RenderRequest& render_request,
    Render* render,
    const planet::GeometryResources& planet_geometry_resources);

void notify_model_update(
    SceneView* view,
    scene_model::UpdateType update_type,
    const scene_model::SceneViewSettingsPath& settings_path);

std::optional<picking::PositionTicket> schedule_pick(
    SceneView*,
    lm::ivec2 mouse_position,
    gsl::span<const hrz_proto::LayerHandle> included_rasters);

std::optional<picking::AreaTicket> schedule_pick(SceneView*, lm::ibbox2 rect);

bool retrieve_picking_result(
    SceneView* view,
    picking::PositionTicket ticket,
    picking::PositionResult* pick_result);

bool retrieve_picking_result(
    SceneView* view,
    picking::AreaTicket ticket,
    std::vector<picking::AreaResult>& pick_result);

PlanetGeometry* get_planet_geometry(SceneView*);
VectorFlatOverlaySystem* get_vector_flat_overlay(SceneView*);
PickingSystem* get_picking_system(SceneView*);
double get_camera_height(SceneView*);
const hrz_proto::ViewScaleAltitude& get_view_scale_altitude(SceneView*);

} // namespace scene
} // namespace hrz
