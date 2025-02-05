#pragma once

#include <hrz_common_geo.h>

#include <lin_maths.h>

namespace hrz::camera
{
// For tilt between -pi and pi
static constexpr double kPoseTiltLimit = lm::PI / 2 - 0.0001;

// For tilt between 0 and pi
static constexpr double kTiltLimitMin = 0.0001;
static constexpr double kTiltLimitMax = lm::PI - 0.0001;

static constexpr double kMaxAltitude = hrz::EARTH_RADIUS * 2;

struct AngularViewpoint
{
    GeoPosition3 target;

    double distance;

    // In radians, 0 is north, pi/2 is east.
    double bearing;

    // In radians, 0 is straight down, pi/2 is horizontal.
    double tilt;
};

AngularViewpoint from_proto(const hrz_proto::AngularViewpoint&);
hrz_proto::AngularViewpoint to_proto(const AngularViewpoint&);
AngularViewpoint from_bounds_view(const hrz_proto::BoundsView&, double fovy, double aspect_ratio);
lm::ddual_quat to_dual_quat(const AngularViewpoint&);
AngularViewpoint angular_viewpoint_from_dual_quat(const lm::ddual_quat&);

// Computes the local tilt for a given pose as dual quaternion.
// The local tilt is 0 when straight down, pi/2 when horizontal.
// When not in [0, pi], the camera would look upside-down.
double local_tilt(const lm::ddual_quat&);

struct Pose
{
    GeoPosition3 position;

    // In radians, 0 is north, pi/2 is east.
    double bearing;

    // In radians, 0 is horizontal, -pi/2 is straight down.
    double tilt;
};

Pose from_proto(const hrz_proto::Pose&);
hrz_proto::Pose to_proto(const Pose&);
lm::ddual_quat to_dual_quat(const Pose&);
Pose pose_from_dual_quat(const lm::ddual_quat&);

struct PositionalViewpoint
{
    GeoPosition3 position;
    GeoPosition3 target;
};

PositionalViewpoint from_proto(const hrz_proto::PositionalViewpoint&);
hrz_proto::PositionalViewpoint to_proto(const PositionalViewpoint&);
lm::ddual_quat to_dual_quat(const PositionalViewpoint&);
PositionalViewpoint positional_viewpoint_from_dual_quat(const lm::ddual_quat&);

// An optional pose can be given which can be used as a hint to find
// the bearing when the position is at the vertical of the target.
AngularViewpoint positional_to_angular_viewpoint(
    const PositionalViewpoint& vp,
    std::optional<lm::ddual_quat> pose);

} // namespace hrz::camera
