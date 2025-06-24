#include "camera/hrz_core_camera_animation.h"
#include "camera/hrz_core_camera_manipulator.h"
#include "camera/hrz_core_camera_system.h"
#include "camera/hrz_core_camera_viewpoint.h"
#include "hrz_core_events.h"
#include "hrz_core_picking_system.h"
#include "hrz_core_render.h"
#include "planet/hrz_core_planet_surface.h"

#include <hrz_common_geo.h>
#include <hrz_common_geometry.h>
#include <hrz_common_proto_geo.h>
#include <hrz_fnd_format.h>
#include <hrz_fnd_log.h>
#include <hrz_fnd_maths.h>
#include <hrz_fnd_time.h>
#include <hrz_protocol_path_builder.h>

extern "C"
{
#include <microui/microui.h>
}

// Quick overview of the camera control model:
//
// Manipulators define a set of degrees of freedom & constraints on the camera, and internally
// handle a state machine.
//
// This state machine is a set of controllers, each controller representing a state, more or less.
// Controllers respond to events to implement movement, or give control to other controllers. More
// controllers use drivers to actually implement the movement.
//
// Drivers move the camera using a set of degrees of freedom & apply the associated constraints.
// They also handle inertia and animations. They don't depend on input.
//
// So to summarize :
//  - Manipulator = camera mode
//  - Controller = state in the manipulator state machine
//  - Driver = actually moves the camera

namespace hrz::camera
{
bool is_interruptible_event(const ViewportEvent& e)
{
    return e.is_in_viewport
        && (is_mouse_wheel_event(e) || is_platform_event(e, platform::Event::Kind::MouseButtonDown)
            || is_platform_event(e, platform::Event::Kind::MouseButtonDoubleClick)
            || e.kind == Event::Kind::Gesture);
}

class TransitionAnimationManipulator : public CameraManipulator
{
    std::unique_ptr<CameraManipulator> _next;
    lm::ddual_quat _pose;
    bool _is_ready{};

    AnimationPlayer _animation;

    hrz_proto::CameraAnimationOptions _options;
    bool _interruptible{};
    double _min_height_above_terrain{};
    double _terrain_collision_inertia{};

    void bake_animation(const lm::ddual_quat& to)
    {
        assert(!_is_ready);
        _animation = AnimationPlayer(
            _pose, to, _options, _min_height_above_terrain, _terrain_collision_inertia);
        _is_ready = true;
        context()->add_notification()->mutable_animation_started();
    }

public:
    TransitionAnimationManipulator(
        std::unique_ptr<CameraManipulator> next,
        const lm::ddual_quat& from,
        const std::optional<lm::ddual_quat>& to,
        const hrz_proto::CameraAnimationOptions& options,
        bool interruptible,
        double min_height_above_terrain,
        double terrain_collision_inertia) :
        _next{std::move(next)},
        _pose{from},
        _is_ready{false},
        _options{options},
        _interruptible{interruptible},
        _min_height_above_terrain{min_height_above_terrain},
        _terrain_collision_inertia{terrain_collision_inertia}
    {
        if (to.has_value())
        {
            bake_animation(*to);
        }
    }

    void interrupt()
    {
        context()->add_notification()->mutable_animation_interrupted();
        _animation.force_finish();
        _next->initialize_with_pose(_pose);
    }

    void handle_event(
        const ViewportEvent& e,
        hrz_proto::SceneViewIndex view_index,
        const CameraViewInfo& view) override
    {
        if (_interruptible && !_animation.is_finished() && is_interruptible_event(e))
        {
            interrupt();
        }

        if (_animation.is_finished()) _next->handle_event(e, view_index, view);
    }

    bool is_ready_to_handle_movements() const override
    {
        // We just discard those that we shouldn't yet have received.
        return true;
    }

    void handle_movement(
        MovementEventType movement_type,
        const hrz_proto::CameraMovement& movement,
        hrz_proto::SceneViewIndex view_index,
        const CameraViewInfo& view) override
    {
        if (_interruptible && !_animation.is_finished())
        {
            interrupt();
        }

        if (_animation.is_finished())
        {
            _next->handle_movement(movement_type, movement, view_index, view);
        }
    }

    void set_pose(const lm::ddual_quat& pose) override { bake_animation(pose); }

    lm::ddual_quat work_inner(
        double dt,
        float fovy,
        const ViewportInfo& viewport,
        double height_above_terrain,
        const std::array<PickingSystem*, hrz::SCENE_VIEW_COUNT>& picking,
        const std::function<void(gsl::span<const hrz_proto::CameraNotification>)>& notifications_cb)
        override
    {
        if (!_animation.is_finished())
        {
            _pose = _animation.advance(dt, height_above_terrain);
            if (_animation.is_finished())
            {
                _next->initialize_with_pose(_pose);
                context()->add_notification()->mutable_animation_ended();
            }
        }

        work_controller(dt, height_above_terrain, picking);

        if (_animation.is_finished())
        {
            return _next->work(dt, fovy, viewport, height_above_terrain, picking, notifications_cb);
        }
        else
        {
            return _pose;
        }
    }

