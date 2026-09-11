// SPDX-FileCopyrightText: Copyright 2025 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/common/vector_data/geometry_utils.h"

#include "hrz/common/geo.h"

#include <cassert>
#include <limits>

namespace hrz::vector_data
{

lm::dvec3 compute_ring_average(std::span<const lm::dvec3> linestring)
{
    lm::dvec3 centroid(0.0);

    for (const auto& p : linestring)
    {
        centroid += p;
    }

    centroid /= (double)linestring.size();
    return centroid;
}

void compute_linestring_middle_and_angle(
    std::span<const lm::dvec3> points,
    std::span<const uint32_t> linestring_sizes,
    lm::dvec3* middle,
    float* angle)
{
    if (points.empty())
    {
        *middle = lm::dvec3(std::numeric_limits<double>::quiet_NaN());
        *angle = 0;
        return;
    }

    double total_length = 0;

    uint32_t first_linestring_point = 0;
    for (const uint32_t linestring_size : linestring_sizes)
    {
        for (uint32_t i = 1; i < linestring_size; ++i)
        {
            const lm::dvec3 a = points[first_linestring_point + i - 1];
            const lm::dvec3 b = points[first_linestring_point + i];
            total_length += lm::length(a.xy - b.xy);
        }
        first_linestring_point += linestring_size;
    }

    if (total_length < 0.01F)
    {
        *middle = points[0];
        *angle = 0;
        return;
    }

    double length_left_to_middle = total_length / 2;

    first_linestring_point = 0;
    for (const uint32_t linestring_size : linestring_sizes)
    {
        for (uint32_t i = 1; i < linestring_size; ++i)
        {
            const lm::dvec3 a = points[first_linestring_point + i - 1];
            const lm::dvec3 b = points[first_linestring_point + i];
            const double segment_length = lm::length(a.xy - b.xy);

            if (segment_length > length_left_to_middle)
            {
                const double t = length_left_to_middle / segment_length;
                *middle = lm::mix(a, b, t);

                const lm::dvec2 diff = b.xy - a.xy;
                *angle = std::atan2(diff.y, diff.x);
                return;
            }
            length_left_to_middle -= segment_length;
        }
        first_linestring_point += linestring_size;
    }

    // Why are we here?
    *middle = points[0];
    *angle = 0;
}

double compute_vector_tile_tolerance(uint32_t lod)
{
    const double max_lod_tile_size = hrz::MERCATOR_RANGE / (1 << lod);
    const double meters_per_pixel = max_lod_tile_size / hrz::MERCATOR_TILE_SIZE;

    return meters_per_pixel * meters_per_pixel;
}

} // namespace hrz::vector_data
