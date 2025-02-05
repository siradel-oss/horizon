#include "camera/hrz_core_camera_terrain.h"

#include "camera/hrz_core_camera_viewpoint.h"

#include <hrz_fnd_maths.h>

namespace hrz::camera
{
lm::ddual_quat push_camera_position_above_terrain(
    const lm::ddual_quat& pose,
    double height_above_terrain,
    double target_height_above_terrain,
    double collision_inertia,
    double dt)
{
    double damping = hrz::clamp(dt / collision_inertia, 0.0, 1.0);
    target_height_above_terrain =
        hrz::lerp(height_above_terrain, target_height_above_terrain, damping);

    Pose before = pose_from_dual_quat(pose);

    double target_alt = before.position.alt - height_above_terrain + target_height_above_terrain;
    before.position.alt = target_alt;

    return to_dual_quat(before);
}

PushedCameraPositionForTarget push_camera_position_above_terrain_with_target(
    const lm::ddual_quat& pose,
    double height_above_terrain,
    double target_height_above_terrain,
    const lm::dvec3& target,
    double collision_inertia,
    double dt)
{
    double damping = hrz::clamp(dt / collision_inertia, 0.0, 1.0);
    target_height_above_terrain =
        hrz::lerp(height_above_terrain, target_height_above_terrain, damping);

    Pose before = pose_from_dual_quat(pose);
    double distance_to_target = lm::length(hrz::geo_to_ecef(before.position) - target);

    hrz::GeoPosition3 height_adjusted_geo = before.position;
    height_adjusted_geo.alt += -height_above_terrain + target_height_above_terrain;

    lm::dvec3 height_adjusted_ecef = hrz::geo_to_ecef(height_adjusted_geo);
    lm::dvec3 dist_adjusted_ecef =
        lm::normalize(height_adjusted_ecef - target) * distance_to_target + target;
    hrz::GeoPosition3 dist_ajusted_geo = hrz::ecef_to_geo3(dist_adjusted_ecef);

    auto transform = hrz::ecef_to_enu_transform_for_geo(dist_ajusted_geo);
    lm::dvec3 enu_target = (transform * lm::dvec4(target, 1.0)).xyz;

    double tilt = std::atan2(enu_target.z, lm::length(enu_target.xy));

    Pose adjusted_pose = Pose{dist_ajusted_geo, before.bearing, tilt};

    double tilt_for_viewpoint = tilt + lm::PI * 0.5;

    return {to_dual_quat(adjusted_pose), tilt_for_viewpoint};
}
} // namespace hrz::camera