    void reset_north(const hrz_proto::ResetNorthParams& params) override
    {
        if (_interruptible && !_animation.is_finished())
        {
            interrupt();
        }

        if (_animation.is_finished())
        {
            _next->reset_north(params);
        }
    }

    void destroy(const std::array<PickingSystem*, hrz::SCENE_VIEW_COUNT>& picking) override
    {
        CameraManipulator::destroy(picking);
        _next->destroy(picking);
    }

    void update_energy_half_time(const EnergyHalfTime& e) override
    {
        _next->update_energy_half_time(e);
    }

    void update_terrain_settings(double min_height_above_terrain, double terrain_collision_inertia)
        override
    {
        _next->update_terrain_settings(min_height_above_terrain, terrain_collision_inertia);
    }

    void dev_ui(mu_Context* ctx) const override
    {
        if (mu_begin_treenode(ctx, "Transition animation manipulator"))
        {
            static const int layout[] = {150, -1};
            mu_layout_row(ctx, 2, layout, 0);

            mu_text(ctx, "Ready");
            mu_text(ctx, _is_ready ? "Yes" : "No");
            mu_text(ctx, "Interruptible");
            mu_text(ctx, _interruptible ? "Yes" : "No");

            {
                static const int layout[] = {-1};
                mu_layout_row(ctx, 1, layout, 0);

                mu_text(ctx, "Next:");
                _next->dev_ui(ctx);
            }

            mu_end_treenode(ctx);
        }
    }
};

static constexpr double kMinInertia = 0.0001;
static constexpr double kIdleDetectionDelayS = 0.3;

class CameraImpl : public Camera
{
    EnergyHalfTime _energy_half_time{};
    double _min_height_above_terrain{};
    double _terrain_collision_inertia{};
    lm::ddual_quat _pose{};
    CameraInfo _baked_info;
    std::vector<std::unique_ptr<CameraManipulator>> _manipulators_to_destroy;
    std::unique_ptr<CameraManipulator> _manipulator;
    double _last_work_time = 0;

    hrz_proto::CameraIndex _index;
    bool _camera_settings_changed = true;

    bool _last_frame_idle = true;
    double _last_movement_time = 0;

    struct QueuedElevationQuery
    {
        lm::ddual_quat pose;
        hrz::GeoPosition2 target;
        std::optional<planet::ElevationQueryTicket> ticket;
    };

    std::optional<QueuedElevationQuery> _queued_elevation_query;
    std::vector<planet::ElevationQueryTicket> _elevation_queries_to_cancel;

    void bake_info() { _baked_info = CameraInfo::make_from_fov_and_pose(_baked_info.fovy, _pose); }

    void cancel_elevation_query_if_any()
    {
        if (_queued_elevation_query && _queued_elevation_query->ticket)
        {
            _elevation_queries_to_cancel.push_back(_queued_elevation_query->ticket.value());
        }
        _queued_elevation_query = std::nullopt;
    }

    void finish_elevation_query(double elevation)
    {
        lm::ddual_quat pose = _queued_elevation_query->pose;
        hrz::GeoPosition2 target = _queued_elevation_query->target;
        _queued_elevation_query.reset();

        lm::dvec3 normal = hrz::geo_to_normal(target);
        pose = lm::translation_dquat(normal * elevation) * pose;

        _manipulator->initialize_with_pose(pose);
        _pose = pose;
        bake_info();
    }

public:
    CameraImpl(
        hrz_proto::CameraIndex index,
        float fov,
        double user_controls_inertia,
        double movements_inertia,
        double min_height_above_terrain,
        double terrain_collision_inertia) :
        _energy_half_time{
            std::max(kMinInertia, user_controls_inertia), std::max(kMinInertia, movements_inertia)},
        _min_height_above_terrain(min_height_above_terrain),
        _terrain_collision_inertia(terrain_collision_inertia),
        _last_work_time(hrz::now_frame_s()),
        _index(index)
    {
        _baked_info.fovy = lm::radians(fov);
    }

    const CameraInfo& get_info() const override
    {
        assert(_manipulator && "Getting camera info before it has been baked");
        return _baked_info;
    }

    void handle_event(
        const ViewportEvent& e,
        hrz_proto::SceneViewIndex view_index,
        const CameraViewInfo& view) override
    {
        if (_manipulator)
        {
            _manipulator->handle_event(e, view_index, view);
        }
    }

    void notify_model_update(
        scene_model::UpdateType update_type,
        const scene_model::CameraSettingsPath& path) override
    {
        _camera_settings_changed = true;
    }

