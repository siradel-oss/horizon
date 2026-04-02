#pragma once

#include "hrz/core/camera/types.h"
#include "hrz/core/events.h"
#include "hrz/core/picking_system.h"
#include "hrz/core/scene_view_bitset.h"
#include "hrz/protocol/camera/movement.pb.h"
#include "hrz/protocol/camera/notification.pb.h"

namespace hrz::camera
{

struct PositionPickingTicketWithContext
{
    picking::PositionTicket ticket;
    hrz_proto::SceneViewIndex view;
    // Never access the system by pointer, this is just to check that we are indeed canceling the
    // ticket with the correct system
    uintptr_t system_ptr;
};

enum class MovementEventType
{
    Instant,
    BeginContinuous,
    EndContinuous,
};

// Controllers are responsible for translating events into actions that drivers can
// understand. They are essentially states in a state machine, and this can indicate to the
// manipulator when to switch state by returned a new controller to use instead of themselves.
class CameraManipulatorContext;

// We have to forward-declare this because CameraManipulatorContext is in
// hrz_core_camera_manipulator.h, we would need to include it to access the add_notification method,
// but it would create a circular dependency. So here we are, thank you C++.
hrz_proto::CameraNotification* add_notification(CameraManipulatorContext*);

class IGenericCameraController
{
public:
    virtual ~IGenericCameraController() = default;

    virtual lm::ddual_quat work(
        CameraManipulatorContext* ctx,
        double dt,
        double height_above_terrain,
        bool keep_bearing,
        const std::array<PickingSystem*, hrz::SCENE_VIEW_COUNT>&) = 0;

    virtual void update_energy_half_time(const EnergyHalfTime& e) = 0;

    virtual void update_terrain_settings(
        double min_height_above_terrain,
        double terrain_collision_inertia) = 0;

    virtual void on_start(CameraManipulatorContext*) = 0;

    virtual void on_end(CameraManipulatorContext*) = 0;
};

enum class CameraControllerType
{
    UserControl = 0,
    ContinuousMovement = 1,
    Animation = 2,
};

template<typename ManipulatorState>
class ICameraController : public IGenericCameraController
{
public:
    ~ICameraController() override = default;

    virtual std::unique_ptr<ICameraController> handle_event(
        CameraManipulatorContext* ctx,
        const ManipulatorState&,
        const ViewportEvent& e,
        hrz_proto::SceneViewIndex view_index,
        const CameraViewInfo& view) = 0;

    virtual std::unique_ptr<ICameraController> handle_movement(
        CameraManipulatorContext* ctx,
        const ManipulatorState&,
        MovementEventType type,
        const hrz_proto::CameraMovement&,
        hrz_proto::SceneViewIndex view_index,
        const CameraViewInfo& view) = 0;

    virtual bool is_idle() = 0;

    virtual CameraControllerType type() const = 0;
};

class IContinuousMovementController
{
public:
    virtual ~IContinuousMovementController() = default;

    virtual void on_end(CameraManipulatorContext* ctx) {}

    virtual lm::ddual_quat work(
        CameraManipulatorContext* ctx,
        double dt,
        double height_above_terrain,
        bool should_keep_bearing,
        const std::array<PickingSystem*, hrz::SCENE_VIEW_COUNT>& picking) = 0;

    // Returns whether the event was handled or not.
    virtual bool handle_begin_continuous_movement(
        const hrz_proto::CameraMovement& movement,
        const CameraViewInfo& view) = 0;

    // Returns whether the event was handled or not.
    virtual bool handle_end_continuous_movement(const hrz_proto::CameraMovement& movement) = 0;

    virtual bool is_active() const = 0;

    virtual bool is_idle() const = 0;

    virtual void restart_with_pose(CameraManipulatorContext* ctx, const lm::ddual_quat& pose) = 0;

    virtual hrz_proto::CameraMovementType movement_type() const = 0;

    virtual void update_energy_half_time(double e) = 0;

    virtual void update_terrain_settings(
        double min_height_above_terrain,
        double terrain_collision_inertia) = 0;
};

template<typename ManipulatorState, typename BaseController>
class ContinuousMovementInterruptionController : public BaseController
{
    using IController = ICameraController<ManipulatorState>;

    std::unique_ptr<IContinuousMovementController> _controller;
    std::unique_ptr<IController> _interrupted_by;

    bool _can_be_interrupted = true;
    bool _should_resume_after_interruption = true;

    struct NewControllerAction
    {
        enum Action
        {
            SameController = 0,
            ReplacedInterruptController,
            ReplaceMainController,
        };

        Action action{};
        std::unique_ptr<IController> main_controller_to_replace;
    };

    NewControllerAction handle_new_controller(
        CameraManipulatorContext* ctx,
        std::unique_ptr<IController> new_controller)
    {
        if (!new_controller)
        {
            return {NewControllerAction::SameController};
        }
        else if (
            new_controller->type() == CameraControllerType::ContinuousMovement
            || !_should_resume_after_interruption || !_controller->is_active())
        {
            return {NewControllerAction::ReplaceMainController, std::move(new_controller)};
        }
        else
        {
            if (!_interrupted_by)
            {
                add_notification(ctx)->set_continuous_movement_interrupted_will_resume(
                    _controller->movement_type());
            }
            _interrupted_by = std::move(new_controller);
            return {NewControllerAction::ReplacedInterruptController};
        }
    }

public:
    ContinuousMovementInterruptionController(
        std::unique_ptr<IContinuousMovementController> controller,
        hrz_proto::CameraContinuousMovementInterruption interruption) :
        _controller{std::move(controller)}
    {
        set_interruption(interruption);
    }

