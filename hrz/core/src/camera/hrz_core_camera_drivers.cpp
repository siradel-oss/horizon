#include "camera/hrz_core_camera_driver.h"
#include "camera/hrz_core_camera_terrain.h"
#include "camera/hrz_core_camera_viewpoint.h"

#include <hrz_common_geo.h>
#include <hrz_fnd_log.h>

#include <optional>

namespace hrz::camera
{
namespace
{
// If not stiff enough, can fill a bit slugish.
static constexpr double kDragTerrainCollisionTimeFactor = 0.5;

// If too stiff, correcting over the delayed height above terrain can
// result in undesirably large rotations.
static constexpr double kRotationTerrainCollisionTimeFactor = 2.0;

double add_or_replace_if_opposite(double a, double b)
{
    if (a * b < 0)
        return b;
    else
        return a + b;
}
} // namespace

class OrbitLimiter
{
    Pose _from;
    Pose _to;

    void interpolate(double t)
    {
        _to.position.lat = hrz::lerp(_from.position.lat, _to.position.lat, t);
        _to.position.lon = hrz::lerp(_from.position.lon, _to.position.lon, t);
        _to.position.alt = hrz::lerp(_from.position.alt, _to.position.alt, t);
        _to.tilt = hrz::lerp(_from.tilt, _to.tilt, t);
        _to.bearing = hrz::lerp(_from.bearing, _to.bearing, t);
    }

    bool limit_angle(const PrecomputedClampAngle& clamp, double from, double to)
    {
        double diff = to - from;

        if (diff != 0 && clamp.try_clamp(&to))
        {
            double t = hrz::clamp((to - from) / diff, 0.0, 1.1);
            interpolate(t);
            return true;
        }

        return false;
    }

public:
    OrbitLimiter(const Pose& from, const Pose& to) : _from(from), _to(to) {}

    bool limit_latitude(const PrecomputedClampAngle& clamp)
    {
        return limit_angle(clamp, _from.position.lat, _to.position.lat);
    }

    bool limit_longitude(const PrecomputedClampAngle& clamp)
    {
        return limit_angle(clamp, _from.position.lon, _to.position.lon);
    }

    bool limit_tilt(const PrecomputedClampAngle& clamp)
    {
        return limit_angle(clamp, _from.tilt, _to.tilt);
    }

    bool limit_altitude(double max_altitude)
    {
        double diff = _to.position.alt - _from.position.alt;

        if (diff != 0 && _to.position.alt > max_altitude)
        {
            double t = hrz::clamp((max_altitude - _from.position.alt) / diff, 0.0, 1.0);
            interpolate(t);
            return true;
        }

        return false;
    }

    Pose get_pose() const { return _to; }
};

static double convert_energy(double energy, double old_half_time, double new_half_time)
{
    double old_lambda = Inertia::lambda(old_half_time);
    double new_lambda = Inertia::lambda(new_half_time);

    // Integrate the old energy to get the total quantity.
    // Then compute the new energy using the new half time.
    //
    // ∫ E0 exp(-λ0 t) dt = ∫ E1 exp(-λ1 t) dt
    // We solve for E1

    return old_lambda * energy / new_lambda;
}

class ZoomBearingDriverImpl final : public ZoomBearingDriver
{
    double _energy_half_time{};
    lm::ddual_quat _pose;
    lm::dvec3 _target;
    lm::dvec3 _bearing_axis;

    hrz::PrecomputedClampAngle _lat_clamp;
    hrz::PrecomputedClampAngle _lon_clamp;
    hrz::PrecomputedClampAngle _tilt_clamp;
    double _max_altitude;
    double _min_height_above_terrain;
    double _terrain_collision_inertia;

    enum StepsType
    {
        StepsNone,
        StepsThisFrame,
        StepsTotal,
        StepsVelocity,
    };

    StepsType _steps_type{};
    double _received_steps{};
    double _steps_energy{};

    enum BearingType
    {
        BearingNone,
        BearingThisFrame,
    };

    BearingType _bearing_type{};
    double _received_bearing{};
    double _bearing_energy{};

public:
    ZoomBearingDriverImpl(
        double energy_half_time,
        const lm::ddual_quat& pose,
        const hrz::GeoBounds& bounds_limit,
        double max_altitude,
        double min_tilt,
        double max_tilt,
        double min_height_above_terrain,
        double terrain_collision_inertia) :
        _energy_half_time(energy_half_time),
        _pose(pose),
        _lat_clamp(bounds_limit.south, bounds_limit.north),
        _lon_clamp(bounds_limit.west, bounds_limit.east),
        _tilt_clamp(min_tilt - lm::PI / 2, max_tilt - lm::PI / 2),
        _max_altitude(max_altitude),
        _min_height_above_terrain(min_height_above_terrain),
        _terrain_collision_inertia(terrain_collision_inertia)
    {
    }

    std::unique_ptr<ZoomDriver> recreate_with_pose(const lm::ddual_quat& pose) override
    {
        return ZoomDriver::create(
            _energy_half_time, pose,
            hrz::GeoBounds{_lon_clamp.min, _lon_clamp.max, _lat_clamp.min, _lat_clamp.max},
            _max_altitude, _tilt_clamp.min + lm::PI / 2, _tilt_clamp.max + lm::PI / 2,
            _min_height_above_terrain, _terrain_collision_inertia);
    }