    void place_camera_at_initial_location(const ViewportInfo& viewport)
    {
        // Place the camera such that the whole planet is visible
        double aspect_ratio = 1.0;
        if (viewport.size.x != 0 && viewport.size.y != 0)
        {
            aspect_ratio = viewport.aspect_ratio();
        }

        double distance = (hrz::EARTH_RADIUS * 1.05)
                / (std::sin(_baked_info.fovy * 0.5) * std::min(aspect_ratio, 1.0))
            - hrz::EARTH_RADIUS;

        distance = std::min(distance, 2 * hrz::EARTH_RADIUS);

        AngularViewpoint vp = {GeoPosition3{0, 0, 0}, distance, 0, 0};
        _pose = to_dual_quat(vp);
        bake_info();
    }

    bool work(
        SceneModel* model,
        const ViewportInfo& viewport,
        double height_above_terrain,
        const std::array<PickingSystem*, hrz::SCENE_VIEW_COUNT>& picking,
        PlanetSurface* planet,
        const std::function<void(gsl::span<const hrz_proto::CameraNotification>)>& notifications_cb)
        override
    {
        if (_camera_settings_changed)
        {
            SceneModelAccessor acc(model);
            hrz_proto::CameraSettingsPathBuilder<SceneModelAccessor> builder(acc, _index);
            auto settings = builder.get();
            _baked_info.fovy = lm::radians(settings.fovy());
            _energy_half_time = EnergyHalfTime{
                std::max(kMinInertia, (double)settings.user_controls_inertia()),
                std::max(kMinInertia, (double)settings.movements_inertia())};
            _min_height_above_terrain = settings.min_height_above_terrain();
            _terrain_collision_inertia = settings.terrain_collision_inertia();
            if (_manipulator)
            {
                _manipulator->update_energy_half_time(_energy_half_time);
                _manipulator->update_terrain_settings(
                    _min_height_above_terrain, _terrain_collision_inertia);
            }
            _camera_settings_changed = false;
        }

        if (!_elevation_queries_to_cancel.empty())
        {
            for (auto ticket : _elevation_queries_to_cancel)
            {
                planet::cancel_elevation_query(planet, ticket);
            }
            _elevation_queries_to_cancel.clear();
        }

        if (_queued_elevation_query)
        {
            if (!_queued_elevation_query->ticket)
            {
                lm::dvec2 point = hrz::geo_to_web_mercator(_queued_elevation_query->target);
                _queued_elevation_query->ticket =
                    planet::query_elevation(planet, point, {monitoring::systems::Camera});
            }
            else if (planet::is_elevation_query_ready(planet, *_queued_elevation_query->ticket))
            {
                auto result =
                    planet::retrieve_elevation_query(planet, *_queued_elevation_query->ticket);
                if (result)
                {
                    assert(result->size() == 1);
                    // @Safety the lifetime of the temporary blob array data is extended to the end
                    // of the call, and anyway the value is a primitive type, copied without keeping
                    // a reference.
                    finish_elevation_query(result->get_data().unsafe_at(0));
                }
                else
                {
                    finish_elevation_query(0.0);
                }
                assert(!_queued_elevation_query);
            }
        }

        // At this point, if the elevation query was ready, we would have finished and dequeued it.
        // So it's not finished. So we can't go any further.
        if (_queued_elevation_query) return false;

        if (!_manipulator)
        {
            place_camera_at_initial_location(viewport);
            replace_manipulator(create_orbit_manipulator(
                _energy_half_time, _pose, hrz::GeoBounds::full(), kMaxAltitude, kTiltLimitMin,
                kTiltLimitMax, _min_height_above_terrain, _terrain_collision_inertia, {}));
            bake_info();
        }

        assert(_manipulator);

        double now = hrz::now_frame_s();
        double dt = now - std::exchange(_last_work_time, now);
        lm::ddual_quat new_pose = _manipulator->work(
            dt, _baked_info.fovy, viewport, height_above_terrain, picking, notifications_cb);

        bool has_moved = std::exchange(_pose, new_pose) != new_pose;
        if (has_moved)
        {
            if (_last_frame_idle)
            {
                _last_frame_idle = false;

                hrz_proto::CameraNotification notification[1]{};
                notification[0].set_camera_index(_index);
                notification[0].mutable_motion_started();
                notifications_cb(notification);
            }

            _last_movement_time = now;
            bake_info();
        }
        else if (!_last_frame_idle && now - _last_movement_time > kIdleDetectionDelayS)
        {
            _last_frame_idle = true;

            hrz_proto::CameraNotification notification[1]{};
            notification[0].set_camera_index(_index);
            notification[0].mutable_motion_ended();
            notifications_cb(notification);
        }

        if (!_manipulators_to_destroy.empty())
        {
            for (auto& it : _manipulators_to_destroy)
            {
                it->destroy(picking);
            }
            _manipulators_to_destroy.clear();
        }

        return has_moved;
    }

