// SPDX-FileCopyrightText: Copyright 2026 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/core/vector/in_memory_geometry.h"

#include "hrz/protocol/vector/geometry.pb.h"
#include "hrz/protocol/vector/geometry_type.pb.h"

#include <gtest/gtest.h>

#include <cmath>
#include <initializer_list>
#include <span>
#include <vector>

using namespace hrz::vector_data;
using namespace hrz::vector_data::in_memory;

namespace
{

using Feature = VectorTileGeometry::Feature;
using GeometryPart = VectorTileGeometry::GeometryPart;

void add_geometry(
    hrz_proto::VectorFeature* feature,
    hrz_proto::VectorGeometryType type,
    std::initializer_list<lm::dvec3> points,
    std::initializer_list<uint32_t> linestring_sizes = {})
{
    auto* geometry = feature->add_geometry();
    geometry->set_type(type);
    for (const auto& point : points)
    {
        geometry->add_coords(point.x);
        geometry->add_coords(point.y);
        geometry->add_coords(point.z);
    }
    for (const auto size : linestring_sizes)
    {
        geometry->add_linestring_sizes(size);
    }
}

FeatureGeometry decode(const hrz_proto::VectorFeature& feature)
{
    return decode_feature_geometry(feature, [](std::span<lm::dvec3>) {});
}

void expect_points_eq(std::span<const lm::dvec3> actual, std::initializer_list<lm::dvec3> expected)
{
    ASSERT_EQ(actual.size(), expected.size());
    size_t i = 0;
    for (const auto& expected_point : expected)
    {
        SCOPED_TRACE(testing::Message() << "point " << i);
        EXPECT_DOUBLE_EQ(actual[i].x, expected_point.x);
        EXPECT_DOUBLE_EQ(actual[i].y, expected_point.y);
        EXPECT_DOUBLE_EQ(actual[i].z, expected_point.z);
        ++i;
    }
}

void expect_simple_feature(
    const Feature& feature,
    hrz_proto::VectorGeometryType type,
    uint32_t first_point,
    uint32_t point_count)
{
    EXPECT_FALSE(feature.flags.is_collection);
    EXPECT_EQ(feature.flags.primitive_geometry_type(), type);
    EXPECT_EQ(feature.first_geometry_part, first_point);
    EXPECT_EQ(feature.geometry_part_count, point_count);
}

void expect_part_eq(const GeometryPart& actual, const GeometryPart& expected)
{
    EXPECT_EQ(actual.type, expected.type);
    EXPECT_EQ(actual.first_point, expected.first_point);
    EXPECT_EQ(actual.point_count, expected.point_count);
    EXPECT_EQ(actual.linestring_count, expected.linestring_count);
    if (expected.linestring_count > 1)
    {
        EXPECT_EQ(actual.first_linestring_size, expected.first_linestring_size);
    }
}

void expect_empty(const FeatureGeometry& geometry)
{
    EXPECT_TRUE(geometry.encoded.flags.is_collection);
    EXPECT_EQ(geometry.encoded.geometry_part_count, 0);
    EXPECT_FALSE(geometry.encoded.flags.has_geometry(hrz_proto::POINT_GEOMETRY));
    EXPECT_FALSE(geometry.encoded.flags.has_geometry(hrz_proto::POLYLINE_GEOMETRY));
    EXPECT_FALSE(geometry.encoded.flags.has_geometry(hrz_proto::POLYGON_GEOMETRY));
    EXPECT_TRUE(std::isnan(geometry.encoded.anchor.x));
    EXPECT_TRUE(geometry.arrays.points.empty());
    EXPECT_TRUE(geometry.arrays.parts.empty());
    EXPECT_TRUE(geometry.arrays.sizes.empty());
}

} // anonymous namespace

TEST(InMemoryGeometry, no_geometry)
{
    const hrz_proto::VectorFeature feature;
    expect_empty(decode(feature));
}