    void update_energy_half_time(double e) override
    {
        _steps_energy = convert_energy(_steps_energy, _energy_half_time, e);
        _bearing_energy = convert_energy(_bearing_energy, _energy_half_time, e);
        _energy_half_time = e;
    }

    void update_terrain_settings(double min_height_above_terrain, double terrain_collision_inertia)
        override
    {
        _min_height_above_terrain = min_height_above_terrain;
        _terrain_collision_inertia = terrain_collision_inertia;
    }

    lm::ddual_quat get_pose() const override { return _pose; }

    void set_bearing_rotation_this_frame(
        const lm::dvec3& target,
        const lm::dvec3& axis,
        double angle) override
    {
        _bearing_type = BearingThisFrame;
        _received_bearing = angle;
        _bearing_axis = axis;
        _target = target;
    }

    void set_steps(const lm::dvec3& target, double steps) override
    {
        _steps_type = StepsThisFrame;
        _target = target;
        _received_steps = steps;
    }

    void add_steps(const lm::dvec3& target, double steps) override
    {
        if (_steps_type != StepsTotal)
        {
            _received_steps = 0;
        }

        _steps_type = StepsTotal;
        _target = target;
        _received_steps += steps;
    }

    void set_steps_velocity(const lm::dvec3& target, double steps) override
    {
        _steps_type = StepsVelocity;
        _target = target;
        _received_steps = steps;
    }

    lm::ddual_quat work(double dt, double height_above_terrain, bool should_keep_bearing) override
    {
        if (height_above_terrain < _min_height_above_terrain)
        {
            _pose = push_camera_position_above_terrain(
                _pose, height_above_terrain, _min_height_above_terrain, _terrain_collision_inertia,
                dt);
        }

        Inertia inertia(_energy_half_time, dt);

        if (_steps_type != StepsNone)
        {
            switch (_steps_type)
            {
                case StepsThisFrame:
                {
                    _steps_energy = inertia.apply_quantity_this_frame(_received_steps);
                    break;
                }
                case StepsTotal:
                {
                    double new_energy = inertia.apply_quantity_total(_received_steps);
                    _steps_energy = add_or_replace_if_opposite(_steps_energy, new_energy);
                    break;
                }
                case StepsVelocity:
                {
                    _steps_energy = inertia.apply_velocity_this_frame(_received_steps);
                    break;
                }
                default: break;
            }
            _received_steps = 0;
            _steps_type = StepsNone;
        }

        if (_bearing_type != BearingNone)
        {
            switch (_bearing_type)
            {
                case BearingThisFrame:
                {
                    _bearing_energy = inertia.apply_quantity_this_frame(_received_bearing);
                    break;
                }
                default: break;
            }
            _received_bearing = 0;
            _bearing_type = BearingNone;
        }

        if (_steps_energy != 0 || _bearing_energy != 0)
        {
            double zoom_steps = inertia.get_quantity_this_frame(_steps_energy);
            double bearing_rotation = inertia.get_quantity_this_frame(_bearing_energy);

            Pose old_pose = pose_from_dual_quat(_pose);

            if (zoom_steps != 0)
            {
                lm::dvec3 position = lm::extract_translation(_pose);
                position = _target + (position - _target) * std::pow(2, -zoom_steps);

                // Rotate the camera so that it stays parallel to the horizon
                lm::dvec3 cam_side = _pose.r * lm::dvec3(1, 0, 0);
                lm::dvec3 geo_normal = hrz::geo_to_normal(hrz::ecef_to_geo2(position));
                lm::dvec3 geo_side = lm::cross(lm::cross(geo_normal, cam_side), geo_normal);
                lm::dquat correction_rotation = lm::rotation_between_vectors(cam_side, geo_side);

                _pose =
                    lm::translation_dquat(position) * lm::ddual_quat(correction_rotation * _pose.r);
            }

            if (bearing_rotation != 0)
            {
                _pose = lm::translation_dquat(_target)
                    * lm::axis_angle_dquat(_bearing_axis, bearing_rotation)
                    * lm::translation_dquat(-_target) * _pose;
            }

            Pose new_pose = pose_from_dual_quat(_pose);
            if (should_keep_bearing && bearing_rotation == 0)
            {
                new_pose.bearing = old_pose.bearing;
            }

            OrbitLimiter limiter(old_pose, new_pose);
            if (limiter.limit_latitude(_lat_clamp))
            {
                _steps_energy = 0;
                _bearing_energy = 0;
            }

            if (limiter.limit_longitude(_lon_clamp))
            {
                _steps_energy = 0;
                _bearing_energy = 0;
            }

            if (limiter.limit_altitude(_max_altitude))
            {
                _steps_energy = 0;
                _bearing_energy = 0;
            }

            new_pose = limiter.get_pose();
            new_pose.tilt = _tilt_clamp.clamp(new_pose.tilt);

            // Doing this round-trip is useful even when not keeping the bearing because it allows
            // the camera not to end up upside down when zooming below the forward direction.
            _pose = to_dual_quat(new_pose);

            _steps_energy = inertia.apply_energy_loss(_steps_energy, 0.03);
            _bearing_energy = inertia.apply_energy_loss(_bearing_energy, 0.01);
        }

        return _pose;
    }

