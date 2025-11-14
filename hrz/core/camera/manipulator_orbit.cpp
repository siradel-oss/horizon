#include "hrz/common/geo.h"
#include "hrz/core/camera/controller.h"
#include "hrz/core/camera/driver.h"
#include "hrz/core/camera/manipulator.h"
#include "hrz/core/camera/viewpoint.h"
#include "hrz/core/events.h"
#include "hrz/core/picking_system.h"
#include "hrz/core/render.h"
#include "hrz/fnd/format.h"
#include "hrz/fnd/log.h"
#include "hrz/fnd/time.h"

extern "C"
{
#include <microui/microui.h>
}

#include <deque>

// When doing a translation movement, this is the distance to move per meter of altitude.
static constexpr double kDefaultTranslationDistance = 0.5;

// Above this limit, we don't try no point north anymore.
static constexpr double kNorthBearingEpsilon = lm::radians(7.0);
// Above (or below) this latitude, we don't try to point north anymore.
static constexpr double kNorthLatitudeEpsilon = lm::radians(75.0);

namespace hrz::camera
{
template<int ATTEMPTS = 1>
class OneTimePicking
{
    hrz_proto::SceneViewIndex _view_index;
    lm::ivec2 _initial_positions[ATTEMPTS];
    PositionPickingTicketWithContext _picking_tickets[ATTEMPTS]{};
    bool _has_positions{};
    bool _has_issued_picks{};
    bool _has_retrieved_result{};
    std::optional<lm::dvec3> _result;

public:
    OneTimePicking(hrz_proto::SceneViewIndex view_index, const lm::ivec2 (&positions)[ATTEMPTS]) :
        _view_index(view_index)
    {
        set_positions(positions);
    }

    explicit OneTimePicking(hrz_proto::SceneViewIndex view_index) : _view_index(view_index) {}

    void set_positions(const lm::ivec2 (&positions)[ATTEMPTS])
    {
        assert(!_has_positions);
        for (int i = 0; i < ATTEMPTS; ++i)
        {
            _initial_positions[i] = positions[i];
        }
        _has_positions = true;
    }

    void work(const std::array<PickingSystem*, hrz::SCENE_VIEW_COUNT>& picking)
    {
        if (!_has_issued_picks && _has_positions)
        {
            PickingSystem* ps = picking[_view_index];
            if (ps)
            {
                for (int i = 0; i < ATTEMPTS; ++i)
                {
                    _picking_tickets[i] = {
                        picking::schedule_pick(ps, _initial_positions[i], {}), _view_index,
                        (uintptr_t)ps};
                }
            }
            _has_issued_picks = true;
        }

        if (!_has_retrieved_result)
        {
            PickingSystem* ps = picking[_view_index];
            picking::PositionResult result;

            // Start with the last one so that it will get overriden by picks with more priority.
            for (int i = ATTEMPTS - 1; i >= 0; --i)
            {
                if (ps && _picking_tickets[i].system_ptr == (uintptr_t)ps
                    && picking::retrieve_result(ps, _picking_tickets[i].ticket, &result))
                {
                    _has_retrieved_result = true;
                    if (result.position)
                    {
                        _result = result.position;
                    }
                }
            }
        }
    }

    void cancel(CameraManipulatorContext* ctx)
    {
        if (_has_issued_picks && !_has_retrieved_result)
        {
            for (int i = 0; i < ATTEMPTS; ++i)
            {
                ctx->cancel_picking(_picking_tickets[i]);
            }
        }
    }

    void reset(CameraManipulatorContext* ctx)
    {
        cancel(ctx);
        _has_issued_picks = false;
        _has_retrieved_result = false;
        _result = std::nullopt;
    }

    constexpr bool has_retrieved_result() const { return _has_retrieved_result; }

    constexpr const std::optional<lm::dvec3>& target() const
    {
        assert(_has_retrieved_result);
        return _result;
    }
};

class TranslationMovementDirectionHelper
{
    hrz_proto::CameraFrame _frame =
        hrz_proto::CameraFrame::CameraFrame_INT_MAX_SENTINEL_DO_NOT_USE_; // lol
    lm::dvec2 _direction = {0, 0};

public:
    struct Result
    {
        bool changed{};
        lm::dquat rotation;
        double rotation_t{};
    };

    // Returns whether the movement has changed in a way that makes it needs to reset inertia in the
    // previous direction.
    Result change_direction(
        const lm::ddual_quat& pose,
        bool should_keep_bearing,
        const hrz_proto::CameraTranslate& translation)
    {
        lm::dvec3 position = lm::extract_translation(pose);
        GeoPosition3 geo_position = ecef_to_geo3(position);

        lm::dvec2 spherical_distances = std::max(1.0, geo_position.alt)
            * kDefaultTranslationDistance
            * lm::dvec2{translation.angle_factor_x(), translation.angle_factor_y()};

        // The reason we call this a spherical distance and use "angle factor" is because we
        // translate around the Earth, which means we actually rotate around the Earth
        // center. Now we compute the angle to rotate based on this distance and the
        // distance from the center of the Earth using trigonometry.
        lm::dvec2 angles = spherical_distances / (EARTH_RADIUS + geo_position.alt);
        static constexpr double deg1 = lm::radians(1.0);

        auto new_frame = translation.frame();
        if (should_keep_bearing && new_frame == hrz_proto::CameraFrame::CAMERA_FRAME_TANGENTIAL)
        {
            new_frame = hrz_proto::CameraFrame::CAMERA_FRAME_CARDINAL;
        }

        bool frame_changed = std::exchange(_frame, new_frame) != new_frame;
        lm::dvec2 new_direction =
            (angles == lm::dvec2{0, 0}) ? lm::dvec2{0, 0} : lm::normalize(angles);
        bool direction_changed =
            lm::dot(std::exchange(_direction, new_direction), new_direction) <= 0.0;

        lm::dquat enu_to_ecef = enu_to_ecef_quat_for_geo(geo_position.latlon());
        lm::dquat rotation;

        // First find the rotation axis
        switch (_frame)
        {
            case hrz_proto::CameraFrame::CAMERA_FRAME_CARDINAL:
            {
                lm::dvec3 axis_x = lm::dvec3{0, 0, 1};
                lm::dvec3 axis_y = enu_to_ecef * lm::dvec3{-1, 0, 0};
                rotation = lm::axis_angle(axis_x, _direction.x * deg1)
                    * lm::axis_angle(axis_y, _direction.y * deg1);
                break;
            }
            case hrz_proto::CameraFrame::CAMERA_FRAME_TANGENTIAL:
            {
                lm::dvec3 up = enu_to_ecef * lm::dvec3{0, 0, 1};
                lm::dvec3 side = pose.r * lm::dvec3{1, 0, 0};
                lm::dvec3 forward = lm::normalize(lm::cross(up, side));
                lm::dvec3 axis_x = forward;
                lm::dvec3 axis_y = lm::normalize(lm::cross(up, forward));
                rotation = lm::axis_angle(axis_x, _direction.x * deg1)
                    * lm::axis_angle(axis_y, _direction.y * deg1);
                break;
            }
            default: break;
        }

        // The rotation is always 1°. This is because if it's too low, the slerp in the
        // driver will switch to "linear" mode, which is fine for interpolation between 0
        // and 1, but we might want to extrapolate (t > 1 or t < 0), and then if t is too
        // large this might break stuff. So just make sure the angle is not too small, and
        // then we compute t accordingly.
        // Also this makes sure the sent energy is always the "same" : t = 1 for a rotation of
        // 10° is not the same as t = 1 for a rotation of 2°, so if we change the rotation and
        // accumulate the energy we could end up with too much or not enough energy. So by
        // normalizing be prevent this from happening.
        double rotation_t = lm::length(angles) / deg1;

        return Result{frame_changed || direction_changed, rotation, rotation_t};
    }
};

class ZoomMovementPicking
{
    OneTimePicking<3> _picking;
    bool _has_issued_picks{};
    bool _has_pick_result{};
    std::optional<lm::dvec3> _target;

