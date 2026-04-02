#include "hrz/core/camera/driver.h"
#include "hrz/core/camera/manipulator.h"
#include "hrz/fnd/format.h"

extern "C"
{
#include <microui/microui.h>
}

namespace hrz::camera
{

// https://stackoverflow.com/questions/53408962/try-to-understand-compiler-error-message-default-member-initializer-required-be
namespace
{

struct Config
{
    EnergyHalfTime energy_half_time{};
    double min_tilt{};
    double max_tilt{};
    double min_distance{};
    double max_distance{};
    double min_height_above_terrain{};
    double terrain_collision_inertia{};
};

} // namespace

class FixedTargetManipulator : public CameraManipulator
{
    // State before we have the initial pose.
    // We know initially the target, distance, tilt, when we have the "real" pose,
    // we'll readjust the target and that will be it.
    struct UninitializedState
    {
        Config config;
        AngularViewpoint viewpoint;
    };

    struct InitializedState
    {
        Config config;
        lm::ddual_quat pose;
        lm::dvec3 target;
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

        std::unique_ptr<IController> handle_event(
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
            else if (
                e.is_in_viewport
                && is_mouse_button_down_event(e, platform::Event::MouseButton::Right))
            {
                return std::make_unique<MouseDragZoomController>(state, view_index);
            }
            else if (e.is_in_viewport && is_mouse_wheel_event(e))
            {
                return std::make_unique<MouseWheelZoomStepController>(state);
            }
            else if (
                e.is_in_viewport
                && is_platform_event(e, platform::Event::Kind::MouseButtonDoubleClick))
            {
                return std::make_unique<MouseDoubleClickZoomStepController>(state);
            }
            else if (
                e.is_in_viewport
                && is_two_finger_gesture_qualification_event(
                    e, gestures::TwoFingerGesture::Type::PinchRotate))
            {
                return std::make_unique<PinchZoomController>(
                    state, view_index, e.gesture.two_finger_gesture().id,
                    e.gesture.two_finger_gesture().spread());
            }

            return nullptr;
        }

