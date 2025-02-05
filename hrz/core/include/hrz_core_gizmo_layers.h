#pragma once

#include "hrz_core_render_request.h"
#include "hrz_core_scene_model.h"
#include "hrz_core_scene_path.h"
#include "hrz_core_scene_view.h"

#include <cstdint>

namespace hrz
{
struct CameraViewInfo;
struct ClientMessageQueue;
struct PickingIdAllocator;
struct Render;
struct RenderViewInfo;
struct DebugDraw;
struct ViewportEvent;

/**
 * This system is responsible for drawing gizmo layers.
 */
struct GizmoLayerSystem;

namespace gizmo_layers
{
/**
 * Create the gizmo layer system.
 */
GizmoLayerSystem* create_system(PickingIdAllocator*);

/**
 * Destroy the gizmo layer system.
 */
void destroy_system(GizmoLayerSystem*, Render*, SceneModel*, PickingIdAllocator*);

void initialize_rendering(GizmoLayerSystem*, Render*);

/**
 * Register a gizmo layer in the system.
 */
void register_layer(GizmoLayerSystem*, SceneModel*, uint64_t layer_id);

/**
 * Remove a gizmo layer from the system. The layer will be removed on the next
 * call to the work function.
 */
void unregister_layer(GizmoLayerSystem*, uint64_t layer_id);

/**
 * Notify that the layer with the given id has changed.
 * Appropriate actions will be taken during the next call to the work function.
 */
void notify_model_update(
    GizmoLayerSystem*,
    uint64_t layer_id,
    scene_model::UpdateType update_type,
    const scene_model::GizmoLayerPath& path);

/**
 * Update the gizmo layers (creation, deletion, etc).
 * This should be called once per frame.
 */
RenderRequest work(
    GizmoLayerSystem*,
    SceneModel*,
    ClientMessageQueue*,
    hrz_proto::SceneViewIndex main_view_index,
    gsl::span<const RenderViewInfo> view_infos);

/**
 * This function should be called once per frame.
 */
void work_gpu(GizmoLayerSystem*, Render*);

/**
 * Draw the gizmos.
 */
void draw(GizmoLayerSystem*, Render*, gsl::span<const RenderViewInfo> view_infos);

/**
 * Handle events.
 */
bool handle_event(
    GizmoLayerSystem*,
    const ViewportEvent& event,
    hrz_proto::SceneViewIndex view_index,
    const hrz::CameraViewInfo& view_info);

} // namespace gizmo_layers
} // namespace hrz