    void issue_picks(const CameraViewInfo& view)
    {
        assert(!_has_issued_picks);
        // We try 3 directions, straight forward, ~15° down and 30° down.
        lm::dvec4 dir_view[3] = {{0, 0, -1000, 1}, {0, -268, -1000, 1}, {0, -577, -1000, 1}};
        lm::ivec2 screen_pos[3];

        for (int i = 0; i < 3; ++i)
        {
            lm::dvec4 direction = view.proj * dir_view[i];
            direction /= direction.w;
            direction.xy = direction.xy * 0.5 + lm::dvec2(0.5);
            direction.y = 1 - direction.y;

            lm::ivec2 pos(lm::round(direction.xy * view.viewport.subview_size()));
            pos = lm::clamp(
                pos, lm::ivec2{0, 0}, lm::ivec2(view.viewport.subview_size()) - lm::ivec2{1, 1});

            screen_pos[i] = pos;
        }

        _picking.set_positions(screen_pos);
        _has_issued_picks = true;
    }

public:
    explicit ZoomMovementPicking(hrz_proto::SceneViewIndex scene_view) : _picking(scene_view) {}

    void set_view(const CameraViewInfo& view)
    {
        if (!_has_issued_picks)
        {
            issue_picks(view);
        }
    }

    void work(const std::array<PickingSystem*, hrz::SCENE_VIEW_COUNT>& picking)
    {
        if (_has_issued_picks && !_has_pick_result)
        {
            _picking.work(picking);
            if (_picking.has_retrieved_result())
            {
                _has_pick_result = true;
                _target = _picking.target();
            }
        }
    }

    constexpr bool has_result() const { return _has_pick_result; }

    constexpr const std::optional<lm::dvec3>& target() const { return _target; }

    void cancel(CameraManipulatorContext* ctx) { _picking.cancel(ctx); }

    void reset(CameraManipulatorContext* ctx)
    {
        _picking.reset(ctx);
        _has_pick_result = false;
        _target = std::nullopt;
    }
};

// https://stackoverflow.com/questions/53408962/try-to-understand-compiler-error-message-default-member-initializer-required-be
namespace
{
struct Config
{
    EnergyHalfTime energy_half_time;
    hrz::GeoBounds bounds_limits;
    double max_altitude{};
    double min_tilt{}, max_tilt{};
    double min_height_above_terrain{};
    double terrain_collision_inertia{};
    hrz_proto::CameraAnimationOptions correction;
};
} // namespace

class OrbitManipulator : public CameraManipulator
{
    struct UninitializedState
    {
        Config config;
    };

    struct InitializedState
    {
        Config config;
        lm::ddual_quat pose;
        bool alt{};
        bool should_keep_bearing = true;

        const hrz::GeoBounds& controller_bounds_limits() const
        {
            static const hrz::GeoBounds default_bounds = hrz::GeoBounds::full();
            return config.correction.duration() > 0 ? default_bounds : config.bounds_limits;
        }

        double controller_max_altitude() const
        {
            return config.correction.duration() > 0 ? kMaxAltitude : config.max_altitude;
        }
    };

    using State = std::variant<UninitializedState, InitializedState>;
    State _state;

    using IController = ICameraController<InitializedState>;

    IController* controller() { return (IController*)generic_controller(); }

    double _last_non_idle_time = 0;
    double _last_height_above_terrain = 0;

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
            if (e.is_in_viewport && !state.alt
                && is_mouse_button_down_event(e, platform::Event::MouseButton::Middle))
            {
                return std::make_unique<OrbitMouseDragRotationController>(
                    state, view_index,
                    lm::ivec2{e.platform.mouse_button.x, e.platform.mouse_button.y},
                    platform::Event::MouseButton::Middle);
            }
            else if (
                e.is_in_viewport && state.alt
                && is_mouse_button_down_event(e, platform::Event::MouseButton::Left))
            {
                return std::make_unique<OrbitMouseDragRotationController>(
                    state, view_index,
                    lm::ivec2{e.platform.mouse_button.x, e.platform.mouse_button.y},
                    platform::Event::MouseButton::Left);
            }
            else if (
                e.is_in_viewport && state.alt
                && is_mouse_button_down_event(e, platform::Event::MouseButton::Middle))
            {
                return std::make_unique<MiddleAltDragRotateAroundCenterController>(
                    state, view_index);
            }
            else if (e.is_in_viewport && !state.alt && is_single_finger_gesture_start_event(e))
            {
                return std::make_unique<TouchDragController>(
                    state, view_index, e.gesture.single_finger_gesture().id,
                    e.gesture.single_finger_gesture().position);
            }
            else if (
                e.is_in_viewport && !state.alt
                && is_mouse_button_down_event(e, platform::Event::MouseButton::Left))
            {
                return std::make_unique<LeftDragController>(
                    state, view_index,
                    lm::ivec2{e.platform.mouse_button.x, e.platform.mouse_button.y});
            }
            else if (
                e.is_in_viewport && !state.alt
                && is_mouse_button_down_event(e, platform::Event::MouseButton::Right))
            {
                return std::make_unique<RightDragController>(
                    state, view_index,
                    lm::ivec2{e.platform.mouse_button.x, e.platform.mouse_button.y});
            }
            else if (e.is_in_viewport && !state.alt && is_mouse_wheel_event(e))
            {
                return std::make_unique<MouseWheelController>(state, view_index);
            }
            else if (
                e.is_in_viewport && !state.alt
                && is_platform_event(e, platform::Event::Kind::MouseButtonDoubleClick))
            {
                // Left or right click when there is still energy cancels the previous double click
                // zoom driver, which means that double clicking too fast looks a bit choppy. But
                // Google Earth does the same, so don't care lol.
                //      -slerouzic, 2023-04-28
                return std::make_unique<DoubleClickController>(state, view_index);
            }
            else if (
                e.is_in_viewport && !state.alt
                && is_two_finger_gesture_qualification_event(
                    e, gestures::TwoFingerGesture::Type::PinchRotate))
            {
                return std::make_unique<PinchZoomRotateController>(
                    state, view_index, e.gesture.two_finger_gesture().id,
                    e.gesture.two_finger_gesture().spread(),
                    e.gesture.two_finger_gesture().orientation(),
                    e.gesture.two_finger_gesture().center());
            }
            else if (
                e.is_in_viewport && !state.alt
                && is_two_finger_gesture_qualification_event(
                    e, gestures::TwoFingerGesture::Type::Drag))
            {
                return std::make_unique<TwoFingerDragController>(
                    state, view_index, e.gesture.two_finger_gesture().id,
                    e.gesture.two_finger_gesture().center());
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
            if (type == MovementEventType::Instant && movement.has_zoom())
            {
                return std::make_unique<InstantMovementZoomStepController>(state, view_index);
            }
            else if (type == MovementEventType::Instant && movement.has_rotation())
            {
                return std::make_unique<InstantMovementRotationController>(state);
            }
            else if (type == MovementEventType::Instant && movement.has_translation())
            {
                return std::make_unique<InstantMovementTranslationController>(state);
            }
            else if (type == MovementEventType::BeginContinuous && movement.has_rotation())
            {
                return std::make_unique<
                    ContinuousMovementInterruptionController<InitializedState, BaseController>>(
                    std::make_unique<ContinuousMovementRotationController>(state),
                    movement.continuous_movement_interruption());
            }
            else if (type == MovementEventType::BeginContinuous && movement.has_zoom())
            {
                return std::make_unique<
                    ContinuousMovementInterruptionController<InitializedState, BaseController>>(
                    std::make_unique<ContinuousMovementZoomController>(state, view_index),
                    movement.continuous_movement_interruption());
            }
            else if (type == MovementEventType::BeginContinuous && movement.has_translation())
            {
                return std::make_unique<
                    ContinuousMovementInterruptionController<InitializedState, BaseController>>(
                    std::make_unique<ContinuousMovementTranslationController>(state),
                    movement.continuous_movement_interruption());
            }

            return nullptr;
        }