        std::unique_ptr<IController> handle_movement(
            CameraManipulatorContext* ctx,
            const InitializedState& state,
            MovementEventType type,
            const hrz_proto::CameraMovement& movement,
            hrz_proto::SceneViewIndex view_index,
            const CameraViewInfo& view) override
        {
            if (type == MovementEventType::Instant && movement.has_zoom())
            {
                return std::make_unique<InstantMovementZoomStepController>(state);
            }
            else if (type == MovementEventType::Instant && movement.has_rotation())
            {
                return std::make_unique<InstantMovementRotationStepController>(state);
            }
            else if (type == MovementEventType::BeginContinuous && movement.has_rotation())
            {
                return std::make_unique<
                    ContinuousMovementInterruptionController<InitializedState, BaseController>
                >(std::make_unique<ContinuousMovementRotationController>(state),
                  movement.continuous_movement_interruption());
            }
            else if (type == MovementEventType::BeginContinuous && movement.has_zoom())
            {
                return std::make_unique<
                    ContinuousMovementInterruptionController<InitializedState, BaseController>
                >(std::make_unique<ContinuousMovementZoomController>(state),
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

    class RotationController : public BaseController
    {
        std::unique_ptr<RotateAroundFixedTargetDriver> _driver;
        bool _finished = false;
        lm::dvec2 _rotation_this_frame{};

    protected:
        constexpr bool is_finished() const { return _finished; }

        constexpr void finish() { _finished = true; }

    public:
        explicit RotationController(const InitializedState& state) :
            _driver{RotateAroundFixedTargetDriver::create(
                state.config.energy_half_time.for_user_controls,
                state.pose,
                state.target,
                state.config.min_tilt,
                state.config.max_tilt,
                state.config.min_height_above_terrain,
                state.config.terrain_collision_inertia)}
        {
        }

        void update_energy_half_time(const EnergyHalfTime& e) override
        {
            _driver->update_energy_half_time(e.for_user_controls);
        }

        void update_terrain_settings(
            double min_height_above_terrain,
            double terrain_collision_inertia) override
        {
            _driver->update_terrain_settings(min_height_above_terrain, terrain_collision_inertia);
        }

        bool is_idle() override { return _finished && _driver->is_idle(); }

        void handle_move(lm::dvec2 move)
        {
            _rotation_this_frame += kRotateAroundTargetSensitivity * move;
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
                _driver->set_rotation_this_frame(_rotation_this_frame.x, -_rotation_this_frame.y);
                _rotation_this_frame = lm::dvec2{0, 0};
            }

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
        const hrz_proto::SceneViewIndex _scene_view;
        const platform::Event::MouseButton _button;

    public:
        MouseDragRotationController(
            const InitializedState& state,
            hrz_proto::SceneViewIndex scene_view,
            platform::Event::MouseButton button) :
            RotationController(state), _scene_view{scene_view}, _button{button}
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

                if (view_index == _scene_view
                    && is_platform_event(e, platform::Event::Kind::MouseMove))
                {
                    lm::dvec2 move(e.platform.mouse_move.dx, e.platform.mouse_move.dy);
                    handle_move(move);
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

        bool _actually_started = false;
        lm::vec2 _last_position;

    public:
        TouchDragRotationController(
            const InitializedState& state,
            hrz_proto::SceneViewIndex scene_view,
            gestures::GestureId gesture_id,
            lm::vec2 initial_position) :
            RotationController(state),
            _scene_view{scene_view},
            _gesture_id{gesture_id},
            _last_position{initial_position}
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
                    handle_move(movement);
                }
                return nullptr;
            }
            else
            {
                return BaseController::handle_event(ctx, state, e, view_index, view);
            }
        }
    };

    class ContinuousZoomController : public BaseController
    {
        std::unique_ptr<DistanceToFixedTargetDriver> _driver;
        bool _finished = false;
        double _steps_this_frame{};

    protected:
        const hrz_proto::SceneViewIndex _scene_view;

        constexpr bool is_finished() const { return _finished; }

        constexpr void finish() { _finished = true; }

        constexpr void add_steps_this_frame(double steps) { _steps_this_frame += steps; }

    public:
        ContinuousZoomController(
            const InitializedState& state,
            double energy_half_time,
            hrz_proto::SceneViewIndex scene_view) :
            _driver{DistanceToFixedTargetDriver::create(
                energy_half_time,
                state.pose,
                state.target,
                state.config.min_distance,
                state.config.max_distance,
                state.config.min_height_above_terrain,
                state.config.terrain_collision_inertia)},
            _scene_view{scene_view}
        {
        }

        void update_energy_half_time_inner(double e) { _driver->update_energy_half_time(e); }

        void update_terrain_settings(
            double min_height_above_terrain,
            double terrain_collision_inertia) override
        {
            _driver->update_terrain_settings(min_height_above_terrain, terrain_collision_inertia);
        }

        bool is_idle() override { return _finished && _driver->is_idle(); }

        lm::ddual_quat work(
            CameraManipulatorContext* ctx,
            double dt,
            double height_above_terrain,
            bool keep_bearing,
            const std::array<PickingSystem*, hrz::SCENE_VIEW_COUNT>&) override
        {
            if (!_finished)
            {
                _driver->set_steps_this_frame(_steps_this_frame);
                _steps_this_frame = 0.0;
            }

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
            if (_finished)
            {
                return BaseController::handle_movement(
                    ctx, state, type, movement, view_index, view);
            }
            return nullptr;
        }
    };

    class MouseDragZoomController : public ContinuousZoomController
    {
    public:
        MouseDragZoomController(
            const InitializedState& state,
            hrz_proto::SceneViewIndex scene_view) :
            ContinuousZoomController(
                state,
                state.config.energy_half_time.for_user_controls,
                scene_view)
        {
        }

        void update_energy_half_time(const EnergyHalfTime& e) override
        {
            update_energy_half_time_inner(e.for_user_controls);
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
                if (is_mouse_button_up_event(e, platform::Event::MouseButton::Right))
                {
                    finish();
                }
                else if (
                    view_index == _scene_view
                    && is_platform_event(e, platform::Event::Kind::MouseMove))
                {
                    add_steps_this_frame(e.platform.mouse_move.dy * kMouseDragZoomSensitivity);
                }
            }
            else
            {
                return BaseController::handle_event(ctx, state, e, view_index, view);
            }
            return nullptr;
        }
    };

    class PinchZoomController : public ContinuousZoomController
    {
        gestures::GestureId _gesture_id;
        float _last_spread;

    public:
        PinchZoomController(
            const InitializedState& state,
            hrz_proto::SceneViewIndex scene_view,
            gestures::GestureId gesture_id,
            float initial_spread) :
            ContinuousZoomController(
                state,
                state.config.energy_half_time.for_user_controls,
                scene_view),
            _gesture_id{gesture_id},
            _last_spread{initial_spread}
        {
        }

