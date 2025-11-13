#include "hrz/core/camera/controller.h"
#include "hrz/core/camera/driver.h"
#include "hrz/core/camera/manipulator.h"

extern "C"
{
#include <microui/microui.h>
}

#include <limits>

namespace hrz::camera
{

// https://stackoverflow.com/questions/53408962/try-to-understand-compiler-error-message-default-member-initializer-required-be
namespace
{
constexpr double kMinHeightAboveTerrain = std::numeric_limits<double>::lowest();
constexpr double kTerrainCollisionInertia = 0.0;

struct Config
{
    EnergyHalfTime energy_half_time;
    double min_tilt{};
    double max_tilt{};
};
} // namespace

class FixedPositionManipulator : public CameraManipulator
{
    struct UninitializedState
    {
        Config config;
    };

    struct InitializedState
    {
        Config config;
        lm::ddual_quat pose;
    };

    using State = std::variant<UninitializedState, InitializedState>;
    State _state;

    using IController = ICameraController<InitializedState>;

    IController* controller() { return (IController*)generic_controller(); }

    class BaseController : public IController
    {
    public:
        void on_start(CameraManipulatorContext*) override {}

        void on_end(CameraManipulatorContext*) override {}

        std::unique_ptr<ICameraController> handle_event(
            CameraManipulatorContext* ctx,
            const InitializedState& state,
            const ViewportEvent& e,
            hrz_proto::SceneViewIndex view_index,
            const CameraViewInfo& view) override
        {
            if (e.is_in_viewport
                && is_mouse_button_down_event(e, platform::Event::MouseButton::Left))
            {
                return std::make_unique<MouseDragRotationController>(
                    state, view_index, platform::Event::MouseButton::Left);
            }
            else if (
                e.is_in_viewport
                && is_mouse_button_down_event(e, platform::Event::MouseButton::Middle))
            {
                return std::make_unique<MouseDragRotationController>(
                    state, view_index, platform::Event::MouseButton::Middle);
            }
            else if (e.is_in_viewport && is_single_finger_gesture_start_event(e))
            {
                return std::make_unique<TouchDragRotationController>(
                    state, view_index, e.gesture.single_finger_gesture().id,
                    e.gesture.single_finger_gesture().position);
            }

            return nullptr;
        }

        std::unique_ptr<ICameraController> handle_movement(
            CameraManipulatorContext* ctx,
            const InitializedState& state,
            MovementEventType type,
            const hrz_proto::CameraMovement& movement,
            hrz_proto::SceneViewIndex view_index,
            const CameraViewInfo& view) override
        {
            if (type == MovementEventType::Instant && movement.has_rotation())
            {
                return std::make_unique<InstantMovementRotationStepController>(state);
            }
            else if (type == MovementEventType::BeginContinuous && movement.has_rotation())
            {
                return std::make_unique<
                    ContinuousMovementInterruptionController<InitializedState, BaseController>>(
                    std::make_unique<ContinuousMovementRotationController>(state),
                    movement.continuous_movement_interruption());
            }

            return nullptr;
        }

        CameraControllerType type() const override { return CameraControllerType::UserControl; }
    };

    class IdleController : public BaseController
    {
        lm::ddual_quat _pose;

    public:
        explicit IdleController(const lm::ddual_quat& pose) : _pose{pose} {}

        lm::ddual_quat work(
            CameraManipulatorContext* ctx,
            double dt,
            double height_above_terrain,
            bool keep_bearing,
            const std::array<PickingSystem*, hrz::SCENE_VIEW_COUNT>&) override
        {
            return _pose;
        }

        void update_energy_half_time(const EnergyHalfTime& e) override {}

        void update_terrain_settings(
            double min_height_above_terrain,
            double terrain_collision_inertia) override
        {
        }

        bool is_idle() override { return true; }
    };

    class AnimationController : public BaseController
    {
        std::unique_ptr<AnimationDriver> _driver;
        bool _finished = false;

    public:
        AnimationController(
            const lm::ddual_quat& from,
            const lm::ddual_quat& to,
            const hrz_proto::CameraAnimationOptions& options) :
            _driver{AnimationDriver::create(
                from,
                to,
                options,
                kMinHeightAboveTerrain,
                kTerrainCollisionInertia)}
        {
        }

        lm::ddual_quat work(
            CameraManipulatorContext* ctx,
            double dt,
            double height_above_terrain,
            bool keep_bearing,
            const std::array<PickingSystem*, hrz::SCENE_VIEW_COUNT>&) override
        {
            lm::ddual_quat new_pose = _driver->work(dt, height_above_terrain, false);
            if (!_finished && _driver->is_idle())
            {
                _finished = true;
                ctx->add_notification()->mutable_animation_ended();
            }
            return new_pose;
        }

