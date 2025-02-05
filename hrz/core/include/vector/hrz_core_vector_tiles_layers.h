#pragma once

#include "hrz_core_picking_types.h"
#include "hrz_core_render_request.h"
#include "hrz_core_scene_model.h"
#include "hrz_core_scene_path.h"

#include <hrz_protocol_all.h>

#include <gsl/gsl-lite.hpp>
#include <lin_maths.h>

#include <cstdint>
#include <optional>

namespace hrz
{
struct PlanetSurface;
struct Render;
struct RenderViewInfo;
struct AssetsLoader;
struct BlobAllocator;
struct CameraViewInfo;
struct FontRasterizer;
struct ImageDecoder;
struct JobScheduler;
struct PickingIdAllocator;
struct SceneModel;
struct VectorDataLoader;
struct SelectionSystem;
struct HeatmapReprRegistry;
struct SymbolCullingSystem;
struct AttributionRegistry;
struct ActorRunner;

namespace camera
{
class Camera;
};

/**
 * This system is responsible for drawing vector tiles layers.
 */
struct VectorTilesLayerSystem;

namespace vector_tiles_layers
{
/**
 * Create a vector tiles layer system.
 */
VectorTilesLayerSystem* create_system(PickingIdAllocator*);

/**
 * Delete the given vector tiles layer system.
 */
void destroy_system(
    VectorTilesLayerSystem* system,
    Render* render,
    VectorDataLoader* vdl,
    AssetsLoader* al,
    BlobAllocator* ba,
    JobScheduler* js,
    FontRasterizer* fr,
    SymbolCullingSystem* symbol_culling,
    PickingIdAllocator* picking_id_allocator,
    SceneModel* model,
    PlanetSurface*);

/**
 * Initialise GPU resources.
 */
void initialize_rendering(VectorTilesLayerSystem* system, Render* render);

/**
 * Registers a given layer in this system.
 */
void register_layer(
    VectorTilesLayerSystem* system,
    SceneModel* model,
    hrz::PlanetSurface*,
    VectorDataLoader*,
    ActorRunner*,
    uint64_t layer_id);

/**
 * Unregisters a layer from this system. It will be actually
 * removed at the next call of the work function.
 */
void unregister_layer(VectorTilesLayerSystem* system, uint64_t layer_id);

/**
 * Returns the heatmap representation registry used by the system.
 */
const HeatmapReprRegistry* get_heatmap_repr_registry(VectorTilesLayerSystem* system);

/**
 * Use to indicate that the layer with the given id has been modified.
 * The needed actions will be undertaken during the next call to `work()`.
 */
void notify_update(
    VectorTilesLayerSystem* system,
    uint64_t layer_id,
    scene_model::UpdateType update_type,
    const scene_model::VectorTilesLayerPath& path);

/**
 * Update the vector tiles layers.
 * This function should be called once per frame.
 */
RenderRequest work(
    VectorTilesLayerSystem* system,
    SceneModel* model,
    VectorDataLoader* vector_data_loader,
    AssetsLoader* al,
    BlobAllocator* ba,
    JobScheduler* js,
    ImageDecoder* imgdec,
    FontRasterizer* fr,
    SymbolCullingSystem* symbol_culling,
    AttributionRegistry* attributions,
    ActorRunner* ar,
    gsl::span<const RenderViewInfo> views_info,
    PlanetSurface* planet,
    const SelectionSystem* selection);

/**
 * Use to indicate that a picking event has occured.
 * The vector tiles layer system is responsible for checking whether
 * or not the picked object is one of its own, via the `system_id`
 * parameter and setting the fields in the pick result message.
 */
void pick(
    VectorTilesLayerSystem* system,
    const picking::ObjectReference&,
    gsl::span<const std::pair<uint32_t, float>> heatmap_values,
    hrz_proto::PickResults&);

std::pair<size_t, size_t> make_typed_object_references(
    VectorTilesLayerSystem* system,
    gsl::span<const picking::ObjectReference> objs,
    gsl::span<hrz_proto::TypedObjectReference> output);

/**
 * This transforms a generic picking ID (which points to a particular instance
 * in a tile) into a new one that can identify a single feature across multiple
 * geometries and tiles. This is useful for the highlighting system.
 */
std::optional<picking::FeatureReference> make_feature_picking_id(
    VectorTilesLayerSystem* system,
    const picking::ObjectReference& obj);

/**
 * This function should be called once per frame.
 */
RenderRequest work_gpu(
    VectorTilesLayerSystem* system,
    Render* render,
    BlobAllocator*,
    SymbolCullingSystem*,
    gsl::span<const RenderViewInfo> views_info);

/**
 * Draw the vector tiles layers.
 */
void draw(
    VectorTilesLayerSystem* system,
    Render* render,
    const RenderRequest& render_request,
    gsl::span<const RenderViewInfo> views_info,
    SymbolCullingSystem* symbol_culling,
    AttributionRegistry* attributions);

bool is_working(VectorTilesLayerSystem* system);

} // namespace vector_tiles_layers
} // namespace hrz
