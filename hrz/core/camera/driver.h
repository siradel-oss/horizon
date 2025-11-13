#pragma once

#include "hrz/common/geo.h"
#include "hrz/common/maths.h"
#include "hrz/core/camera/animation.h"

#include <lin_maths.h>

namespace hrz::camera
{
// Driver model a type of movement. They are input independent and handle things like limits,
// inertia, etc.
// Each type of driver exposes an interfaces that correspond to the possible actions on the degrees
// of freedom it implements. Just like manipulators, the initial pose can be given later for delayed
// initialization, for example for elevation queries.

class ICameraDriver
{
public:
    virtual ~ICameraDriver() = default;

    virtual lm::ddual_quat get_pose() const = 0;

    // This shouldn't be called before a pose has been provided.
    virtual lm::ddual_quat work(
        double dt,
        double height_above_terrain,
        bool should_keep_bearing) = 0;

    virtual bool is_idle() const = 0;

    virtual void update_energy_half_time(double) = 0;

    virtual void update_terrain_settings(
        double min_height_above_terrain,
        double terrain_collision_inertia) = 0;
};

class ZoomDriver : public ICameraDriver
{
public:
    static std::unique_ptr<ZoomDriver> create(
        double energy_half_time,
        const lm::ddual_quat& pose,
        const hrz::GeoBounds& bounds_limit,
        double max_altitude,
        double min_tilt,
        double max_tilt,
        double min_height_above_terrain,
        double terrain_collision_inertia);

    virtual std::unique_ptr<ZoomDriver> recreate_with_pose(const lm::ddual_quat& pose) = 0;

    // Queue an "instant" zoom: zoom by the given amount at the next frame
    virtual void set_steps(const lm::dvec3& target, double steps) = 0;

    // Queue a delayed zoom: zoom by the given amount during the next few frames
    virtual void add_steps(const lm::dvec3& target, double steps) = 0;

    // Queue a zoom velocity: zoom by the given amount during the next second
    virtual void set_steps_velocity(const lm::dvec3& target, double steps) = 0;
};

class ZoomBearingDriver : public ZoomDriver
{
public:
    static std::unique_ptr<ZoomBearingDriver> create(
        double energy_half_time,
        const lm::ddual_quat& pose,
        const hrz::GeoBounds& bounds_limit,
        double max_altitude,
        double min_tilt,
        double max_tilt,
        double min_height_above_terrain,
        double terrain_collision_inertia);

    virtual void set_bearing_rotation_this_frame(
        const lm::dvec3& target,
        const lm::dvec3& axis,
        double angle) = 0;
};

class RotateAroundCenterDriver : public ICameraDriver
{
public:
    // Min & max tilt use the 0=down convention
    static std::unique_ptr<RotateAroundCenterDriver> create(
        double energy_half_time,
        const lm::ddual_quat& pose,
        double min_tilt,
        double max_tilt,
        double min_height_above_terrain,
        double terrain_collision_inertia);

    virtual std::unique_ptr<RotateAroundCenterDriver> recreate_with_pose(
        const lm::ddual_quat& pose) = 0;
    virtual void set_rotation_this_frame(double d_bearing, double d_tilt) = 0;
    virtual void add_rotation_total(double d_bearing, double d_tilt) = 0;
    virtual void set_rotation_velocity(double d_bearing, double d_tilt) = 0;
};

class MoveAroundPlanetDriver : public ICameraDriver
{
public:
    static std::unique_ptr<MoveAroundPlanetDriver> create(
        double energy_half_time,
        const lm::ddual_quat& pose,
        const hrz::GeoBounds& limit_bounds,
        double min_height_above_terrain,
        double terrain_collision_inertia);

    virtual std::unique_ptr<MoveAroundPlanetDriver> recreate_with_pose(
        const lm::ddual_quat& pose) = 0;

    // Sets the rotation to apply next frame. t is the slerp coefficient, 1 for full rotation.
    virtual void set_rotation_around_earth_center_this_frame(
        const lm::dquat& q,
        double t = 1.0) = 0;

    // Sets the rotation to apply over the next frames. t is the slerp coefficient, 1 for full
    // rotation.
    virtual void set_rotation_around_earth_center_total(const lm::dquat& q, double t) = 0;

    // Adds more energy to apply over the next frames. The rotation can be changed.
    virtual void add_rotation_around_earth_center_total(const lm::dquat& q, double t) = 0;

    // Sets the rotation velocity to apply this frame. t is the slerp coefficient per
    // second, 1 for full rotation.
    virtual void set_rotation_velocity_around_earth_center(const lm::dquat& q, double t) = 0;
};

class OrbitRotateAroundTargetDriver : public ICameraDriver
{
public:
    static std::unique_ptr<OrbitRotateAroundTargetDriver> create(
        double energy_half_time,
        const lm::ddual_quat& pose,
        const hrz::GeoBounds& bounds_limit,
        double max_altitude,
        double min_tilt,
        double max_tilt,
        double min_height_above_terrain,
        double terrain_collision_inertia);

    virtual void set_rotation_this_frame(
        const lm::dvec3& target,
        const lm::dvec3& bearing_axis,
        float bearing,
        float tilt) = 0;
};

class RotateAroundFixedTargetDriver : public ICameraDriver
{
public:
    static std::unique_ptr<RotateAroundFixedTargetDriver> create(
        double energy_half_time,
        const lm::ddual_quat& pose,
        const lm::dvec3& target,
        double min_tilt,
        double max_tilt,
        double min_height_above_terrain,
        double terrain_collision_inertia);

    virtual std::unique_ptr<RotateAroundFixedTargetDriver> recreate_with_pose(
        const lm::ddual_quat& pose) = 0;
    virtual void set_rotation_this_frame(double d_bearing, double d_tilt) = 0;
    virtual void add_rotation_total(double d_bearing, double d_tilt) = 0;
    virtual void set_rotation_velocity(double d_bearing, double d_tilt) = 0;
};

class DistanceToFixedTargetDriver : public ICameraDriver
{
public:
    static std::unique_ptr<DistanceToFixedTargetDriver> create(
        double energy_half_time,
        const lm::ddual_quat& pose,
        const lm::dvec3& target,
        double min_distance,
        double max_distance,
        double min_height_above_terrain,
        double terrain_collision_inertia);

    virtual std::unique_ptr<DistanceToFixedTargetDriver> recreate_with_pose(
        const lm::ddual_quat& pose) = 0;
    virtual void set_steps_this_frame(double steps) = 0;
    virtual void add_steps_total(double steps) = 0;
    virtual void set_steps_velocity(double steps) = 0;
};

class MaintainHeightAboveTerrainDriver : public ICameraDriver
{
public:
    static std::unique_ptr<MaintainHeightAboveTerrainDriver> create(
        const lm::ddual_quat& pose,
        double min_height_above_terrain,
        double terrain_collision_inertia);

    virtual std::unique_ptr<MaintainHeightAboveTerrainDriver> recreate_with_pose(
        const lm::ddual_quat& pose) = 0;
};

class AnimationDriver : public ICameraDriver
{
public:
    static std::unique_ptr<AnimationDriver> create(
        const lm::ddual_quat& from,
        const lm::ddual_quat& to,
        const hrz_proto::CameraAnimationOptions&,
        double min_height_above_terrain,
        double terrain_collision_inertia);

    static std::unique_ptr<AnimationDriver> create(
        const Animation& animation,
        const hrz_proto::CameraAnimationOptions& options,
        double min_height_above_terrain,
        double terrain_collision_inertia);
};

} // namespace hrz::camera