        CameraControllerType type() const override { return CameraControllerType::UserControl; }
    };

    class IdleController : public BaseController
    {
        std::unique_ptr<MaintainHeightAboveTerrainDriver> _driver;

    public:
        explicit IdleController(
            const lm::ddual_quat& pose,
            double min_height_above_terrain,
            double terrain_collision_inertia) :
            _driver{MaintainHeightAboveTerrainDriver::create(
                pose,
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
            return _driver->work(dt, height_above_terrain, keep_bearing);
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
            const hrz_proto::CameraAnimationOptions& options,
            double min_height_above_terrain,
            double terrain_collision_inertia) :
            _driver{AnimationDriver::create(
                from,
                to,
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

    class MiddleAltDragRotateAroundCenterController : public BaseController
    {
        std::unique_ptr<RotateAroundCenterDriver> _driver;
        const hrz_proto::SceneViewIndex _scene_view;
        lm::dvec2 _rotation_this_frame{};
        bool _finished = false;

    public:
        MiddleAltDragRotateAroundCenterController(
            const InitializedState& state,
            hrz_proto::SceneViewIndex scene_view) :
            _driver{RotateAroundCenterDriver::create(
                state.config.energy_half_time.for_user_controls,
                state.pose,
                state.config.min_tilt,
                state.config.max_tilt,
                state.config.min_height_above_terrain,
                state.config.terrain_collision_inertia)},
            _scene_view{scene_view}
        {
        }

        std::unique_ptr<IController> handle_event(
            CameraManipulatorContext* ctx,
            const InitializedState& state,
            const ViewportEvent& e,
            hrz_proto::SceneViewIndex view_index,
            const CameraViewInfo& view) override
        {
            if (!_finished)
            {
                if (is_mouse_button_up_event(e, platform::Event::MouseButton::Middle))
                {
                    _finished = true;
                }
                else if (
                    view_index == _scene_view
                    && is_platform_event(e, platform::Event::Kind::MouseMove))
                {
                    double sensitivity = view.cam.fovy / view.viewport.size.y;
                    _rotation_this_frame += sensitivity
                        * lm::dvec2(-e.platform.mouse_move.dx, e.platform.mouse_move.dy);
                }
                return nullptr;
            }
            else
            {
                return BaseController::handle_event(ctx, state, e, view_index, view);
            }
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
                _driver->set_rotation_this_frame(_rotation_this_frame.x, _rotation_this_frame.y);
                _rotation_this_frame = lm::dvec2{0, 0};
            }
            return _driver->work(dt, height_above_terrain, keep_bearing);
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
            _driver->update_terrain_settings(min_height_above_terrain, terrain_collision_inertia);
        }
    };

    class RotateAroundPickedTargetController : public BaseController
    {
        std::unique_ptr<OrbitRotateAroundTargetDriver> _driver;
        OneTimePicking<1> _picking;

        bool _has_received_target = false;
        std::optional<lm::dvec3> _target;
        std::optional<lm::dvec3> _bearing_axis;

        lm::vec2 _movement{};
        bool _has_received_movement{};
        bool _finished{};

    protected:
        const hrz_proto::SceneViewIndex _view_index;

        constexpr bool is_finished() const { return _finished; }

        constexpr void finish() { _finished = true; }

        void add_movement(const lm::vec2& move)
        {
            _movement += move;
            _has_received_movement = true;
        }

    public:
        RotateAroundPickedTargetController(
            const InitializedState& state,
            hrz_proto::SceneViewIndex view_index,
            const lm::ivec2& initial_position) :
            _driver{OrbitRotateAroundTargetDriver::create(
                state.config.energy_half_time.for_user_controls,
                state.pose,
                state.controller_bounds_limits(),
                state.controller_max_altitude(),
                state.config.min_tilt,
                state.config.max_tilt,
                state.config.min_height_above_terrain,
                state.config.terrain_collision_inertia)},
            _picking(view_index, {initial_position}),
            _view_index(view_index)
        {
        }

        lm::ddual_quat work(
            CameraManipulatorContext* ctx,
            double dt,
            double height_above_terrain,
            bool keep_bearing,
            const std::array<PickingSystem*, hrz::SCENE_VIEW_COUNT>& picking) override
        {
            if (!_finished)
            {
                _picking.work(picking);

                if (!_has_received_target && _picking.has_retrieved_result())
                {
                    _target = _picking.target();
                    _has_received_target = true;

                    if (_target)
                    {
                        _bearing_axis = hrz::geo_to_normal(hrz::ecef_to_geo2(*_target));
                    }
                }

                if (_target)
                {
                    _driver->set_rotation_this_frame(
                        *_target, *_bearing_axis, -_movement.x * kRotateAroundTargetSensitivity,
                        -_movement.y * kRotateAroundTargetSensitivity);
                    _movement = lm::vec2(0);
                }
            }
            return _driver->work(dt, height_above_terrain, keep_bearing);
        }

        void on_end(CameraManipulatorContext* ctx) override { _picking.cancel(ctx); }

        bool is_idle() override { return _finished && _driver->is_idle(); }

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
    };

    class OrbitMouseDragRotationController : public RotateAroundPickedTargetController
    {
        const platform::Event::MouseButton _button;

    public:
        OrbitMouseDragRotationController(
            const InitializedState& state,
            hrz_proto::SceneViewIndex view_index,
            const lm::ivec2& initial_position,
            platform::Event::MouseButton button) :
            RotateAroundPickedTargetController(state, view_index, initial_position), _button{button}
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
                    view_index == _view_index
                    && is_platform_event(e, platform::Event::Kind::MouseMove))
                {
                    add_movement(lm::vec2(e.platform.mouse_move.dx, e.platform.mouse_move.dy));
                }
                return nullptr;
            }
            else
            {
                return RotateAroundPickedTargetController::handle_event(
                    ctx, state, e, view_index, view);
            }
        }
    };

    class TwoFingerDragController : public RotateAroundPickedTargetController
    {
        const gestures::GestureId _gesture_id;
        lm::vec2 _last_position;