    bool is_idle() const override { return _steps_energy == 0 && _bearing_energy == 0; }
};

std::unique_ptr<ZoomDriver> ZoomDriver::create(
    double energy_half_time,
    const lm::ddual_quat& pose,
    const hrz::GeoBounds& bounds_limit,
    double max_altitude,
    double min_tilt,
    double max_tilt,
    double min_height_above_terrain,
    double terrain_collision_inertia)
{
    return std::make_unique<ZoomBearingDriverImpl>(
        energy_half_time, pose, bounds_limit, max_altitude, min_tilt, max_tilt,
        min_height_above_terrain, terrain_collision_inertia);
}

std::unique_ptr<ZoomBearingDriver> ZoomBearingDriver::create(
    double energy_half_time,
    const lm::ddual_quat& pose,
    const hrz::GeoBounds& bounds_limit,
    double max_altitude,
    double min_tilt,
    double max_tilt,
    double min_height_above_terrain,
    double terrain_collision_inertia)
{
    return std::make_unique<ZoomBearingDriverImpl>(
        energy_half_time, pose, bounds_limit, max_altitude, min_tilt, max_tilt,
        min_height_above_terrain, terrain_collision_inertia);
}

class RotateAroundCenterDriverImpl final : public RotateAroundCenterDriver
{
    double _energy_half_time{};
    const PrecomputedClampAngle _tilt_clamp;
    double _min_height_above_terrain;
    double _terrain_collision_inertia;

    enum RotationType
    {
        None,
        ThisFrame,
        Total,
        Velocity,
    };

    RotationType _rotation_type{};
    lm::dvec2 _rotation_angle{};

    lm::dvec2 _energy{};

    lm::ddual_quat _pose;
    Pose _vp_pose;

public:
    RotateAroundCenterDriverImpl(
        double energy_half_time,
        const lm::ddual_quat& pose,
        double min_tilt,
        double max_tilt,
        double min_height_above_terrain,
        double terrain_collision_inertia) :
        _energy_half_time(energy_half_time),
        _tilt_clamp(min_tilt - lm::PI / 2, max_tilt - lm::PI / 2),
        _min_height_above_terrain(min_height_above_terrain),
        _terrain_collision_inertia(terrain_collision_inertia),
        _pose(pose),
        _vp_pose(pose_from_dual_quat(_pose))
    {
    }

    std::unique_ptr<RotateAroundCenterDriver> recreate_with_pose(
        const lm::ddual_quat& pose) override
    {
        return create(
            _energy_half_time, pose, _tilt_clamp.min + lm::PI / 2, _tilt_clamp.max + lm::PI / 2,
            _min_height_above_terrain, _terrain_collision_inertia);
    }

    lm::ddual_quat get_pose() const override { return _pose; }

    void set_rotation_this_frame(double d_bearing, double d_tilt) override
    {
        _rotation_angle = lm::dvec2{d_bearing, d_tilt};
        _rotation_type = ThisFrame;
    }

    void add_rotation_total(double d_bearing, double d_tilt) override
    {
        if (_rotation_type != Total)
        {
            _rotation_angle = {};
        }

        _rotation_angle = {d_bearing, d_tilt};
        _rotation_type = Total;
    }

    void set_rotation_velocity(double d_bearing, double d_tilt) override
    {
        _rotation_angle = lm::dvec2{d_bearing, d_tilt};
        _rotation_type = Velocity;
    }

    lm::ddual_quat work(double dt, double height_above_terrain, bool should_keep_bearing) override
    {
        if (height_above_terrain < _min_height_above_terrain)
        {
            _vp_pose = pose_from_dual_quat(push_camera_position_above_terrain(
                _pose, height_above_terrain, _min_height_above_terrain, _terrain_collision_inertia,
                dt));
        }

        Inertia inertia(_energy_half_time, dt);

        if (_rotation_type != None)
        {
            switch (_rotation_type)
            {
                case ThisFrame:
                    _energy.x = inertia.apply_quantity_this_frame(_rotation_angle.x);
                    _energy.y = inertia.apply_quantity_this_frame(_rotation_angle.y);
                    break;
                case Total:
                    _energy.x = add_or_replace_if_opposite(
                        _energy.x, inertia.apply_quantity_total(_rotation_angle.x));
                    _energy.y = add_or_replace_if_opposite(
                        _energy.y, inertia.apply_quantity_total(_rotation_angle.y));
                    break;
                case Velocity:
                    _energy.x = inertia.apply_velocity_this_frame(_rotation_angle.x);
                    _energy.y = inertia.apply_velocity_this_frame(_rotation_angle.y);
                    break;
                default: break;
            }
            _rotation_type = None;
        }

        _vp_pose.bearing += inertia.get_quantity_this_frame(_energy.x);
        _vp_pose.tilt += inertia.get_quantity_this_frame(_energy.y);

        if (_tilt_clamp.try_clamp(&_vp_pose.tilt))
        {
            _energy.y = 0;
        }

        _pose = to_dual_quat(_vp_pose);

        _energy.x = inertia.apply_energy_loss(_energy.x, 0.01);
        _energy.y = inertia.apply_energy_loss(_energy.y, 0.01);

        return _pose;
    }

