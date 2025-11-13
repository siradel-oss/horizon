#pragma once

#include "hrz/core/render.h"
#include "hrz/core/render_request.h"
#include "hrz/core/scene_model.h"
#include "hrz/core/scene_path/scene_path.h"
#include "hrz/protocol/all.h"

namespace hrz
{
/**
 * This system is responsible for handling clipping plane layers.
 */
struct ClippingPlaneLayerSystem;
struct Render;

struct ClippingPlaneInfo
{
    lm::dmat4 view;
    lm::dvec3 normal;
    lm::vec4 outline_color;
    float outline_distance;
};

namespace clipping_plane_layers
{
/**
 * Create a clipping plane layer system.
 */
ClippingPlaneLayerSystem* create_system();

/**
 * Delete the given single layer system, as well as the model instances.
 */
void destroy_system(ClippingPlaneLayerSystem*, hrz::Render*);

void initialize_rendering(ClippingPlaneLayerSystem*, Render*);

/**
 * Registers a given layer in this system.
 */
void register_layer(ClippingPlaneLayerSystem*, SceneModel*, uint64_t layer_id);

/**
 * Unregisters a layer from this system. It will be actually
 * removed at the next call of the work function.
 */
void unregister_layer(ClippingPlaneLayerSystem*, uint64_t layer_id);

/**
 * Use to indicate that the layer with the given id has been modified.
 * The needed actions will be undertaken during the next call to `work()`.
 */
void notify_update(
    ClippingPlaneLayerSystem*,
    uint64_t layer_id,
    scene_model::UpdateType update_type,
    const scene_model::ClippingPlaneLayerPath& path);

/**
 * Update the clipping plane layers.
 * This updates their position, normal, etc.
 * This function should be called once per frame.
 */
RenderRequest work(ClippingPlaneLayerSystem*, SceneModel*);

void draw(ClippingPlaneLayerSystem*, Render*, std::span<const hrz::RenderViewInfo> views_info);

/**
 * Retrieve all clipping planes info.
 * Information about origin, frame, outline color and distance.
 */
void get_clip_planes_info(
    ClippingPlaneLayerSystem*,
    ClippingPlaneInfo (&cpi)[HRZ_S_MAX_CLIP_PLANES]);
} // namespace clipping_plane_layers

} // namespace hrz
