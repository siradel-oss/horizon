// SPDX-FileCopyrightText: Copyright 2025 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include <lin_maths.h>

#include <span>

namespace hrz::vector_data
{

lm::dvec3 compute_ring_average(std::span<const lm::dvec3> linestring);

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