    bool is_idle() const override { return _energy.x == 0 && _energy.y == 0; }

    void update_energy_half_time(double e) override
    {
        _energy.x = convert_energy(_energy.x, _energy_half_time, e);
        _energy.y = convert_energy(_energy.y, _energy_half_time, e);
        _energy_half_time = e;
    }

    void update_terrain_settings(double min_height_above_terrain, double terrain_collision_inertia)
        override
    {
        _min_height_above_terrain = min_height_above_terrain;
        _terrain_collision_inertia = terrain_collision_inertia;
    }
};

std::unique_ptr<RotateAroundCenterDriver> RotateAroundCenterDriver::create(
    double energy_half_time,
    const lm::ddual_quat& pose,
    double min_tilt,
    double max_tilt,
    double min_height_above_terrain,
    double terrain_collision_inertia)
{
    return std::make_unique<RotateAroundCenterDriverImpl>(
        energy_half_time, pose, min_tilt, max_tilt, min_height_above_terrain,
        terrain_collision_inertia);
}

class MoveAroundPlanetDriverImpl final : public MoveAroundPlanetDriver
{
    double _energy_half_time{};
    lm::ddual_quat _pose;

    hrz::PrecomputedClampAngle _lat_clamp;
    hrz::PrecomputedClampAngle _lon_clamp;
    double _min_height_above_terrain;
    double _terrain_collision_inertia;

    lm::PrecomputedSlerp<double> _slerp;

    enum EnergyType
    {
        None,
        ThisFrame,
        Total,
        Velocity,
    };

    double _slerp_t_energy_received = 0;
    EnergyType _energy_type = ThisFrame;
    double _slerp_t_energy = 0;
    double _energy_zero_threshold = 0;

public:
    MoveAroundPlanetDriverImpl(
        double energy_half_time,
        const lm::ddual_quat& pose,
        const hrz::GeoBounds& bounds,
        double min_height_above_terrain,
        double terrain_collision_inertia) :
        _energy_half_time(energy_half_time),
        _pose(pose),
        _lat_clamp(bounds.south, bounds.north),
        _lon_clamp(bounds.west, bounds.east),
        _min_height_above_terrain(min_height_above_terrain),
        _terrain_collision_inertia(terrain_collision_inertia)
    {
    }

    std::unique_ptr<MoveAroundPlanetDriver> recreate_with_pose(const lm::ddual_quat& pose) override
    {
        return MoveAroundPlanetDriver::create(
            _energy_half_time, pose,
            hrz::GeoBounds{_lon_clamp.min, _lon_clamp.max, _lat_clamp.min, _lat_clamp.max},
            _min_height_above_terrain, _terrain_collision_inertia);
    }

    lm::ddual_quat get_pose() const override { return _pose; }

    void set_rotation_around_earth_center_this_frame(const lm::dquat& q, double t) override
    {
        _slerp = lm::PrecomputedSlerp<double>(lm::dquat(), q);
        _slerp_t_energy_received = (q == lm::dquat()) ? 0.0 : t;
        _energy_type = ThisFrame;
    }

    void set_rotation_around_earth_center_total(const lm::dquat& q, double t) override
    {
        _slerp_t_energy_received = 0.0;
        add_rotation_around_earth_center_total(q, (q == lm::dquat{}) ? 0.0 : t);
    }

    void add_rotation_around_earth_center_total(const lm::dquat& q, double t) override
    {
        _energy_type = Total;
        _slerp = lm::PrecomputedSlerp<double>(lm::dquat(), q);
        _slerp_t_energy_received += t;
    }

    void set_rotation_velocity_around_earth_center(const lm::dquat& q, double t) override
    {
        _slerp = lm::PrecomputedSlerp<double>(lm::dquat(), q);
        _slerp_t_energy_received = (q == lm::dquat()) ? 0.0 : t;
        _energy_type = Velocity;
    }

    // @Todo(camera) In the future we might want to have different inertia parameters per action and
    // stuff. In this case refactor all this to take in dt, have inertia a parameter of constructor
    // and build the Inertia struct when needed.
    // For now it's fine.
    //      -slerouzic, 2023-06-27