    public:
        TwoFingerDragController(
            const InitializedState& state,
            hrz_proto::SceneViewIndex view_index,
            gestures::GestureId gesture_id,
            const lm::vec2& initial_position) :
            RotateAroundPickedTargetController(state, view_index, lm::ivec2(initial_position)),
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
                if (is_two_finger_gesture_end_event(e, _gesture_id))
                {
                    finish();
                }
                else if (
                    view_index == _view_index && is_two_finger_gesture_move_event(e, _gesture_id))
                {
                    lm::vec2 position = e.gesture.two_finger_gesture().center();
                    lm::vec2 diff = position - std::exchange(_last_position, position);
                    add_movement(diff);
                }

                return nullptr;
            }
            else
            {
                return RotateAroundPickedTargetController::handle_event(
                    ctx, state, e, view_index, view);
            }
        }
    };

    class MoveAroundPlanetController : public BaseController
    {
        static constexpr double kNoAltitude = -99999;

        std::unique_ptr<MoveAroundPlanetDriver> _driver;
        OneTimePicking<1> _picking;
        std::optional<double> _camera_altitude;
        bool _finished = false;

        lm::vec2 _position;
        lm::vec2 _movement{};

        std::optional<double> _ellipsoid_altitude{};
        CameraViewInfo _last_view_info{};

    protected:
        const hrz_proto::SceneViewIndex _view_index;

        constexpr bool is_finished() const { return _finished; }

        constexpr void finish() { _finished = true; }

        void set_movement(lm::vec2 position, lm::vec2 movement, const CameraViewInfo& view)
        {
            _position = position;
            _movement += movement;
            _last_view_info = view;
        }

    public:
        MoveAroundPlanetController(
            const InitializedState& state,
            hrz_proto::SceneViewIndex view_index,
            const lm::vec2& position) :
            _driver{MoveAroundPlanetDriver::create(
                state.config.energy_half_time.for_user_controls,
                state.pose,
                state.controller_bounds_limits(),
                state.config.min_height_above_terrain,
                state.config.terrain_collision_inertia)},
            _picking(view_index, {lm::ivec2(position)}),
            _position(position),
            _view_index(view_index)
        {
        }

        lm::ddual_quat work(
            CameraManipulatorContext* ctx,
            double dt,
            double height_above_terrain,
            bool keep_bearing,
            const std::array<PickingSystem*, hrz::SCENE_VIEW_COUNT>& picking) override
        {
            if (!_finished)
            {
                if (!_camera_altitude)
                {
                    _camera_altitude =
                        hrz::ecef_to_geo3(lm::extract_translation(_driver->get_pose())).alt;
                }

                _picking.work(picking);

                if (!_ellipsoid_altitude && _camera_altitude && _picking.has_retrieved_result())
                {
                    std::optional<lm::dvec3> position = _picking.target();
                    if (position)
                    {
                        _ellipsoid_altitude = ecef_to_geo3(position.value()).alt;
                        if (_ellipsoid_altitude >= _camera_altitude)
                        {
                            _ellipsoid_altitude = kNoAltitude;
                        }
                    }
                    else
                    {
                        _ellipsoid_altitude = kNoAltitude;
                    }
                }

                lm::dquat rotation;

                if (_ellipsoid_altitude && *_ellipsoid_altitude != kNoAltitude
                    && _movement != lm::ivec2{0, 0})
                {
                    lm::vec2 last_position = _position - _movement;
                    _movement = lm::vec2{0, 0};

                    ScreenToEllipsoidTransform transform(
                        _last_view_info.cam.pos, _last_view_info.inv_pv_cc,
                        _last_view_info.viewport.subview_size());
                    auto last_ecef =
                        transform.planet_intersection(last_position, *_ellipsoid_altitude);
                    auto current_ecef =
                        transform.planet_intersection(_position, *_ellipsoid_altitude);

                    if (last_ecef && current_ecef && *last_ecef != *current_ecef)
                    {
                        rotation = lm::rotation_between_vectors<double>(*current_ecef, *last_ecef);
                    }
                }

                _driver->set_rotation_around_earth_center_this_frame(rotation);
            }

            return _driver->work(dt, height_above_terrain, keep_bearing);
        }

        void on_end(CameraManipulatorContext* ctx) override { _picking.cancel(ctx); }

        bool is_idle() override { return _finished && _driver->is_idle(); }

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
    };

    class LeftDragController : public MoveAroundPlanetController
    {
    public:
        LeftDragController(
            const InitializedState& state,
            hrz_proto::SceneViewIndex view_index,
            const lm::ivec2& mouse_position) :
            MoveAroundPlanetController(state, view_index, lm::vec2(mouse_position))
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
                if (is_mouse_button_up_event(e, platform::Event::MouseButton::Left))
                {
                    finish();
                }
                if (view_index == _view_index
                    && is_platform_event(e, platform::Event::Kind::MouseMove))
                {
                    lm::vec2 position(e.platform.mouse_move.x, e.platform.mouse_move.y);
                    lm::vec2 movement(e.platform.mouse_move.dx, e.platform.mouse_move.dy);
                    set_movement(position, movement, view);
                }
                return nullptr;
            }
            else
            {
                return MoveAroundPlanetController::handle_event(ctx, state, e, view_index, view);
            }
        }
    };

    class TouchDragController : public MoveAroundPlanetController
    {
        const gestures::GestureId _gesture_id;
        lm::vec2 _last_position;
        bool _actually_started{};

    public:
        TouchDragController(
            const InitializedState& state,
            hrz_proto::SceneViewIndex view_index,
            gestures::GestureId gesture_id,
            lm::vec2 position) :
            MoveAroundPlanetController(state, view_index, position),
            _gesture_id(gesture_id),
            _last_position(position)
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
                    !_actually_started && view_index == _view_index
                    && is_single_finger_gesture_qualification_event(
                        e, _gesture_id, gestures::SingleFingerGesture::Type::Drag))
                {
                    _actually_started = true;
                }
                else if (
                    _actually_started && view_index == _view_index
                    && is_single_finger_gesture_move_event(e, _gesture_id))
                {
                    lm::vec2 position = e.gesture.single_finger_gesture().position;
                    lm::vec2 movement = position - std::exchange(_last_position, position);
                    set_movement(position, movement, view);
                }
                return nullptr;
            }
            else
            {
                return MoveAroundPlanetController::handle_event(ctx, state, e, view_index, view);
            }
        }
    };

    class ContinuousZoomRotateAroundPickedTargetController : public BaseController
    {
        std::unique_ptr<ZoomBearingDriver> _driver;
        OneTimePicking<1> _picking;
        double _zoom_steps{};
        double _rotate_angle{};
        bool _finished = false;

        std::optional<lm::dvec3> _target;
        std::optional<lm::dvec3> _bearing_axis;

    protected:
        const hrz_proto::SceneViewIndex _view_index;

        constexpr bool is_finished() const { return _finished; }

        constexpr void finish() { _finished = true; }

        ContinuousZoomRotateAroundPickedTargetController(
            const InitializedState& state,
            hrz_proto::SceneViewIndex view_index,
            const lm::ivec2& position) :
            _driver{ZoomBearingDriver::create(
                state.config.energy_half_time.for_user_controls,
                state.pose,
                state.controller_bounds_limits(),
                state.controller_max_altitude(),
                state.config.min_tilt,
                state.config.max_tilt,
                state.config.min_height_above_terrain,
                state.config.terrain_collision_inertia)},
            _picking(view_index, {position}),
            _view_index(view_index)
        {
        }

        void add_steps(double value) { _zoom_steps += value; }

        void add_bearing_rotation(double angle) { _rotate_angle += angle; }

    public:
        lm::ddual_quat work(
            CameraManipulatorContext* ctx,
            double dt,
            double height_above_terrain,
            bool keep_bearing,
            const std::array<PickingSystem*, hrz::SCENE_VIEW_COUNT>& picking) override
        {
            if (!_finished)
            {
                _picking.work(picking);

                if (!_target && _picking.has_retrieved_result() && _picking.target())
                {
                    _target = _picking.target().value();
                    _bearing_axis = hrz::geo_to_normal(hrz::ecef_to_geo2(*_target));
                }

                if (_target)
                {
                    _driver->set_steps(*_target, _zoom_steps);
                    _driver->set_bearing_rotation_this_frame(
                        *_target, *_bearing_axis, _rotate_angle);
                }

                _zoom_steps = 0;
                _rotate_angle = 0;
            }
            return _driver->work(dt, height_above_terrain, keep_bearing);
        }

        void on_end(CameraManipulatorContext* ctx) override { _picking.cancel(ctx); }

        bool is_idle() override { return _finished && _driver->is_idle(); }

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
    };

    class RightDragController : public ContinuousZoomRotateAroundPickedTargetController
    {
    public:
        RightDragController(
            const InitializedState& state,
            hrz_proto::SceneViewIndex view_index,
            const lm::ivec2& mouse_position) :
            ContinuousZoomRotateAroundPickedTargetController(state, view_index, mouse_position)
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
                if (is_mouse_button_up_event(e, platform::Event::MouseButton::Right))
                {
                    finish();
                }
                else if (
                    view_index == _view_index
                    && is_platform_event(e, platform::Event::Kind::MouseMove))
                {
                    add_steps(e.platform.mouse_move.dy * kMouseDragZoomSensitivity);
                }
                return nullptr;
            }
            else
            {
                return ContinuousZoomRotateAroundPickedTargetController::handle_event(
                    ctx, state, e, view_index, view);
            }
        }
    };

    class PinchZoomRotateController : public ContinuousZoomRotateAroundPickedTargetController
    {
        const gestures::GestureId _gesture_id{};
        float _last_spread;
        float _last_orientation;

    public:
        PinchZoomRotateController(
            const InitializedState& state,
            hrz_proto::SceneViewIndex view_index,
            gestures::GestureId gesture_id,
            float spread,
            float orientation,
            const lm::vec2& position) :
            ContinuousZoomRotateAroundPickedTargetController(
                state,
                view_index,
                lm::ivec2(position)),
            _gesture_id(gesture_id),
            _last_spread(spread),
            _last_orientation(orientation)
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
                if (is_two_finger_gesture_end_event(e, _gesture_id))
                {
                    finish();
                }
                else if (
                    view_index == _view_index && is_two_finger_gesture_move_event(e, _gesture_id))
                {
                    float spread = e.gesture.two_finger_gesture().spread();
                    float spread_diff = spread - std::exchange(_last_spread, spread);
                    float orientation = e.gesture.two_finger_gesture().orientation();
                    float orientation_diff = hrz::normalize_angle_around_zero(
                        orientation - std::exchange(_last_orientation, orientation));
                    add_steps(spread_diff * kPinchZoomSensitivity);
                    add_bearing_rotation(orientation_diff);
                }
                return nullptr;
            }
            else
            {
                return ContinuousZoomRotateAroundPickedTargetController::handle_event(
                    ctx, state, e, view_index, view);
            }
        }
    };

    class DiscreteZoomController : public BaseController
    {
        std::unique_ptr<ZoomDriver> _driver;

    public:
        DiscreteZoomController(const InitializedState& state, double energy_half_time) :
            _driver{ZoomDriver::create(
                energy_half_time,
                state.pose,
                state.controller_bounds_limits(),
                state.controller_max_altitude(),
                state.config.min_tilt,
                state.config.max_tilt,
                state.config.min_height_above_terrain,
                state.config.terrain_collision_inertia)}
        {
        }

        void add_steps(const lm::dvec3& target, double steps) { _driver->add_steps(target, steps); }

        lm::ddual_quat work(
            CameraManipulatorContext* ctx,
            double dt,
            double height_above_terrain,
            bool keep_bearing,
            const std::array<PickingSystem*, hrz::SCENE_VIEW_COUNT>& picking) override
        {
            return _driver->work(dt, height_above_terrain, keep_bearing);
        }

        void update_energy_half_time_inner(double e) { _driver->update_energy_half_time(e); }

        void update_terrain_settings(
            double min_height_above_terrain,
            double terrain_collision_inertia) override
        {
            _driver->update_terrain_settings(min_height_above_terrain, terrain_collision_inertia);
        }

        bool is_idle() override { return _driver->is_idle(); }
    };

    class InstantMovementZoomStepController : public DiscreteZoomController
    {
        ZoomMovementPicking _picking;
        double _steps{};

    public:
        InstantMovementZoomStepController(
            const InitializedState& state,
            hrz_proto::SceneViewIndex scene_view_index) :
            DiscreteZoomController(state, state.config.energy_half_time.for_movements),
            _picking(scene_view_index)
        {
        }

        bool is_idle() override
        {
            return _picking.has_result() && DiscreteZoomController::is_idle();
        }

        std::unique_ptr<IController> handle_movement(
            CameraManipulatorContext* ctx,
            const InitializedState& state,
            MovementEventType type,
            const hrz_proto::CameraMovement& movement,
            hrz_proto::SceneViewIndex view_index,
            const CameraViewInfo& view) override
        {
            if (type == MovementEventType::Instant && movement.has_zoom()
                && movement.zoom().ratio() > 0)
            {
                _picking.set_view(view);
                _steps += std::log2(movement.zoom().ratio());
                return nullptr;
            }
            else
            {
                return DiscreteZoomController::handle_movement(
                    ctx, state, type, movement, view_index, view);
            }
        }

        void on_end(CameraManipulatorContext* ctx) override
        {
            _picking.cancel(ctx);
            DiscreteZoomController::on_end(ctx);
        }

        lm::ddual_quat work(
            CameraManipulatorContext* ctx,
            double dt,
            double height_above_terrain,
            bool keep_bearing,
            const std::array<PickingSystem*, hrz::SCENE_VIEW_COUNT>& picking) override
        {
            _picking.work(picking);

            if (_picking.target() && _steps != 0)
            {
                add_steps(*_picking.target(), _steps);
                _steps = 0;
            }

            return DiscreteZoomController::work(
                ctx, dt, height_above_terrain, keep_bearing, picking);
        }

        void update_energy_half_time(const EnergyHalfTime& e) override
        {
            update_energy_half_time_inner(e.for_movements);
        }
    };

    class ContinuousMovementZoomController : public IContinuousMovementController
    {
        std::unique_ptr<ZoomDriver> _driver;
        ZoomMovementPicking _picking;
        double _steps{};
        bool _active = false;

    public:
        ContinuousMovementZoomController(
            const InitializedState& state,
            hrz_proto::SceneViewIndex scene_view) :
            _driver{ZoomDriver::create(
                state.config.energy_half_time.for_movements,
                state.pose,
                state.controller_bounds_limits(),
                state.controller_max_altitude(),
                state.config.min_tilt,
                state.config.max_tilt,
                state.config.min_height_above_terrain,
                state.config.terrain_collision_inertia)},
            _picking(scene_view)
        {
        }

        void on_end(CameraManipulatorContext* ctx) override { _picking.cancel(ctx); }

        lm::ddual_quat work(
            CameraManipulatorContext* ctx,
            double dt,
            double height_above_terrain,
            bool should_keep_bearing,
            const std::array<PickingSystem*, hrz::SCENE_VIEW_COUNT>& picking) override
        {
            _picking.work(picking);
            if (_active && _picking.target())
            {
                _driver->set_steps_velocity(*_picking.target(), _steps);
            }
            return _driver->work(dt, height_above_terrain, false);
        }

        bool handle_begin_continuous_movement(
            const hrz_proto::CameraMovement& movement,
            const CameraViewInfo& view) override
        {
            if (movement.has_zoom())
            {
                _picking.set_view(view);
                _steps = std::log2(movement.zoom().ratio());
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

        bool is_active() const override { return _active; }

        bool is_idle() const override { return !_active && _driver->is_idle(); }

        void restart_with_pose(CameraManipulatorContext* ctx, const lm::ddual_quat& pose) override
        {
            _driver = _driver->recreate_with_pose(pose);
            _picking.reset(ctx);
        }

        hrz_proto::CameraMovementType movement_type() const override
        {
            return hrz_proto::CameraMovementType::CAMERA_ZOOM;
        }

        void update_energy_half_time(double e) override
        {
            return _driver->update_energy_half_time(e);
        }

        void update_terrain_settings(
            double min_height_above_terrain,
            double terrain_collision_inertia) override
        {
            _driver->update_terrain_settings(min_height_above_terrain, terrain_collision_inertia);
        }
    };

    class DiscreteZoomPickedTargetController : public DiscreteZoomController
    {
        struct StepRequest
        {
            PositionPickingTicketWithContext ticket;
            lm::ivec2 mouse_position;
            double value{};
        };

        std::optional<StepRequest> _next_step;
        std::deque<StepRequest> _queued_steps;
        hrz_proto::SceneViewIndex _scene_view;

        // We accumulate mouse scrolls if the mouse position has not changed much since last
        // unfinished query.
        StepRequest* get_step_request(lm::ivec2 mouse_position)
        {
            if (!_next_step && !_queued_steps.empty())
            {
                auto* candidate = &_queued_steps.back();
                lm::vec2 diff(candidate->mouse_position - mouse_position);

                if (lm::length(diff < 100))
                {
                    return candidate;
                }
            }

            if (!_next_step)
            {
                _next_step = StepRequest{};
                _next_step->mouse_position = mouse_position;
                _next_step->value = 0;
            }

            return &*_next_step;
        }

    protected:
        void make_step_request(
            lm::ivec2 mouse_position,
            double value,
            hrz_proto::SceneViewIndex scene_view)
        {
            StepRequest* step = get_step_request(mouse_position);
            _scene_view = scene_view;
            step->value += value;
        }

        DiscreteZoomPickedTargetController(
            const InitializedState& state,
            hrz_proto::SceneViewIndex scene_view) :
            DiscreteZoomController(state, state.config.energy_half_time.for_user_controls),
            _scene_view(scene_view)
        {
        }

    public:
        bool is_idle() override
        {
            return (_queued_steps.empty() || _next_step.has_value())
                && DiscreteZoomController::is_idle();
        }

        lm::ddual_quat work(
            CameraManipulatorContext* ctx,
            double dt,
            double height_above_terrain,
            bool keep_bearing,
            const std::array<PickingSystem*, hrz::SCENE_VIEW_COUNT>& picking) override
        {
            // Dequeue steps when picking is finished (or when invalid).
            // Stop at the first valid but unfinished step because since they are
            // ordered by the time they have been queued, we know that no subsequent
            // queued step will be completed.
            bool should_continue_dequeuing = true;
            while (!_queued_steps.empty() && should_continue_dequeuing)
            {
                const auto& step = _queued_steps.front();

                PickingSystem* ps = picking[step.ticket.view];

                if (ps && step.ticket.system_ptr == (uintptr_t)ps)
                {
                    picking::PositionResult res;
                    bool got_result = picking::retrieve_result(ps, step.ticket.ticket, &res);
                    if (!got_result)
                    {
                        should_continue_dequeuing = false;
                        break;
                    }

                    if (res.position)
                    {
                        lm::dvec3 position = *res.position;
                        add_steps(position, step.value);
                    }
                }

                if (should_continue_dequeuing)
                {
                    _queued_steps.pop_front();
                }
            }

            if (_next_step)
            {
                StepRequest step = *_next_step;
                _next_step = std::nullopt;

                PickingSystem* ps = picking[_scene_view];
                if (ps)
                {
                    step.ticket.system_ptr = (uintptr_t)ps;
                    step.ticket.view = _scene_view;
                    step.ticket.ticket = picking::schedule_pick(ps, step.mouse_position);
                    _queued_steps.push_back(step);
                }
            }

            return DiscreteZoomController::work(
                ctx, dt, height_above_terrain, keep_bearing, picking);
        }

        void on_end(CameraManipulatorContext* ctx) override
        {
            for (const auto& s : _queued_steps)
            {
                ctx->cancel_picking(s.ticket);
            }
        }

        void update_energy_half_time(const EnergyHalfTime& e) override
        {
            update_energy_half_time_inner(e.for_user_controls);
        }
    };

    class MouseWheelController : public DiscreteZoomPickedTargetController
    {
    public:
        MouseWheelController(const InitializedState& state, hrz_proto::SceneViewIndex scene_view) :
            DiscreteZoomPickedTargetController(state, scene_view)
        {
        }

        std::unique_ptr<IController> handle_event(
            CameraManipulatorContext* ctx,
            const InitializedState& state,
            const ViewportEvent& e,
            hrz_proto::SceneViewIndex view_index,
            const CameraViewInfo& view) override
        {
            if (e.is_in_viewport && is_mouse_wheel_event(e))
            {
                double value = (double)e.platform.mouse_wheel.wheel * kMouseWheelZoomSensitivity;
                lm::ivec2 mouse_position = {e.platform.mouse_wheel.x, e.platform.mouse_wheel.y};
                make_step_request(mouse_position, value, view_index);
                return nullptr;
            }
            else
            {
                return DiscreteZoomPickedTargetController::handle_event(
                    ctx, state, e, view_index, view);
            }
        }
    };

    class DoubleClickController : public DiscreteZoomPickedTargetController
    {
    public:
        DoubleClickController(const InitializedState& state, hrz_proto::SceneViewIndex scene_view) :
            DiscreteZoomPickedTargetController(state, scene_view)
        {
        }

        std::unique_ptr<IController> handle_event(
            CameraManipulatorContext* ctx,
            const InitializedState& state,
            const ViewportEvent& e,
            hrz_proto::SceneViewIndex view_index,
            const CameraViewInfo& view) override
        {
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
                lm::ivec2 mouse_position = {e.platform.mouse_button.x, e.platform.mouse_button.y};
                make_step_request(mouse_position, value, view_index);
                return nullptr;
            }
            else
            {
                return DiscreteZoomPickedTargetController::handle_event(
                    ctx, state, e, view_index, view);
            }
        }
    };

    class InstantMovementTranslationController : public BaseController
    {
        std::unique_ptr<MoveAroundPlanetDriver> _driver;
        TranslationMovementDirectionHelper _helper;

    public:
        explicit InstantMovementTranslationController(const InitializedState& state) :
            _driver{MoveAroundPlanetDriver::create(
                state.config.energy_half_time.for_movements,
                state.pose,
                state.controller_bounds_limits(),
                state.config.min_height_above_terrain,
                state.config.terrain_collision_inertia)}
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
            if (type == MovementEventType::Instant && movement.has_translation())
            {
                std::optional<lm::ddual_quat> pose = _driver->get_pose();
                if (!pose) return nullptr;

                auto rotation = _helper.change_direction(
                    *pose, state.should_keep_bearing, movement.translation());
                if (rotation.changed)
                {
                    _driver->set_rotation_around_earth_center_total(
                        rotation.rotation, rotation.rotation_t);
                }
                else
                {
                    _driver->add_rotation_around_earth_center_total(
                        rotation.rotation, rotation.rotation_t);
                }

                return nullptr;
            }
            else
            {
                return BaseController::handle_movement(
                    ctx, state, type, movement, view_index, view);
            }
        }

        lm::ddual_quat work(
            CameraManipulatorContext* ctx,
            double dt,
            double height_above_terrain,
            bool keep_bearing,
            const std::array<PickingSystem*, hrz::SCENE_VIEW_COUNT>& picking) override
        {
            return _driver->work(dt, height_above_terrain, keep_bearing);
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
    };

    class ContinuousMovementTranslationController : public IContinuousMovementController
    {
        std::unique_ptr<MoveAroundPlanetDriver> _driver;
        TranslationMovementDirectionHelper _helper;

        std::optional<hrz_proto::CameraTranslate> _movement_to_apply;
        std::optional<hrz_proto::CameraTranslate> _last_received_movement;

        lm::dquat _rotation;
        double _rotation_t{};
        bool _active = false;

    public:
        explicit ContinuousMovementTranslationController(const InitializedState& state) :
            _driver{MoveAroundPlanetDriver::create(
                state.config.energy_half_time.for_movements,
                state.pose,
                state.controller_bounds_limits(),
                state.config.min_height_above_terrain,
                state.config.terrain_collision_inertia)}
        {
        }

        lm::ddual_quat work(
            CameraManipulatorContext* ctx,
            double dt,
            double height_above_terrain,
            bool should_keep_bearing,
            const std::array<PickingSystem*, hrz::SCENE_VIEW_COUNT>& picking) override
        {
            if (_movement_to_apply)
            {
                auto result = _helper.change_direction(
                    _driver->get_pose(), should_keep_bearing, _movement_to_apply.value());
                _movement_to_apply = std::nullopt;
                _rotation = result.rotation;
                _rotation_t = result.rotation_t;
            }

            if (_active)
            {
                _driver->set_rotation_velocity_around_earth_center(_rotation, _rotation_t);
            }
            return _driver->work(dt, height_above_terrain, should_keep_bearing);
        }

        bool handle_begin_continuous_movement(
            const hrz_proto::CameraMovement& movement,
            const CameraViewInfo&) override
        {
            if (movement.has_translation())
            {
                _movement_to_apply = movement.translation();
                _last_received_movement = _movement_to_apply;
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
            if (movement.has_translation())
            {
                _movement_to_apply = std::nullopt;
                _last_received_movement = std::nullopt;
                _active = false;
                return true;
            }
            else
            {
                return false;
            }
        }

        bool is_active() const override { return _active; }

        bool is_idle() const override { return _driver->is_idle(); }

        void restart_with_pose(CameraManipulatorContext* ctx, const lm::ddual_quat& pose) override
        {
            _movement_to_apply = _last_received_movement;
            _driver = _driver->recreate_with_pose(pose);
        }

        hrz_proto::CameraMovementType movement_type() const override
        {
            return hrz_proto::CameraMovementType::CAMERA_TRANSLATION;
        }

        void update_energy_half_time(double e) override
        {
            return _driver->update_energy_half_time(e);
        }

        void update_terrain_settings(
            double min_height_above_terrain,
            double terrain_collision_inertia) override
        {
            _driver->update_terrain_settings(min_height_above_terrain, terrain_collision_inertia);
        }
    };

    class InstantMovementRotationController : public BaseController
    {
        std::unique_ptr<RotateAroundCenterDriver> _driver;

    public:
        explicit InstantMovementRotationController(const InitializedState& state) :
            _driver{RotateAroundCenterDriver::create(
                state.config.energy_half_time.for_movements,
                state.pose,
                state.config.min_tilt,
                state.config.max_tilt,
                state.config.min_height_above_terrain,
                state.config.terrain_collision_inertia)}
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

        lm::ddual_quat work(
            CameraManipulatorContext* ctx,
            double dt,
            double height_above_terrain,
            bool keep_bearing,
            const std::array<PickingSystem*, hrz::SCENE_VIEW_COUNT>&) override
        {
            return _driver->work(dt, height_above_terrain, keep_bearing);
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
    };

    class ContinuousMovementRotationController : public IContinuousMovementController
    {
        std::unique_ptr<RotateAroundCenterDriver> _driver;
        lm::dvec2 _velocity;
        bool _active = false;

    public:
        explicit ContinuousMovementRotationController(const InitializedState& state) :
            _driver{RotateAroundCenterDriver::create(
                state.config.energy_half_time.for_movements,
                state.pose,
                state.config.min_tilt,
                state.config.max_tilt,
                state.config.min_height_above_terrain,
                state.config.terrain_collision_inertia)}
        {
        }

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

        bool is_active() const override { return _active; }

        bool is_idle() const override { return _driver->is_idle(); }

        void restart_with_pose(CameraManipulatorContext* ctx, const lm::ddual_quat& pose) override
        {
            _driver = _driver->recreate_with_pose(pose);
        }

        hrz_proto::CameraMovementType movement_type() const override
        {
            return hrz_proto::CameraMovementType::CAMERA_ROTATION;
        }

        void update_energy_half_time(double e) override
        {
            return _driver->update_energy_half_time(e);
        }

        void update_terrain_settings(
            double min_height_above_terrain,
            double terrain_collision_inertia) override
        {
            _driver->update_terrain_settings(min_height_above_terrain, terrain_collision_inertia);
        }
    };

    static bool can_continue_pointing_north(const lm::ddual_quat& pose)
    {
        Pose p = pose_from_dual_quat(pose);
        return std::abs(p.bearing) <= kNorthBearingEpsilon
            && std::abs(p.position.lat) <= kNorthLatitudeEpsilon;
    }

    void initialize_with_pose(const lm::ddual_quat& pose)
    {
        assert(std::holds_alternative<UninitializedState>(_state));
        auto config = std::get<UninitializedState>(_state).config;
        _state = InitializedState{config, pose, false};
        replace_controller(std::make_unique<IdleController>(
            pose, config.min_height_above_terrain, config.terrain_collision_inertia));
    }

public:
    OrbitManipulator(
        const EnergyHalfTime& energy_half_time,
        std::optional<lm::ddual_quat> pose,
        const hrz::GeoBounds& bounds_limits,
        double max_altitude,
        double min_tilt,
        double max_tilt,
        double min_height_above_terrain,
        double terrain_collision_inertia,
        const hrz_proto::CameraAnimationOptions& correction) :
        _state{UninitializedState{
            energy_half_time,
            bounds_limits.is_empty()
                ? hrz::GeoBounds{bounds_limits.west, bounds_limits.east, -lm::PI, lm::PI}
                : bounds_limits,
            max_altitude, min_tilt, max_tilt, min_height_above_terrain, terrain_collision_inertia,
            correction}}
    {
        if (bounds_limits.is_empty())
        {
            HRZ_LOG_WARNING("Invalid latitude bounds for orbit manipulator");
        }

        if (pose) initialize_with_pose(*pose);
    }

    void set_pose(const lm::ddual_quat& pose) override { initialize_with_pose(pose); }

    void handle_event(
        const ViewportEvent& e,
        hrz_proto::SceneViewIndex view_index,
        const CameraViewInfo& view) override
    {
        if (!std::holds_alternative<InitializedState>(_state)) return;
        auto& state = std::get<InitializedState>(_state);

        if (e.kind == Event::Kind::ModKeyDown)
            state.alt = true;
        else if (e.kind == Event::Kind::ModKeyUp)
            state.alt = false;

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

    bool correct_position(GeoPosition3* pos) const
    {
        assert(std::holds_alternative<InitializedState>(_state));
        const auto& config = std::get<InitializedState>(_state).config;

        // Overcorrect slightly so that we're sure that even with numerical innacuracies we're
        // inside the correction zone.
        static constexpr double kAltitudeOvercorrection = 0.95;
        static constexpr double kAngleOvercorrection = 0.95;

        bool corrected = false;
        const hrz::PrecomputedClampAngle lon_clamp(
            config.bounds_limits.west, config.bounds_limits.east);
        const hrz::PrecomputedClampAngle lat_clamp(
            config.bounds_limits.south, config.bounds_limits.north);

        if (lat_clamp.try_clamp(&pos->lat))
        {
            pos->lat = normalize_angle_around_zero(
                normalize_angle_around_zero(pos->lat - lat_clamp.center_angle)
                    * kAngleOvercorrection
                + lat_clamp.center_angle);
            corrected = true;
        }

        if (lon_clamp.try_clamp(&pos->lon))
        {
            pos->lon = normalize_angle_around_zero(
                normalize_angle_around_zero(pos->lon - lon_clamp.center_angle)
                    * kAngleOvercorrection
                + lon_clamp.center_angle);
            corrected = true;
        }

        if (pos->alt > config.max_altitude)
        {
            pos->alt = config.max_altitude * kAltitudeOvercorrection;
            corrected = true;
        }

        return corrected;
    }

    bool is_animating()
    {
        auto* c = controller();
        return c->type() == CameraControllerType::Animation && !c->is_idle();
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
        _last_height_above_terrain = height_above_terrain;

        if (!std::holds_alternative<InitializedState>(_state)) return lm::ddual_quat{};
        InitializedState& state = std::get<InitializedState>(_state);

        if (!is_animating())
        {
            state.should_keep_bearing =
                state.should_keep_bearing && can_continue_pointing_north(state.pose);
        }

        if (!controller()->is_idle())
        {
            _last_non_idle_time = hrz::now_frame_s();
        }

        if (state.config.correction.duration() > 0)
        {
            // The delay before we start the correction animation is half the duration of the
            // correction animation.
            if (hrz::now_frame_s() - _last_non_idle_time > state.config.correction.duration() * 0.5)
            {
                Pose pose = pose_from_dual_quat(state.pose);
                if (correct_position(&pose.position))
                {
                    replace_controller(std::make_unique<AnimationController>(
                        state.pose, to_dual_quat(pose), state.config.correction,
                        state.config.min_height_above_terrain,
                        state.config.terrain_collision_inertia));
                }
                // So that we don't check too often
                _last_non_idle_time = hrz::now_frame_s();
            }
        }

        state.pose = work_controller(dt, height_above_terrain, picking);

        return state.pose;
    }

    bool should_keep_bearing() const override
    {
        if (std::holds_alternative<InitializedState>(_state))
            return std::get<InitializedState>(_state).should_keep_bearing;
        else
            return true;
    }

    void reset_north(const hrz_proto::ResetNorthParams& params) override
    {
        if (!std::holds_alternative<InitializedState>(_state)) return;
        InitializedState& state = std::get<InitializedState>(_state);

        Pose new_pose = pose_from_dual_quat(state.pose);
        new_pose.bearing = 0;
        new_pose.tilt = clamp_angle(
            new_pose.tilt, state.config.min_tilt - lm::PI / 2, state.config.max_tilt - lm::PI / 2);
        correct_position(&new_pose.position);

        if (params.reset_tilt())
        {
            new_pose.tilt = state.config.min_tilt - lm::PI / 2;
        }

        if (params.animation_options().duration() > 0)
        {
            replace_controller(std::make_unique<AnimationController>(
                state.pose, to_dual_quat(new_pose), params.animation_options(),
                state.config.min_height_above_terrain, state.config.terrain_collision_inertia));
        }
        else
        {
            state.pose = to_dual_quat(new_pose);
            replace_controller(std::make_unique<IdleController>(
                state.pose, state.config.min_height_above_terrain,
                state.config.terrain_collision_inertia));
        }

        state.should_keep_bearing = true;
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
        if (mu_begin_treenode(ctx, "Orbit manipulator"))
        {
            if (std::holds_alternative<InitializedState>(_state))
            {
                const auto& state = std::get<InitializedState>(_state);

                static const int layout[] = {150, -1};
                mu_layout_row(ctx, 2, layout, 0);

                fmt::memory_buffer buffer;

                mu_text(ctx, "Western bound");
                mu_text(
                    ctx,
                    hrz::format_to_buffer(
                        buffer, "{:.6f} deg", lm::degrees(state.config.bounds_limits.west)));
                mu_text(ctx, "Eastern bound");
                mu_text(
                    ctx,
                    hrz::format_to_buffer(
                        buffer, "{:.6f} deg", lm::degrees(state.config.bounds_limits.east)));
                mu_text(ctx, "Southern bound");
                mu_text(
                    ctx,
                    hrz::format_to_buffer(
                        buffer, "{:.6f} deg", lm::degrees(state.config.bounds_limits.south)));
                mu_text(ctx, "Northern bound");
                mu_text(
                    ctx,
                    hrz::format_to_buffer(
                        buffer, "{:.6f} deg", lm::degrees(state.config.bounds_limits.north)));
                mu_text(ctx, "Max altitude");
                mu_text(ctx, hrz::format_to_buffer(buffer, "{:.2f} m", state.config.max_altitude));
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
                mu_text(ctx, "Should keep bearing");
                mu_text(ctx, state.should_keep_bearing ? "Yes" : "No");
                mu_text(ctx, "Alternative mode");
                mu_text(ctx, state.alt ? "Yes" : "No");
            }

            mu_end_treenode(ctx);
        }
    }
};

constexpr double OrbitManipulator::MoveAroundPlanetController::kNoAltitude;

std::unique_ptr<CameraManipulator> create_orbit_manipulator(
    const EnergyHalfTime& energy_half_time,
    std::optional<lm::ddual_quat> pose,
    const hrz::GeoBounds& bounds_limits,
    double max_altitude,
    double min_tilt,
    double max_tilt,
    double min_height_above_terrain,
    double terrain_collision_inertia,
    const hrz_proto::CameraAnimationOptions& correction)
{
    return std::make_unique<OrbitManipulator>(
        energy_half_time, pose, bounds_limits, max_altitude, min_tilt, max_tilt,
        min_height_above_terrain, terrain_collision_inertia, correction);
}

} // namespace hrz::camera