TEST(InMemoryGeometry, point)
{
    hrz_proto::VectorFeature feature;
    add_geometry(&feature, hrz_proto::POINT_GEOMETRY, {{1, 2, 3}});

    const auto geometry = decode(feature);
    expect_simple_feature(geometry.encoded, hrz_proto::POINT_GEOMETRY, 0, 1);
    expect_points_eq(geometry.arrays.points, {{1, 2, 3}});
    EXPECT_DOUBLE_EQ(geometry.encoded.anchor.x, 1);
    EXPECT_DOUBLE_EQ(geometry.encoded.anchor.y, 2);
}

TEST(InMemoryGeometry, multi_point_keeps_all_points)
{
    hrz_proto::VectorFeature feature;
    add_geometry(&feature, hrz_proto::POINT_GEOMETRY, {{0, 0, 0}, {2, 0, 0}, {4, 0, 0}});

    const auto geometry = decode(feature);
    expect_simple_feature(geometry.encoded, hrz_proto::POINT_GEOMETRY, 0, 3);
    expect_points_eq(geometry.arrays.points, {{0, 0, 0}, {2, 0, 0}, {4, 0, 0}});
}

TEST(InMemoryGeometry, polyline_with_empty_linestring_sizes)
{
    hrz_proto::VectorFeature feature;
    add_geometry(&feature, hrz_proto::POLYLINE_GEOMETRY, {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}});

    const auto geometry = decode(feature);
    expect_simple_feature(geometry.encoded, hrz_proto::POLYLINE_GEOMETRY, 0, 3);
    EXPECT_TRUE(geometry.arrays.sizes.empty());
}

TEST(InMemoryGeometry, polygon_with_empty_linestring_sizes)
{
    hrz_proto::VectorFeature feature;
    add_geometry(&feature, hrz_proto::POLYGON_GEOMETRY, {{0, 0, 0}, {4, 0, 0}, {0, 4, 0}});

    const auto geometry = decode(feature);
    expect_simple_feature(geometry.encoded, hrz_proto::POLYGON_GEOMETRY, 0, 3);
}

TEST(InMemoryGeometry, multi_polyline)
{
    hrz_proto::VectorFeature feature;
    add_geometry(
        &feature, hrz_proto::POLYLINE_GEOMETRY,
        {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}, {2, 1, 0}}, {2, 3});

    const auto geometry = decode(feature);
    EXPECT_TRUE(geometry.encoded.flags.is_collection);
    EXPECT_TRUE(geometry.encoded.flags.has_geometry(hrz_proto::POLYLINE_GEOMETRY));
    EXPECT_EQ(geometry.encoded.first_geometry_part, 0);
    EXPECT_EQ(geometry.encoded.geometry_part_count, 1);

    ASSERT_EQ(geometry.arrays.parts.size(), 1);
    expect_part_eq(geometry.arrays.parts[0], {hrz_proto::POLYLINE_GEOMETRY, 0, 5, 0, 2});
    EXPECT_EQ(geometry.arrays.sizes, (std::vector<uint32_t>{2, 3}));
}

TEST(InMemoryGeometry, polygon_with_hole)
{
    hrz_proto::VectorFeature feature;
    add_geometry(
        &feature, hrz_proto::POLYGON_GEOMETRY,
        {{0, 0, 0}, {8, 0, 0}, {0, 8, 0}, {1, 1, 0}, {1, 2, 0}, {2, 2, 0}, {2, 1, 0}}, {3, 4});

    const auto geometry = decode(feature);
    EXPECT_TRUE(geometry.encoded.flags.is_collection);
    EXPECT_EQ(geometry.encoded.geometry_part_count, 1);

    ASSERT_EQ(geometry.arrays.parts.size(), 1);
    expect_part_eq(geometry.arrays.parts[0], {hrz_proto::POLYGON_GEOMETRY, 0, 7, 0, 2});
    EXPECT_EQ(geometry.arrays.sizes, (std::vector<uint32_t>{3, 4}));
}

