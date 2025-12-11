#include "hrz/common/three_d_tiles.h"

#include "hrz/common/geo.h"
#include "hrz/common/horizon_culling.h"
#include "hrz/common/profiling.h"
#include "hrz/fnd/hash.h"
#include "hrz/fnd/log.h"

#include <limits>

namespace
{
using namespace hrz::three_d_tiles;

void normalize_box(BoundingVolume::Box& box)
{
    bool u_is_zero = box.u_half_length < std::numeric_limits<double>::epsilon();
    bool v_is_zero = box.v_half_length < std::numeric_limits<double>::epsilon();
    bool w_is_zero = box.w_half_length < std::numeric_limits<double>::epsilon();

    if (!u_is_zero && !v_is_zero && !w_is_zero) return;

    auto perpendicular_to_one = [](const lm::dvec3& v)
    {
        // From https://math.stackexchange.com/a/4112622
        return lm::normalize(lm::dvec3{
            std::copysign(v.z, v.x), std::copysign(v.z, v.y),
            -std::copysign(std::abs(v.x) + std::abs(v.y), v.z)});
    };

    auto perpendicular_to_two = [](const lm::dvec3& v1, lm::dvec3& v2)
    { return lm::cross(v1, v2); };

    if (w_is_zero)
    {
        if (v_is_zero)
        {
            if (u_is_zero)
            {
                box.u_axis = {1.0, 0.0, 0.0};
                box.v_axis = {0.0, 1.0, 0.0};
                box.w_axis = {0.0, 0.0, 1.0};
                return;
            }

            box.v_axis = perpendicular_to_one(box.u_axis);
        }
        else if (u_is_zero)
        {
            box.u_axis = perpendicular_to_one(box.v_axis);
        }

        box.w_axis = perpendicular_to_two(box.u_axis, box.v_axis);
    }
    else if (v_is_zero)
    {
        if (u_is_zero)
        {
            box.u_axis = perpendicular_to_one(box.w_axis);
        }

        box.v_axis = perpendicular_to_two(box.u_axis, box.w_axis);
    }
    else if (u_is_zero)
    {
        box.u_axis = perpendicular_to_two(box.v_axis, box.w_axis);
    }
}

BoundingVolume::Box make_box_from_region(const hrz::GeoVolumeBounds& wgs84_bbox)
{
    lm::dvec3 ecef_corners[] = {
        hrz::geo_to_ecef(wgs84_bbox.south, wgs84_bbox.west, wgs84_bbox.min_height),
        hrz::geo_to_ecef(wgs84_bbox.south, wgs84_bbox.east, wgs84_bbox.min_height),
        hrz::geo_to_ecef(wgs84_bbox.north, wgs84_bbox.west, wgs84_bbox.min_height),
        hrz::geo_to_ecef(wgs84_bbox.north, wgs84_bbox.east, wgs84_bbox.min_height),
        hrz::geo_to_ecef(wgs84_bbox.south, wgs84_bbox.west, wgs84_bbox.max_height),
        hrz::geo_to_ecef(wgs84_bbox.south, wgs84_bbox.east, wgs84_bbox.max_height),
        hrz::geo_to_ecef(wgs84_bbox.north, wgs84_bbox.west, wgs84_bbox.max_height),
        hrz::geo_to_ecef(wgs84_bbox.north, wgs84_bbox.east, wgs84_bbox.max_height),
    };

    lm::dvec3 ecef_center = {0, 0, 0};
    for (const auto& corner : ecef_corners)
    {
        ecef_center += corner / 8.0f;
    }

    lm::dvec3 u_axis = ecef_corners[5] - ecef_corners[4];
    lm::dvec3 v_axis = ecef_corners[6] - ecef_corners[4];

    lm::dvec3 ecef_centers[] = {
        hrz::geo_to_ecef(
            (wgs84_bbox.south + wgs84_bbox.north) * 0.5, (wgs84_bbox.west + wgs84_bbox.east) * 0.5,
            wgs84_bbox.min_height),
        hrz::geo_to_ecef(
            (wgs84_bbox.south + wgs84_bbox.north) * 0.5, (wgs84_bbox.west + wgs84_bbox.east) * 0.5,
            wgs84_bbox.max_height),
    };

    lm::dvec3 w_axis = ecef_centers[1] - ecef_centers[0];

    // Expand the box by the curvature of the Earth.
    // See https://earthcurvature.com/
    double u_ratio = (std::abs(u_axis.x) * 0.5) / hrz::EARTH_RADIUS;
    double v_ratio = (std::abs(v_axis.y) * 0.5) / hrz::EARTH_RADIUS;
    double u_drop = hrz::EARTH_RADIUS * (1.0 - std::cos(u_ratio));
    double v_drop = hrz::EARTH_RADIUS * (1.0 - std::cos(v_ratio));
    double drop = std::max(u_drop, v_drop);

    if (!std::isfinite(drop)) drop = 0.0;

#define NORMALIZE_BOX_AXIS(x)                                \
    do                                                       \
    {                                                        \
        double length = lm::length(x##_axis);                \
        if (length > std::numeric_limits<double>::epsilon()) \
        {                                                    \
            box.x##_axis = x##_axis / length;                \
            box.x##_half_length = length * 0.5;              \
        }                                                    \
        else                                                 \
        {                                                    \
            box.x##_axis = {1.0, 0.0, 0.0};                  \
            box.x##_half_length = 0;                         \
        }                                                    \
    } while (0)

    BoundingVolume::Box box;
    NORMALIZE_BOX_AXIS(u);
    NORMALIZE_BOX_AXIS(v);
    NORMALIZE_BOX_AXIS(w);
    normalize_box(box);
    box.w_half_length += drop;
    box.center = ecef_center + box.w_axis * drop * 0.5; // Raise the centre by half the drop.

#undef NORMALIZE_BOX_AXIS

    return box;
}

BoundingVolume::Sphere make_sphere_from_region(const hrz::GeoVolumeBounds& wgs84_bbox)
{
    // Note that there exists a function to compute a bounding sphere at
    // `hrz::compute_bounding_sphere()`. However it takes about 8 to 10
    // times as much time, for the same results, compared to this rather
    // naive version. This is mainly because here we only have to deal
    // with a simple case.

    double min_lon = wgs84_bbox.west;
    double min_lat = wgs84_bbox.south;
    double max_lon = wgs84_bbox.east;
    double max_lat = wgs84_bbox.north;
    double min_ele = wgs84_bbox.min_height;
    double max_ele = wgs84_bbox.max_height;
    double mid_lon = (min_lon + max_lon) / 2.0;
    double mid_lat = (min_lat + max_lat) / 2.0;
    lm::dvec3 points[18] = {
        hrz::geo_to_ecef(min_lat, min_lon, min_ele), hrz::geo_to_ecef(min_lat, min_lon, max_ele),
        hrz::geo_to_ecef(min_lat, max_lon, min_ele), hrz::geo_to_ecef(min_lat, max_lon, max_ele),
        hrz::geo_to_ecef(max_lat, max_lon, min_ele), hrz::geo_to_ecef(max_lat, max_lon, max_ele),
        hrz::geo_to_ecef(max_lat, min_lon, min_ele), hrz::geo_to_ecef(max_lat, min_lon, max_ele),
        hrz::geo_to_ecef(min_lat, mid_lon, min_ele), hrz::geo_to_ecef(min_lat, mid_lon, max_ele),
        hrz::geo_to_ecef(max_lat, mid_lon, min_ele), hrz::geo_to_ecef(max_lat, mid_lon, max_ele),
        hrz::geo_to_ecef(mid_lat, min_lon, min_ele), hrz::geo_to_ecef(mid_lat, min_lon, max_ele),
        hrz::geo_to_ecef(mid_lat, max_lon, min_ele), hrz::geo_to_ecef(mid_lat, max_lon, max_ele),
        hrz::geo_to_ecef(mid_lat, mid_lon, min_ele), hrz::geo_to_ecef(mid_lat, mid_lon, max_ele)};

    lm::dvec3 origin = points[0];
    for (unsigned int i = 0; i < 18; ++i)
    {
        points[i] -= origin;
    }

    BoundingVolume::Sphere sphere;

    sphere.center = {0, 0, 0};
    for (auto& point : points)
    {
        sphere.center += point;
    }
    sphere.center.x = sphere.center.x / 18;
    sphere.center.y = sphere.center.y / 18;
    sphere.center.z = sphere.center.z / 18;

    sphere.radius = -1;
    for (auto& point : points)
    {
        sphere.radius = std::max(sphere.radius, lm::length(point - sphere.center));
    }

    sphere.center += origin;

    return sphere;
}

double get_box_radius(const BoundingVolume::Box& box)
{
    return std::sqrt(
        box.u_half_length * box.u_half_length + box.v_half_length * box.v_half_length
        + box.w_half_length * box.w_half_length);
}
} // namespace

namespace hrz::three_d_tiles
{
void optimize(BoundingVolume& volume)
{
    if (std::holds_alternative<BoundingVolume::Region>(volume.volume))
    {
        auto& region = std::get<BoundingVolume::Region>(volume.volume);
        const auto& wgs84_bbox = region.bounds;

        // Regions have shapes that do not lean themselves to easy and
        // fast computations, so we convert them to boxes or spheres.
        // The conversion to boxes introduces errors that can become
        // too large if the region is too curvy, so it is only used
        // for small regions that are not too close to the poles.

        if (std::max(std::abs(wgs84_bbox.south), std::abs(wgs84_bbox.north)) < lm::radians(75.0)
            && wgs84_bbox.east - wgs84_bbox.west < lm::radians(1.0)
            && wgs84_bbox.north - wgs84_bbox.south < lm::radians(1.0))
        {
            // Small region, convert to box.

            auto box = make_box_from_region(wgs84_bbox);

            region.box = {box};
            region.sphere = std::nullopt;

            volume.center = box.center;
            volume.radius = get_box_radius(box);
        }
        else
        {
            // Big region, convert to sphere.

            auto sphere = make_sphere_from_region(wgs84_bbox);

            region.sphere = {sphere};
            region.box = std::nullopt;

            volume.center = sphere.center;
            volume.radius = sphere.radius;
        }
    }
    else if (std::holds_alternative<BoundingVolume::Box>(volume.volume))
    {
        auto& box = std::get<BoundingVolume::Box>(volume.volume);
        normalize_box(box);
        volume.center = box.center;
        volume.radius = get_box_radius(box);
    }
    else if (std::holds_alternative<BoundingVolume::Sphere>(volume.volume))
    {
        const auto& sphere = std::get<BoundingVolume::Sphere>(volume.volume);
        volume.center = sphere.center;
        volume.radius = sphere.radius;
    }
    else
    {
        assert(false && "Unhandled case");
    }
}

double distance(const BoundingVolume& volume, const lm::dvec3& ecef_pos)
{
    HRZ_SCOPED_SAMPLE_A("3D tiles bounding volume distance");

    auto distance_to_box = [](const BoundingVolume::Box& box, const lm::dvec3& ecef_pos)
    {
        // From https://www.sciencedirect.com/topics/computer-science/oriented-bounding-box

        // Transform `ecef_pos` into `box`s coordinate frame.
        lm::dvec3 offset = ecef_pos - box.center;
        lm::dvec3 p_prime = {
            lm::dot(offset, box.u_axis), lm::dot(offset, box.v_axis), lm::dot(offset, box.w_axis)};

        // Project `p_prime` onto box.
        double distance_squared = 0;

        if (p_prime.x < -box.u_half_length)
        {
            double d = p_prime.x + box.u_half_length;
            distance_squared += d * d;
        }
        else if (p_prime.x > box.u_half_length)
        {
            double d = p_prime.x - box.u_half_length;
            distance_squared += d * d;
        }

        if (p_prime.y < -box.v_half_length)
        {
            double d = p_prime.y + box.v_half_length;
            distance_squared += d * d;
        }
        else if (p_prime.y > box.v_half_length)
        {
            double d = p_prime.y - box.v_half_length;
            distance_squared += d * d;
        }

        if (p_prime.z < -box.w_half_length)
        {
            double d = p_prime.z + box.w_half_length;
            distance_squared += d * d;
        }
        else if (p_prime.z > box.w_half_length)
        {
            double d = p_prime.z - box.w_half_length;
            distance_squared += d * d;
        }

        return std::sqrt(distance_squared);
    };

    auto distance_to_sphere = [](const BoundingVolume::Sphere& sphere, const lm::dvec3& ecef_pos)
    {
        if (sphere.radius < 0.0) return std::numeric_limits<double>::max();
        return lm::length(sphere.center - ecef_pos) - sphere.radius;
    };

    if (std::holds_alternative<BoundingVolume::Region>(volume.volume))
    {
        const auto& region = std::get<BoundingVolume::Region>(volume.volume);
        if (region.box.has_value())
        {
            return distance_to_box(region.box.value(), ecef_pos);
        }
        else
        {
            return hrz::distance_to_wgs84_bbox(ecef_pos, region.bounds);
        }
    }
    else if (std::holds_alternative<BoundingVolume::Box>(volume.volume))
    {
        const auto& box = std::get<BoundingVolume::Box>(volume.volume);
        return distance_to_box(box, ecef_pos);
    }
    else if (std::holds_alternative<BoundingVolume::Sphere>(volume.volume))
    {
        const auto& sphere = std::get<BoundingVolume::Sphere>(volume.volume);
        return distance_to_sphere(sphere, ecef_pos);
    }
    else
    {
        assert(false && "Unhandled case");
        return std::numeric_limits<double>::max();
    }
}

bool intersects_space_subset(
    const BoundingVolume& volume,
    std::span<const lm::dvec4> planes,
    std::span<const lm::dvec3> vertices)
{
    HRZ_SCOPED_SAMPLE_A("3D tiles bounding volume intersects space subset");

    auto box_check = [&](const BoundingVolume::Box& box)
    {
        // See https://gamedev.stackexchange.com/a/44501
        //     https://iquilezles.org/articles/frustumcorrect/

        lm::dmat3 orientation{
            {box.u_axis.x, box.v_axis.x, box.w_axis.x},
            {box.u_axis.y, box.v_axis.y, box.w_axis.y},
            {box.u_axis.z, box.v_axis.z, box.w_axis.z},
        };

        auto classify = [&](const lm::dvec4& plane)
        {
            lm::dvec3 normal = orientation * plane.xyz;

            // Maximum extent in direction of plane normal
            double r = std::abs(box.u_half_length * normal.x)
                + std::abs(box.v_half_length * normal.y) + std::abs(box.w_half_length * normal.z);

            // Signed distance between box center and plane
            double d = lm::dot(plane.xyz, box.center) + plane.w;

            // Return signed distance
            if (std::abs(d) < r)
            {
                return 0.0;
            }
            else if (d < 0.0)
            {
                return d + r;
            }
            return d - r;
        };

        for (const auto& plane : planes)
        {
            double side = classify(plane);
            if (side > 0)
            {
                return false;
            }
        }

        if (!vertices.empty())
        {
            // Try each face plane of the box as a separating plane.

            auto are_all_vertices_on_same_side = [&](const lm::dvec3& axis, double half_length)
            {
                lm::dvec3 plane_normal = axis;
                lm::dvec3 plane_center = box.center + axis * half_length;
                bool all_vertices_on_same_side = true;
                for (const auto& vertex : vertices)
                {
                    if (lm::dot(plane_normal, vertex - plane_center) < 0.0)
                    {
                        all_vertices_on_same_side = false;
                        break;
                    }
                }
                return all_vertices_on_same_side;
            };

            if (are_all_vertices_on_same_side(box.u_axis, box.u_half_length)) return false;
            if (are_all_vertices_on_same_side(-box.u_axis, box.u_half_length)) return false;
            if (are_all_vertices_on_same_side(box.v_axis, box.v_half_length)) return false;
            if (are_all_vertices_on_same_side(-box.v_axis, box.v_half_length)) return false;
            if (are_all_vertices_on_same_side(box.w_axis, box.w_half_length)) return false;
            if (are_all_vertices_on_same_side(-box.w_axis, box.w_half_length)) return false;

            return true;
        }
        else
        {
            return true;
        }
    };

    auto sphere_check = [&](const BoundingVolume::Sphere& sphere)
    {
        if (sphere.radius < 0.0) return false;

        for (const auto& plane : planes)
        {
            double side = lm::dot(sphere.center, plane.xyz) + plane.w;
            if (side > sphere.radius)
            {
                return false;
            }
        }

        if (!vertices.empty())
        {
            // Try the tangent plane passing through the point on the sphere
            // the closest to the first vertex as separating plane.
            lm::dvec3 plane_normal = lm::normalize(vertices.front() - sphere.center);
            lm::dvec3 plane_center = sphere.center + plane_normal * sphere.radius;

            for (size_t i = 1; i < vertices.size(); ++i)
            {
                if (lm::dot(plane_normal, vertices[i] - plane_center) < 0.0)
                {
                    return true;
                }
            }

            return false;
        }
        else
        {
            return true;
        }
    };

    if (std::holds_alternative<BoundingVolume::Region>(volume.volume))
    {
        const auto& region = std::get<BoundingVolume::Region>(volume.volume);
        if (region.box.has_value())
        {
            return box_check(region.box.value());
        }
        else if (region.sphere.has_value())
        {
            return sphere_check(region.sphere.value());
        }
        else
        {
            assert(false && "Call optimize() before this function");
            return false;
        }
    }
    else if (std::holds_alternative<BoundingVolume::Box>(volume.volume))
    {
        const auto& box = std::get<BoundingVolume::Box>(volume.volume);
        return box_check(box);
    }
    else if (std::holds_alternative<BoundingVolume::Sphere>(volume.volume))
    {
        const auto& sphere = std::get<BoundingVolume::Sphere>(volume.volume);
        return sphere_check(sphere);
    }
    else
    {
        assert(false && "Unhandled case");
        return false;
    }
}

BoundingVolume transform_bounding_volume(
    const BoundingVolume& volume,
    const lm::dmat4& transform,
    const lm::dmat4& root_transform)
{
    HRZ_SCOPED_SAMPLE_A("3D tiles bounding volume transform");

    auto transform_box = [](const BoundingVolume::Box& box, const lm::dmat4& transform)
    {
#define TRANSFORM_BOX_AXIS(x)                                                                 \
    do                                                                                        \
    {                                                                                         \
        bool has_zero_length = box.x##_half_length <= std::numeric_limits<double>::epsilon(); \
        lm::dvec4 half_##x##_axis = has_zero_length                                           \
            ? lm::dvec4(box.x##_axis)                                                         \
            : lm::dvec4(box.x##_axis * box.x##_half_length);                                  \
                                                                                              \
        half_##x##_axis = transform * half_##x##_axis;                                        \
        transformed_box.x##_half_length = lm::length(half_##x##_axis);                        \
                                                                                              \
        if (transformed_box.x##_half_length > std::numeric_limits<double>::epsilon())         \
        {                                                                                     \
            transformed_box.x##_axis = half_##x##_axis.xyz / transformed_box.x##_half_length; \
        }                                                                                     \
        else                                                                                  \
        {                                                                                     \
            transformed_box.x##_axis = half_##x##_axis.xyz;                                   \
        }                                                                                     \
    } while (0)
        BoundingVolume::Box transformed_box;
        transformed_box.center = (transform * lm::dvec4(box.center, 1)).xyz;
        TRANSFORM_BOX_AXIS(u);
        TRANSFORM_BOX_AXIS(v);
        TRANSFORM_BOX_AXIS(w);
        return transformed_box;

#undef TRANSFORM_BOX_AXIS
    };

    auto transform_sphere = [](const BoundingVolume::Sphere& sphere, const lm::dmat4& transform)
    {
        // Under a linear transformation, a sphere becomes an ellipsoid.
        // Its centre is the transformation of the sphere's centre.
        // For the sake of simplicity, we go from a sphere to (ideally) the bounding
        // sphere of the ellipsoid.
        BoundingVolume::Sphere transformed_sphere;

        transformed_sphere.center = (transform * lm::dvec4(sphere.center, 1)).xyz;

        if (sphere.radius >= 0.0)
        {
            // Note that this calculation is wrong. Obtaining the radius of the bounding
            // sphere of the ellipse is much more complex. However this is the calculation
            // that Cesium makes, so it should suffice for now.
            // See
            // https://github.com/CesiumGS/cesium/blob/2fd0e8f7e4212bd1e7084299187f70597a6bbfd8/Source/Scene/Cesium3DTile.js#L1355
            // @Todo Compute the actual radius of the bounding sphere.
            transformed_sphere.radius = sphere.radius
                * std::max(std::max(
                               lm::length(transform.col[0].xyz), lm::length(transform.col[1].xyz)),
                           lm::length(transform.col[2].xyz));
        }
        else
        {
            transformed_sphere.radius = sphere.radius;
        }

        return transformed_sphere;
    };

    BoundingVolume transformed_volume;

    if (std::holds_alternative<BoundingVolume::Region>(volume.volume))
    {
        transformed_volume = volume;

        // Do not apply the tile's transform, per the spec,
        // but apply the root transform of the tileset, per
        // https://github.com/CesiumGS/cesium/pull/6755
        if (root_transform == lm::dmat4::identity())
        {
            transformed_volume.center = volume.center;
            transformed_volume.radius = volume.radius;
        }
        else
        {
            const auto& region = std::get<BoundingVolume::Region>(volume.volume);
            auto& transformed_region = std::get<BoundingVolume::Region>(transformed_volume.volume);
            if (region.box.has_value())
            {
                auto transformed_box = transform_box(region.box.value(), root_transform);
                transformed_region.box = {transformed_box};
                transformed_volume.center = transformed_box.center;
                transformed_volume.radius = get_box_radius(transformed_box);
            }
            else if (region.sphere.has_value())
            {
                auto transformed_sphere = transform_sphere(region.sphere.value(), root_transform);
                transformed_region.sphere = {transformed_sphere};
                transformed_volume.center = transformed_sphere.center;
                transformed_volume.radius = transformed_sphere.radius;
            }
            else
            {
                assert(false && "Call optimize() before this function");
            }
        }
    }
    else if (std::holds_alternative<BoundingVolume::Box>(volume.volume))
    {
        const auto& box = std::get<BoundingVolume::Box>(volume.volume);
        auto transformed_box = transform_box(box, transform);
        transformed_volume.volume = transformed_box;
        transformed_volume.center = transformed_box.center;
        transformed_volume.radius = get_box_radius(transformed_box);
    }
    else if (std::holds_alternative<BoundingVolume::Sphere>(volume.volume))
    {
        const auto& sphere = std::get<BoundingVolume::Sphere>(volume.volume);
        auto transformed_sphere = transform_sphere(sphere, transform);
        transformed_volume.volume = transformed_sphere;
        transformed_volume.center = transformed_sphere.center;
        transformed_volume.radius = transformed_sphere.radius;
    }
    else
    {
        assert(false && "Unhandled case");
    }

    return transformed_volume;
}

size_t compute_hash(const BoundingVolume& volume)
{
    if (std::holds_alternative<BoundingVolume::Region>(volume.volume))
    {
        const auto& region = std::get<BoundingVolume::Region>(volume.volume);
        return hrz::hash_values(
            region.bounds.west, region.bounds.east, region.bounds.south, region.bounds.north,
            region.bounds.min_height, region.bounds.max_height);
    }
    else if (std::holds_alternative<BoundingVolume::Box>(volume.volume))
    {
        const auto& box = std::get<BoundingVolume::Box>(volume.volume);
        return hrz::hash_values(
            box.center.x, box.center.y, box.center.z, box.u_axis, box.u_half_length, box.v_axis,
            box.v_half_length, box.w_axis, box.w_half_length);
    }
    else if (std::holds_alternative<BoundingVolume::Sphere>(volume.volume))
    {
        const auto& sphere = std::get<BoundingVolume::Sphere>(volume.volume);
        return hrz::hash_values(sphere.center.x, sphere.center.y, sphere.center.z, sphere.radius);
    }
    else
    {
        assert(false && "Unhandled case");
        return 0;
    }
}

std::optional<lm::dvec3> compute_horizon_occlusion_point(const BoundingVolume& volume)
{
    if (std::holds_alternative<BoundingVolume::Region>(volume.volume))
    {
        return hrz::horizon_culling::compute_occlusion_point(
            std::get<BoundingVolume::Region>(volume.volume).bounds);
    }
    else if (std::holds_alternative<BoundingVolume::Box>(volume.volume))
    {
        return hrz::horizon_culling::compute_occlusion_point(
            std::get<BoundingVolume::Box>(volume.volume));
    }
    else if (std::holds_alternative<BoundingVolume::Sphere>(volume.volume))
    {
        return hrz::horizon_culling::compute_occlusion_point(
            std::get<BoundingVolume::Sphere>(volume.volume));
    }
    else
    {
        assert(false && "Unhandled case");
        return volume.center;
    }
}

std::optional<AttributeComponentType> component_type_from_string(std::string_view str)
{
    if (str == "BYTE")
    {
        return AttributeComponentType::BYTE;
    }
    else if (str == "UNSIGNED_BYTE")
    {
        return AttributeComponentType::UNSIGNED_BYTE;
    }
    else if (str == "SHORT")
    {
        return AttributeComponentType::SHORT;
    }
    else if (str == "UNSIGNED_SHORT")
    {
        return AttributeComponentType::UNSIGNED_SHORT;
    }
    else if (str == "INT")
    {
        return AttributeComponentType::INT;
    }
    else if (str == "UNSIGNED_INT")
    {
        return AttributeComponentType::UNSIGNED_INT;
    }
    else if (str == "FLOAT")
    {
        return AttributeComponentType::FLOAT;
    }
    else if (str == "DOUBLE")
    {
        return AttributeComponentType::DOUBLE;
    }

    HRZ_LOG_ERROR("Invalid component type: {}", str.data());
    return {};
}
} // namespace hrz::three_d_tiles
