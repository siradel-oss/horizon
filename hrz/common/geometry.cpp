// SPDX-FileCopyrightText: Copyright 2021 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/common/geometry.h"

namespace hrz
{

bool ray_sphere_intersection(const Ray& ray, const lm::dvec3& center, double radius, lm::dvec3* hit)
{
    // 'o' denotes the ray origin.
    // 'c' denotes the sphere center.
    // 'p' denotes the first intersection point.

    lm::dvec3 oc = center - ray.o;
    double proj_oc = lm::dot(ray.dir, oc);

    if (proj_oc < 0) return false; // Sphere is behind the ray origin.

    double d_sq = lm::length2(oc) - proj_oc * proj_oc;
    double radius_sq = radius * radius;

    if (d_sq > radius_sq) return false; // Ray passes next to the sphere and doesn't intersect.

    double proj_cp = std::sqrt(radius_sq - d_sq);

    double t0 = proj_oc - proj_cp;
    double t1 = proj_oc + proj_cp;

    if (t0 > t1) std::swap(t0, t1);

    if (t0 < 0)
    {
        t0 = t1;                  // if t0 is negative, let's use t1 instead
        if (t0 < 0) return false; // both t0 and t1 are negative
    }

    double t = t0;

    *hit = ray.o + ray.dir * t;
    return true;
}

bool ray_sphere_intersection(const Ray& ray, const hrz::BSphere<double>& sphere, lm::dvec3* hit)
{
    return ray_sphere_intersection(ray, sphere.center, sphere.radius, hit);
}

bool ray_plane_intersection(const Ray& ray, const lm::dvec3& p, const lm::dvec3& n, lm::dvec3* hit)
{
    double denom = lm::dot(n, ray.dir);
    if (std::abs(denom) < 1e-12) return false;

    double t = lm::dot(n, p - ray.o) / denom;
    *hit = ray.o + t * ray.dir;
    return t >= 0;
}

bool ray_square_intersection(
    const Ray& ray,
    const lm::dvec3& corner,
    const lm::dvec3& axis_x,
    const lm::dvec3& axis_y,
    const lm::dvec3& n,
    double square_size,
    lm::dvec3* hit)
{
    if (!ray_plane_intersection(ray, corner, n, hit)) return false;

    // Check if it is within the square boundaries.
    lm::dvec3 v = *hit - corner;

    double x = lm::dot(v, axis_x);
    double y = lm::dot(v, axis_y);

    return x >= 0 && x <= square_size && y >= 0 && y <= square_size;
}

bool ray_ring_intersection(
    const Ray& ray,
    const lm::dvec3& center,
    const lm::dvec3& n,
    double inner_radius,
    double outer_radius,
    lm::dvec3* hit)
{
    if (!ray_plane_intersection(ray, center, n, hit)) return false;

    // Check if it intersects the 2D ring.
    double dist_sq = lm::length2(*hit - center);

    return dist_sq >= inner_radius * inner_radius && dist_sq <= outer_radius * outer_radius;
}

// clang-format off
// # Ray - Capped cylinder (arbitrarily oriented) intersection.
//
// ## Solving for an infinite cylinder
//
//              |               The cylinder is defined by a core axis va, a point B and a radius r.
//      +-------|-------+       Q is a point on the cylinder.
//      |       |       |
//      |       |       |       For any point Q, <BQ, va> gives the projection of Q onto the core
//      |       |       |       axis given ||va|| = 1.
//      |       |       |       We can construct a vector orthogonal to va with BQ - BM. Then we can
//      |      M|       |       find if Q is on the cylinder iff ||BQ - BM|| = r
//      |       +-------+ Q
//      |       ^      ^|       If we expand all the terms we have the equation for a cylinder oriented
//      |       |     / |       along an arbitrary axis:
//      |       |    /  |           ((Q - B) - <Q - B, va> * va)² = r²
//      |       |   /   |
//      |       |  /    |       We substitute Q by the ray equation P(t) = O + t*d and develop the polynomial.
//      |       ^ /     |       to obtain an equation of the form ax²+bx+c=0
//      |     va|/      |
//      +-------+-------+           (O - B + t*d - <va, O - B + t*d> * va)² - r² = 0
//             B|   r
//              |               After many mistakes and tears we are able to find values for a, b and c.
//              |               Then, we simply solve the equation like any other quadratic polynomial with:
//                                  a = (d - <d,va>*va)²
//                                  b = 2<d - <d, va>*va, bo - <bo,va>*va>
//                                  c = (bo - <bo,va>*va)² - r²
//                              where bo = O - B
//
// ## Solving for a capped cylinder
//
// 1. Perform intersection test with the infinite cylinder.
// 2. Keep candidates that are within the cylinder i.e. between the two delimiting planes.
// 3. Perform intersection test with the planes.
// 4. Keep candidates i.e. points on the plane within the cylinder radius.
// 5. Take the candidates closest to the ray origin.
//
// ## Refs:
//  - [1] https://math.stackexchange.com/questions/406446/cylinder-ray-intersections-equation
//
// clang-format on
bool ray_cylinder_intersection(
    const Ray& ray,
    const lm::dvec3& base,
    const lm::dvec3& axis,
    double radius,
    double height,
    lm::dvec3* hit)
{
    double radius_sq = radius * radius;

    lm::dvec3 pa = base;
    lm::dvec3 pb = base + height * axis;

    double candidates[4] = {
        std::numeric_limits<double>::max(), std::numeric_limits<double>::max(),
        std::numeric_limits<double>::max(), std::numeric_limits<double>::max()
    };

    // Check intersections with cylinder lateral surface.

    lm::dvec3 bo = ray.o - base;

    lm::dvec3 tmp_1 = ray.dir - lm::dot(ray.dir, axis) * axis;
    lm::dvec3 tmp_2 = bo - lm::dot(bo, axis) * axis;

    double a = lm::length2(tmp_1);
    double b = 2 * lm::dot(tmp_1, tmp_2);
    double c = lm::length2(tmp_2) - radius_sq;

    double delta = b * b - 4 * a * c;

    bool cylinder_intersect = false;
    if (delta >= 0)
    {
        double delta_sqrt = std::sqrt(delta);
        double inv_denom = 1 / (2 * a);
        double t0 = (-b - delta_sqrt) * inv_denom;
        double t1 = (-b + delta_sqrt) * inv_denom;

        double t0_valid = true;
        double t1_valid = true;

        lm::dvec3 q0 = ray.o + t0 * ray.dir;
        lm::dvec3 q1 = ray.o + t1 * ray.dir;

        // Keep solutions between the delimiting planes.

        if (t0 < 0 || lm::dot(q0 - pa, axis) < 0 || lm::dot(q0 - pb, axis) > 0)
        {
            t0 = std::numeric_limits<double>::max();
            t0_valid = false;
        }
        if (t1 < 0 || lm::dot(q1 - pa, axis) < 0 || lm::dot(q1 - pb, axis) > 0)
        {
            t1 = std::numeric_limits<double>::max();
            t1_valid = false;
        }

        candidates[0] = t0;
        candidates[1] = t1;

        cylinder_intersect = t0_valid || t1_valid;
    }

    // Check intersections with cylinder caps.

    bool cap_intersect = false;
    {
        double inv_denom = 1.0 / lm::dot(ray.dir, axis);
        double t0 = lm::dot(pa - ray.o, axis) * inv_denom;
        double t1 = lm::dot(pb - ray.o, axis) * inv_denom;

        bool t0_valid = true;
        bool t1_valid = true;

        lm::dvec3 q0 = ray.o + t0 * ray.dir;
        lm::dvec3 q1 = ray.o + t1 * ray.dir;

        // Keep solutions at a valid distance from the cylinder core axis.

        if (lm::length2(q0 - pa) > radius_sq)
        {
            t0 = std::numeric_limits<double>::max();
            t0_valid = false;
        }
        if (lm::length2(q1 - pa) > radius_sq)
        {
            t1 = std::numeric_limits<double>::max();
            t1_valid = false;
        }

        candidates[2] = t0;
        candidates[3] = t1;

        cap_intersect = t0_valid || t1_valid;
    }

    double t =
        std::min(std::min(candidates[0], candidates[1]), std::min(candidates[2], candidates[3]));
    *hit = ray.o + t * ray.dir;

    return cylinder_intersect || cap_intersect;
}

std::pair<double, double> find_lines_closest_points(
    const lm::dvec3& p1,
    const lm::dvec3& v1,
    const lm::dvec3& p2,
    const lm::dvec3& v2)
{
    lm::dvec3 n = lm::cross(v1, v2);

    lm::dvec3 n1 = lm::cross(v1, n);
    lm::dvec3 n2 = lm::cross(v2, n);

    double s = lm::dot(p2 - p1, n2) / lm::dot(v1, n2);
    double t = lm::dot(p1 - p2, n2) / lm::dot(v2, n1);

    return {s, t};
}

} // namespace hrz
