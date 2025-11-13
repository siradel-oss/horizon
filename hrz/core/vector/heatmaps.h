#pragma once

#include "hrz/common/palette.h"
#include "hrz/core/render_request.h"
#include "hrz/core/scene_model.h"
#include "hrz/core/scene_path/scene_path.h"
#include "hrz/core/vector/flat_overlay.h"

namespace hrz
{
struct HeatmapSystem;
struct HeatmapReprRegistry;
struct OverlayCamerasInfo;
struct Render;
struct RenderView;
struct CameraHeightSystem;
struct SceneViewRenderGraphUserData;

namespace heatmaps
{
enum
{
    UboHeatmapPoints = hrz::vector_flat_overlay::UboVectorOverlayPass
};

using ReprId = uint32_t;

struct OverlayConfig
{
    Palette palette;
    uint32_t z_index;
};

struct RenderUserData
{
    ReprId repr_id;
    const SceneViewRenderGraphUserData* scene_view_data;
};

struct VectorTilesInfo
{
    uint32_t repr_id;
    uint32_t repr_scene_views;
    uint64_t layer_id;
};

/**
 * Creates a representation registry.
 * It is used as a bridge between the heatmap representations (which live in the scene
 * model) and the heatmap systems (which are tied to one scene view each).
 */
HeatmapReprRegistry* create_repr_registry();

/**
 * Destroys a representation registry.
 */
void destroy_repr_registry(HeatmapReprRegistry*);

/**
 * Registers a heatmap representation in the registry that the systems should track and
 * allocate (or recycle) resources for.
 */
ReprId register_repr(HeatmapReprRegistry*, const VectorTilesInfo&, const OverlayConfig& config);

/**
 * Unregisters a heatmap representation. The associated resources will be discarded, or recycled
 * if new representations are registered before the next call to `work`.
 */
void unregister_repr(HeatmapReprRegistry*, ReprId id);

/**
 * Returns information about a representation's vector tiles layer.
 */
const VectorTilesInfo& get_repr_vector_tiles_info(HeatmapReprRegistry*, ReprId id);

/**
 * Schedule the render heatmap representations defined in the given vector tiles layer.
 */
void make_layer_visible(HeatmapReprRegistry*, uint64_t layer_id);

/**
 * Clears the list of vector tiles layers whose heatmap representations should be drawn.
 */
void hide_all_layers(HeatmapReprRegistry*);

/**
 * Creates a heatmap system tied to a unique scene view.
 * It manages the resources of all heatamp representations for its associated scene view.
 */
HeatmapSystem* create_system(uint32_t scene_view_index);

/**
 * Destroys a heatmap system.
 */
void destroy_system(HeatmapSystem*, Render*);

/**
 * Initializes the heatmap system's resources.
 */
void initialize_rendering(HeatmapSystem*, size_t texture_size, CameraHeightSystem*, Render*);

/**
 * Returns the heatmap system's scene graph output.
 * It is a dummy target used for scheduling, which is never rendered to.
 */
const char* get_output_target_name(const HeatmapSystem*);

/**
 * Add the heatmap system's render pass to the render graph.
 */
void add_passes_to_render_graph(HeatmapSystem*, RenderView*);

/**
 * Updates the heatmap system using the representations defined in the given registry.
 * Should be called once per frame.
 */
RenderRequest work(HeatmapSystem*, const HeatmapReprRegistry*, const VectorFlatOverlaySystem*);

void work_gpu(HeatmapSystem*, hrz::Render* render);
void draw(HeatmapSystem*, hrz::Render*);

/**
 * Returns the FBOs of all heatmap representations used by the system for rendering, along with
 * the representation ID.
 */
std::vector<std::pair<ReprId, my::ResourceHandle>> get_fbos(HeatmapSystem*);

/**
 * Registers the renderer views used for rendering the heatmap points in this scene view
 * (it does not exactly match any of the views used by the flat overlay system).
 */
void register_views(
    HeatmapSystem*,
    const VectorFlatOverlaySystem*,
    hrz::Render*,
    std::vector<my::Renderer::ViewId>& created_views);

/**
 * Converts an ECEF position to a pixel position on the heatmap points texture (or nullopt if
 * it is out of bounds).
 * It requires information about the camera and texture used for flat overlay rendering.
 */
std::optional<lm::uvec2> world_position_to_heatmap_texture_coordinates(
    lm::dvec3 world_pos,
    const hrz::OverlayCamerasInfo&,
    uint32_t flat_overlay_texture_size);

} // namespace heatmaps
} // namespace hrz