TEST(InMemoryGeometry, multi_polygon)
{
    hrz_proto::VectorFeature feature;
    add_geometry(&feature, hrz_proto::POLYGON_GEOMETRY, {{0, 0, 0}, {4, 0, 0}, {0, 4, 0}});
    add_geometry(
        &feature, hrz_proto::POLYGON_GEOMETRY,
        {{10, 0, 0}, {18, 0, 0}, {10, 8, 0}, {11, 1, 0}, {11, 2, 0}, {12, 2, 0}, {12, 1, 0}},
        {3, 4});

    const auto geometry = decode(feature);
    EXPECT_TRUE(geometry.encoded.flags.is_collection);
    EXPECT_TRUE(geometry.encoded.flags.has_geometry(hrz_proto::POLYGON_GEOMETRY));
    EXPECT_FALSE(geometry.encoded.flags.has_geometry(hrz_proto::POINT_GEOMETRY));
    EXPECT_EQ(geometry.encoded.geometry_part_count, 2);

    ASSERT_EQ(geometry.arrays.parts.size(), 2);
    expect_part_eq(geometry.arrays.parts[0], {hrz_proto::POLYGON_GEOMETRY, 0, 3, 0, 1});
    expect_part_eq(geometry.arrays.parts[1], {hrz_proto::POLYGON_GEOMETRY, 3, 7, 0, 2});
    EXPECT_EQ(geometry.arrays.sizes, (std::vector<uint32_t>{3, 4}));
}

TEST(InMemoryGeometry, geometry_collection)
{
    hrz_proto::VectorFeature feature;
    add_geometry(&feature, hrz_proto::POINT_GEOMETRY, {{5, 5, 0}});
    add_geometry(&feature, hrz_proto::POLYGON_GEOMETRY, {{0, 0, 0}, {4, 0, 0}, {0, 4, 0}});

    const auto geometry = decode(feature);
    EXPECT_TRUE(geometry.encoded.flags.is_collection);
    EXPECT_TRUE(geometry.encoded.flags.has_geometry(hrz_proto::POINT_GEOMETRY));
    EXPECT_FALSE(geometry.encoded.flags.has_geometry(hrz_proto::POLYLINE_GEOMETRY));
    EXPECT_TRUE(geometry.encoded.flags.has_geometry(hrz_proto::POLYGON_GEOMETRY));

    ASSERT_EQ(geometry.arrays.parts.size(), 2);
    expect_part_eq(geometry.arrays.parts[0], {hrz_proto::POINT_GEOMETRY, 0, 1, 0, 0});
    expect_part_eq(geometry.arrays.parts[1], {hrz_proto::POLYGON_GEOMETRY, 1, 3, 0, 1});

    // The anchor of a collection is the anchor of its first geometry.
    EXPECT_DOUBLE_EQ(geometry.encoded.anchor.x, 5);
    EXPECT_DOUBLE_EQ(geometry.encoded.anchor.y, 5);
}

TEST(InMemoryGeometry, invalid_geometry_is_dropped_but_others_are_kept)
{
    hrz_proto::VectorFeature feature;
    // The linestring sizes require 6 points, but there are only 5.
    add_geometry(
        &feature, hrz_proto::POLYGON_GEOMETRY,
        {{0, 0, 0}, {4, 0, 0}, {0, 4, 0}, {1, 1, 0}, {1, 2, 0}}, {3, 3});
    add_geometry(&feature, hrz_proto::POINT_GEOMETRY, {{7, 8, 9}});

    const auto geometry = decode(feature);
    expect_simple_feature(geometry.encoded, hrz_proto::POINT_GEOMETRY, 0, 1);
    expect_points_eq(geometry.arrays.points, {{7, 8, 9}});
}

TEST(InMemoryGeometry, all_invalid_geometries_give_an_empty_feature)
{
    hrz_proto::VectorFeature feature;
    add_geometry(&feature, hrz_proto::POLYLINE_GEOMETRY, {{0, 0, 0}});
    add_geometry(&feature, hrz_proto::POLYGON_GEOMETRY, {{0, 0, 0}, {1, 0, 0}});
    add_geometry(&feature, hrz_proto::POLYLINE_GEOMETRY, {{0, 0, 0}, {1, 0, 0}}, {3});

    expect_empty(decode(feature));
}

