#pragma once

#include "hrz/common/picking_types.h"
#include "hrz/core/camera/camera.h"
#include "hrz/core/render_request.h"
#include "hrz/core/scene_model.h"
#include "hrz/core/scene_path/layer/editable_shape_layer_paths.h"
#include "hrz/protocol/identification/object_reference.pb.h"
#include "hrz/protocol/identification/picking_result.pb.h"
#include "hrz/protocol/shape_editor/service.pb.h"

#include <lin_maths.h>

#include <optional>

namespace hrz
{
struct ShapeEditor;
struct ClientMessageQueue;
struct PickingIdAllocator;
struct PickingSystem;
struct Render;
struct ViewportEvent;

namespace planet
{
struct GeometryResources;
}

namespace editor
{
/**
 * Instantiate a new shape editor.
 */
ShapeEditor* create_editor(PickingIdAllocator*);

/**
 * Destroy the given shape editor.
 */
void destroy_editor(ShapeEditor*, PickingIdAllocator*, Render*);

/**
 * Initialise GPU resources.
 */
void initialize_rendering(ShapeEditor*, Render*);

/**
 * Register an editable shape layer in this system.
 */
void register_layer(ShapeEditor*, SceneModel*, uint64_t layer_id);

/**
 * Unregister a layer from this system. It will be actually
 * removed at the next call of the ̀work()` function.
 */
void unregister_layer(ShapeEditor*, uint64_t layer_id);

/**
 * Use to indicate that the layer with the given id has been modified.
 * The needed actions will be undertaken during the next call to `work()`.
 */
void notify_model_update(
    ShapeEditor*,
    uint64_t layer_id,
    scene_model::UpdateType,
    const scene_model::EditableShapeLayerPath&);

/**
 * Use to indicate that a picking event has occured.
 * The shape editor is responsible for checking whether
 * or not the picked object is one of its own, via the `system_id`
 * parameter and setting the fields in the pick result message.
 */
void pick(
    const ShapeEditor*,
    const picking::ObjectReference&,
    const lm::dvec3& position,
    hrz_proto::PickResults&);

std::pair<size_t, size_t> make_typed_object_references(
    const ShapeEditor*,
    std::span<const picking::ObjectReference> refs,
    std::span<hrz_proto::TypedObjectReference> output);

/**
 * Update the editable shape layers.
 * This function should be called once per frame.
 */
RenderRequest work(ShapeEditor*, SceneModel*, ClientMessageQueue*);

/**
 * This function should be called once per frame.
 */
RenderRequest work_gpu(ShapeEditor*, Render*);

void draw(ShapeEditor*, Render*, const planet::GeometryResources&);

void work_picking(ShapeEditor*, PickingSystem*, hrz_proto::SceneViewIndex);

bool handle_event(
    ShapeEditor*,
    const ViewportEvent& event,
    hrz_proto::SceneViewIndex view_index,
    const CameraViewInfo& view_info);

std::optional<uint64_t> get_selected_shape(const ShapeEditor*);

void select_shape(ShapeEditor*, std::optional<uint64_t> layer_id);

void lock_shape_selection(ShapeEditor*);

void unlock_shape_selection(ShapeEditor*);

std::optional<uint32_t> get_selected_control_point(const ShapeEditor*);

void select_control_point(ShapeEditor*, std::optional<uint32_t> control_point_index);

hrz_proto::ShapeEditorMode get_current_mode(const ShapeEditor*);

void set_mode(ShapeEditor*, hrz_proto::ShapeEditorMode);

hrz_proto::ShapeInformation get_shape_information(ShapeEditor*, uint64_t layer_id);

void delete_selected_control_point(ShapeEditor*);
} // namespace editor
} // namespace hrz