        void update_energy_half_time(const EnergyHalfTime& e) override {}

        void update_terrain_settings(
            double min_height_above_terrain,
            double terrain_collision_inertia) override
        {
        }

        bool is_idle() override { return _driver->is_idle(); }

        void on_start(CameraManipulatorContext* ctx) override
        {
            hrz_proto::CameraNotification notification;
            ctx->add_notification()->mutable_animation_started();
        }

        void on_end(CameraManipulatorContext* ctx) override
        {
            if (!_finished)
            {
                ctx->add_notification()->mutable_animation_interrupted();
            }
        }

        CameraControllerType type() const override { return CameraControllerType::Animation; }
    };

    class RotationController : public BaseController
    {
        std::unique_ptr<RotateAroundCenterDriver> _driver;
        lm::dvec2 _rotation_this_frame{};
        bool _finished = false;

    protected:
        constexpr bool is_finished() const { return _finished; }

        constexpr void finish() { _finished = true; }

    public:
        explicit RotationController(const InitializedState& state) :
            _driver{RotateAroundCenterDriver::create(
                state.config.energy_half_time.for_user_controls,
                state.pose,
                state.config.min_tilt,
                state.config.max_tilt,
                kMinHeightAboveTerrain,
                kTerrainCollisionInertia)}
        {
        }

        void handle_move(lm::dvec2 move, const CameraViewInfo& view)
        {
            double sensitivity = view.cam.fovy / view.viewport.size.y;
            _rotation_this_frame += sensitivity * move;
        }

        lm::ddual_quat work(
            CameraManipulatorContext* ctx,
            double dt,
            double height_above_terrain,
            bool keep_bearing,
            const std::array<PickingSystem*, hrz::SCENE_VIEW_COUNT>&) override
        {
            if (!_finished)
            {
                _driver->set_rotation_this_frame(-_rotation_this_frame.x, _rotation_this_frame.y);
                _rotation_this_frame = lm::dvec2{0, 0};
            }
            return _driver->work(dt, height_above_terrain, false);
        }

        bool is_idle() override { return _finished && _driver->is_idle(); }

        void update_energy_half_time(const EnergyHalfTime& e) override
        {
            _driver->update_energy_half_time(e.for_user_controls);
        }

        void update_terrain_settings(
            double min_height_above_terrain,
            double terrain_collision_inertia) override
        {
        }

        std::unique_ptr<IController> handle_movement(
            CameraManipulatorContext* ctx,
            const InitializedState& state,
            MovementEventType type,
            const hrz_proto::CameraMovement& movement,
            hrz_proto::SceneViewIndex view_index,
            const CameraViewInfo& view) override
        {
            if (_finished)
            {
                return BaseController::handle_movement(
                    ctx, state, type, movement, view_index, view);
            }
            return nullptr;
        }
    };

    class MouseDragRotationController : public RotationController
    {
        const platform::Event::MouseButton _button;
        const hrz_proto::SceneViewIndex _scene_view;

    public:
        MouseDragRotationController(
            const InitializedState& state,
            hrz_proto::SceneViewIndex scene_view,
            platform::Event::MouseButton button) :
            RotationController(state), _button(button), _scene_view(scene_view)
        {
        }

        std::unique_ptr<IController> handle_event(
            CameraManipulatorContext* ctx,
            const InitializedState& state,
            const ViewportEvent& e,
            hrz_proto::SceneViewIndex view_index,
            const CameraViewInfo& view) override
        {
            if (!is_finished())
            {
                if (is_mouse_button_up_event(e, _button))
                {
                    finish();
                }
                else if (
                    view_index == _scene_view
                    && is_platform_event(e, platform::Event::Kind::MouseMove))
                {
                    lm::dvec2 move(e.platform.mouse_move.dx, e.platform.mouse_move.dy);
                    handle_move(move, view);
                }
                return nullptr;
            }
            else
            {
                return BaseController::handle_event(ctx, state, e, view_index, view);
            }
        }
    };

    class TouchDragRotationController : public RotationController
    {
        const hrz_proto::SceneViewIndex _scene_view;
        const gestures::GestureId _gesture_id;

        lm::vec2 _last_position;
        bool _actually_started{};

    public:
        TouchDragRotationController(
            const InitializedState& state,
            hrz_proto::SceneViewIndex view_index,
            gestures::GestureId gesture_id,
            lm::vec2 initial_position) :
            RotationController(state),
            _scene_view(view_index),
            _gesture_id(gesture_id),
            _last_position(initial_position)
        {
        }