    lm::ddual_quat work(double dt, double height_above_terrain, bool should_keep_bearing) override
    {
        if (height_above_terrain < _min_height_above_terrain)
        {
            _pose = push_camera_position_above_terrain(
                _pose, height_above_terrain, _min_height_above_terrain,
                _terrain_collision_inertia * kDragTerrainCollisionTimeFactor, dt);
        }

        Inertia inertia(_energy_half_time, dt);

        if (_energy_type != None)
        {
            switch (_energy_type)
            {
                case ThisFrame:
                    _slerp_t_energy = inertia.apply_quantity_this_frame(_slerp_t_energy_received);
                    break;
                case Total:
                    _slerp_t_energy += inertia.apply_quantity_total(_slerp_t_energy_received);
                    break;
                case Velocity:
                    _slerp_t_energy = inertia.apply_velocity_this_frame(_slerp_t_energy_received);
                    break;
                default: break;
            }
            _slerp_t_energy_received = 0;
            _energy_type = None;
            _energy_zero_threshold = std::abs(_slerp_t_energy) * 0.003;
        }

        if (_slerp_t_energy != 0)
        {
            const Pose before = pose_from_dual_quat(_pose);

            const double t = inertia.get_quantity_this_frame(_slerp_t_energy);
            _pose = lm::ddual_quat(_slerp.slerp(t)) * _pose;

            Pose after = pose_from_dual_quat(_pose);
            after.position.alt = before.position.alt;
            if (should_keep_bearing)
            {
                after.bearing = before.bearing;
            }

            OrbitLimiter limiter(before, after);

            if (limiter.limit_latitude(_lat_clamp))
            {
                _slerp_t_energy = 0;
            }

            if (limiter.limit_longitude(_lon_clamp))
            {
                _slerp_t_energy = 0;
            }

            _pose = to_dual_quat(limiter.get_pose());

            _slerp_t_energy = inertia.apply_energy_loss(_slerp_t_energy, _energy_zero_threshold);
        }

        return _pose;
    }

    bool is_idle() const override { return _slerp_t_energy == 0; }

    void update_energy_half_time(double e) override
    {
        _slerp_t_energy = convert_energy(_slerp_t_energy, _energy_half_time, e);
        _energy_half_time = e;
    }

    void update_terrain_settings(double min_height_above_terrain, double terrain_collision_inertia)
        override
    {
        _min_height_above_terrain = min_height_above_terrain;
        _terrain_collision_inertia = terrain_collision_inertia;
    }
};

std::unique_ptr<MoveAroundPlanetDriver> MoveAroundPlanetDriver::create(
    double energy_half_time,
    const lm::ddual_quat& pose,
    const hrz::GeoBounds& limit_bounds,
    double min_height_above_terrain,
    double terrain_collision_inertia)
{
    return std::make_unique<MoveAroundPlanetDriverImpl>(
        energy_half_time, pose, limit_bounds, min_height_above_terrain, terrain_collision_inertia);
}

class OrbitRotateAroundTargetDriverImpl final : public OrbitRotateAroundTargetDriver
{
    double _energy_half_time{};
    lm::ddual_quat _pose;

    PrecomputedClampAngle _lat_clamp;
    PrecomputedClampAngle _lon_clamp;
    PrecomputedClampAngle _tilt_clamp;
    double _max_altitude;
    double _min_height_above_terrain;
    double _terrain_collision_inertia;

    std::optional<lm::dvec3> _target;
    lm::dvec3 _bearing_axis;

    bool _has_received_movement{};
    double _bearing_move{};
    double _tilt_move{};

    double _bearing_energy{};
    double _tilt_energy{};

public:
    OrbitRotateAroundTargetDriverImpl(
        double energy_half_time,
        const lm::ddual_quat& pose,
        const hrz::GeoBounds& bounds_limit,
        double max_altitude,
        double min_tilt,
        double max_tilt,
        double min_height_above_terrain,
        double terrain_collision_inertia) :
        _energy_half_time(energy_half_time),
        _pose(pose),
        _lat_clamp(bounds_limit.south, bounds_limit.north),
        _lon_clamp(bounds_limit.west, bounds_limit.east),
        _tilt_clamp(min_tilt - lm::PI / 2, max_tilt - lm::PI / 2),
        _max_altitude(max_altitude),
        _min_height_above_terrain(min_height_above_terrain),
        _terrain_collision_inertia(terrain_collision_inertia)
    {
    }

    lm::ddual_quat get_pose() const override { return _pose; }

    void set_rotation_this_frame(
        const lm::dvec3& target,
        const lm::dvec3& bearing_axis,
        float bearing,
        float tilt) override
    {
        _target = target;
        _bearing_axis = bearing_axis;
        _bearing_move = bearing;
        _tilt_move = tilt;
        _has_received_movement = true;
    }

