// SPDX-FileCopyrightText: Copyright 2023 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/core/camera/types.h"
#include "hrz/core/camera/viewpoint.h"
#include "hrz/protocol/camera/animation.pb.h"

#include <lin_maths.h>

#include <variant>

namespace hrz::camera
{

inline double easing_in(double t, double p)
{
    assert(p >= 0.0);
    p = (p == 0.0) ? 1.0 : p;
    return std::pow(t, p);
}

inline double easing_out(double t, double p)
{
    assert(p >= 0.0);
    p = (p == 0.0) ? 1.0 : p;
    return 1.0 - std::pow(1.0 - t, p);
}

inline double easing_inout(double t, double p)
{
    assert(p >= 0.0);
    p = (p == 0.0) ? 1.0 : p;
    return 0.5 + 0.5 * lm::sign(t - 0.5) * (1.0 - std::pow(1.0 - std::abs(2.0 * t - 1.0), p));
}

inline double easing(double t, double p, hrz_proto::EasingFunctions f)
{
    switch (f)
    {
        case hrz_proto::EasingFunctions::EASE_IN: return easing_in(t, p);
        case hrz_proto::EasingFunctions::EASE_OUT: return easing_out(t, p);
        case hrz_proto::EasingFunctions::EASE_INOUT: return easing_inout(t, p);
        default: return t;
    }
}

struct LinearAnimation
{
    double bearing_0, bearing_diff;
    double tilt_0, tilt_diff;
    double lat_0, lat_diff;
    double lon_0, lon_diff;
    double alt_0, alt_diff;

    lm::ddual_quat operator ()(double t) const
    {
        return to_dual_quat(
            Pose{
                GeoPosition3{
                    lat_0 + t * lat_diff,
                    lon_0 + t * lon_diff,
                    alt_0 + t * alt_diff,
                },
                bearing_0 + t * bearing_diff,
                tilt_0 + t * tilt_diff,
            });
    }
};

struct AroundTargetAnimation
{
    double bearing_0, bearing_diff;
    double tilt_0, tilt_diff;
    double dist_0, dist_diff;
    hrz::GeoPosition3 target;

    lm::ddual_quat operator ()(double t) const
    {
        return to_dual_quat(
            AngularViewpoint{
                target,
                dist_0 + t * dist_diff,
                bearing_0 + t * bearing_diff,
                tilt_0 + t * tilt_diff,
            });
    }
};

struct ArcAnimation
{
    // This evaluated offset(t_path) = h_arc(t_arc(t_path)) - h_path(t_path)
    // And t_arc(t_path) is an affine transform. See the graph above.
    //
    // h_arc(t_arc) = radius * sqrt(1 - t_arc²)

    LinearAnimation target_animation;

    double t_arc_0{}, t_arc_diff{};
    double h_path_0{}, h_path_diff{};
    double radius{};

    double offset_at(double t_path) const
    {
        double t_arc = t_arc_0 + t_path * t_arc_diff;
        double h_path = h_path_0 + t_path * h_path_diff;
        double h_arc = radius * std::sqrt(1 - t_arc * t_arc);
        return h_arc - h_path;
    }

    lm::ddual_quat operator ()(double t) const
    {
        lm::ddual_quat target = target_animation(t);
        lm::dvec3 forward = target.r * lm::dvec3(0, 0, -1);
        return lm::translation_dquat(-forward * offset_at(t)) * target;
    }
};

using Animation = std::variant<ArcAnimation, LinearAnimation, AroundTargetAnimation>;

lm::ddual_quat animate(const Animation& animation, double t);

Animation make_animation(
    const lm::ddual_quat& from,
    const lm::ddual_quat& to,
    hrz_proto::TrajectoryType trajectory);

Animation make_animation_around_target(
    const AngularViewpoint& from,
    double to_bearing,
    double to_tilt,
    double to_dist);

class AnimationPlayer
{
    hrz::clock::ClockType _clock_type{};
    hrz_proto::EasingFunctions _easing_fn{};
    double _duration{};
    double _t{};
    double _easing_exponent{};
    Animation _animation;
    lm::ddual_quat _to;
    double _min_height_above_terrain{};
    double _terrain_collision_inertia{};

public:
    AnimationPlayer() = default;

    AnimationPlayer(
        const lm::ddual_quat& from,
        const lm::ddual_quat& to,
        const hrz_proto::CameraAnimationOptions& options,
        double min_height_above_terrain,
        double terrain_collision_inertia,
        hrz::clock::ClockType clock_type);

    AnimationPlayer(
        const Animation& animation,
        const hrz_proto::CameraAnimationOptions& options,
        double min_height_above_terrain,
        double terrain_collision_inertia,
        hrz::clock::ClockType clock_type);

    constexpr bool is_finished() const { return _t >= 1.0; }

    constexpr void force_finish() { _t = 1.0; }

    // Guaranteed to return the exact "to" pose when finished.
    lm::ddual_quat advance(hrz::TimeStep dt, double height_above_terrain);
};

} // namespace hrz::camera
