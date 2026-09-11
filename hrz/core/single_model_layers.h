// SPDX-FileCopyrightText: Copyright 2019 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/common/picking_types.h"
#include "hrz/core/render_request.h"
#include "hrz/core/scene_model.h"
#include "hrz/core/scene_path/layer/single_model_layer_paths.h"
#include "hrz/protocol/identification/object_reference.pb.h"
#include "hrz/protocol/identification/picking_result.pb.h"

#include <lin_maths.h>

#include <cstdint>
#include <optional>
#include <span>

namespace hrz
{

struct AssetsLoader;
struct BlobAllocator;
struct ImageDecoder;
struct JobScheduler;
struct PickingIdAllocator;
struct SceneModel;
struct Render;
struct RenderViewInfo;
struct SelectionSystem;
struct AttributionRegistry;
struct PlanetSurface;

/**
 * This system is responsible for drawing single model layers.
 */
struct SingleModelLayerSystem;

namespace single_model_layers
{

/**
 * Create a single model layer system.
 */
SingleModelLayerSystem* create_system(PickingIdAllocator*);

/**
 * Delete the given single layer system, as well as the model instances.
 */
void destroy_system(
    SingleModelLayerSystem*,
    Render*,
    AssetsLoader*,
    JobScheduler*,
    BlobAllocator*,
    PickingIdAllocator*,
    SceneModel*,
    PlanetSurface*);

/**
 * Initialise GPU resources.
 */
void initialize_rendering(SingleModelLayerSystem*, Render*);

/**
 * Registers a given layer in this system.
 */
void register_layer(SingleModelLayerSystem*, SceneModel*, uint64_t layer_id);

/**
 * Unregisters a layer from this system. It will be actually
 * removed at the next call of the work function.
 */
void unregister_layer(SingleModelLayerSystem*, uint64_t layer_id);

/**
 * Use to indicate that the layer with the given id has been modified.
 * The needed actions will be undertaken during the next call to `work()`.
 */
void notify_model_update(
    SingleModelLayerSystem*,
    uint64_t layer_id,
    scene_model::UpdateType update_type,
    const scene_model::SingleModelLayerPath& path);

/**
 * Use to indicate that a picking event has occured.
 * The single model layer system is responsible for checking whether
 * or not the picked object is one of its own, via the `system_id`
 * parameter and setting the fields in the pick result message.
 */
void pick(SingleModelLayerSystem*, const picking::PositionResult& result, hrz_proto::PickResults&);

std::pair<size_t, size_t> make_typed_object_references(
    SingleModelLayerSystem*,
    std::span<const picking::ObjectReference> objs,
    std::span<hrz_proto::TypedObjectReference> output);

std::optional<picking::FeatureReference> make_feature_reference(
    SingleModelLayerSystem*,
    const picking::ObjectReference& obj);
/**
 * Update the single model layers.
 * This loads or delete model instances, updates their position, etc.
 * This function should be called once per frame.
 */
RenderRequest work(
    SingleModelLayerSystem*,
    SceneModel*,
    AssetsLoader*,
    JobScheduler*,
    BlobAllocator*,
    ImageDecoder*,
    const SelectionSystem*,
    AttributionRegistry*,
    PlanetSurface*,
    std::span<const RenderViewInfo> views_info);

RenderRequest work_gpu(SingleModelLayerSystem*, Render*, BlobAllocator*);

void draw(SingleModelLayerSystem*, Render*, AttributionRegistry*);

bool is_working(SingleModelLayerSystem*);

} // namespace single_model_layers

} // namespace hrz