    lm::ddual_quat work(double dt, double height_above_terrain, bool /*should_keep_bearing*/)
        override
    {
        if (!_target.has_value()) return _pose;

        auto& target = _target.value();

        if (height_above_terrain < _min_height_above_terrain)
        {
            _pose = push_camera_position_above_terrain_with_target(
                        _pose, height_above_terrain, _min_height_above_terrain, target,
                        _terrain_collision_inertia * kRotationTerrainCollisionTimeFactor, dt)
                        .pose;
        }

        Inertia inertia(_energy_half_time, dt);

        if (_has_received_movement)
        {
            _bearing_energy = inertia.apply_quantity_this_frame(_bearing_move);
            _tilt_energy = inertia.apply_quantity_this_frame(_tilt_move);
            _has_received_movement = false;
        }

        if (_bearing_energy != 0 || _tilt_energy != 0)
        {
            Pose before = pose_from_dual_quat(_pose);

            double d_bearing = inertia.get_quantity_this_frame(_bearing_energy);
            double d_tilt = inertia.get_quantity_this_frame(_tilt_energy);

            lm::ddual_quat transform = lm::translation_dquat(target)
                * lm::axis_angle_dquat(_bearing_axis, d_bearing) * lm::translation_dquat(-target);
            _pose = transform * _pose;

            lm::dvec3 side_axis = _pose.r * lm::dvec3(1, 0, 0);
            lm::dquat tilt_quat = lm::axis_angle(side_axis, d_tilt);

            // First apply the tilt tentatively, then correct it. Use the difference to interpolate
            // the original tilt rotation. This is not 100% correct, but good enough for small
            // angles (which we expect at high framerates).

            transform = lm::translation_dquat(target) * lm::ddual_quat(tilt_quat)
                * lm::translation_dquat(-target);
            double tilt_t = 1.0;
            double pre_transform_tilt = local_tilt(_pose);
            double tentative_tilt = local_tilt(transform * _pose);
            double corrected_tilt = hrz::clamp_angle(tentative_tilt, kTiltLimitMin, kTiltLimitMax);
            if (corrected_tilt != tentative_tilt && pre_transform_tilt != tentative_tilt)
            {
                tilt_t =
                    (corrected_tilt - pre_transform_tilt) / (tentative_tilt - pre_transform_tilt);
                _tilt_energy = 0;
            }

            transform = lm::translation_dquat(target)
                * lm::ddual_quat(lm::slerp(lm::dquat(), tilt_quat, tilt_t))
                * lm::translation_dquat(-target);
            _pose = transform * _pose;

            _bearing_energy = inertia.apply_energy_loss(_bearing_energy, 0.001);
            _tilt_energy = inertia.apply_energy_loss(_tilt_energy, 0.001);

            OrbitLimiter limiter(before, pose_from_dual_quat(_pose));

            bool limited = limiter.limit_latitude(_lat_clamp);
            limited = limiter.limit_longitude(_lon_clamp) || limited;
            limiter.limit_tilt(_tilt_clamp) || limited;
            limiter.limit_altitude(_max_altitude) || limited;

            if (limited)
            {
                _bearing_energy = 0;
                _tilt_energy = 0;
            }

            _pose = to_dual_quat(limiter.get_pose());
        }

        return _pose;
    }

    bool is_idle() const override { return _bearing_energy == 0 && _tilt_energy == 0; }

    void update_energy_half_time(double e) override
    {
        _bearing_energy = convert_energy(_bearing_energy, _energy_half_time, e);
        _tilt_energy = convert_energy(_tilt_energy, _energy_half_time, e);
        _energy_half_time = e;
    }

    void update_terrain_settings(double min_height_above_terrain, double terrain_collision_inertia)
        override
    {
        _min_height_above_terrain = min_height_above_terrain;
        _terrain_collision_inertia = terrain_collision_inertia;
    }
};

std::unique_ptr<OrbitRotateAroundTargetDriver> OrbitRotateAroundTargetDriver::create(
    double energy_half_time,
    const lm::ddual_quat& pose,
    const hrz::GeoBounds& bounds_limit,
    double max_altitude,
    double min_tilt,
    double max_tilt,
    double min_height_above_terrain,
    double terrain_collision_inertia)
{
    return std::make_unique<OrbitRotateAroundTargetDriverImpl>(
        energy_half_time, pose, bounds_limit, max_altitude, min_tilt, max_tilt,
        min_height_above_terrain, terrain_collision_inertia);
}

class RotateAroundFixedTargetDriverImpl : public RotateAroundFixedTargetDriver
{
    enum EnergyType
    {
        None,
        ThisFrame,
        Total,
        Velocity,
    };

    double _energy_half_time{};
    EnergyType _energy_type{};
    lm::dvec2 _received{};
    lm::dvec2 _energy{};

    AngularViewpoint _viewpoint;
    lm::ddual_quat _pose;
    lm::dvec3 _target;
    hrz::PrecomputedClampAngle _tilt_clamp;
    double _min_height_above_terrain;
    double _terrain_collision_inertia;

public:
    RotateAroundFixedTargetDriverImpl(
        double energy_half_time,
        lm::ddual_quat pose,
        const lm::dvec3& target,
        double min_tilt,
        double max_tilt,
        double min_height_above_terrain,
        double terrain_collision_inertia) :
        _energy_half_time(energy_half_time),
        _target(target),
        _tilt_clamp(min_tilt, max_tilt),
        _min_height_above_terrain(min_height_above_terrain),
        _terrain_collision_inertia(terrain_collision_inertia)
    {
        PositionalViewpoint vp{ecef_to_geo3(lm::extract_translation(pose)), ecef_to_geo3(_target)};
        _viewpoint = positional_to_angular_viewpoint(vp, pose);
        _pose = to_dual_quat(_viewpoint);
    }

    std::unique_ptr<RotateAroundFixedTargetDriver> recreate_with_pose(
        const lm::ddual_quat& pose) override
    {
        return create(
            _energy_half_time, pose, _target, _tilt_clamp.min, _tilt_clamp.max,
            _min_height_above_terrain, _terrain_collision_inertia);
    }

    void set_rotation_this_frame(double d_bearing, double d_tilt) override
    {
        _energy_type = ThisFrame;
        _received = lm::dvec2{d_bearing, d_tilt};
    }

