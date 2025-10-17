#pragma once

#include "hrz_core_render_request.h"
#include "hrz_core_scene_view.h"

#include <hrz_common_geo.h>
#include <hrz_common_layers.h>
#include <hrz_fnd_flat_hash_map.h>
#include <hrz_fnd_static_vector.h>
#include <hrz_protocol_all.h>

#include <lin_maths.h>
#include <mycelium_backend.h>

#include <cstdint>
#include <optional>
#include <span>

namespace hrz
{
/**
 * This structure contains the definition of a scene, namely
 * what layers it comprises and how these layers are parametrised.
 */
struct Scene;

struct SceneModel;
struct AssetsLoader;
struct BlobAllocator;
struct FontRasterizer;
struct ImageDecoder;
struct JobScheduler;
struct ClientMessageQueue;
struct PickingIdAllocator;
struct PlatformInfo;
struct PlanetSurface;
struct VectorDataLoader;
struct VectorTilesLayerSystem;
struct PickingSystem;
struct VectorFlatOverlaySystem;
struct ShapeEditor;
struct FrameUniformData;
struct GpuResourceContext;
struct DebugDrawSystem;
struct Event;
struct AttributionRegistry;
struct ActorRunner;

namespace scene
{
struct PositionPickingTicket
{
    uint64_t ticket;
    hrz_proto::SceneViewIndex scene_view;
};

struct AreaPickingTicket
{
    uint64_t ticket;
    hrz_proto::SceneViewIndex scene_view;
};

struct RasterDataFetchTicket
{
    uint64_t ticket;
};

/**
 * Instantiate a new scene.
 */
Scene* create(
    uint32_t imagery_merge_group_count,
    uint32_t raster_atlas_size,
    bool compress_atlas_textures,
    uint32_t default_raster_provider_tile_cache_size,
    uint32_t flat_overlay_cascade_count,
    uint32_t flat_overlay_texture_size,
    uint32_t shadow_map_cascade_count,
    lm::uvec2 canvas_size,
    float device_pixel_ratio,
    const PlatformInfo& platform_info,
    const my::Instance::Info& my_instance_info,
    AssetsLoader*);

/**
 * Delete a scene, including the scenes it contains.
 */
void destroy(
    Scene*,
    AssetsLoader*,
    BlobAllocator*,
    JobScheduler*,
    FontRasterizer*,
    my::Instance*,
    GpuResourceContext*);

void initialize_rendering(Scene*, my::Instance*, GpuResourceContext*);

StaticVector<std::pair<my::ResourceHandle, lm::ibbox2>, SCENE_VIEW_COUNT> get_color_outputs(Scene*);

bool handle_event(Scene*, const Event&, float device_pixel_ratio);

void work_start_frame(
    Scene*,
    AssetsLoader*,
    BlobAllocator*,
    JobScheduler*,
    my::Instance*,
    GpuResourceContext*);

void work(
    Scene*,
    AssetsLoader*,
    BlobAllocator*,
    JobScheduler*,
    ImageDecoder*,
    FontRasterizer*,
    ClientMessageQueue*,
    ActorRunner*);

void work_gpu(Scene*, my::Instance*, GpuResourceContext*, BlobAllocator*);

void draw(Scene*, my::Instance*, GpuResourceContext*);

/**
 * Create a new layer of the given type and return its id.
 * The layer is initialised with its default values.
 */
uint64_t create_layer(
    Scene* scene,
    hrz_proto::LayerType type,
    std::string_view name,
    ActorRunner* ar);

/**
 * Renames the layer with the given id.
 */
void rename_layer(Scene* scene, uint64_t layer_id, std::string_view name);

/**
 * Delete the layer with the given id.
 */
void destroy_layer(Scene* scene, uint64_t layer_id);

/**
 * Returns true if the layer with the given id exists, false
 * otherwise.
 */
bool is_layer_valid(const Scene* scene, uint64_t layer_id);

/**
 * Get the type of the layer with the given id.
 */
hrz_proto::LayerType get_layer_type(const Scene* scene, uint64_t layer_id);

/**
 * Fills the layer object with the information of the layer with the given id if it exists.
 */
void retrieve_layer_info(const Scene* scene, uint64_t layer_id, ::hrz_proto::Layer&);

/**
 * Fills the layer array with all the layers currently in the scene.
 */
void retrieve_all_layers(const Scene* scene, ::hrz_proto::LayerArray&);

/**
 * Retrieves raw data from the scene model.
 */
std::string get_model(const Scene*, const hrz_proto::Path&);

/**
 * Sets raw data in the scene model.
 */
void set_model(Scene*, const hrz_proto::Path&, std::string_view payload);

/**
 * Adds raw data to a repeated field in the scene model.
 * Returns the new number of elements in the field.
 */
uint32_t add_model(Scene*, const hrz_proto::Path&, std::string_view payload);

/**
 * Removes an element from a repeated field in the scene model.
 * Returns the new number of elements in the field.
 */
uint32_t remove_model(Scene*, const hrz_proto::Path&);

/**
 * Returns the number of elements in a repeated field in the scene model.
 */
uint32_t count_model(const Scene*, const hrz_proto::Path&);

// Implementation of the scene dump service.
void dump_scene(
    const Scene*,
    const hrz_proto::SceneDumpRequest& input,
    hrz_proto::SceneDump& output);
void load_scene_dump(
    Scene*,
    const hrz_proto::SceneLoadRequest& input,
    hrz_proto::LayerArray& output,
    ActorRunner* ar);

/**
 * Schedules picking for a single mouse position.
 */
std::optional<PositionPickingTicket> schedule_pick(
    Scene*,
    lm::ivec2 mouse_position,
    std::span<const hrz_proto::LayerHandle> included_rasters = {});

/**
 * Schedules picking for a rectangle area.
 */
std::optional<AreaPickingTicket> schedule_pick(Scene*, lm::ibbox2 rect);

/**
 * Schedules a data fetch for a single world position from the given raster layers.
 */
RasterDataFetchTicket schedule_raster_data_fetch(
    Scene*,
    const GeoPosition2& position,
    std::span<const hrz_proto::LayerHandle> raster_layers);

/**
 * Retrieves limited information about a position pick.
 * Returns wether the result was ready or not.
 */
bool retrieve_mouse_hover_info(
    Scene*,
    PositionPickingTicket,
    hrz_proto::MouseHoverInfoMessage&,
    picking::ObjectReference* obj_ref);

/**
 * Retrieves full information about a position pick.
 * Returns wether the result was ready or not.
 */
bool retrieve_pick_results(Scene*, PositionPickingTicket, hrz_proto::PickResults&);

/**
 * Retrieves the list of objects in an area pick.
 * Returns weather the result was ready or not.
 */
bool retrieve_pick_area_result(
    Scene*,
    AreaPickingTicket,
    std::vector<hrz_proto::TypedObjectReference>&);

/**
 * Retrieves full information about a raster data fetch.
 * Returns whether the result was ready or not.
 */
bool retrieve_raster_data_fetch_results(
    Scene*,
    RasterDataFetchTicket,
    std::vector<hrz_proto::PickLayerResult>&);

/**
 * Selects features from a given layer from the scene.
 * Returns the number of features currently selected.
 */
size_t select(
    Scene*,
    uint64_t scene_layer_id,
    std::span<const vector_data::FeatureIdHash> feature_id_hashes);

/**
 * Deselects features from a given layer from the scene.
 * Returns the number of features currently selected.
 */
size_t deselect(
    Scene*,
    uint64_t scene_layer_id,
    std::span<const vector_data::FeatureIdHash> feature_id_hashes);

/**
 * Deselect all currently selected features from the scene.
 */
void deselect_all(Scene*);

/**
 * Turns a picking object reference into a feature reference, that
 * can be used for example for the quick highlight.
 */
std::optional<picking::FeatureReference> make_feature_reference(
    Scene*,
    const picking::ObjectReference&);

/**
 * Sets the quick highlight picking ID.
 */
void quick_highlight(Scene*, const picking::FeatureReference&);

/**
 * Return the currently requested render types, which are reset
 * in `work_start_frame()`.
 */
RenderRequest get_render_request(Scene*);

/**
 * Get the scene status.
 */
bool is_working(Scene*);

VectorDataLoader* get_vector_data_loader(Scene*);

VectorTilesLayerSystem* get_vector_tiles_layers(Scene*);

hrz_proto::ICameraService* get_camera_service(Scene* scene);

ShapeEditor* get_shape_editor(Scene* scene);

PlanetSurface* get_planet(Scene*);

DebugDrawSystem* get_debug_draw(Scene*);

LayersInfo* get_layers_info(Scene*);

CameraViewInfo get_main_camera_view_info(const Scene*);

SceneModel* get_model(Scene*);

AttributionRegistry* get_attribution_registry(Scene*);

// Only sends the message if current hash is different from last.
// Also sets last hash to the new one.
void enqueue_attribution_message(
    Scene*,
    ClientMessageQueue*,
    std::optional<hrz::uint128>* last_hash);

void provide_client_vector_data(Scene*, const hrz_proto::VectorDataRequestResponse&);
void invalidate_client_vector_data(Scene*, const hrz_proto::VectorDataInvalidation&);
} // namespace scene
} // namespace hrz