        void update_energy_half_time(const EnergyHalfTime& e) override
        {
            update_energy_half_time_inner(e.for_user_controls);
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
                if (is_two_finger_gesture_end_event(e, _gesture_id))
                {
                    finish();
                }
                else if (
                    view_index == _scene_view && is_two_finger_gesture_move_event(e, _gesture_id))
                {
                    float spread = e.gesture.two_finger_gesture().spread();
                    float diff = spread - std::exchange(_last_spread, spread);
                    add_steps_this_frame(diff * kPinchZoomSensitivity);
                }
            }
            else
            {
                return BaseController::handle_event(ctx, state, e, view_index, view);
            }
            return nullptr;
        }
    };

    class StepZoomController : public BaseController
    {
        std::unique_ptr<DistanceToFixedTargetDriver> _driver;

    protected:
        inline void add_steps(double s) { _driver->add_steps_total(s); }

    public:
        StepZoomController(const InitializedState& state, double energy_half_time) :
            _driver{DistanceToFixedTargetDriver::create(
                energy_half_time,
                state.pose,
                state.target,
                state.config.min_distance,
                state.config.max_distance,
                state.config.min_height_above_terrain,
                state.config.terrain_collision_inertia)}
        {
        }

        void update_energy_half_time_inner(double e) { _driver->update_energy_half_time(e); }

        void update_terrain_settings(
            double min_height_above_terrain,
            double terrain_collision_inertia) override
        {
            _driver->update_terrain_settings(min_height_above_terrain, terrain_collision_inertia);
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
    };

    class InstantMovementZoomStepController : public StepZoomController
    {
    public:
        explicit InstantMovementZoomStepController(const InitializedState& state) :
            StepZoomController(state, state.config.energy_half_time.for_movements)
        {
        }

        void update_energy_half_time(const EnergyHalfTime& e) override
        {
            update_energy_half_time_inner(e.for_movements);
        }

        std::unique_ptr<IController> handle_movement(
            CameraManipulatorContext* ctx,
            const InitializedState& state,
            MovementEventType type,
            const hrz_proto::CameraMovement& movement,
            hrz_proto::SceneViewIndex view_index,
            const CameraViewInfo& view) override
        {
            if (type == MovementEventType::Instant && movement.has_zoom())
            {
                add_steps(std::log2(movement.zoom().ratio()));
            }
            else
            {
                return StepZoomController::handle_movement(
                    ctx, state, type, movement, view_index, view);
            }
            return nullptr;
        }
    };

    class MouseWheelZoomStepController : public StepZoomController
    {
    public:
        explicit MouseWheelZoomStepController(const InitializedState& state) :
            StepZoomController(state, state.config.energy_half_time.for_user_controls)
        {
        }

        void update_energy_half_time(const EnergyHalfTime& e) override
        {
            update_energy_half_time_inner(e.for_user_controls);
        }

        std::unique_ptr<IController> handle_event(
            CameraManipulatorContext* ctx,
            const InitializedState& state,
            const ViewportEvent& e,
            hrz_proto::SceneViewIndex view_index,
            const CameraViewInfo& view) override
        {
            if (e.is_in_viewport && is_platform_event(e, platform::Event::Kind::MouseWheel))
            {
                add_steps(e.platform.mouse_wheel.wheel * kMouseWheelZoomSensitivity);
            }
            else
            {
                return StepZoomController::handle_event(ctx, state, e, view_index, view);
            }
            return nullptr;
        }
    };

    class MouseDoubleClickZoomStepController : public StepZoomController
    {
    public:
        explicit MouseDoubleClickZoomStepController(const InitializedState& state) :
            StepZoomController(state, state.config.energy_half_time.for_user_controls)
        {
        }

        void update_energy_half_time(const EnergyHalfTime& e) override
        {
            update_energy_half_time_inner(e.for_user_controls);
        }

        std::unique_ptr<IController> handle_event(
            CameraManipulatorContext* ctx,
            const InitializedState& state,
            const ViewportEvent& e,
            hrz_proto::SceneViewIndex view_index,
            const CameraViewInfo& view) override
        {
            // We don't test the scene view because we may double click from different views since
            // this controller is persistent (although realistically since double click is preceded
            // by a single click, it will probably have been removed already anyway).
            if (e.is_in_viewport
                && is_platform_event(e, platform::Event::Kind::MouseButtonDoubleClick))
            {
                double value = 0;
                switch (e.platform.mouse_button.button)
                {
                    case platform::Event::MouseButton::Left: value = kDoubleTapZoomSteps; break;
                    case platform::Event::MouseButton::Right: value = -kDoubleTapZoomSteps; break;
                    default: value = 0; break;
                }
                add_steps(value);
            }
            else
            {
                return BaseController::handle_event(ctx, state, e, view_index, view);
            }
            return nullptr;
        }
    };