    void transition(
        lm::ddual_quat new_final_pose,
        std::optional<GeoPosition2> elevation_query_point,
        hrz_proto::AltitudeMode altitude_mode,
        const hrz_proto::CameraAnimationOptions& go_to_animation,
        bool is_interruptible)
    {
        std::optional<lm::ddual_quat> new_pose = new_final_pose;
        lm::ddual_quat current_pose = _pose;

        cancel_elevation_query_if_any();
        if (altitude_mode == hrz_proto::AltitudeMode::RELATIVE_TO_TERRAIN)
        {
            if (!elevation_query_point)
            {
                elevation_query_point = hrz::ecef_to_geo2(lm::extract_translation(*new_pose));
            }

            assert(elevation_query_point && !_queued_elevation_query);
            _queued_elevation_query =
                QueuedElevationQuery{*new_pose, *elevation_query_point, std::nullopt};

            // Remove the pose: it will be delayed by the elevation query
            new_pose = std::nullopt;
        }
        else
        {
            _pose = *new_pose;
            bake_info();
        }

        if (go_to_animation.duration() > 0)
        {
            replace_manipulator(std::make_unique<TransitionAnimationManipulator>(
                std::exchange(_manipulator, nullptr), current_pose, new_pose, go_to_animation,
                is_interruptible, _min_height_above_terrain, _terrain_collision_inertia));
        }
        else if (new_pose)
        {
            _manipulator->initialize_with_pose(new_pose.value());
        }
    }

    void replace_manipulator(std::unique_ptr<CameraManipulator> new_manipulator)
    {
        if (_manipulator)
        {
            _manipulators_to_destroy.emplace_back(_manipulator.release());
        }
        _manipulator = std::move(new_manipulator);
    }

    void go_to_orbit(
        const hrz_proto::OrbitCameraTransition& trans,
        double fovy_rad,
        double aspect_ratio) override
    {
        lm::ddual_quat new_pose;
        std::optional<hrz::GeoPosition2> elevation_query_point;

        switch (trans.view_case())
        {
            case hrz_proto::OrbitCameraTransition::ViewCase::kCurrent: new_pose = _pose; break;
            case hrz_proto::OrbitCameraTransition::ViewCase::kAngularViewpoint:
                new_pose = to_dual_quat(from_proto(trans.angular_viewpoint()));
                elevation_query_point =
                    hrz::from_proto(trans.angular_viewpoint().target()).latlon();
                break;
            case hrz_proto::OrbitCameraTransition::ViewCase::kPositionalViewpoint:
                new_pose = to_dual_quat(from_proto(trans.positional_viewpoint()));
                elevation_query_point =
                    hrz::from_proto(trans.positional_viewpoint().target()).latlon();
                break;
            case hrz_proto::OrbitCameraTransition::ViewCase::kPose:
                new_pose = to_dual_quat(from_proto(trans.pose()));
                break;
            case hrz_proto::OrbitCameraTransition::ViewCase::kBounds:
            {
                AngularViewpoint vp = from_bounds_view(trans.bounds(), fovy_rad, aspect_ratio);
                new_pose = to_dual_quat(vp);
                elevation_query_point = vp.target.latlon();
                break;
            }
            default: assert(false);
        }

        replace_manipulator(create_orbit_manipulator(
            _energy_half_time, std::nullopt,
            trans.has_limit_bounds() ? hrz::from_proto(trans.limit_bounds())
                                     : hrz::GeoBounds::full(),
            hrz::clamp(trans.max_altitude(), 0.0, kMaxAltitude),
            hrz::clamp_angle(trans.min_tilt(), kTiltLimitMin, kTiltLimitMax),
            hrz::clamp_angle(trans.max_tilt(), kTiltLimitMin, kTiltLimitMax),
            _min_height_above_terrain, _terrain_collision_inertia, trans.correction_animation()));

        transition(
            new_pose, elevation_query_point, trans.altitude_mode(), trans.go_to_animation(),
            trans.is_interruptible());
    }

    void go_to_fixed_position(const hrz_proto::FixedPositionCameraTransition& trans) override
    {
        lm::ddual_quat new_pose;
        std::optional<hrz::GeoPosition2> elevation_query_point;

        switch (trans.view_case())
        {
            case hrz_proto::FixedPositionCameraTransition::ViewCase::kCurrent:
                new_pose = _pose;
                break;
            case hrz_proto::FixedPositionCameraTransition::ViewCase::kAngularViewpoint:
                new_pose = to_dual_quat(from_proto(trans.angular_viewpoint()));
                elevation_query_point =
                    hrz::from_proto(trans.angular_viewpoint().target()).latlon();
                break;
            case hrz_proto::FixedPositionCameraTransition::ViewCase::kPositionalViewpoint:
                new_pose = to_dual_quat(from_proto(trans.positional_viewpoint()));
                elevation_query_point =
                    hrz::from_proto(trans.positional_viewpoint().target()).latlon();
                break;
            case hrz_proto::FixedPositionCameraTransition::ViewCase::kPose:
                new_pose = to_dual_quat(from_proto(trans.pose()));
                break;
            default: assert(false);
        }

        replace_manipulator(create_fixed_position_manipulator(
            _energy_half_time, std::nullopt,
            hrz::clamp_angle(trans.min_tilt(), kTiltLimitMin, kTiltLimitMax),
            hrz::clamp_angle(trans.max_tilt(), kTiltLimitMin, kTiltLimitMax)));

        transition(
            new_pose, elevation_query_point, trans.altitude_mode(), trans.go_to_animation(), false);
    }