    void set_interruption(hrz_proto::CameraContinuousMovementInterruption interruption)
    {
        switch (interruption)
        {
            case hrz_proto::CONTINUOUS_MOVEMENT_NOT_INTERRUPTIBLE:
                _can_be_interrupted = false;
                _should_resume_after_interruption = false;
                break;
            case hrz_proto::CONTINUOUS_MOVEMENT_INTERRUPTIBLE_NO_RESUME:
                _can_be_interrupted = true;
                _should_resume_after_interruption = false;
                break;
            case hrz_proto::CONTINUOUS_MOVEMENT_INTERRUPTIBLE_THEN_RESUME:
                _can_be_interrupted = true;
                _should_resume_after_interruption = true;
                break;
            default: assert(false);
        }
    }

    std::unique_ptr<IController> handle_event(
        CameraManipulatorContext* ctx,
        const ManipulatorState& state,
        const ViewportEvent& e,
        hrz_proto::SceneViewIndex view_index,
        const CameraViewInfo& view) final
    {
        NewControllerAction new_controller;

        if (_interrupted_by)
        {
            new_controller = handle_new_controller(
                ctx, _interrupted_by->handle_event(ctx, state, e, view_index, view));
        }
        else if (!_controller->is_active() || _can_be_interrupted)
        {
            new_controller = handle_new_controller(
                ctx, BaseController::handle_event(ctx, state, e, view_index, view));
        }

        switch (new_controller.action)
        {
            case NewControllerAction::SameController: return nullptr;
            case NewControllerAction::ReplacedInterruptController:
                // Replay event in cast it needs to be handled by the new controller
                return handle_event(ctx, state, e, view_index, view);
            case NewControllerAction::ReplaceMainController:
                return std::move(new_controller.main_controller_to_replace);
            default: assert(false); return nullptr;
        }
    }

    std::unique_ptr<IController> handle_movement(
        CameraManipulatorContext* ctx,
        const ManipulatorState& state,
        MovementEventType type,
        const hrz_proto::CameraMovement& movement,
        hrz_proto::SceneViewIndex view_index,
        const CameraViewInfo& view) final
    {
        bool was_active = _controller->is_active();

        if (type == MovementEventType::BeginContinuous && !_interrupted_by)
        {
            if (_controller->handle_begin_continuous_movement(movement, view))
            {
                if (!_interrupted_by && _controller->is_active() && !was_active)
                {
                    add_notification(ctx)->set_continuous_movement_started(
                        _controller->movement_type());
                }
                set_interruption(movement.continuous_movement_interruption());
                return nullptr;
            }
        }
        else if (type == MovementEventType::EndContinuous)
        {
            if (_controller->handle_end_continuous_movement(movement))
            {
                if (!_controller->is_active() && was_active)
                {
                    add_notification(ctx)->set_continuous_movement_ended(
                        _controller->movement_type());
                }
                return nullptr;
            }
        }

        NewControllerAction new_controller;

        if (_interrupted_by)
        {
            new_controller = handle_new_controller(
                ctx,
                _interrupted_by->handle_movement(ctx, state, type, movement, view_index, view));
        }
        else if (
            !_controller->is_active() || _can_be_interrupted
            || type == MovementEventType::BeginContinuous)
        {
            new_controller = handle_new_controller(
                ctx, BaseController::handle_movement(ctx, state, type, movement, view_index, view));
        }

        switch (new_controller.action)
        {
            case NewControllerAction::SameController: return nullptr;
            case NewControllerAction::ReplacedInterruptController:
                // Replay event in cast it needs to be handled by the new controller
                return handle_movement(ctx, state, type, movement, view_index, view);
            case NewControllerAction::ReplaceMainController:
                return std::move(new_controller.main_controller_to_replace);
            default: assert(false); return nullptr;
        }
    }

    void on_end(CameraManipulatorContext* ctx) override
    {
        if (_interrupted_by)
        {
            _interrupted_by->on_end(ctx);
        }

        if (_controller->is_active())
        {
            add_notification(ctx)->set_continuous_movement_interrupted_no_resume(
                _controller->movement_type());
        }

        _controller->on_end(ctx);
    }

    lm::ddual_quat work(
        CameraManipulatorContext* ctx,
        double dt,
        double height_above_terrain,
        bool keep_bearing,
        const std::array<PickingSystem*, hrz::SCENE_VIEW_COUNT>& picking) final
    {
        if (_interrupted_by)
        {
            lm::ddual_quat new_pose =
                _interrupted_by->work(ctx, dt, height_above_terrain, keep_bearing, picking);
            if (_interrupted_by->is_idle())
            {
                _interrupted_by.reset();
                _controller->restart_with_pose(ctx, new_pose);
                if (_controller->is_active())
                {
                    add_notification(ctx)->set_continuous_movement_resumed(
                        _controller->movement_type());
                }
            }
            else
            {
                return new_pose;
            }
        }

        assert(!_interrupted_by);
        return _controller->work(ctx, dt, height_above_terrain, keep_bearing, picking);
    }

    void update_energy_half_time(const EnergyHalfTime& e) override
    {
        _controller->update_energy_half_time(e.for_movements);
        if (_interrupted_by)
        {
            _interrupted_by->update_energy_half_time(e);
        }
    }

    void update_terrain_settings(double min_height_above_terrain, double terrain_collision_inertia)
        override
    {
        _controller->update_terrain_settings(min_height_above_terrain, terrain_collision_inertia);
        if (_interrupted_by)
        {
            _interrupted_by->update_terrain_settings(
                min_height_above_terrain, terrain_collision_inertia);
        }
    }

    bool is_idle() override
    {
        return _controller->is_idle() && (!_interrupted_by || _interrupted_by->is_idle());
    }

    CameraControllerType type() const override { return CameraControllerType::ContinuousMovement; }
};

} // namespace hrz::camera
