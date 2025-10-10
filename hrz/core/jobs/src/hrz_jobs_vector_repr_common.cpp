#include "hrz_jobs_vector_repr_common.h"

#include <hrz_common_proj.h>

namespace hrz::vector_repr
{
void compute_rel_coords(
    hrz::ArrayView<const lm::dvec3> abs_coords,
    const lm::dvec3& center,
    hrz::ArrayView<lm::vec3> rel_coords)
{
    assert(abs_coords.size() == rel_coords.size());

    for (size_t i = 0; i < abs_coords.size(); ++i)
    {
        rel_coords[i] = lm::vec3(abs_coords[i] - center);
    }
}

hrz::BSphere<double> compute_tile_bounding_sphere(const TileCoords& tile_coords)
{
    double radius{};
    lm::dvec3 center;
    compute_tile_radius_center(tile_coords, &radius, &center);
    return hrz::BSphere<double>{center, radius};
}

hrz::BSphere<double> compute_tile_bounding_sphere(const lm::dbbox2& bbox)
{
    double radius{};
    lm::dvec3 center;
    compute_tile_radius_center(bbox, &radius, &center);
    return hrz::BSphere<double>{center, radius};
}

void compute_tile_radius_center(const TileCoords& tile_coords, double* radius, lm::dvec3* center)
{
    const lm::dbbox2 bbox = hrz::mercator_tile_bbox_meters(tile_coords);
    const lm::dvec2 tile_center(lm::center(bbox));

    double tile_points[] = {bbox.min.x,    bbox.min.y,    0, bbox.max.x, bbox.max.y, 0,
                            tile_center.x, tile_center.y, 0};
    pl_transform_in_place_canonical(&hrz_proj::wmerc_to_ecef, 3, tile_points);

    *radius = lm::radius(lm::dbbox3(
        {{tile_points[0], tile_points[1], tile_points[2]},
         {tile_points[3], tile_points[4], tile_points[5]}}));

    *center = lm::dvec3(tile_points[6], tile_points[7], tile_points[8]);
}

void compute_tile_radius_center(const lm::dbbox2& bbox, double* radius, lm::dvec3* center)
{
    const lm::dvec2 bbox_center(lm::center(bbox));

    double tile_points[] = {bbox.min.x,    bbox.min.y,    0, bbox.max.x, bbox.max.y, 0,
                            bbox_center.x, bbox_center.y, 0};
    pl_transform_in_place_canonical(&hrz_proj::wmerc_to_ecef, 3, tile_points);

    *radius = lm::radius(lm::dbbox3(
        {{tile_points[0], tile_points[1], tile_points[2]},
         {tile_points[3], tile_points[4], tile_points[5]}}));

    *center = lm::dvec3(tile_points[6], tile_points[7], tile_points[8]);
}

//                n \       /
//                   \     / d
//                    \   /
//  A                  \ /
//   -------------------- B
//                     /|
//                    / |
//                   /  |
//                  /   |
//                 /    |
//                /     |
//               -      | C
//
// This computes the normal (n) to the bisecting plane (d) of the two segments AB and BC.
// This is used to make the joints of the cylinders.
// When the angle is too small between AB and BC, the normal is just
// the direction of the main segment. The main segment is always AB, and C is the
// second point of the segment after AB, used to correctly orient the joint.
std::pair<lm::vec3, bool> compute_joint_normal(
    const lm::dvec3& a,
    const lm::dvec3& b,
    const lm::dvec3& c)
{
    lm::dvec3 d0 = lm::normalize(a - b);
    lm::dvec3 d1 = lm::normalize(c - b);

    if (std::isnan(d0.x))
    {
        return std::make_pair(lm::vec3(d1), true);
    }
    else if (std::isnan(d1.x))
    {
        return std::make_pair(lm::vec3(-d0), true);
    }

    double d = lm::dot(d0, d1);

    // Angle too small
    // 0.86 is ~cos(30°)
    if (d > 0.86)
    {
        return std::make_pair(lm::vec3(-d0), false);
    }

    // This is a vector coplanar to ABC and "inside" the angle.
    lm::dvec3 direction_coplanar_inside = d0 + d1;

    // This is a vector normal to ABC
    lm::dvec3 direction_normal = lm::cross(d0, d1);

    // The two previous vectors form the bisecting plane at AB and BC.
    // Now we just need the normal of that plane...
    return std::make_pair(
        lm::vec3(lm::normalize(lm::cross(direction_normal, direction_coplanar_inside))), true);
}

} // namespace hrz::vector_repr
