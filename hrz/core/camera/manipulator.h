#pragma once

#include "hrz/core/camera/camera.h"
#include "hrz/core/camera/controller.h"
#include "hrz/core/camera/types.h"
#include "hrz/core/camera/viewpoint.h"
#include "hrz/core/events.h"

#include <lin_maths.h>

#include <memory>

struct mu_Context;

namespace hrz
{

struct PickingSystem;

namespace camera
{

// For zoom, +1 step = halve the distance, -1 step = double the distance, 0 = do nothing.
static constexpr double kPinchZoomSensitivity = 1.0 / 100.0;     // steps/pixel
static constexpr double kMouseDragZoomSensitivity = 1.0 / 100.0; // steps/pixel
static constexpr double kMouseWheelZoomSensitivity = 0.5;        // steps/increment
static constexpr double kDoubleTapZoomSteps = 1.7;
static constexpr double kRotateAroundTargetSensitivity = 0.004; // rad/pixel

// Manipulator implement a set of behaviour for a camera. They don't do much work apart from
// jungling with controllers, which are the real work horses of the camera system, but they are
// responsible for defining constraints and other overall behaviours of each mode.

class CameraManipulatorContext
{
    friend class CameraManipulator;

    std::vector<PositionPickingTicketWithContext> _picking_to_cancel;
    std::vector<hrz_proto::CameraNotification> _notifications;

public:
    hrz_proto::CameraNotification* add_notification()
    {
        _notifications.emplace_back();
        return &_notifications.back();
    }

    void cancel_picking(PositionPickingTicketWithContext p) { _picking_to_cancel.push_back(p); }
};

// See in hrz_core_camera_controller.h why we need this
hrz_proto::CameraNotification* add_notification(CameraManipulatorContext*);

class CameraManipulator
{
    std::unique_ptr<IGenericCameraController> _controller;

    CameraManipulatorContext _context;

    std::optional<lm::ddual_quat>
        _delayed_pose; // Stores the parameter of initialize_with_pose to call set_pose later

    struct QueuedMovement
    {
        MovementEventType type;
        hrz_proto::CameraMovement movement;
        hrz_proto::SceneViewIndex view;
    };

    std::vector<QueuedMovement> _queued_movements;

protected:
    void actually_cancel_picking(const std::array<PickingSystem*, hrz::SCENE_VIEW_COUNT>&);

    void replace_controller(std::unique_ptr<IGenericCameraController>&& new_controller)
    {
        if (_controller) _controller->on_end(&_context);
        _controller = std::move(new_controller);
        if (_controller) _controller->on_start(&_context);
    }

    IGenericCameraController* generic_controller() { return _controller.get(); }

    CameraManipulatorContext* context() { return &_context; }

    // Implements delayed initialization
    virtual void set_pose(const lm::ddual_quat& pose) = 0;

    // Should be called by work_inner to do work with the controller that is managed by the
    // manipulator base class.
    lm::ddual_quat work_controller(
        double dt,
        double height_above_terrain,
        const std::array<PickingSystem*, hrz::SCENE_VIEW_COUNT>&);

    // Implements the inner logic of the manipulator.
    virtual lm::ddual_quat work_inner(
        double dt,
        float fovy,
        const ViewportInfo& viewport,
        double height_above_terrain,
        const std::array<PickingSystem*, hrz::SCENE_VIEW_COUNT>&,
        const std::function<void(std::span<const hrz_proto::CameraNotification>)>&
            notifications_cb) = 0;

    // Return whether movements can be handled now. If false, movements will be queued and replayed
    // once the initial pose has been provided and the manipulator is ready.
    virtual bool is_ready_to_handle_movements() const = 0;

public:
    virtual ~CameraManipulator() = default;

    virtual void update_energy_half_time(const EnergyHalfTime&);

    virtual void update_terrain_settings(
        double min_height_above_terrain,
        double terrain_collision_inertia);

    virtual bool should_keep_bearing() const { return false; }

    virtual void handle_event(
        const ViewportEvent& e,
        hrz_proto::SceneViewIndex view_index,
        const CameraViewInfo& view) = 0;

    virtual void handle_movement(
        MovementEventType movement_type,
        const hrz_proto::CameraMovement& movement,
        hrz_proto::SceneViewIndex view_index,
        const CameraViewInfo& view) = 0;

    void handle_movement_or_queue(
        MovementEventType movement_type,
        const hrz_proto::CameraMovement& movement,
        hrz_proto::SceneViewIndex view_index,
        const CameraViewInfo& view)
    {
        if (is_ready_to_handle_movements())
        {
            handle_movement(movement_type, movement, view_index, view);
        }
        else
        {
            _queued_movements.push_back({movement_type, movement, view_index});
        }
    }

    // Can only be called once and when the manipulator was initialized with pose = nullopt. Useful
    // for delayed initialization like when using an elevation query.
    void initialize_with_pose(const lm::ddual_quat& pose) { _delayed_pose = pose; }

    lm::ddual_quat work(
        double dt,
        float fovy,
        const ViewportInfo& viewport,
        double height_above_terrain,
        const std::array<PickingSystem*, hrz::SCENE_VIEW_COUNT>&,
        const std::function<void(std::span<const hrz_proto::CameraNotification>)>&
            notifications_cb);

    virtual void reset_north(const hrz_proto::ResetNorthParams&) = 0;

    // Called just before the manipulator is replaced. Used to free resources that belong to other
    // systems.
    virtual void destroy(const std::array<PickingSystem*, hrz::SCENE_VIEW_COUNT>&);

    virtual void dev_ui(mu_Context* ctx) const = 0;
};

// When pose is nullopt, the manipulator waits for a set_pose.
std::unique_ptr<CameraManipulator> create_orbit_manipulator(
    const EnergyHalfTime&,
    std::optional<lm::ddual_quat> pose,
    const hrz::GeoBounds& bounds_limits,
    double max_altitude,
    double min_tilt,
    double max_tilt,
    double min_height_above_terrain,
    double terrain_collision_inertia,
    const hrz_proto::CameraAnimationOptions& correction);

std::unique_ptr<CameraManipulator> create_fixed_position_manipulator(
    const EnergyHalfTime&,
    std::optional<lm::ddual_quat> pose,
    double min_tilt,
    double max_tilt);

std::unique_ptr<CameraManipulator> create_fixed_target_manipulator(
    const EnergyHalfTime&,
    const AngularViewpoint& initial_viewpoint, // Viewpoint without elevation query
    std::optional<lm::ddual_quat> pose,
    double min_tilt,
    double max_tilt,
    double min_distance,
    double max_distance,
    double min_height_above_terrain,
    double terrain_collision_inertia);

} // namespace camera
} // namespace hrz
