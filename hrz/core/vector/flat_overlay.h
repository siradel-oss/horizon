#pragma once

#include "hrz/core/render/resources.h"
#include "hrz/core/render_request.h"

#include <lin_maths.h>
#include <mycelium/renderer.h>

#include <vector>

/*
 * Flat Overlay system allows rendering of a list of vector data layers from a set of concentric
 * orthograpic cameras around the main viewpoint. This result in a collection of render target
 * texture sampled during the planet forward pass to texture the planet accordingly. @Todo
 * @Performance: For now the rendering is done every 150 ms to prevent fps drop when the scene is
 * filled with micro or overstreched polygons.
 */

struct mu_Context;

namespace hrz
{
struct CameraViewInfo;
struct VectorFlatOverlaySystem;
struct CameraHeightSystem;
struct HeatmapSystem;
struct Render;
struct RenderView;

struct OverlayCamerasInfo
{
    int cascade_count = 0;
    lm::dvec3 pos;
    lm::dmat4 view;
    lm::dmat4 view_cc;
    lm::dmat4 proj[HRZ_S_MAX_OVERLAY_CASCADES];
    lm::dmat4 pv_cc[HRZ_S_MAX_OVERLAY_CASCADES];
    float world_sizes[HRZ_S_MAX_OVERLAY_CASCADES];
    double plane_distances[HRZ_S_MAX_OVERLAY_CASCADES + 1];
    lm::dmat4 mvp_inv_main_view[HRZ_S_MAX_OVERLAY_CASCADES];
    lm::dmat4 heatmap_proj;
};

namespace vector_flat_overlay
{
enum
{
    SamplerOverlayStart = SamplerCustomStart,
};

enum
{
    UboVectorOverlayCameras = UboCustomStart,
    UboVectorOverlayPass
};

extern const char* const sampler_names[HRZ_S_MAX_OVERLAY_CASCADES];
extern const char* const picking_sampler_names[HRZ_S_MAX_OVERLAY_CASCADES];
extern const char* const selection_sampler_names[HRZ_S_MAX_OVERLAY_CASCADES];

/*
 * Get flat overlay passes visual render target name.
 */
void get_visual_target_names(
    const VectorFlatOverlaySystem* system,
    const char* target_names[HRZ_S_MAX_OVERLAY_CASCADES]);

/*
 * Get flat overlay passes picking render target name.
 */
void get_picking_target_names(
    const VectorFlatOverlaySystem* system,
    const char* target_names[HRZ_S_MAX_OVERLAY_CASCADES]);

/*
 * Get flat overlay passes selection render target name.
 */
void get_selection_target_names(
    const VectorFlatOverlaySystem* system,
    const char* target_names[HRZ_S_MAX_OVERLAY_CASCADES]);

const OverlayCamerasInfo& get_latest_overlay_cameras_info(const VectorFlatOverlaySystem* system);

/*
 * Add system passes to render graph.
 */
void add_passes_to_render_graph(VectorFlatOverlaySystem* system, hrz::RenderView* render);

/**
 * Creates a vector flat overlay system.
 */
VectorFlatOverlaySystem* create_system(uint32_t cascade_count);
/**
 * Destroy one.
 */
void destroy_system(VectorFlatOverlaySystem* system, Render* render);

void initialize_rendering(
    VectorFlatOverlaySystem* system,
    size_t texture_size,
    Render* render,
    CameraHeightSystem* camera_height_system,
    HeatmapSystem* heatmap_system);
RenderRequest work(VectorFlatOverlaySystem* system, const CameraViewInfo&, lm::vec2 near_far);
void register_views(
    VectorFlatOverlaySystem* system,
    Render* render,
    std::vector<my::Renderer::ViewId>& created_views);
void draw(VectorFlatOverlaySystem* system, Render* render);

void schedule_visual_render(VectorFlatOverlaySystem* system, bool animation);
void schedule_picking_render(VectorFlatOverlaySystem* system);

bool is_working(const VectorFlatOverlaySystem* system);
bool is_about_to_render(const VectorFlatOverlaySystem* system);

void dev_ui(const VectorFlatOverlaySystem* system, mu_Context* ctx);
}; // namespace vector_flat_overlay

} // namespace hrz
