#pragma once

#include "hrz_core_platform_events.h"

#include <mycelium_backend.h>

#include <stdint.h>

namespace hrz
{
struct AssetsLoader;
struct JobScheduler;
struct PlanetSurface;
struct VectorDataLoader;
struct BlobAllocator;
struct DevUi;
class Monitoring;
struct RemoteMonitoring;
struct Scene;
struct PlatformContext;
struct GpuResourceContext;
struct DebugDrawSystem;
struct Event;
struct VectorTilesLayerSystem;

namespace dev_ui
{
/**
 * This is the data that needs to be displayed in the dev UI. It needs to be
 * passed at every frame.
 */
struct Context
{
    AssetsLoader* assets_loader;
    BlobAllocator* blob_allocator;
    JobScheduler* job_scheduler;
    PlanetSurface* planet;
    VectorDataLoader* vector_loader;
    Monitoring* monitoring;
    my::Instance* my_instance;
    Scene* scene;
    PlatformContext* platform;
    RemoteMonitoring* remote_monitoring;
    DebugDrawSystem* debug_draw;
    VectorTilesLayerSystem* vector_tiles_layers;
};

/**
 * Creates the dev UI.
 */
DevUi* create(PlatformContext*);

/**
 * Creates the necessary resources to draw the development UI.
 */
void initialize_rendering(DevUi*, GpuResourceContext*);

/**
 * Destroys the dev UI.
 */
void destroy(DevUi*, GpuResourceContext*);

/**
 * Sends events to the dev UI.
 * Returns true if the even has been consumed and shouldn't be passed to any
 * other system, false otherwise.
 */
bool handle_event(DevUi*, const Event&, float device_pixel_ratio);

/**
 * Updates the dev UI with the given context.
 * This should be called after events traversal, and before rendering.
 */
void update(DevUi*, Context* ctx);

/**
 * Call this function once per frame.
 */
void work_gpu(DevUi*, GpuResourceContext*);

/**
 * Draws the development UI on the current framebuffer & viewport.
 */
void draw(DevUi*, my::RenderContext*, float device_pixel_ratio);

/**
 * Toggles the development UI.
 */
bool toggle(DevUi*);

/**
 * Moves the developement UI to the cursor position.
 */
void move_to_cursor(DevUi*);

} // namespace dev_ui
} // namespace hrz
