#pragma once

#include "hrz/common/geo.h"
#include "hrz/common/maths.h"
#include "hrz/common/tile_coords.h"
#include "hrz/fnd/array_view.h"

#include <lin_maths.h>

namespace hrz::vector_repr
{

void compute_rel_coords(
    hrz::ArrayView<const lm::dvec3> abs_coords,
    const lm::dvec3& center,
    hrz::ArrayView<lm::vec3> rel_coords);

hrz::BSphere<double> compute_tile_bounding_sphere(const TileCoords& tile_coords);
hrz::BSphere<double> compute_tile_bounding_sphere(const lm::dbbox2& bbox);

void compute_tile_radius_center(const TileCoords& tile_coords, double* radius, lm::dvec3* center);
void compute_tile_radius_center(const lm::dbbox2& bbox, double* radius, lm::dvec3* center);

struct Slice
{
    uint32_t first = 0;
    uint32_t count = 0;
};

// Compute the normal of the bisecting plane between two polylines. Used to
// merge cylinders or polylines together so that there is not gap between them.
// Returns the normal and whether or not it's a normal that can be used to make a nice joint.
// The joint is not "nice" if the angle is too acute.
std::pair<lm::vec3, bool> compute_joint_normal(
    const lm::dvec3& a,
    const lm::dvec3& b,
    const lm::dvec3& c);

static constexpr uint8_t MAX_LOD_FOR_SUBDIVISION = 7;

// Computes the maximum angular length of a segment for a given LOD above
// which it is a good idea to subdivide it for better appearance.
double max_segment_angular_length_for_lod(uint8_t lod);

} // namespace hrz::vector_repr