    class InstantMovementRotationStepController : public BaseController
    {
        std::unique_ptr<RotateAroundFixedTargetDriver> _driver;

    public:
        explicit InstantMovementRotationStepController(const InitializedState& state) :
            _driver{RotateAroundFixedTargetDriver::create(
                state.config.energy_half_time.for_movements,
                state.pose,
                state.target,
                state.config.min_tilt,
                state.config.max_tilt,
                state.config.min_height_above_terrain,
                state.config.terrain_collision_inertia)}
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
            _driver->update_terrain_settings(min_height_above_terrain, terrain_collision_inertia);
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
                    -movement.rotation().bearing(), movement.rotation().tilt());
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
        std::unique_ptr<RotateAroundFixedTargetDriver> _driver;
        bool _active = false;
        lm::dvec2 _velocity{};

    public:
        explicit ContinuousMovementRotationController(const InitializedState& state) :
            _driver{RotateAroundFixedTargetDriver::create(
                state.config.energy_half_time.for_movements,
                state.pose,
                state.target,
                state.config.min_tilt,
                state.config.max_tilt,
                state.config.min_height_above_terrain,
                state.config.terrain_collision_inertia)}
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
            _driver->update_terrain_settings(min_height_above_terrain, terrain_collision_inertia);
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
                _velocity = lm::dvec2(-movement.rotation().bearing(), movement.rotation().tilt());
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

    class ContinuousMovementZoomController : public IContinuousMovementController
    {
        std::unique_ptr<DistanceToFixedTargetDriver> _driver;
        bool _active = false;
        double _velocity = {};

    public:
        explicit ContinuousMovementZoomController(const InitializedState& state) :
            _driver{DistanceToFixedTargetDriver::create(
                state.config.energy_half_time.for_movements,
                state.pose,
                state.target,
                state.config.min_distance,
                state.config.max_distance,
                state.config.min_height_above_terrain,
                state.config.terrain_collision_inertia)}
        {
        }

        void update_energy_half_time(double e) override { _driver->update_energy_half_time(e); }

        void update_terrain_settings(
            double min_height_above_terrain,
            double terrain_collision_inertia) override
        {
            _driver->update_terrain_settings(min_height_above_terrain, terrain_collision_inertia);
        }

        void restart_with_pose(CameraManipulatorContext* ctx, const lm::ddual_quat& pose) override
        {
            _driver = _driver->recreate_with_pose(pose);
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
                _driver->set_steps_velocity(_velocity);
            }
            return _driver->work(dt, height_above_terrain, false);
        }

        hrz_proto::CameraMovementType movement_type() const override
        {
            return hrz_proto::CAMERA_ZOOM;
        }