    void add_rotation_total(double d_bearing, double d_tilt) override
    {
        if (_energy_type != Total)
        {
            _received = lm::dvec2{0, 0};
        }
        _energy_type = Total;
        _received += lm::dvec2{d_bearing, d_tilt};
    }

    void set_rotation_velocity(double d_bearing, double d_tilt) override
    {
        _energy_type = Velocity;
        _received = lm::dvec2{d_bearing, d_tilt};
    }

    lm::ddual_quat get_pose() const override { return _pose; }

    lm::ddual_quat work(double dt, double height_above_terrain, bool should_keep_bearing) override
    {
        bool recompute_pose = false;

        if (height_above_terrain < _min_height_above_terrain)
        {
            _viewpoint.tilt =
                push_camera_position_above_terrain_with_target(
                    _pose, height_above_terrain, _min_height_above_terrain, _target,
                    _terrain_collision_inertia * kRotationTerrainCollisionTimeFactor, dt)
                    .tilt;
            recompute_pose = true;
        }

        Inertia inertia(_energy_half_time, dt);

        if (_energy_type != None)
        {
            switch (_energy_type)
            {
                case ThisFrame:
                    _energy.x = inertia.apply_quantity_this_frame(_received.x);
                    _energy.y = inertia.apply_quantity_this_frame(_received.y);
                    break;
                case Total:
                    _energy.x = add_or_replace_if_opposite(
                        _energy.x, inertia.apply_quantity_total(_received.x));
                    _energy.y = add_or_replace_if_opposite(
                        _energy.y, inertia.apply_quantity_total(_received.y));
                    break;
                case Velocity:
                    _energy.x = inertia.apply_velocity_this_frame(_received.x);
                    _energy.y = inertia.apply_velocity_this_frame(_received.y);
                    break;
                default: break;
            }
            _energy_type = None;
        }

        if (_energy.x != 0 || _energy.y != 0)
        {
            _viewpoint.bearing += inertia.get_quantity_this_frame(_energy.x);
            _viewpoint.tilt += inertia.get_quantity_this_frame(_energy.y);

            if (_tilt_clamp.try_clamp(&_viewpoint.tilt))
            {
                _energy.y = 0;
            }

            _energy.x = inertia.apply_energy_loss(_energy.x, 0.01);
            _energy.y = inertia.apply_energy_loss(_energy.y, 0.01);

            recompute_pose = true;
        }

        if (recompute_pose)
        {
            _pose = to_dual_quat(_viewpoint);
        }

        return _pose;
    }

    bool is_idle() const override { return _energy.x == 0 && _energy.y == 0; }

    void update_energy_half_time(double e) override
    {
        _energy.x = convert_energy(_energy.x, _energy_half_time, e);
        _energy.y = convert_energy(_energy.y, _energy_half_time, e);
        _energy_half_time = e;
    }

    void update_terrain_settings(double min_height_above_terrain, double terrain_collision_inertia)
        override
    {
        _min_height_above_terrain = min_height_above_terrain;
        _terrain_collision_inertia = terrain_collision_inertia;
    }
};

std::unique_ptr<RotateAroundFixedTargetDriver> RotateAroundFixedTargetDriver::create(
    double energy_half_time,
    const lm::ddual_quat& pose,
    const lm::dvec3& target,
    double min_tilt,
    double max_tilt,
    double min_height_above_terrain,
    double terrain_collision_inertia)
{
    return std::make_unique<RotateAroundFixedTargetDriverImpl>(
        energy_half_time, pose, target, min_tilt, max_tilt, min_height_above_terrain,
        terrain_collision_inertia);
}

class DistanceToFixedTargetDriverImpl : public DistanceToFixedTargetDriver
{
    enum EnergyType
    {
        None,
        ThisFrame,
        Total,
        Velocity,
    };

    double _energy_half_time{};
    EnergyType _energy_type{};
    double _received{};
    double _energy{};

    double _min_distance{};
    double _max_distance{};
    double _min_height_above_terrain{};
    double _terrain_collision_inertia{};

    lm::dvec3 _target;
    AngularViewpoint _viewpoint;
    lm::ddual_quat _pose;

public:
    DistanceToFixedTargetDriverImpl(
        double energy_half_time,
        const lm::ddual_quat& pose,
        const lm::dvec3& target,
        double min_distance,
        double max_distance,
        double min_height_above_terrain,
        double terrain_collision_inertia) :
        _energy_half_time(energy_half_time),
        _min_distance(min_distance),
        _max_distance(max_distance),
        _min_height_above_terrain(min_height_above_terrain),
        _terrain_collision_inertia(terrain_collision_inertia),
        _target(target)
    {
        PositionalViewpoint vp{ecef_to_geo3(lm::extract_translation(pose)), ecef_to_geo3(_target)};
        _viewpoint = positional_to_angular_viewpoint(vp, pose);
        _pose = to_dual_quat(_viewpoint);
    }

    std::unique_ptr<DistanceToFixedTargetDriver> recreate_with_pose(
        const lm::ddual_quat& pose) override
    {
        return create(
            _energy_half_time, pose, _target, _min_distance, _max_distance,
            _min_height_above_terrain, _terrain_collision_inertia);
    }