    void go_to_fixed_target(const hrz_proto::FixedTargetCameraTransition& trans) override
    {
        AngularViewpoint viewpoint;

        switch (trans.view_case())
        {
            case hrz_proto::FixedTargetCameraTransition::ViewCase::kAngularViewpoint:
                viewpoint = from_proto(trans.angular_viewpoint());
                break;
            case hrz_proto::FixedTargetCameraTransition::ViewCase::kPositionalViewpoint:
                viewpoint = positional_to_angular_viewpoint(
                    from_proto(trans.positional_viewpoint()), std::nullopt);
                break;
            default: assert(false);
        }

        GeoPosition2 elevation_query_point = viewpoint.target.latlon();

        replace_manipulator(create_fixed_target_manipulator(
            _energy_half_time, viewpoint, std::nullopt,
            hrz::clamp_angle(trans.min_tilt(), kTiltLimitMin, kTiltLimitMax),
            hrz::clamp_angle(trans.max_tilt(), kTiltLimitMin, kTiltLimitMax), trans.min_distance(),
            trans.max_distance(), _min_height_above_terrain, _terrain_collision_inertia));

        transition(
            to_dual_quat(viewpoint), elevation_query_point, trans.altitude_mode(),
            trans.go_to_animation(), false);
    }

    void reset_north(const hrz_proto::ResetNorthParams& params) override
    {
        if (_manipulator)
        {
            _manipulator->reset_north(params);
        }
    }

    void move(
        const hrz_proto::CameraMovement& movement,
        hrz_proto::SceneViewIndex view_index,
        const ViewportInfo& view) override
    {
        if (_manipulator)
        {
            CameraViewInfo cam_info =
                CameraViewInfo::make_from_camera_and_viewport(get_info(), view);
            _manipulator->handle_movement_or_queue(
                MovementEventType::Instant, movement, view_index, cam_info);
        }
    }

    void begin_move(
        const hrz_proto::CameraMovement& movement,
        hrz_proto::SceneViewIndex view_index,
        const ViewportInfo& view) override
    {
        if (_manipulator)
        {
            CameraViewInfo cam_info =
                CameraViewInfo::make_from_camera_and_viewport(get_info(), view);
            _manipulator->handle_movement_or_queue(
                MovementEventType::BeginContinuous, movement, view_index, cam_info);
        }
    }

    void end_move(
        const hrz_proto::CameraMovement& movement,
        hrz_proto::SceneViewIndex view_index,
        const ViewportInfo& view) override
    {
        if (_manipulator)
        {
            CameraViewInfo cam_info =
                CameraViewInfo::make_from_camera_and_viewport(get_info(), view);
            _manipulator->handle_movement_or_queue(
                MovementEventType::EndContinuous, movement, view_index, cam_info);
        }
    }

    hrz_proto::Pose get_pose() const override { return to_proto(pose_from_dual_quat(_pose)); }

    hrz_proto::AngularViewpoint get_angular_viewpoint() const override
    {
        return to_proto(angular_viewpoint_from_dual_quat(_pose));
    }

    hrz_proto::PositionalViewpoint get_positional_viewpoint() const override
    {
        return to_proto(positional_viewpoint_from_dual_quat(_pose));
    }

    CameraInfo get_spherical_earth_camera_info() const
    {
        lm::dvec3 position = lm::extract_translation(_pose);
        position.z /= hrz::WGS84_AXES_LENGTH_RATIO;

        lm::ddual_quat spherical_pose = lm::rotation_position_dquat(_pose.r, position);

        return CameraInfo::make_from_fov_and_pose(_baked_info.fovy, spherical_pose);
    }

