// SPDX-FileCopyrightText: Copyright 2025 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/fnd/inlined_vector.h"
#include "hrz/protocol/vector/geometry_type.pb.h"

#include <lin_maths.h>

#include <span>

namespace hrz::vector_data
{

using LinestringSizes = hrz::InlinedVector<uint32_t, 8>;

// Computes the linestring sizes to use for a geometry with the given number of points.
// * Points ignore linestring sizes: all points form a single "linestring".
// * Empty linestring sizes mean a single linestring made of all the points.
// * Empty linestrings are removed.
// * Points that aren't part of any linestring are to be ignored.
//
// Returns false if the linestring sizes require more points than available, in which case
// the geometry is invalid.
[[nodiscard]]
bool normalize_linestring_sizes(
    hrz_proto::VectorGeometryType type,
    uint32_t point_count,
    std::span<const uint32_t> linestring_sizes,
    LinestringSizes* out_linestring_sizes);

lm::dvec3 compute_ring_average(std::span<const lm::dvec3> linestring);

lm::dvec3 compute_points_center(std::span<const lm::dvec3> points);

void compute_linestring_middle_and_angle(
    std::span<const lm::dvec3> points,
    std::span<const uint32_t> linestring_sizes,
    lm::dvec3* middle,
    float* angle);

// The minimal *squared* distance required between a point and its projection on the "core line"
// of a polyline (which goes from first to last point) for the point to be considered not
// simplifiable at the given level of detail.
double compute_vector_tile_tolerance(uint32_t lod);

} // namespace hrz::vector_data