    void set_steps_this_frame(double steps) override
    {
        _energy_type = ThisFrame;
        _received = steps;
    }

    void add_steps_total(double steps) override
    {
        if (_energy_type != Total)
        {
            _received = 0;
        }
        _energy_type = Total;
        _received += steps;
    }

    void set_steps_velocity(double steps) override
    {
        _energy_type = Velocity;
        _received = steps;
    }

    lm::ddual_quat get_pose() const override { return _pose; }

    lm::ddual_quat work(double dt, double height_above_terrain, bool should_keep_bearing) override
    {
        bool recompute_pose = false;

        if (height_above_terrain < _min_height_above_terrain)
        {
            _viewpoint.tilt = push_camera_position_above_terrain_with_target(
                                  _pose, height_above_terrain, _min_height_above_terrain, _target,
                                  _terrain_collision_inertia, dt)
                                  .tilt;
            recompute_pose = true;
        }

        Inertia inertia(_energy_half_time, dt);

        if (_energy_type != None)
        {
            switch (_energy_type)
            {
                case ThisFrame: _energy = inertia.apply_quantity_this_frame(_received); break;
                case Total:
                    _energy = add_or_replace_if_opposite(
                        _energy, inertia.apply_quantity_total(_received));
                    break;
                case Velocity: _energy = inertia.apply_velocity_this_frame(_received); break;
                default: break;
            }
            _energy_type = None;
        }

        if (_energy != 0)
        {
            double new_distance =
                _viewpoint.distance * std::pow(2, -inertia.get_quantity_this_frame(_energy));

            if (new_distance < _min_distance)
            {
                new_distance = _min_distance;
                _energy = 0;
            }
            else if (new_distance > _max_distance)
            {
                new_distance = _max_distance;
                _energy = 0;
            }

            _energy = inertia.apply_energy_loss(_energy, 0.01);
            _viewpoint.distance = new_distance;

            recompute_pose = true;
        }

        if (recompute_pose)
        {
            _pose = to_dual_quat(_viewpoint);
        }

        return _pose;
    }

    bool is_idle() const override { return _energy == 0; }

    void update_energy_half_time(double e) override
    {
        _energy = convert_energy(_energy, _energy_half_time, e);
        _energy_half_time = e;
    }

    void update_terrain_settings(double min_height_above_terrain, double terrain_collision_inertia)
        override
    {
        _min_height_above_terrain = min_height_above_terrain;
        _terrain_collision_inertia = terrain_collision_inertia;
    }
};

std::unique_ptr<DistanceToFixedTargetDriver> DistanceToFixedTargetDriver::create(
    double energy_half_time,
    const lm::ddual_quat& pose,
    const lm::dvec3& target,
    double min_distance,
    double max_distance,
    double min_height_above_terrain,
    double terrain_collision_inertia)
{
    return std::make_unique<DistanceToFixedTargetDriverImpl>(
        energy_half_time, pose, target, min_distance, max_distance, min_height_above_terrain,
        terrain_collision_inertia);
}

class AnimationDriverImpl : public AnimationDriver
{
    AnimationPlayer _player;
    lm::ddual_quat _pose;

public:
    AnimationDriverImpl(
        const lm::ddual_quat& from,
        const lm::ddual_quat& to,
        const hrz_proto::CameraAnimationOptions& options,
        double min_height_above_terrain,
        double terrain_collision_inertia) :
        _player(from, to, options, min_height_above_terrain, terrain_collision_inertia), _pose{from}
    {
    }

    AnimationDriverImpl(
        const Animation& animation,
        const hrz_proto::CameraAnimationOptions& options,
        double min_height_above_terrain,
        double terrain_collision_inertia) :
        _player(animation, options, min_height_above_terrain, terrain_collision_inertia),
        _pose{animate(animation, 0)}
    {
    }

    lm::ddual_quat get_pose() const override { return _pose; }

    lm::ddual_quat work(double dt, double height_above_terrain, bool should_keep_bearing) override
    {
        if (!_player.is_finished())
        {
            _pose = _player.advance(dt, height_above_terrain);
        }
        return _pose;
    }

    bool is_idle() const override { return _player.is_finished(); }

    void update_energy_half_time(double e) override {}

    void update_terrain_settings(double min_height_above_terrain, double terrain_collision_inertia)
        override
    {
    }
};

std::unique_ptr<AnimationDriver> AnimationDriver::create(
    const lm::ddual_quat& from,
    const lm::ddual_quat& to,
    const hrz_proto::CameraAnimationOptions& options,
    double min_height_above_terrain,
    double terrain_collision_inertia)
{
    return std::make_unique<AnimationDriverImpl>(
        from, to, options, min_height_above_terrain, terrain_collision_inertia);
}

std::unique_ptr<AnimationDriver> AnimationDriver::create(
    const Animation& animation,
    const hrz_proto::CameraAnimationOptions& options,
    double min_height_above_terrain,
    double terrain_collision_inertia)
{
    return std::make_unique<AnimationDriverImpl>(
        animation, options, min_height_above_terrain, terrain_collision_inertia);
}

} // namespace hrz::camera