    hrz_proto::GeographicViewPolygon get_view_polygon(const ViewportInfo& viewport) const override
    {
        if (!_manipulator) return {};

        CameraViewInfo view_info =
            CameraViewInfo::make_from_camera_and_viewport(get_info(), viewport);
        hrz::GeoPosition3 position = hrz::ecef_to_geo3(view_info.cam.pos);

        struct Edge
        {
            enum Type
            {
                OnHorizon,
                Clipped,
            };

            Type type{};
            lm::dvec3 p; // First point of the edge. The second is the p of the next.
        };

        std::vector<Edge> edges;
        edges.reserve(200);

        // Isolate the place we assume the Earth is a sphere!
        lm::dvec4 horizon_plane_sph;
        {
            CameraInfo cam_sph_info = get_spherical_earth_camera_info();

            // Compute the horizon plane: the plane whole intersection with the spherical Earth
            // represents the horizon. Assume spherical Earth.
            // Note: it might be weird to use the enu_to_ecef routine when we assume the Earth is a
            // sphere, but actually as long as we don't take the position into account and only use
            // the rotation, it does what we want. Nice.
            double plane_distance_from_center =
                EARTH_RADIUS * EARTH_RADIUS / (EARTH_RADIUS + position.alt);
            double horizon_circle_radius = std::sqrt(
                EARTH_RADIUS * EARTH_RADIUS
                - plane_distance_from_center * plane_distance_from_center);
            lm::dmat3 enu_to_ecef(hrz::enu_to_ecef_rotation_matrix_for_geo(position.latlon()));
            lm::dvec3 horizon_plane_x_dir = enu_to_ecef * lm::dvec3(1, 0, 0);
            lm::dvec3 horizon_plane_y_dir = enu_to_ecef * lm::dvec3(0, 1, 0);
            lm::dvec3 horizon_plane_normal = lm::normalize(cam_sph_info.pos);
            lm::dvec3 horizon_plane_center = horizon_plane_normal * plane_distance_from_center;

            horizon_plane_sph = lm::dvec4(horizon_plane_normal, -plane_distance_from_center);

            // Generate a bunch of points on the horizon circle.
            // We flatten them to get back to the ellipsoid, no more sphere!
            static constexpr double HorizonAngleStep = 2.0 * lm::PI / 32.0;
            for (double a = 0; a < 2.0 * lm::PI; a += HorizonAngleStep)
            {
                lm::dvec3 p((
                    horizon_plane_center
                    + horizon_circle_radius
                        * (horizon_plane_x_dir * std::cos(a) + horizon_plane_y_dir * std::sin(a))));
                p.z *= hrz::WGS84_AXES_LENGTH_RATIO;
                edges.push_back({Edge::OnHorizon, p});
            }
        }

        // Extract the planes from the view-projection matrix.
        // https://fgiesen.wordpress.com/2012/08/31/frustum-planes-from-the-projection-matrix/
        lm::dmat4 pv_t = lm::transpose(view_info.pv);
        lm::dvec4 camera_planes[4] = {
            pv_t.w - pv_t.x, pv_t.w + pv_t.x, pv_t.w - pv_t.y, pv_t.w + pv_t.y};

        // 1 = inside, -1 = outside, 0 = on plane.
        auto side_fn = [](const lm::dvec3& pt, const lm::dvec4& plane) -> int
        {
            double dot = lm::dot(lm::dvec4(pt, 1.0), plane);
            double eps = std::abs(dot) * std::numeric_limits<double>::epsilon();
            if (dot > eps)
                return 1;
            else if (dot < -eps)
                return -1;
            else
                return 0;
        };

        {
            std::vector<Edge> output;

            // Clip the horizon points using the frustum planes.
            // https://en.wikipedia.org/wiki/Sutherland%E2%80%93Hodgman_algorithm
            // It's a bit modified to keep the edge type information.
            for (lm::dvec4 plane : camera_planes)
            {
                int in_count = edges.size();
                output.clear();
                for (int i = 0; i < in_count; ++i)
                {
                    auto edge_type = edges[i].type;
                    auto p0 = edges[i].p;
                    auto p1 = edges[(i + 1) % in_count].p;

                    int side0 = side_fn(p0, plane);
                    int side1 = side_fn(p1, plane);

                    if (side0 >= 0)
                    {
                        output.push_back({edge_type, p0});
                    }

                    // Points are on both side of the plane, not ON the plane
                    if (side0 + side1 == 0 && side0 != 0)
                    {
                        // Compute intersection of segment and plane
                        double t =
                            -lm::dot(lm::dvec4(p0, 1.0), plane) / lm::dot(p1 - p0, plane.xyz);
                        lm::dvec3 p = lm::mix(p0, p1, t);

                        if (side0 > 0)
                        {
                            output.push_back({Edge::Clipped, p});
                        }
                        else
                        {
                            output.push_back({edge_type, p});
                        }
                    }
                }

                edges.swap(output);
            }

            // Tessellate the segments that are too far apart.
            {
                static constexpr double DistanceThreshold = 1'000'000;
                int in_count = edges.size();
                output.clear();
                for (int i = 0; i < in_count; ++i)
                {
                    auto edge_type = edges[i].type;
                    auto p0 = edges[i].p;
                    auto p1 = edges[(i + 1) % in_count].p;

                    if (edge_type == Edge::Clipped)
                    {
                        // Place point regularily so that they are spaced by about the distance
                        // threshold.

                        double dist = lm::length(p1 - p0);
                        int iter = std::max(1, (int)std::ceil(dist / DistanceThreshold));

                        lm::dvec3 step = (p1 - p0) / iter;
                        lm::dvec3 p = p0;

                        for (int i = 0; i < iter; ++i, p += step)
                        {
                            output.push_back({edge_type, p});
                        }
                    }
                    else
                    {
                        output.push_back({edge_type, p0});
                    }
                }

                edges.swap(output);
            }
        }

        hrz_proto::GeographicViewPolygon polygon;
        polygon.mutable_points()->Reserve(edges.size());

        // Finally reproject the points that need to be reprojected from the camera to the planet
        // surface. This is because when doing clipping, some points may end up below the surface of
        // the planet. Simply pushing them back up to the surface wouldn't make them match their
        // position on the screen so we need to do the raycast.
        {
            int in_count = edges.size();
            for (int i = 0; i < in_count; ++i)
            {
                auto edge_type0 = edges[(i + in_count - 1) % in_count].type;
                auto edge_type1 = edges[i].type;
                auto p = edges[i].p;

                // Only reproject if the edges before and after are the result of clipping.
                // This is because we absolutely don't want to disturb horizon edges.
                if (edge_type0 == Edge::Clipped && edge_type1 == Edge::Clipped)
                {
                    Ray ray{view_info.cam.pos, lm::normalize(p - view_info.cam.pos)};
                    lm::dvec3 hit;
                    if (hrz::planet_intersection(ray, &hit))
                    {
                        *polygon.add_points() = to_proto(ecef_to_geo3(hit));
                    }
                }
                else
                {
                    *polygon.add_points() = to_proto(ecef_to_geo3(p));
                }
            }
        }

        *polygon.mutable_center() = to_proto(position);

        // Clip the north and south poles against the frustum and horizon planes to know if
        // they are visible.
        {
            static constexpr lm::dvec3 north_pole{
                0, 0, hrz::EARTH_RADIUS * hrz::WGS84_AXES_LENGTH_RATIO};
            static constexpr lm::dvec3 south_pole{
                0, 0, -hrz::EARTH_RADIUS * hrz::WGS84_AXES_LENGTH_RATIO};

            static constexpr lm::dvec3 north_pole_sph{0, 0, hrz::EARTH_RADIUS};
            static constexpr lm::dvec3 south_pole_sph{0, 0, -hrz::EARTH_RADIUS};

            lm::dvec4 all_planes[] = {
                camera_planes[0], camera_planes[1], camera_planes[2], camera_planes[3]};
            bool north_in = true;
            bool south_in = true;

            for (lm::dvec4 plane : all_planes)
            {
                north_in = north_in && side_fn(north_pole, plane) >= 0;
                south_in = south_in && side_fn(south_pole, plane) >= 0;
            }

            north_in = north_in && side_fn(north_pole_sph, horizon_plane_sph) >= 0;
            south_in = south_in && side_fn(south_pole_sph, horizon_plane_sph) >= 0;

            polygon.set_encompasses_north_pole(north_in);
            polygon.set_encompasses_south_pole(south_in);
        }

        // Test if any edge crosses the antimeridian.
        // To do that, we only test edges where at least one point is on the side of the hemisphere
        // defined by lon=-180 as center (plane -X). Then if one of those edges cross the
        // antimeridian plane, the view crossed the antimeridian.
        // Note that if we see a pole, we're sure to cross the antimeridian.
        if (!polygon.encompasses_north_pole() && !polygon.encompasses_south_pole())
        {
            static constexpr lm::dvec4 hemi_plane{-1, 0, 0, 0};
            static constexpr lm::dvec4 meridian_plane{0, 1, 0, 0};

            int in_count = edges.size();
            for (int i = 0; i < in_count; ++i)
            {
                auto p0 = edges[i].p;
                auto p1 = edges[(i + 1) % in_count].p;

                if (side_fn(p0, hemi_plane) > 0 || side_fn(p1, hemi_plane) >= 0)
                {
                    int side0 = side_fn(p0, meridian_plane);
                    int side1 = side_fn(p1, meridian_plane);
                    if (side0 + side1 == 0 && side0 != 0)
                    {
                        polygon.set_crosses_antimeridian(true);
                        break;
                    }
                }
            }
        }
        else
        {
            polygon.set_crosses_antimeridian(true);
        }

        return polygon;
    }