TEST(InMemoryGeometry, points_outside_linestrings_are_ignored)
{
    hrz_proto::VectorFeature feature;
    add_geometry(
        &feature, hrz_proto::POLYLINE_GEOMETRY, {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {3, 0, 0}}, {2});

    const auto geometry = decode(feature);
    expect_simple_feature(geometry.encoded, hrz_proto::POLYLINE_GEOMETRY, 0, 2);
    expect_points_eq(geometry.arrays.points, {{0, 0, 0}, {1, 0, 0}});
}

TEST(InMemoryGeometry, projection_is_applied_before_bounds_and_anchor)
{
    hrz_proto::VectorFeature feature;
    add_geometry(&feature, hrz_proto::POINT_GEOMETRY, {{1, 2, 3}, {3, 4, 5}});

    const auto geometry = decode_feature_geometry(
        feature,
        [](std::span<lm::dvec3> points)
        {
            for (auto& point : points)
            {
                point.x += 100;
            }
        });

    expect_points_eq(geometry.arrays.points, {{101, 2, 3}, {103, 4, 5}});
    EXPECT_DOUBLE_EQ(geometry.bbox.min.x, 101);
    EXPECT_DOUBLE_EQ(geometry.bbox.max.x, 103);
    EXPECT_DOUBLE_EQ(geometry.encoded.anchor.x, 102);
}

TEST(InMemoryGeometry, append_rebases_indices)
{
    hrz_proto::VectorFeature simple_feature;
    add_geometry(&simple_feature, hrz_proto::POINT_GEOMETRY, {{1, 1, 0}, {2, 2, 0}});

    hrz_proto::VectorFeature collection_feature;
    add_geometry(
        &collection_feature, hrz_proto::POLYLINE_GEOMETRY,
        {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}, {2, 1, 0}}, {2, 3});
    add_geometry(
        &collection_feature, hrz_proto::POLYGON_GEOMETRY, {{0, 0, 0}, {4, 0, 0}, {0, 4, 0}});

    const auto simple = decode(simple_feature);
    const auto collection = decode(collection_feature);

    GeometryArrays tile;
    const auto appended_simple = append_feature_geometry(simple, &tile);
    const auto appended_collection = append_feature_geometry(collection, &tile);
    const auto appended_simple_again = append_feature_geometry(simple, &tile);

    expect_simple_feature(appended_simple, hrz_proto::POINT_GEOMETRY, 0, 2);

    EXPECT_TRUE(appended_collection.flags.is_collection);
    EXPECT_EQ(appended_collection.first_geometry_part, 0);
    EXPECT_EQ(appended_collection.geometry_part_count, 2);
    EXPECT_DOUBLE_EQ(appended_collection.anchor.x, collection.encoded.anchor.x);

    expect_simple_feature(appended_simple_again, hrz_proto::POINT_GEOMETRY, 10, 2);

    ASSERT_EQ(tile.parts.size(), 2);
    expect_part_eq(tile.parts[0], {hrz_proto::POLYLINE_GEOMETRY, 2, 5, 0, 2});
    expect_part_eq(tile.parts[1], {hrz_proto::POLYGON_GEOMETRY, 7, 3, 0, 1});
    EXPECT_EQ(tile.sizes, (std::vector<uint32_t>{2, 3}));

    expect_points_eq(
        tile.points,
        {{1, 1, 0},
         {2, 2, 0},
         {0, 0, 0},
         {1, 0, 0},
         {0, 1, 0},
         {1, 1, 0},
         {2, 1, 0},
         {0, 0, 0},
         {4, 0, 0},
         {0, 4, 0},
         {1, 1, 0},
         {2, 2, 0}});
}

TEST(InMemoryGeometry, append_empty_feature)
{
    const hrz_proto::VectorFeature feature;
    const auto empty = decode(feature);

    GeometryArrays tile;
    const auto appended = append_feature_geometry(empty, &tile);

    EXPECT_TRUE(appended.flags.is_collection);
    EXPECT_EQ(appended.geometry_part_count, 0);
    EXPECT_TRUE(tile.points.empty());
    EXPECT_TRUE(tile.parts.empty());
}
