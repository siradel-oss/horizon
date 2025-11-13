#include "hrz/common/horizon_culling.h"

#include "hrz/common/geo.h"

namespace hrz
{
HorizonCuller::HorizonCuller(const lm::dvec3& eye_pos) : _eye_pos(eye_pos)
{
    // We work in the spherical Earth model.
    _eye_pos.z /= hrz::WGS84_AXES_LENGTH_RATIO;
    _eye_pos_length = lm::length(_eye_pos);
    _horizon_distance_squared = distance_to_horizon_squared(_eye_pos_length - hrz::EARTH_RADIUS);
    _horizon_plane_distance =
        _eye_pos_length - hrz::EARTH_RADIUS * hrz::EARTH_RADIUS / _eye_pos_length;
}

// See https://cesium.com/blog/2013/04/25/horizon-culling/ for the maths
bool HorizonCuller::is_occluded(lm::dvec3 p) const
{
    p.z /= hrz::WGS84_AXES_LENGTH_RATIO;

    auto p_to_eye = _eye_pos - p;

    double d = lm::dot(p_to_eye, _eye_pos / _eye_pos_length);
    if (d <= _horizon_plane_distance)
    {
        // In front of the horizon plane
        return false;
    }

    double d2 = d * _eye_pos_length;
    if (d2 * d2 / lm::length2(p_to_eye) <= _horizon_distance_squared)
    {
        // Outside the horizon cone
        return false;
    }

    return true;
}

namespace
{
// Compute the intersection point between `axis` and the plane tangent to the surface of the Earth
// passing through `position` (which we will call "horizon plane"), and if it exists, return the
// distance from that point to the center of the Earth.
// (See https://cesium.com/blog/2013/05/09/computing-the-horizon-occlusion-point/ for a schema.)
std::optional<double> compute_occlusion_point_distance(lm::dvec3 position, const lm::dvec3& axis)
{
    position.z /= hrz::WGS84_AXES_LENGTH_RATIO;

    double pos_len2 = lm::length2(position);
    double pos_len = std::sqrt(pos_len2);
    lm::dvec3 pos_normalized = position / pos_len;

    if (pos_len < hrz::EARTH_RADIUS)
    {
        // The position is underground, so it cannot intersect any plane that is tangent to the
        // surface of the Earth. The horizon plane does not exist.
        // Place the occlusion point on the surface of the Earth to cull as early as possible.
        return hrz::EARTH_RADIUS;
    }

    double cos_b = hrz::EARTH_RADIUS / pos_len;
    double sin_b = std::sqrt(pos_len2 - hrz::EARTH_RADIUS * hrz::EARTH_RADIUS) / pos_len;
    double cos_a = lm::dot(axis, pos_normalized);
    double sin_a = lm::length(lm::cross(pos_normalized, axis));

    double cos_a_plus_b = std::min(std::max(-1.0, cos_a * cos_b - sin_a * sin_b), 1.0);
    if (cos_a_plus_b <= 0.0)
    {
        // cos(a + b) is the dot product between `axis` and the horizon plane normal.
        // - When 0, the horizon plane is tangent to `axis`.
        // - When negative, the horizon plane intersects `axis` in its opposite direction.
        // In both cases, there is no valid occlusion point.
        return std::nullopt;
    }

    return hrz::EARTH_RADIUS / cos_a_plus_b;
}
} // namespace

namespace horizon_culling
{
std::optional<lm::dvec3> compute_occlusion_point(const hrz::GeoVolumeBounds& bounds)
{
    lm::dvec3 bounds_center = hrz::geo_to_ecef(bounds.center());

    if (bounds.is_empty())
    {
        return bounds_center;
    }

    lm::dvec3 occlusion_point_axis = bounds_center;
    if (lm::length2(occlusion_point_axis) < 1.0)
    {
        return std::nullopt;
    }

    occlusion_point_axis.z /= hrz::WGS84_AXES_LENGTH_RATIO;
    occlusion_point_axis = lm::normalize(occlusion_point_axis);

    double occlusion_point_distance = 0.0;

    // Lower corners of the bounds are ignored, because they give occlusion point
    // distances that are shorter than upper corners.
    // All upper corners would return the same value if there was no flattening
    // to the ellipsoid, but because of it two corners at different latitudes
    // must be tested.

    for (uint32_t i = 0; i < 2; ++i)
    {
        auto summit_occlusion_point_distance = compute_occlusion_point_distance(
            hrz::geo_to_ecef(bounds.corner(4 + i)), occlusion_point_axis);
        if (!summit_occlusion_point_distance.has_value())
        {
            return std::nullopt;
        }

        occlusion_point_distance =
            std::max(occlusion_point_distance, summit_occlusion_point_distance.value());
    }

    if (occlusion_point_distance < hrz::EARTH_RADIUS)
    {
        assert(false && "Invalid occlusion point");
        return std::nullopt;
    }

    lm::dvec3 horizon_occlusion_point = occlusion_point_axis * occlusion_point_distance;
    horizon_occlusion_point.z *= hrz::WGS84_AXES_LENGTH_RATIO;

    return horizon_occlusion_point;
}

std::optional<lm::dvec3> compute_occlusion_point(const hrz::OrientedBBox3<double>& box)
{
    if (lm::length2(box.center) < 1.0)
    {
        return std::nullopt;
    }

    if (box.u_half_length < 0.0 && box.v_half_length < 0.0 && box.w_half_length < 0.0)
    {
        return {{0, 0, 0}};
    }

    lm::dvec3 occlusion_point_axis = box.center;
    occlusion_point_axis.z /= hrz::WGS84_AXES_LENGTH_RATIO;
    occlusion_point_axis = lm::normalize(occlusion_point_axis);

    double occlusion_point_distance = 0.0;

    for (int u = -1; u <= 1; u += 2)
    {
        for (int v = -1; v <= 1; v += 2)
        {
            for (int w = -1; w <= 1; w += 2)
            {
                auto summit_occlusion_point_distance = compute_occlusion_point_distance(
                    box.center + u * box.u_axis * box.u_half_length
                        + v * box.v_axis * box.v_half_length + w * box.w_axis * box.w_half_length,
                    occlusion_point_axis);
                if (!summit_occlusion_point_distance.has_value())
                {
                    return std::nullopt;
                }

                occlusion_point_distance =
                    std::max(occlusion_point_distance, summit_occlusion_point_distance.value());
            }
        }
    }

    if (occlusion_point_distance < hrz::EARTH_RADIUS)
    {
        assert(false && "Invalid occlusion point");
        return std::nullopt;
    }

    lm::dvec3 horizon_occlusion_point = occlusion_point_axis * occlusion_point_distance;
    horizon_occlusion_point.z *= hrz::WGS84_AXES_LENGTH_RATIO;

    return horizon_occlusion_point;
}

std::optional<lm::dvec3> compute_occlusion_point(const hrz::BSphere<double>& sphere)
{
    if (lm::length2(sphere.center) < 1.0)
    {
        return std::nullopt;
    }

    if (sphere.radius < 0.0)
    {
        return {{0, 0, 0}};
    }

    // Convert sphere to box for computing horizon occlusion point
    // @Todo Compute a better horizon occlusion point for spheres
    hrz::OrientedBBox3<double> box;
    box.center = sphere.center;
    box.u_half_length = sphere.radius;
    box.v_half_length = sphere.radius;
    box.w_half_length = sphere.radius;
    box.u_axis = lm::normalize(box.center);
    box.v_axis = lm::cross(box.u_axis, lm::dvec3(1, 0, 0));
    if (lm::length2(box.v_axis) < 0.001)
    {
        box.v_axis = lm::cross(box.u_axis, lm::dvec3(0, 1, 0));
    }
    box.v_axis = lm::normalize(box.v_axis);
    box.w_axis = lm::cross(box.u_axis, box.v_axis);

    return compute_occlusion_point(box);
}
} // namespace horizon_culling
} // namespace hrz