    hrz_proto::GeographicBounds get_view_box(const ViewportInfo& viewport) const override
    {
        hrz_proto::GeographicViewPolygon polygon = get_view_polygon(viewport);

        auto fix_longitude_centered = [&](double l)
        {
            if (l < -180)
                return l + 360;
            else if (l > 180)
                return l - 360;
            return l;
        };

        double min_lat = 90, max_lat = -90, min_lon_centered = 180, max_lon_centered = -180;
        for (const auto& p : polygon.points())
        {
            double lon_centered = p.longitude() - polygon.center().longitude();
            lon_centered = fix_longitude_centered(lon_centered);

            min_lat = std::min(min_lat, p.latitude());
            max_lat = std::max(max_lat, p.latitude());
            min_lon_centered = std::min(min_lon_centered, lon_centered);
            max_lon_centered = std::max(max_lon_centered, lon_centered);
        }

        double min_lon = min_lon_centered + polygon.center().longitude();
        double max_lon = max_lon_centered + polygon.center().longitude();

        hrz_proto::GeographicBounds box;
        box.set_west(lm::degrees(normalize_longitude(lm::radians(min_lon))));
        box.set_east(lm::degrees(normalize_longitude(lm::radians(max_lon))));
        box.set_south(min_lat);
        box.set_north(max_lat);

        if (polygon.encompasses_north_pole())
        {
            box.set_west(-180);
            box.set_east(180);
            box.set_north(90);
        }
        else if (polygon.encompasses_south_pole())
        {
            box.set_west(-180);
            box.set_east(180);
            box.set_south(-90);
        }

        return box;
    }