        std::unique_ptr<IController> handle_event(
            CameraManipulatorContext* ctx,
            const InitializedState& state,
            const ViewportEvent& e,
            hrz_proto::SceneViewIndex view_index,
            const CameraViewInfo& view) override
        {
            if (!is_finished())
            {
                if (is_single_finger_gesture_end_event(e, _gesture_id))
                {
                    finish();
                }
                else if (
                    !_actually_started && view_index == _scene_view
                    && is_single_finger_gesture_qualification_event(
                        e, _gesture_id, gestures::SingleFingerGesture::Type::Drag))
                {
                    _actually_started = true;
                }
                else if (
                    _actually_started && view_index == _scene_view
                    && is_single_finger_gesture_move_event(e, _gesture_id))
                {
                    lm::vec2 position = e.gesture.single_finger_gesture().position;
                    lm::dvec2 movement(position - std::exchange(_last_position, position));
                    handle_move(movement, view);
                }
                return nullptr;
            }
            else
            {
                return BaseController::handle_event(ctx, state, e, view_index, view);
            }
        }
    };

    class InstantMovementRotationStepController : public BaseController
    {
        std::unique_ptr<RotateAroundCenterDriver> _driver;

    public:
        explicit InstantMovementRotationStepController(const InitializedState& state) :
            _driver{RotateAroundCenterDriver::create(
                state.config.energy_half_time.for_movements,
                state.pose,
                state.config.min_tilt,
                state.config.max_tilt,
                kMinHeightAboveTerrain,
                kTerrainCollisionInertia)}
        {
        }

        void update_energy_half_time(const EnergyHalfTime& e) override
        {
            _driver->update_energy_half_time(e.for_movements);
        }

        void update_terrain_settings(
            double min_height_above_terrain,
            double terrain_collision_inertia) override
        {
        }

        bool is_idle() override { return _driver->is_idle(); }

        lm::ddual_quat work(
            CameraManipulatorContext* ctx,
            double dt,
            double height_above_terrain,
            bool keep_bearing,
            const std::array<PickingSystem*, hrz::SCENE_VIEW_COUNT>&) override
        {
            return _driver->work(dt, height_above_terrain, false);
        }

        std::unique_ptr<IController> handle_movement(
            CameraManipulatorContext* ctx,
            const InitializedState& state,
            MovementEventType type,
            const hrz_proto::CameraMovement& movement,
            hrz_proto::SceneViewIndex view_index,
            const CameraViewInfo& view) override
        {
            if (type == MovementEventType::Instant && movement.has_rotation())
            {
                _driver->add_rotation_total(
                    movement.rotation().bearing(), movement.rotation().tilt());
                return nullptr;
            }
            else
            {
                return BaseController::handle_movement(
                    ctx, state, type, movement, view_index, view);
            }
        }
    };

    class ContinuousMovementRotationController : public IContinuousMovementController
    {
        std::unique_ptr<RotateAroundCenterDriver> _driver;
        bool _active = false;
        lm::dvec2 _velocity{};

    public:
        explicit ContinuousMovementRotationController(const InitializedState& state) :
            _driver{RotateAroundCenterDriver::create(
                state.config.energy_half_time.for_movements,
                state.pose,
                state.config.min_tilt,
                state.config.max_tilt,
                kMinHeightAboveTerrain,
                kTerrainCollisionInertia)}
        {
        }

        void restart_with_pose(CameraManipulatorContext* ctx, const lm::ddual_quat& pose) override
        {
            _driver = _driver->recreate_with_pose(pose);
        }

        void update_energy_half_time(double e) override { _driver->update_energy_half_time(e); }

        void update_terrain_settings(
            double min_height_above_terrain,
            double terrain_collision_inertia) override
        {
        }

        bool is_idle() const override { return _driver->is_idle(); }

        bool is_active() const override { return _active; }

        lm::ddual_quat work(
            CameraManipulatorContext* ctx,
            double dt,
            double height_above_terrain,
            bool should_keep_bearing,
            const std::array<PickingSystem*, hrz::SCENE_VIEW_COUNT>& picking) override
        {
            if (_active)
            {
                _driver->set_rotation_velocity(_velocity.x, _velocity.y);
            }
            return _driver->work(dt, height_above_terrain, false);
        }

        hrz_proto::CameraMovementType movement_type() const override
        {
            return hrz_proto::CAMERA_ROTATION;
        }

        bool handle_begin_continuous_movement(
            const hrz_proto::CameraMovement& movement,
            const CameraViewInfo&) override
        {
            if (movement.has_rotation())
            {
                _velocity = lm::dvec2(movement.rotation().bearing(), movement.rotation().tilt());
                _active = true;
                return true;
            }
            else
            {
                return false;
            }
        }

