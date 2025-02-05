#pragma once

#include <hrz_common_color.h>
#include <hrz_common_geo.h>
#include <hrz_common_maths.h>
#include <hrz_common_vector_data.h>
#include <hrz_fnd_array_view.h>
#include <hrz_fnd_flat_hash_map.h>
#include <hrz_jobs_protocol.h>
#include <hrz_protocol_all.h>

#include <gsl/gsl-lite.hpp>
#include <lin_maths.h>

namespace hrz::vector_repr
{
void compute_rel_coords(
    hrz::ArrayView<const lm::dvec3> abs_coords,
    const lm::dvec3& center,
    hrz::ArrayView<lm::vec3> rel_coords);

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

} // namespace hrz::vector_repr