    bool geo_to_screen(
        const GeoPosition3& pos,
        const ViewportInfo& viewport,
        lm::vec2* position,
        bool* below_horizon) const override
    {
        CameraViewInfo view_info =
            CameraViewInfo::make_from_camera_and_viewport(get_info(), viewport);

        lm::dvec3 ecef = geo_to_ecef(pos);
        lm::dvec4 clip = view_info.pv * lm::dvec4(ecef, 1.0);
        lm::dvec3 ndc = clip.xyz / clip.w;
        if (ndc.x < -1 || ndc.x > 1 || ndc.y < -1 || ndc.y > 1 || ndc.z > 1)
        {
            return false;
        }

        *position = lm::vec2(
            ((ndc.x + 1.0) * 0.5) * view_info.viewport.subview_size().x,
            (1.0 - ((ndc.y + 1.0) * 0.5)) * view_info.viewport.subview_size().y);

        hrz::Ray ray{ecef, lm::normalize(view_info.cam.pos - ecef)};
        lm::dvec3 hit;
        *below_horizon = hrz::planet_intersection(ray, &hit, pos.alt - 10.0);

        return true;
    }

    void dev_ui(mu_Context* ctx) const override
    {
        static const int layout[] = {150, -1};
        mu_layout_row(ctx, 2, layout, 0);

        fmt::memory_buffer buffer;

        auto pos_geo = hrz::ecef_to_geo3(_baked_info.pos);

        mu_text(ctx, "Position (ECEF)");
        mu_text(
            ctx,
            hrz::format_to_buffer(
                buffer, "({:.2f}, {:.2f}, {:.2f})", _baked_info.pos.x, _baked_info.pos.y,
                _baked_info.pos.z));

        mu_text(ctx, "Position (geo)");
        mu_text(
            ctx,
            hrz::format_to_buffer(
                buffer, "(lat. {:.3f} deg, lon. {:.3f} deg, alt. {:.2f} m)",
                lm::degrees(pos_geo.lat), lm::degrees(pos_geo.lon), pos_geo.alt));

        mu_text(ctx, "Forward vector");
        mu_text(
            ctx,
            hrz::format_to_buffer(
                buffer, "({:.2f}, {:.2f}, {:.2f})", _baked_info.forward().x,
                _baked_info.forward().y, _baked_info.forward().z));

        mu_text(ctx, "Up vector");
        mu_text(
            ctx,
            hrz::format_to_buffer(
                buffer, "({:.2f}, {:.2f}, {:.2f})", _baked_info.up().x, _baked_info.up().y,
                _baked_info.up().z));

        mu_text(ctx, "Right vector");
        mu_text(
            ctx,
            hrz::format_to_buffer(
                buffer, "({:.2f}, {:.2f}, {:.2f})", _baked_info.right().x, _baked_info.right().y,
                _baked_info.right().z));

        mu_text(ctx, "Vertical field of view");
        mu_text(ctx, hrz::format_to_buffer(buffer, "{:.1f} deg", lm::degrees(_baked_info.fovy)));

        {
            static const int layout[] = {-1};
            mu_layout_row(ctx, 1, layout, 0);

            mu_text(ctx, "Manipulator:");

            if (_manipulator)
            {
                _manipulator->dev_ui(ctx);
            }

            mu_text(ctx, "");
        }
    }
};

Camera* create(
    hrz_proto::CameraIndex index,
    float fov,
    double user_controls_inertia,
    double movements_inertia,
    double min_height_above_terrain,
    double terrain_collision_inertia)
{
    return new CameraImpl(
        index, fov, user_controls_inertia, movements_inertia, min_height_above_terrain,
        terrain_collision_inertia);
}

void destroy(Camera* cam)
{
    delete cam;
}

} // namespace hrz::camera
