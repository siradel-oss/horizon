#pragma once

#include "hrz/common/maths.h"

#include <lin_maths.h>

namespace hrz
{

bool ray_sphere_intersection(
    const Ray& ray,
    const lm::dvec3& center,
    double radius,
    lm::dvec3* hit);

bool ray_sphere_intersection(const Ray& ray, const hrz::BSphere<double>& sphere, lm::dvec3* hit);

bool ray_plane_intersection(const Ray& ray, const lm::dvec3& p, const lm::dvec3& n, lm::dvec3* hit);

bool ray_square_intersection(
    const Ray& ray,
    const lm::dvec3& corner,
    const lm::dvec3& axis_x,
    const lm::dvec3& axis_y,
    const lm::dvec3& n,
    double square_size,
    lm::dvec3* hit);

bool ray_ring_intersection(
    const Ray& ray,
    const lm::dvec3& center,
    const lm::dvec3& n,
    double inner_radius,
    double outer_radius,
    lm::dvec3* hit);

bool ray_cylinder_intersection(
    const Ray& ray,
    const lm::dvec3& base,
    const lm::dvec3& axis,
    double radius,
    double height,
    lm::dvec3* hit);

// Find closest points of two lines and return for each line a value t along the ray defined by
// p+t*v. The returned pair first contains the value for the line p1+t*v1 then the result for the
// second line p2+s*v2.
std::pair<double, double> find_lines_closest_points(
    const lm::dvec3& p1,
    const lm::dvec3& v1,
    const lm::dvec3& p2,
    const lm::dvec3& v2);

} // namespace hrz
