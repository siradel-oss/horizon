#include "hrz/core/camera/animation.h"

#include "hrz/common/geometry.h"
#include "hrz/core/camera/terrain.h"

namespace hrz::camera
{
namespace
{

// Because angles are between [-180, 180] animations don't take the shortest
// path, for instance when going from longitude 170 to -170 it will pass through
// zero whereas what the user really want is an animation from 170 to 190.
//
// This functions returns the new diff such that from + diff is the shortest path
// for going between the two angles.
double angle_diff_for_shortest_path(double from, double to)
{
    double diff = std::fmod(to - from, 2 * lm::PI);
    if (diff > lm::PI) diff -= 2 * lm::PI;
    if (diff < -lm::PI) diff += 2 * lm::PI;
    return diff;
}

} // namespace

Animation make_animation_around_target(
    const AngularViewpoint& from,
    double to_bearing,
    double to_tilt,
    double to_dist)
{
    return AroundTargetAnimation{
        from.bearing,  angle_diff_for_shortest_path(from.bearing, to_bearing),
        from.tilt,     to_tilt - from.tilt,
        from.distance, to_dist - from.distance,
        from.target
    };
}

LinearAnimation make_linear_animation(const lm::ddual_quat& from, const lm::ddual_quat& to)
{
    Pose pose_from = pose_from_dual_quat(from);
    Pose pose_to = pose_from_dual_quat(to);

    return LinearAnimation{
        pose_from.bearing,
        angle_diff_for_shortest_path(pose_from.bearing, pose_to.bearing),
        pose_from.tilt,
        pose_to.tilt - pose_from.tilt,
        pose_from.position.lat,
        pose_to.position.lat - pose_from.position.lat,
        pose_from.position.lon,
        angle_diff_for_shortest_path(pose_from.position.lon, pose_to.position.lon),
        pose_from.position.alt,
        pose_to.position.alt - pose_from.position.alt,
    };
}

// The arc animation happens is two parts. The targeted point follows the linear path. The camera is
// offset along the "-forward" direction of the camera by a factor we compute with the arc. Let's
// call it offset.
//
// The offset is computed by first creating a circular arc between the from and to points in
// lat-lon-alt space such that the center of the circle is on the planet. This makes sure that
// points higher up the atmosphere have less of an arc around them. See the graph below.
//
// Then, when animating, we compute the offset by looking at the height of the arc (h_arc) vertical
// to the current target point, at altitude h_path. Then offset = h_arc - h_path.
// So the arc is never really used by the camera, it's just used to compute the offset.
// In the end we want offset(t_path).
//
//                                     .     x = arc path
//                         xxxx       .      y = linear path
//            from   xxxxxx    xxxxx .
//                xx      |h_arc    .xx
//                yyy     |        .   xx
//           x       yyy  |      .       x
//                      yy|y    .         x
//                  h_path| yyy.Middle     x
//        x               |   .yyyy         x
//                        |  .     yyyy      x
//      x                 | .          yyy    x
//                        |.              yyy x
//     x                  . Center           yyy To
//
//    -1------------------0-------------------1 = t_arc
//                0---------------------------1 = t_path

Animation make_arc_animation(const lm::ddual_quat& from_pose, const lm::ddual_quat& to_pose)
{
    LinearAnimation target_animation = make_linear_animation(from_pose, to_pose);

    // We compute a sperical arc joining the two points in lat-lon space. We will flatten the Z
    // dimension so that it looks spherical once transformed back to ECEF. Doing this in lat-lon
    // makes the animation not take the shortest path, which looks more natural, even if it's a
    // bit counter-intuitive.

    // This flattens the arc a little bit.
    // 1.0 is circular. The higher, the flatter.
    static constexpr double kArcFlattening = 1.1;

    lm::dvec3 from_ecef = lm::extract_translation(from_pose);
    lm::dvec3 to_ecef = lm::extract_translation(to_pose);

    GeoPosition3 from_geo = ecef_to_geo3(from_ecef);
    GeoPosition3 to_geo = ecef_to_geo3(to_ecef);

    lm::dvec3 from_surface_ecef = geo_to_ecef(from_geo.lat, from_geo.lon, 0);
    lm::dvec3 to_surface_ecef = geo_to_ecef(to_geo.lat, to_geo.lon, 0);

    if (from_surface_ecef == to_surface_ecef)
    {
        return target_animation;
    }

    double lat_diff = to_geo.lat - from_geo.lat;

    // Handle the antimeridian here
    double lon_diff = angle_diff_for_shortest_path(from_geo.lon, to_geo.lon);

    // We're going to work in lat-lon space, however we also have the Z component, which is
    // still in meters. We need to find a factor to multiply it such that the relation between
    // degrees and meters is somewhat proportional. Do to so, we compute the distance between
    // the points at the surface of the planet in lat-lon and ECEF, and the ratio is this
    // factor. The ECEF distance computes the distance "going through" the planet: this is fine
    // and in facts is prevents the arc from being to high when going around the planet.
    double altitude_factor = std::sqrt(lon_diff * lon_diff + lat_diff * lat_diff)
        / lm::length(from_surface_ecef - to_surface_ecef) * kArcFlattening;

    // Now we have the two points in our lat-lon-flattened space we're going to build the arc
    // in. We use vec3 instead of GeoPosition3 since it makes doing math easier.
    lm::dvec3 from(from_geo.lon, from_geo.lat, from_geo.alt * altitude_factor);
    lm::dvec3 to(from_geo.lon + lon_diff, from_geo.lat + lat_diff, to_geo.alt * altitude_factor);

    if (to == from)
    {
        return target_animation;
    }

    // Define the bisecting plane of the two points. We only need its normal. It will go through
    // the point in the middle of them.
    lm::dvec3 bisecting_plane_normal = lm::normalize(to - from);

    // Define the plane the animation (and thus the arc) is going to be in.
    lm::dvec3 animation_plane_normal = lm::cross(lm::dvec3(0, 0, 1), bisecting_plane_normal);
    if (lm::length2(animation_plane_normal) == 0)
    {
        return target_animation;
    }
    animation_plane_normal = lm::normalize(animation_plane_normal);

    // In 2D, all circles going through two points have their center on the bisecting line of
    // those two points. Here is 2D plane is the animation plane. The bisecting line is the
    // intersection of the intersecting plane and the animation plane. By construction we know
    // they are not coplanar. We know that the bisecting plane goes through the middle point,
    // and we know by construction that the middle point is also on the animation plane, so the
    // middle point is also on the bisecting line. Finally since the line is the intersection of
    // two planes, it lies in both planes, and thus it's orthogonal to both their normal. So
    // this direction is the cross product of their normals. We now know the direction and an
    // origin for the bisecting line. Yay.
    lm::dvec3 origin = (to + from) * 0.5;
    lm::dvec3 direction = lm::normalize(lm::cross(bisecting_plane_normal, animation_plane_normal));

    // Now we need to find a point on this line to be the center of the circular arc such that
    // it is "nice". It so happens that a good candidate is the intersection of the bisecting
    // line with the plane z=0 because it means that points at the surface will have a very
    // vertical movement at the beginning of their trajectory, but the higher a point is, the
    // flatter it will get. This is good because we don't want transitions between points high
    // in the atmosphere to form a large arc.
    Ray ray{origin, direction};

    // Since our intersection routine only compute hits "forward", we may need to reverse the
    // direction of the ray.
    if ((ray.o.z > 0) == (ray.dir.z > 0))
    {
        ray.dir *= -1.0;
    }

    lm::dvec3 center;
    if (!ray_plane_intersection(ray, {}, {0, 0, 1}, &center))
    {
        return target_animation;
    }

    double radius = lm::length(to - center);

    // Construct a 2D orthonormal basis in the animation plane with y being vertical and x being
    // horizontal, in the animation plane, with the circle center as origin.
    lm::dvec3 x_dir = lm::normalize(lm::cross(animation_plane_normal, lm::dvec3{0, 0, 1}));

    //   -1 ----- a0 -------- 0 -- a1 ------------ 1 . . . . t_arc
    //    |        |          |     |              |
    //    |        0 -------------- 1  . . . . . . | . . . . t_path
    //    |        |          |     |              |
    //   -R ----- x0 -------- 0 -- x1 ------------ R . . . . x

    // For t_arc, -1 and 1 will be at x=-radius and x = radius (or maybe the reverse).
    // Now we compute the x of t_path = 0 and 1.
    double x0 = lm::dot(from - center, x_dir);
    double x1 = lm::dot(to - center, x_dir);

    // Now compute t_arc for t_path = 0 and 1.
    double a0 = x0 / radius;
    double a1 = x1 / radius;

    return ArcAnimation{target_animation,
                        a0,
                        a1 - a0,
                        from.z / altitude_factor,
                        (to.z - from.z) / altitude_factor,
                        radius / altitude_factor};
}

lm::ddual_quat animate(const Animation& animation, double t)
{
    if (std::holds_alternative<LinearAnimation>(animation))
    {
        return std::get<LinearAnimation>(animation)(t);
    }
    else if (std::holds_alternative<ArcAnimation>(animation))
    {
        return std::get<ArcAnimation>(animation)(t);
    }
    else if (std::holds_alternative<AroundTargetAnimation>(animation))
    {
        return std::get<AroundTargetAnimation>(animation)(t);
    }
    else
    {
        assert(false);
        return {};
    }
}

Animation make_animation(
    const lm::ddual_quat& from,
    const lm::ddual_quat& to,
    hrz_proto::TrajectoryType trajectory)
{
    if (trajectory == hrz_proto::TrajectoryType::BALLISTIC)
    {
        return make_arc_animation(from, to);
    }
    else
    {
        return make_linear_animation(from, to);
    }
}

AnimationPlayer::AnimationPlayer(
    const lm::ddual_quat& from,
    const lm::ddual_quat& to,
    const hrz_proto::CameraAnimationOptions& options,
    double min_height_above_terrain,
    double terrain_collision_inertia) :
    _easing_fn{options.easing_function()},
    _duration{options.duration()},
    _easing_exponent{options.easing_exponent()},
    _animation{make_animation(from, to, options.trajectory_type())},
    _to{to},
    _min_height_above_terrain(min_height_above_terrain),
    _terrain_collision_inertia(terrain_collision_inertia)
{
}

AnimationPlayer::AnimationPlayer(
    const Animation& animation,
    const hrz_proto::CameraAnimationOptions& options,
    double min_height_above_terrain,
    double terrain_collision_inertia) :
    _easing_fn{options.easing_function()},
    _duration{options.duration()},
    _easing_exponent{options.easing_exponent()},
    _animation{animation},
    _to{animate(animation, 1)},
    _min_height_above_terrain(min_height_above_terrain),
    _terrain_collision_inertia(terrain_collision_inertia)
{
}

lm::ddual_quat AnimationPlayer::advance(double dt, double height_above_terrain)
{
    _t += dt / _duration;
    if (_t < 1.0)
    {
        double effective_t = easing(_t, _easing_exponent, _easing_fn);
        lm::ddual_quat pose = animate(_animation, effective_t);

        if (height_above_terrain < _min_height_above_terrain)
        {
            pose = push_camera_position_above_terrain(
                pose, height_above_terrain, _min_height_above_terrain, _terrain_collision_inertia,
                dt);
        }

        return pose;
    }
    else
    {
        return _to;
    }
}

} // namespace hrz::camera