        bool handle_end_continuous_movement(const hrz_proto::CameraMovement& movement) override
        {
            if (movement.has_rotation())
            {
                _active = false;
                return true;
            }
            else
            {
                return false;
            }
        }
    };

    void initialize_with_pose(const lm::ddual_quat& pose)
    {
        assert(std::holds_alternative<UninitializedState>(_state));
        Config config = std::get<UninitializedState>(_state).config;

        _state = InitializedState{config, pose};
        replace_controller(std::make_unique<IdleController>(pose));
    }

public:
    FixedPositionManipulator(
        const EnergyHalfTime& energy_half_time,
        std::optional<lm::ddual_quat> pose,
        double min_tilt,
        double max_tilt) :
        _state{UninitializedState{energy_half_time, min_tilt, max_tilt}}
    {
        if (pose) initialize_with_pose(*pose);
    }

    void set_pose(const lm::ddual_quat& pose) override { initialize_with_pose(pose); }

    void handle_event(
        const ViewportEvent& e,
        hrz_proto::SceneViewIndex view_index,
        const CameraViewInfo& view) override
    {
        if (!std::holds_alternative<InitializedState>(_state)) return;
        InitializedState& state = std::get<InitializedState>(_state);

        auto new_controller = controller()->handle_event(context(), state, e, view_index, view);
        if (new_controller)
        {
            replace_controller(std::move(new_controller));

            // Replay the event for the new controller
            // This should never infinitely recurse because controllers that have been created for a
            // particular event should respond to that event, not delegate it to someone else.
            handle_event(e, view_index, view);
        }
    }

    bool is_ready_to_handle_movements() const override
    {
        return std::holds_alternative<InitializedState>(_state);
    }

    void handle_movement(
        MovementEventType movement_type,
        const hrz_proto::CameraMovement& movement,
        hrz_proto::SceneViewIndex view_index,
        const CameraViewInfo& view) override
    {
        if (!std::holds_alternative<InitializedState>(_state)) return;
        InitializedState& state = std::get<InitializedState>(_state);

        auto new_controller = controller()->handle_movement(
            context(), state, movement_type, movement, view_index, view);
        if (new_controller)
        {
            replace_controller(std::move(new_controller));

            // Replay the event for the new controller
            // This should never infinitely recurse because controllers that have been created for a
            // particular event should respond to that event, not delegate it to someone else.
            handle_movement(movement_type, movement, view_index, view);
        }
    }

    lm::ddual_quat work_inner(
        double dt,
        float fovy,
        const ViewportInfo& viewport,
        double height_above_terrain,
        const std::array<PickingSystem*, hrz::SCENE_VIEW_COUNT>& picking,
        const std::function<void(std::span<const hrz_proto::CameraNotification>)>& notifications_cb)
        override
    {
        if (!std::holds_alternative<InitializedState>(_state)) return lm::ddual_quat{};
        InitializedState& state = std::get<InitializedState>(_state);

        state.pose = work_controller(dt, height_above_terrain, picking);
        return state.pose;
    }

    void reset_north(const hrz_proto::ResetNorthParams& params) override
    {
        if (!std::holds_alternative<InitializedState>(_state)) return;
        InitializedState& state = std::get<InitializedState>(_state);

        Pose new_pose = pose_from_dual_quat(state.pose);
        new_pose.bearing = 0;
        new_pose.tilt = clamp_angle(
            new_pose.tilt, state.config.min_tilt - lm::PI / 2, state.config.max_tilt - lm::PI / 2);

        if (params.reset_tilt())
        {
            new_pose.tilt =
                (state.config.max_tilt - state.config.min_tilt) * 0.33 + state.config.min_tilt;
        }

        if (params.animation_options().duration() > 0)
        {
            replace_controller(std::make_unique<AnimationController>(
                state.pose, to_dual_quat(new_pose), params.animation_options()));
        }
        else
        {
            state.pose = to_dual_quat(new_pose);
            replace_controller(std::make_unique<IdleController>(state.pose));
        }
    }

    void update_energy_half_time(const EnergyHalfTime& e) override
    {
        std::visit([e](auto& state) { state.config.energy_half_time = e; }, _state);
        CameraManipulator::update_energy_half_time(e);
    }

    void dev_ui(mu_Context* ctx) const override { mu_text(ctx, "Fixed position manipulator"); }
};

std::unique_ptr<CameraManipulator> create_fixed_position_manipulator(
    const EnergyHalfTime& energy_half_time,
    std::optional<lm::ddual_quat> pose,
    double min_tilt,
    double max_tilt)
{
    return std::make_unique<FixedPositionManipulator>(energy_half_time, pose, min_tilt, max_tilt);
}

} // namespace hrz::camera
