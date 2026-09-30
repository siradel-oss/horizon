// SPDX-FileCopyrightText: Copyright 2026 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/common/three_d_tiles.h"

#include <gtest/gtest.h>
#include <lin_maths.h>

#include <algorithm>
#include <cmath>

namespace
{

using hrz::three_d_tiles::BoundingVolume;

// Returns the box computed for a region small enough to be converted to a box.
BoundingVolume::Box make_region_box(const hrz::GeoVolumeBounds& bounds)
{
    BoundingVolume volume;
    volume.volume = BoundingVolume::Region{bounds, std::nullopt, std::nullopt};
    hrz::three_d_tiles::optimize(volume);

    const auto& region = std::get<BoundingVolume::Region>(volume.volume);
    EXPECT_TRUE(region.box.has_value());
    return region.box.value_or(BoundingVolume::Box{});
}

// Returns the largest distance (in meters) by which the region, sampled on a
// fine grid, lies outside the box.
double max_distance_outside(const hrz::GeoVolumeBounds& bounds, const BoundingVolume::Box& box)
{
    // A region crossing the antimeridian has West > East.
    double lon_span = bounds.east - bounds.west;
    if (lon_span < 0.0) lon_span += 2.0 * lm::PI;

    const int n = 100;
    double result = 0.0;
    for (int i = 0; i <= n; ++i)
    {
        double lon = bounds.west + lon_span * i / n;
        for (int j = 0; j <= n; ++j)
        {
            double lat = bounds.south + (bounds.north - bounds.south) * j / n;
            for (double height : {bounds.min_height, bounds.max_height})
            {
                lm::dvec3 offset = hrz::geo_to_ecef(lat, lon, height) - box.center;
                result = std::max(
                    {result, std::abs(lm::dot(offset, box.u_axis)) - box.u_half_length,
                     std::abs(lm::dot(offset, box.v_axis)) - box.v_half_length,
                     std::abs(lm::dot(offset, box.w_axis)) - box.w_half_length});
            }
        }
    }
    return result;
}

hrz::GeoVolumeBounds make_bounds(
    double center_lat_deg,
    double center_lon_deg,
    double size_deg,
    double min_height,
    double max_height)
{
    double half_size = lm::radians(size_deg) * 0.5;
    double lat = lm::radians(center_lat_deg);
    double lon = lm::radians(center_lon_deg);
    return hrz::GeoVolumeBounds(
        hrz::normalize_longitude(lon - half_size), hrz::normalize_longitude(lon + half_size),
        lat - half_size, lat + half_size, min_height, max_height);
}

} // namespace

TEST(CommonThreeDTiles, region_box_contains_region)
{
    const hrz::GeoVolumeBounds regions[] = {
        make_bounds(48.1, -1.7, 9.99, 0.0, 50.0),   // Rennes, largest region turned into a box
        make_bounds(48.1, -1.7, 0.99, 0.0, 50.0),   // Rennes, medium region
        make_bounds(48.1, -1.7, 0.5, 0.0, 50.0),    // Rennes, smaller region
        make_bounds(48.1, -1.7, 0.02, 30.0, 45.0),  // Rennes, building-sized region
        make_bounds(48.1, 90.0, 0.99, 0.0, 50.0),   // same latitude, other longitude
        make_bounds(0.0, 10.0, 0.99, 0.0, 50.0),    // across the equator
        make_bounds(-33.9, 151.2, 0.99, 0.0, 50.0), // southern hemisphere
        make_bounds(0.0, 10.0, 9.99, 0.0, 50.0),    // large, across the equator
        make_bounds(-33.9, 151.2, 9.99, 0.0, 50.0), // large, southern hemisphere
        make_bounds(89.0, 20.0, 0.99, 0.0, 50.0),   // close to the North Pole
        make_bounds(84.0, 20.0, 9.99, 0.0, 50.0),   // large, close to the North Pole
        // Touching the North Pole
        hrz::GeoVolumeBounds(
            lm::radians(20.0), lm::radians(20.99), lm::radians(89.5), lm::radians(90.0), 0.0, 50.0),
        // Large, touching the North Pole
        hrz::GeoVolumeBounds(
            lm::radians(20.0), lm::radians(29.99), lm::radians(80.01), lm::radians(90.0), 0.0,
            50.0),
        // Touching the South Pole, across the antimeridian
        hrz::GeoVolumeBounds(
            lm::radians(179.5), lm::radians(-179.5), lm::radians(-90.0), lm::radians(-89.5), 0.0,
            50.0),
        make_bounds(10.0, 179.4, 0.99, -20.0, 0.0),  // close to the antimeridian, below sea level
        make_bounds(10.0, 180.0, 0.5, 0.0, 50.0),    // centred on the antimeridian
        make_bounds(-40.0, -179.8, 0.99, 0.0, 50.0), // across the antimeridian, off centre
        make_bounds(-40.0, 177.0, 9.99, 0.0, 50.0),  // large, across the antimeridian
    };

    for (const auto& region : regions)
    {
        auto box = make_region_box(region);
        EXPECT_LE(max_distance_outside(region, box), 1e-3)
            << "region: " << lm::degrees(region.west) << ", " << lm::degrees(region.south);
    }
}

TEST(CommonThreeDTiles, region_box_is_only_thickened_by_the_curvature)
{
    // 0.5 degree wide region, with a 50m height range: its corners are 33km
    // away from its centre, and the Earth drops by about 88m there.
    auto region = make_bounds(48.1, -1.7, 0.5, 0.0, 50.0);
    auto box = make_region_box(region);

    EXPECT_GE(box.w_half_length, (50.0 + 85.0) / 2);
    EXPECT_LE(box.w_half_length, (50.0 + 90.0) / 2);

    // The axes are the local east, north and up directions.
    EXPECT_NEAR(lm::dot(box.u_axis, box.v_axis), 0.0, 1e-12);
    EXPECT_NEAR(lm::dot(box.u_axis, box.w_axis), 0.0, 1e-12);
    EXPECT_NEAR(lm::dot(box.v_axis, box.w_axis), 0.0, 1e-12);
    EXPECT_GT(box.v_axis.z, 0.0);
}

TEST(CommonThreeDTiles, wide_region_across_the_antimeridian_is_turned_into_a_sphere)
{
    // 20 degrees wide, from 170 E to 170 W.
    auto region = make_bounds(0.0, 180.0, 20.0, 0.0, 50.0);

    BoundingVolume volume;
    volume.volume = BoundingVolume::Region{region, std::nullopt, std::nullopt};
    hrz::three_d_tiles::optimize(volume);

    const auto& optimized = std::get<BoundingVolume::Region>(volume.volume);
    EXPECT_FALSE(optimized.box.has_value());
    ASSERT_TRUE(optimized.sphere.has_value());

    // The sphere is centred on the antimeridian, not on the opposite side of
    // the globe.
    lm::dvec3 antimeridian_dir = lm::normalize(hrz::geo_to_ecef(0.0, lm::PI, 0.0));
    EXPECT_GT(lm::dot(lm::normalize(optimized.sphere->center), antimeridian_dir), 0.99);
}
