#include "camera/hrz_core_camera_viewpoint.h"

#include "hrz_core_debug_draw.h"

#include <hrz_common_maths.h>
#include <hrz_common_proto_geo.h>
#include <hrz_fnd_log.h>
#include <hrz_fnd_static_vector.h>

namespace hrz::camera
{
AngularViewpoint positional_to_angular_viewpoint(
    const PositionalViewpoint& vp,
    std::optional<lm::ddual_quat> pose)
{
    lm::dmat4 target_ecef_to_enu = hrz::ecef_to_enu_transform_for_geo(vp.target);
    lm::dvec3 position_ecef = hrz::geo_to_ecef(vp.position);
    lm::dvec3 position_in_target_enu = (target_ecef_to_enu * lm::dvec4(position_ecef, 1)).xyz;
    lm::dvec3 forward_enu = lm::normalize(-position_in_target_enu);

    double bearing = 0;
    if (pose)
    {
        lm::dvec3 side_ecef = pose->r * lm::dvec3(1, 0, 0);
        lm::dvec3 side_in_target_enu = (target_ecef_to_enu * lm::dvec4(side_ecef, 0.0)).xyz;
        bearing = std::atan2(-side_in_target_enu.y, side_in_target_enu.x);
    }
    else if (std::abs(forward_enu.z) < 0.9999999999)
    {
        bearing = std::atan2(forward_enu.x, forward_enu.y);
    }

    double tilt = std::atan2(lm::length(forward_enu.xy), -forward_enu.z);

    return AngularViewpoint{vp.target, lm::length(position_in_target_enu), bearing, tilt};
}

AngularViewpoint from_proto(const hrz_proto::AngularViewpoint& pv)
{
    AngularViewpoint vp;
    vp.target = hrz::from_proto(pv.target());
    vp.distance = pv.distance();
    vp.bearing = pv.bearing();
    vp.tilt = pv.tilt();
    return vp;
}

AngularViewpoint angular_viewpoint_from_dual_quat(const lm::ddual_quat& pose)
{
    return positional_to_angular_viewpoint(positional_viewpoint_from_dual_quat(pose), pose);
}

hrz_proto::AngularViewpoint to_proto(const AngularViewpoint& src)
{
    hrz_proto::AngularViewpoint dst;
    dst.mutable_target()->CopyFrom(to_proto(src.target));
    dst.set_distance(src.distance);
    dst.set_tilt(src.tilt);
    dst.set_bearing(src.bearing);
    return dst;
}

AngularViewpoint from_bounds_view(
    const hrz_proto::BoundsView& bounds_view,
    double fovy,
    double aspect_ratio)
{
    double min_alt = bounds_view.min_altitude();
    double max_alt = bounds_view.max_altitude();
    if (min_alt > max_alt) std::swap(min_alt, max_alt);

    // We don't use from_proto to transfom the geo bounds because we don't want to normalize the
    // coordinates such that -180 and 180 longitude remain as separate coordinates so we can detect
    // that we want to display the entire extent of the Earth vs. just a longitude line.
    double north = hrz::clamp_latitude(lm::radians(bounds_view.bounds().north()));
    double south = hrz::clamp_latitude(lm::radians(bounds_view.bounds().south()));
    double east = lm::radians(bounds_view.bounds().east());
    double west = lm::radians(bounds_view.bounds().west());
    if (north < south) std::swap(north, south);

    double longitude_extent = hrz::clamp(east - west, -2 * lm::PI, 2 * lm::PI);
    if (longitude_extent < 0) longitude_extent += 2 * lm::PI;
    double center_longitude = west + longitude_extent / 2;

    // We don't need to see more than 180° on the Earth because we are in 3D so we can't!
    GeoPosition3 geo_extent_half(
        (north - south) / 2, std::min(longitude_extent, lm::PI) * 0.5, (max_alt - min_alt) / 2);

    GeoPosition3 center((north + south) / 2, center_longitude, (max_alt + min_alt) / 2);

    // Compute the plane representing the tilt at the given center
    lm::dvec3 center_ecef = hrz::geo_to_ecef(center);
    lm::dmat3 enu_to_ecef(hrz::enu_to_ecef_rotation_matrix_for_geo(center.latlon()));
    lm::dvec3 plane_x_ecef = enu_to_ecef.x;
    lm::dmat3 plane_tilt_rotation =
        lm::rotation3_normalized(lm::axis_angle<double>(plane_x_ecef, bounds_view.tilt()))
        * enu_to_ecef;
    lm::dvec3 plane_y_ecef = plane_tilt_rotation * lm::dvec3(0, 1, 0);

    // Generate a bunch of points around the volume and project them to the plane parallel to the
    // view that goes through the center, use that to compute a 2D bbox on this plane that contains
    // all points. In fact this bbox is forced to be centered around the center to actually we only
    // compute the maximum of the half extents.
    double extent_x = 0;
    double extent_y = 0;
    for (double lat = -1; lat <= 1; lat += 0.5)
    {
        for (double lon = -1; lon <= 1; lon += 0.5)
        {
            for (double alt = -1; alt <= 1; alt += 2)
            {
                GeoPosition3 pos_geo(
                    center.lat + geo_extent_half.lat * lat, center.lon + geo_extent_half.lon * lon,
                    center.alt + geo_extent_half.alt * alt);

                lm::dvec3 p = hrz::geo_to_ecef(pos_geo) - center_ecef;
                double x_on_plane = lm::dot(p, plane_x_ecef);
                double y_on_plane = lm::dot(p, plane_y_ecef);

                extent_x = std::max(extent_x, std::abs(x_on_plane));
                extent_y = std::max(extent_y, std::abs(y_on_plane));
            }
        }
    }

    // Compute the distance necessary to see the projected extent, and choose the largest one to
    // place the camera at.
    double fovx = vertical_to_horizontal_fov(fovy, aspect_ratio);
    double dist_x = extent_x / std::tan(fovx / 2);
    double dist_y = extent_y / std::tan(fovy / 2);
    double distance = hrz::clamp(std::max(dist_x, dist_y), 100.0, 2 * hrz::EARTH_RADIUS);

    AngularViewpoint destination;
    destination.target = center;
    destination.bearing = 0;
    destination.tilt = bounds_view.tilt();
    destination.distance = distance;

    return destination;
}

lm::ddual_quat to_dual_quat(const AngularViewpoint& vp)
{
    // By default the camera is looking at -z (tilt = 0), with x to the right
    // and y to the top (bearing = 0).
    return hrz::enu_to_ecef_dual_quat_for_geo(vp.target)
        * lm::axis_angle_dquat<double>({0, 0, 1}, -vp.bearing)
        * lm::axis_angle_dquat<double>({1, 0, 0}, vp.tilt)
        * lm::translation_dquat<double>({0, 0, vp.distance});
}

Pose from_proto(const hrz_proto::Pose& proto)
{
    Pose p;
    p.position = hrz::from_proto(proto.position());
    p.bearing = proto.bearing();
    p.tilt = proto.tilt();
    return p;
}

hrz_proto::Pose to_proto(const Pose& pose)
{
    hrz_proto::Pose p;
    p.mutable_position()->CopyFrom(hrz::to_proto(pose.position));
    p.set_tilt(pose.tilt);
    p.set_bearing(pose.bearing);
    return p;
}

lm::ddual_quat to_dual_quat(const Pose& pose)
{
    // By default the camera is looking at y, x to the right, and z to the top.

    // The matrix going from camera view to ENU in the initial configuration is
    // [ 1  0  0 ]
    // [ 0  0 -1 ]
    // [ 0  1  0 ]
    // Unrolling manually the matrix_to_quaternion function we get the
    // coresponding quaternion.
    static constexpr double INV_SQRT2 = 0.7071067811865475244;
    static const lm::dquat cam_to_enu(INV_SQRT2, 0.0, 0.0, INV_SQRT2);

    return hrz::enu_to_ecef_dual_quat_for_geo(pose.position)
        * lm::ddual_quat(
               lm::axis_angle<double>({0, 0, 1}, -pose.bearing)
               * lm::axis_angle<double>({1, 0, 0}, pose.tilt) * cam_to_enu);
}

Pose pose_from_dual_quat(const lm::ddual_quat& q)
{
    Pose pose{};
    pose.position = ecef_to_geo3(lm::extract_translation(q));

    lm::dmat3 rotation =
        lm::rotation3_normalized(ecef_to_enu_quat_for_geo(pose.position.latlon()) * q.r);

    lm::dvec3 forward_enu = rotation * lm::dvec3(0, 0, -1);
    lm::dvec3 side_enu = rotation * lm::dvec3(1, 0, 0);

    pose.bearing = std::atan2(-side_enu.y, side_enu.x);
    pose.tilt = std::atan2(forward_enu.z, lm::length(forward_enu.xy));

    return pose;
}

PositionalViewpoint from_proto(const hrz_proto::PositionalViewpoint& proto)
{
    PositionalViewpoint vp;
    vp.target = hrz::from_proto(proto.target());
    vp.position = hrz::from_proto(proto.position());
    return vp;
}

PositionalViewpoint positional_viewpoint_from_dual_quat(const lm::ddual_quat& pose)
{
    lm::dvec3 position_ecef = lm::extract_translation(pose);
    lm::dvec3 target_ecef;

    lm::dvec3 forward_ecef = pose.r * lm::dvec3(0, 0, -1);
    hrz::Ray forward_ray{position_ecef, forward_ecef};
    lm::dvec3 forward_target_ecef;
    if (hrz::planet_intersection(forward_ray, &forward_target_ecef, 0))
    {
        // If the camera is below elevation 0, the forward ray intersection
        // test returns a position on the other side of the planet. This is
        // not conceptually correct, but can also lead to precisions issues
        // when the distance to the target is large.
        // So we also check backwards. If this second test confirms that the
        // camera is inside the planet, a dummy target slightly in front of
        // the camera is returned.
        lm::dvec3 backward_ecef = pose.r * lm::dvec3(0, 0, 1);
        hrz::Ray backward_ray{position_ecef, backward_ecef};
        lm::dvec3 backward_target_ecef;
        if (hrz::planet_intersection(backward_ray, &backward_target_ecef, 0)
            && (lm::length2(backward_target_ecef - position_ecef)
                < lm::length2(forward_target_ecef - position_ecef)))
        {
            target_ecef = position_ecef + forward_ecef;
        }
        else
        {
            target_ecef = forward_target_ecef;
        }
    }
    else
    {
        target_ecef = position_ecef + forward_ecef;
    }
    return PositionalViewpoint{hrz::ecef_to_geo3(position_ecef), hrz::ecef_to_geo3(target_ecef)};
}

hrz_proto::PositionalViewpoint to_proto(const PositionalViewpoint& src)
{
    hrz_proto::PositionalViewpoint dst;
    dst.mutable_position()->CopyFrom(hrz::to_proto(src.position));
    dst.mutable_target()->CopyFrom(hrz::to_proto(src.target));
    return dst;
}

lm::ddual_quat to_dual_quat(const PositionalViewpoint& vp)
{
    return to_dual_quat(positional_to_angular_viewpoint(vp, std::nullopt));
}

double local_tilt(const lm::ddual_quat& q)
{
    auto geo_position = ecef_to_geo3(lm::extract_translation(q));
    lm::dmat3 rotation =
        lm::rotation3_normalized(ecef_to_enu_quat_for_geo(geo_position.latlon()) * q.r);

    // Rotate the frame such that the x axis is aligned with the "side" axis of the pose in ENU
    // frame. Then the "forward" vector is in the YZ plane, which is what we want to compute the
    // tilt such that atan2 computes the angle correctly.
    lm::dvec3 side_enu = rotation * lm::dvec3(1, 0, 0);
    rotation =
        lm::dmat3({side_enu.x, -side_enu.y, 0}, {side_enu.y, side_enu.x, 0}, {0, 0, 1}) * rotation;
    lm::dvec3 forward_aligned = rotation * lm::dvec3(0, 0, -1);

    return std::atan2(forward_aligned.y, -forward_aligned.z);
}

} // namespace hrz::camera