        bool handle_begin_continuous_movement(
            const hrz_proto::CameraMovement& movement,
            const CameraViewInfo&) override
        {
            if (movement.has_zoom())
            {
                _velocity = std::log2(movement.zoom().ratio());
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
            if (movement.has_zoom())
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

    class AnimationController : public BaseController
    {
        std::unique_ptr<AnimationDriver> _driver;
        bool _finished = false;

    public:
        AnimationController(
            const Animation& animation,
            const hrz_proto::CameraAnimationOptions& options,
            double min_height_above_terrain,
            double terrain_collision_inertia) :
            _driver{AnimationDriver::create(
                animation,
                options,
                min_height_above_terrain,
                terrain_collision_inertia)}
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
            _driver->update_terrain_settings(min_height_above_terrain, terrain_collision_inertia);
        }

        bool is_idle() override { return _driver->is_idle(); }

        void on_start(CameraManipulatorContext* ctx) override
        {
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

    void initialize_with_pose(const lm::ddual_quat& pose)
    {
        assert(!std::holds_alternative<InitializedState>(_state));
        const UninitializedState& previous_state = std::get<UninitializedState>(_state);

        lm::dvec3 forward = pose.r * lm::dvec3{0, 0, -1};
        lm::dvec3 target =
            lm::extract_translation(pose) + forward * previous_state.viewpoint.distance;
        _state = InitializedState{previous_state.config, pose, target};
        replace_controller(std::make_unique<IdleController>(pose));
    }

public:
    FixedTargetManipulator(
        const EnergyHalfTime& energy_half_time,
        const AngularViewpoint& initial_viewpoint,
        std::optional<lm::ddual_quat> pose,
        double min_tilt,
        double max_tilt,
        double min_distance,
        double max_distance,
        double min_height_above_terrain,
        double terrain_collision_inertia) :
        _state{UninitializedState{
            Config{
                energy_half_time, min_tilt, max_tilt, min_distance, max_distance,
                min_height_above_terrain, terrain_collision_inertia
            },
            initial_viewpoint
        }}
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

        AngularViewpoint new_vp = positional_to_angular_viewpoint(
            PositionalViewpoint{
                ecef_to_geo3(lm::extract_translation(state.pose)), ecef_to_geo3(state.target)
            },
            std::nullopt);
        AngularViewpoint old_vp = new_vp;

        new_vp.bearing = 0;
        new_vp.tilt = clamp_angle(new_vp.tilt, state.config.min_tilt, state.config.max_tilt);
        if (params.reset_tilt())
        {
            new_vp.tilt =
                (state.config.max_tilt - state.config.min_tilt) * 0.33 + state.config.min_tilt;
        }

        if (params.animation_options().duration() > 0)
        {
            Animation animation =
                make_animation_around_target(old_vp, new_vp.bearing, new_vp.tilt, new_vp.distance);
            replace_controller(
                std::make_unique<AnimationController>(
                    animation, params.animation_options(), state.config.min_height_above_terrain,
                    state.config.terrain_collision_inertia));
        }
        else
        {
            state.pose = to_dual_quat(new_vp);
            replace_controller(std::make_unique<IdleController>(state.pose));
        }
    }

    void update_energy_half_time(const EnergyHalfTime& e) override
    {
        std::visit([e](auto& state) { state.config.energy_half_time = e; }, _state);
        CameraManipulator::update_energy_half_time(e);
    }

    void update_terrain_settings(double min_height_above_terrain, double terrain_collision_inertia)
        override
    {
        std::visit(
            [min_height_above_terrain, terrain_collision_inertia](auto& state)
            {
                state.config.min_height_above_terrain = min_height_above_terrain;
                state.config.terrain_collision_inertia = terrain_collision_inertia;
            },
            _state);
        CameraManipulator::update_terrain_settings(
            min_height_above_terrain, terrain_collision_inertia);
    }

    void dev_ui(mu_Context* ctx) const override
    {
        if (mu_begin_treenode(ctx, "Fixed target manipulator"))
        {
            static const int layout[] = {150, -1};
            mu_layout_row(ctx, 2, layout, 0);

            fmt::memory_buffer buffer;

            mu_text(ctx, "Target");
            if (std::holds_alternative<InitializedState>(_state))
            {
                auto& state = std::get<InitializedState>(_state);
                mu_text(
                    ctx,
                    hrz::format_to_buffer(
                        buffer, "({:.2f}, {:.2f}, {:.2f})", state.target.x, state.target.y,
                        state.target.z));
                mu_text(ctx, "Min distance");
                mu_text(ctx, hrz::format_to_buffer(buffer, "{:.2f} m", state.config.min_distance));
                mu_text(ctx, "Max distance");
                mu_text(ctx, hrz::format_to_buffer(buffer, "{:.2f} m", state.config.max_distance));
                mu_text(ctx, "Min tilt");
                mu_text(
                    ctx,
                    hrz::format_to_buffer(
                        buffer, "{:.2f} deg", lm::degrees(state.config.min_tilt)));
                mu_text(ctx, "Max tilt");
                mu_text(
                    ctx,
                    hrz::format_to_buffer(
                        buffer, "{:.2f} deg", lm::degrees(state.config.max_tilt)));
            }
            else
            {
                mu_text(ctx, "None");
            }

            mu_end_treenode(ctx);
        }
    }
};

std::unique_ptr<CameraManipulator> create_fixed_target_manipulator(
    const EnergyHalfTime& energy_half_time,
    const AngularViewpoint& initial_viewpoint, // Viewpoint without elevation query
    std::optional<lm::ddual_quat> pose,
    double min_tilt,
    double max_tilt,
    double min_distance,
    double max_distance,
    double min_height_above_terrain,
    double terrain_collision_inertia)
{
    return std::make_unique<FixedTargetManipulator>(
        energy_half_time, initial_viewpoint, pose, min_tilt, max_tilt, min_distance, max_distance,
        min_height_above_terrain, terrain_collision_inertia);
}

} // namespace hrz::camera
