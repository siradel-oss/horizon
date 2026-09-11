// SPDX-FileCopyrightText: Copyright 2019 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/common/picking_types.h"
#include "hrz/core/render_request.h"
#include "hrz/core/scene_model.h"
#include "hrz/core/scene_path/layer/three_d_tiles_layer_paths.h"
#include "hrz/protocol/identification/object_reference.pb.h"
#include "hrz/protocol/identification/picking_result.pb.h"

#include <lin_maths.h>

#include <cstdint>
#include <span>

namespace hrz
{

struct AssetsLoader;
struct BlobAllocator;
struct CameraViewInfo;
struct ImageDecoder;
struct JobScheduler;
struct PickingIdAllocator;
struct Render;
struct RenderViewInfo;
struct SelectionSystem;
struct VectorDataLoader;
struct AttributionRegistry;

/**
 * This system is responsible for drawing 3D Tiles layers.
 */
struct ThreeDTilesLayerSystem;

namespace three_d_tiles_layers
{

/**
 * Create a 3D Tiles layer system.
 */
ThreeDTilesLayerSystem* create_system(PickingIdAllocator*, VectorDataLoader*);

/**
 * Delete the given 3D Tiles layer system.
 */
void destroy_system(
    ThreeDTilesLayerSystem*,
    AssetsLoader*,
    JobScheduler*,
    BlobAllocator*,
    Render*,
    PickingIdAllocator*,
    SceneModel*);

/**
 * Initialise GPU resources.
 */
void initialize_rendering(ThreeDTilesLayerSystem*, Render*);

/**
 * Register a layer in this system.
 */
void register_layer(ThreeDTilesLayerSystem* system, SceneModel* model, uint64_t layer_id);

/**
 * Unregister a layer from this system. It will be actually
 * removed at the next call of the ̀work()` function.
 */
void unregister_layer(ThreeDTilesLayerSystem* system, uint64_t layer_id);

/**
 * Use to indicate that the layer with the given id has been modified.
 * The needed actions will be undertaken during the next call to `work()`.
 */
void notify_update(
    ThreeDTilesLayerSystem* system,
    uint64_t layer_id,
    scene_model::UpdateType update_type,
    const scene_model::ThreeDTilesLayerPath& path);

/**
 * Update the vector tiles layers.
 * This function should be called once per frame.
 */
RenderRequest work(
    ThreeDTilesLayerSystem* system,
    SceneModel* model,
    const SelectionSystem*,
    AssetsLoader* al,
    JobScheduler* js,
    BlobAllocator* ba,
    ImageDecoder* imgdec,
    AttributionRegistry* attributions,
    std::span<const RenderViewInfo> views_info);

/**
 * Use to indicate that a picking event has occured.
 * The vector tiles layer system is responsible for checking whether
 * or not the picked object is one of its own, via the `system_id`
 * parameter and setting the fields in the pick result message.
 */
void pick(ThreeDTilesLayerSystem* system, const picking::PositionResult&, hrz_proto::PickResults&);

std::pair<size_t, size_t> make_typed_object_references(
    ThreeDTilesLayerSystem* system,
    std::span<const picking::ObjectReference> objs,
    std::span<hrz_proto::TypedObjectReference> output);

/**
 * This transforms a generic picking ID (which points to a 3DTiles batch) into
 * a new one that can identify a single feature across multiple tiles. This is
 * useful for the highlighting system.
 */
std::optional<picking::FeatureReference> make_feature_reference(
    ThreeDTilesLayerSystem* system,
    const picking::ObjectReference& obj);

[[nodiscard]]
RenderRequest work_gpu(ThreeDTilesLayerSystem*, Render*, BlobAllocator*);

void draw(ThreeDTilesLayerSystem*, Render*, AttributionRegistry*);

bool is_working(ThreeDTilesLayerSystem*);

} // namespace three_d_tiles_layers
} // namespace hrz
