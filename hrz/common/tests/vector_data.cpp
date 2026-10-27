// SPDX-FileCopyrightText: Copyright 2024 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/common/vector_data/attribute_type_owned.h"
#include "hrz/common/vector_data/feature_id.h"
#include "hrz/common/vector_data/geometry_utils.h"
#include "hrz/common/vector_data/tile_geometry.h"
#include "hrz/common/vector_data/tile_geometry_builder.h"
#include "hrz/fnd/defer.h"
#include "hrz/fnd/function_ref.h"
#include "hrz/protocol/vector/geometry_type.pb.h"

#include <gtest/gtest.h>

#include <initializer_list>
#include <span>
#include <variant>
#include <vector>

using namespace hrz::vector_data;

TEST(FeatureId, same_hash_on_all_platforms)
{
    FeatureId::Builder builder;
    builder.add_value(1, attr_from<OwnedAttributeValue>(1));
    builder.add_value(2, attr_from<OwnedAttributeValue>(2.5));
    builder.add_value(3, attr_from<OwnedAttributeValue>(UINT64_C(0xffff'ffff'ffff'ffff)));
    builder.add_value(3, attr_from<OwnedAttributeValue>(INT64_C(0x1fff'ffff'ffff'ffff)));
    builder.add_value(4, attr_from<OwnedAttributeValue>("ezhfozijfiozjf"));
    const FeatureId id = builder.build();
    EXPECT_EQ(id.hash(), UINT64_C(14593653686077784397));
}

TEST(FeatureId, build_out_of_order)
{
    FeatureId::Builder builder;
    builder.add_value(2, attr_from<OwnedAttributeValue>(2.5));
    builder.add_value(1, attr_from<OwnedAttributeValue>(1));
    builder.add_value(3, attr_from<OwnedAttributeValue>(UINT64_C(0xffff'ffff'ffff'ffff)));
    builder.add_value(4, attr_from<OwnedAttributeValue>("ezhfozijfiozjf"));
    const FeatureId id = builder.build();

    FeatureId::Builder builder2;
    builder2.add_value(4, attr_from<OwnedAttributeValue>("ezhfozijfiozjf"));
    builder2.add_value(1, attr_from<OwnedAttributeValue>(1));
    builder2.add_value(2, attr_from<OwnedAttributeValue>(2.5));
    builder2.add_value(3, attr_from<OwnedAttributeValue>(UINT64_C(0xffff'ffff'ffff'ffff)));
    const FeatureId id2 = builder2.build();

    EXPECT_EQ(id.hash(), id2.hash());
}

namespace
{

template<typename T>
std::optional<hrz::BlobArray<T>> make_blob_array(hrz::BlobAllocator* ba, std::span<const T> data)
{
    auto blob_opt = hrz::blobs::allocate_blob_sync(ba, data.size_bytes());
    if (!blob_opt) return std::nullopt;

    {
        auto mutable_data = blob_opt->get_mutable_data();
        auto to_bytes = mutable_data.as_writable_bytes();
        assert(to_bytes.size() == data.size_bytes());
        memcpy(to_bytes.data(), data.data(), data.size_bytes());
    }

    return hrz::BlobArray<T>::make_blob_array(ba, std::move(*blob_opt));
}

using ReaderGeometry = VectorTileGeometry::Geometry;
using ReaderPoint = VectorTileGeometry::PointGeometry;
using ReaderPolyline = VectorTileGeometry::PolylineGeometry;
using ReaderPolygon = VectorTileGeometry::PolygonGeometry;

std::vector<ReaderGeometry> collect_simple_geometries(
    const VectorTileGeometry::Reader& reader,
    uint32_t feature_index)
{
    std::vector<ReaderGeometry> geometries;
    reader.iterate_geometries_for_feature(
        feature_index, [&geometries](const ReaderGeometry& geom) { geometries.push_back(geom); });
    return geometries;
}

void expect_point_eq(const lm::dvec3& actual, const lm::dvec3& expected)
{
    EXPECT_DOUBLE_EQ(actual.x, expected.x);
    EXPECT_DOUBLE_EQ(actual.y, expected.y);
    EXPECT_DOUBLE_EQ(actual.z, expected.z);
}

void expect_points_eq(std::span<const lm::dvec3> actual, std::initializer_list<lm::dvec3> expected)
{
    ASSERT_EQ(actual.size(), expected.size());
    size_t i = 0;
    for (const lm::dvec3& expected_point : expected)
    {
        SCOPED_TRACE(testing::Message() << "point " << i);
        expect_point_eq(actual[i], expected_point);
        ++i;
    }
}

void expect_point(
    const ReaderGeometry& geom,
    const lm::dvec3& expected,
    uint32_t expected_point_index)
{
    const auto* point = std::get_if<ReaderPoint>(&geom);
    ASSERT_NE(point, nullptr) << "Expected a point geometry";
    expect_point_eq(point->point, expected);
    EXPECT_EQ(point->point_index, expected_point_index);
}

void expect_linestring(
    const ReaderGeometry& geom,
    std::initializer_list<lm::dvec3> expected,
    uint32_t expected_first_point_index)
{
    const auto* linestring = std::get_if<ReaderPolyline>(&geom);
    ASSERT_NE(linestring, nullptr) << "Expected a linestring geometry";
    expect_points_eq(linestring->points, expected);
    EXPECT_EQ(linestring->first_point_index, expected_first_point_index);
}

void expect_polygon(
    const ReaderGeometry& geom,
    std::initializer_list<lm::dvec3> expected_points,
    std::initializer_list<uint32_t> expected_ring_sizes,
    uint32_t expected_first_point_index)
{
    const auto* polygon = std::get_if<ReaderPolygon>(&geom);
    ASSERT_NE(polygon, nullptr) << "Expected a polygon geometry";
    expect_points_eq(polygon->points, expected_points);
    EXPECT_EQ(polygon->first_point_index, expected_first_point_index);
    EXPECT_EQ(
        std::vector<uint32_t>(polygon->ring_sizes.begin(), polygon->ring_sizes.end()),
        std::vector<uint32_t>(expected_ring_sizes));
}

using Feature = VectorTileGeometry::Feature;
using FeatureFlags = VectorTileGeometry::FeatureFlags;
using GeometryPart = VectorTileGeometry::GeometryPart;

template<typename T>
std::vector<T> copy_to_vector(const hrz::BlobArray<T>& array)
{
    const auto data = array.get_cdata();
    const auto span = data.as_span();
    return std::vector<T>(span.begin(), span.end());
}

// Anchors are not compared.
void expect_feature_eq(const Feature& actual, const Feature& expected)
{
    EXPECT_EQ(actual.flags.is_collection, expected.flags.is_collection);
    if (!expected.flags.is_collection)
    {
        EXPECT_EQ(actual.flags.primitive_geometry_type(), expected.flags.primitive_geometry_type());
    }
    for (const auto type :
         {hrz_proto::POINT_GEOMETRY, hrz_proto::POLYLINE_GEOMETRY, hrz_proto::POLYGON_GEOMETRY})
    {
        SCOPED_TRACE(
            testing::Message() << "has_geometry(" << hrz_proto::VectorGeometryType_Name(type)
                               << ")");
        EXPECT_EQ(actual.flags.has_geometry(type), expected.flags.has_geometry(type));
    }
    EXPECT_EQ(actual.first_geometry_part, expected.first_geometry_part);
    EXPECT_EQ(actual.geometry_part_count, expected.geometry_part_count);
}

void expect_features_eq(const hrz::BlobArray<Feature>& actual, std::span<const Feature> expected)
{
    const auto actual_features = copy_to_vector(actual);
    ASSERT_EQ(actual_features.size(), expected.size());
    for (size_t i = 0; i < expected.size(); ++i)
    {
        SCOPED_TRACE(testing::Message() << "feature " << i);
        expect_feature_eq(actual_features[i], expected[i]);
    }
}

// Only compares the fields that are meaningful for the expected part.
void expect_part_eq(const GeometryPart& actual, const GeometryPart& expected)
{
    EXPECT_EQ(actual.type, expected.type);
    EXPECT_EQ(actual.first_point, expected.first_point);
    EXPECT_EQ(actual.point_count, expected.point_count);

    switch (expected.type)
    {
        case hrz_proto::POINT_GEOMETRY: break;
        case hrz_proto::POLYLINE_GEOMETRY:
        case hrz_proto::POLYGON_GEOMETRY:
            EXPECT_EQ(actual.linestring_count, expected.linestring_count);
            if (expected.linestring_count > 1)
            {
                EXPECT_EQ(actual.first_linestring_size, expected.first_linestring_size);
            }
            break;
        default: FAIL() << "Unhandled geometry type"; break;
    }
}

void expect_parts_eq(
    const hrz::BlobArray<GeometryPart>& actual,
    std::span<const GeometryPart> expected)
{
    const auto actual_parts = copy_to_vector(actual);
    ASSERT_EQ(actual_parts.size(), expected.size());
    for (size_t i = 0; i < expected.size(); ++i)
    {
        SCOPED_TRACE(testing::Message() << "geometry part " << i);
        expect_part_eq(actual_parts[i], expected[i]);
    }
}

void expect_points_eq(const hrz::BlobArray<lm::dvec3>& actual, std::span<const lm::dvec3> expected)
{
    const auto actual_points = copy_to_vector(actual);
    ASSERT_EQ(actual_points.size(), expected.size());
    for (size_t i = 0; i < expected.size(); ++i)
    {
        SCOPED_TRACE(testing::Message() << "point " << i);
        expect_point_eq(actual_points[i], expected[i]);
    }
}

void expect_uints_eq(const hrz::BlobArray<uint32_t>& actual, std::span<const uint32_t> expected)
{
    EXPECT_EQ(copy_to_vector(actual), std::vector<uint32_t>(expected.begin(), expected.end()));
}

FeatureFlags collection_of(std::initializer_list<hrz_proto::VectorGeometryType> types)
{
    auto flags = FeatureFlags::make_collection();
    for (const auto type : types)
    {
        flags.collection_geometry_types = static_cast<uint8_t>(
            flags.collection_geometry_types | (1U << static_cast<uint8_t>(type)));
    }
    return flags;
}

namespace reference_tile
{

// clang-format off

// Feature 0: 1 points at (1, 2)
// Feature 1: 3 points at (1, 1), (2, 2), (3, 3)
// Feature 2: 1 polyline (1, 1), (2, 2), (3, 3)
// Feature 3: 2 polylines: (1, 1), (2, 2) and (3, 3), (4, 4), (5, 5)
// Feature 4: 1 polygon with 1 exterior ring (0, 0), (4, 0), (0, 4)
// Feature 5: 1 polygon with 1 hole: exterior ring (0, 0), (4, 0), (0, 4) and interior ring (1, 1), (1, 2), (2, 2), (2, 1)
// Feature 6: 2 polygons: exterior ring (0, 0), (4, 0), (0, 4) and exterior ring (1, 1), (2, 1), (2, 2), (1, 2)
// Feature 7: 1 collection with 1 point (1, 1) and 1 multi-polyline (2, 2), (3, 3) and (4, 4), (5, 5)

const lm::dvec3 points[] = {
    {1, 2, 0}, // Feature 0
    {1, 1, 0}, {2, 2, 0}, {3, 3, 0}, // Feature 1
    {1, 1, 0}, {2, 2, 0}, {3, 3, 0}, // Feature 2
    {1, 1, 0}, {2, 2, 0}, {3, 3, 0}, {4, 4, 0}, {5, 5, 0}, // Feature 3
    {0, 0, 0}, {4, 0, 0}, {0, 4, 0}, // Feature 4
    {0, 0, 0}, {4, 0, 0}, {0, 4, 0}, {1, 1, 0}, {1, 2, 0}, {2, 2, 0}, {2, 1, 0}, // Feature 5
    {0, 0, 0}, {4, 0, 0}, {0, 4, 0}, {1, 1, 0}, {2, 1, 0}, {2, 2, 0}, {1, 2, 0}, // Feature 6
    {1, 1, 0}, {2, 2, 0}, {3, 3, 0}, {4, 4, 0}, {5, 5, 0} // Feature 7
};

const uint32_t linestring_sizes[] = {
    2, 3, // Feature 3
    3, 4, // Feature 5
    2, 2, // Feature 7
};

const VectorTileGeometry::GeometryPart geometry_parts[] = {
    {hrz_proto::VectorGeometryType::POLYLINE_GEOMETRY, 7, 5, 0, 2}, // Feature 3
    {hrz_proto::VectorGeometryType::POLYGON_GEOMETRY, 15, 7, 2, 2}, // Feature 5
    {hrz_proto::VectorGeometryType::POLYGON_GEOMETRY, 22, 3, 0, 1}, // Feature 6, part 0
    {hrz_proto::VectorGeometryType::POLYGON_GEOMETRY, 25, 4, 0, 1}, // Feature 6, part 1
    {hrz_proto::VectorGeometryType::POINT_GEOMETRY, 29, 1, 0, 0},   // Feature 7, part 0
    {hrz_proto::VectorGeometryType::POLYLINE_GEOMETRY, 30, 4, 4, 2} // Feature 7, part 1
};

const VectorTileGeometry::Feature features[] = {
    {0,  1, {}, 0.0F, VectorTileGeometry::FeatureFlags::make_simple_feature(hrz_proto::POINT_GEOMETRY)},    // Feature 0
    {1,  3, {}, 0.0F, VectorTileGeometry::FeatureFlags::make_simple_feature(hrz_proto::POINT_GEOMETRY)},    // Feature 1
    {4,  3, {}, 0.0F, VectorTileGeometry::FeatureFlags::make_simple_feature(hrz_proto::POLYLINE_GEOMETRY)}, // Feature 2
    {0,  1, {}, 0.0F, collection_of({hrz_proto::POLYLINE_GEOMETRY})},                                 // Feature 3
    {12, 3, {}, 0.0F, VectorTileGeometry::FeatureFlags::make_simple_feature(hrz_proto::POLYGON_GEOMETRY)},  // Feature 4
    {1,  1, {}, 0.0F, collection_of({hrz_proto::POLYGON_GEOMETRY})},                                 // Feature 5
    {2,  2, {}, 0.0F, collection_of({hrz_proto::POLYGON_GEOMETRY})},                                 // Feature 6
    {4,  2, {}, 0.0F, collection_of({hrz_proto::POINT_GEOMETRY, hrz_proto::POLYLINE_GEOMETRY})}                                  // Feature 7
};

// clang-format on

} // namespace reference_tile

} // anonymous namespace

TEST(TileGeometryReader, iterate_geometries)
{
    hrz::BlobAllocator* ba = hrz::blobs::create_allocator(1024 * 1024, true);
    HRZ_DEFER[ba]
    {
        hrz::blobs::destroy_allocator(ba);
    };

    auto points_blob_opt = make_blob_array<lm::dvec3>(ba, reference_tile::points);
    auto linestring_sizes_blob_opt =
        make_blob_array<uint32_t>(ba, reference_tile::linestring_sizes);
    auto geometry_parts_blob_opt =
        make_blob_array<VectorTileGeometry::GeometryPart>(ba, reference_tile::geometry_parts);
    auto features_blob_opt =
        make_blob_array<VectorTileGeometry::Feature>(ba, reference_tile::features);

    ASSERT_TRUE(points_blob_opt);
    ASSERT_TRUE(linestring_sizes_blob_opt);
    ASSERT_TRUE(geometry_parts_blob_opt);
    ASSERT_TRUE(features_blob_opt);

    const VectorTileGeometry geometry{
        .bounds = {},
        .features = std::move(*features_blob_opt),
        .geometry_parts = std::move(*geometry_parts_blob_opt),
        .points = std::move(*points_blob_opt),
        .linestring_sizes = std::move(*linestring_sizes_blob_opt),
    };

    const auto reader = geometry.make_reader();

    {
        SCOPED_TRACE("Feature 0");
        const auto geoms = collect_simple_geometries(reader, 0);
        ASSERT_EQ(geoms.size(), 1);
        expect_point(geoms[0], {1, 2, 0}, 0);
    }

    {
        SCOPED_TRACE("Feature 1");
        const auto geoms = collect_simple_geometries(reader, 1);
        ASSERT_EQ(geoms.size(), 3);
        expect_point(geoms[0], {1, 1, 0}, 1);
        expect_point(geoms[1], {2, 2, 0}, 2);
        expect_point(geoms[2], {3, 3, 0}, 3);
    }

    {
        SCOPED_TRACE("Feature 2");
        const auto geoms = collect_simple_geometries(reader, 2);
        ASSERT_EQ(geoms.size(), 1);
        expect_linestring(geoms[0], {{1, 1, 0}, {2, 2, 0}, {3, 3, 0}}, 4);
    }

    {
        SCOPED_TRACE("Feature 3");
        const auto geoms = collect_simple_geometries(reader, 3);
        ASSERT_EQ(geoms.size(), 2);
        expect_linestring(geoms[0], {{1, 1, 0}, {2, 2, 0}}, 7);
        expect_linestring(geoms[1], {{3, 3, 0}, {4, 4, 0}, {5, 5, 0}}, 9);
    }

    {
        SCOPED_TRACE("Feature 4");
        const auto geoms = collect_simple_geometries(reader, 4);
        ASSERT_EQ(geoms.size(), 1);
        expect_polygon(geoms[0], {{0, 0, 0}, {4, 0, 0}, {0, 4, 0}}, {3}, 12);
    }

    {
        SCOPED_TRACE("Feature 5");
        const auto geoms = collect_simple_geometries(reader, 5);
        ASSERT_EQ(geoms.size(), 1);
        expect_polygon(
            geoms[0], {{0, 0, 0}, {4, 0, 0}, {0, 4, 0}, {1, 1, 0}, {1, 2, 0}, {2, 2, 0}, {2, 1, 0}},
            {3, 4}, 15);
    }

    {
        SCOPED_TRACE("Feature 6");
        const auto geoms = collect_simple_geometries(reader, 6);
        ASSERT_EQ(geoms.size(), 2);
        expect_polygon(geoms[0], {{0, 0, 0}, {4, 0, 0}, {0, 4, 0}}, {3}, 22);
        expect_polygon(geoms[1], {{1, 1, 0}, {2, 1, 0}, {2, 2, 0}, {1, 2, 0}}, {4}, 25);
    }

    {
        SCOPED_TRACE("Feature 7");
        const auto geoms = collect_simple_geometries(reader, 7);
        ASSERT_EQ(geoms.size(), 3);
        expect_point(geoms[0], {1, 1, 0}, 29);
        expect_linestring(geoms[1], {{2, 2, 0}, {3, 3, 0}}, 30);
        expect_linestring(geoms[2], {{4, 4, 0}, {5, 5, 0}}, 32);
    }

    {
        SCOPED_TRACE("Out of range feature");
        EXPECT_TRUE(collect_simple_geometries(reader, 8).empty());
    }

    for (uint32_t i = 0; i < std::size(reference_tile::features); ++i)
    {
        SCOPED_TRACE(testing::Message() << "get_feature(" << i << ")");
        const auto* feature = reader.get_feature(i);
        ASSERT_NE(feature, nullptr);
        expect_feature_eq(*feature, reference_tile::features[i]);
    }
    EXPECT_EQ(reader.get_feature(8), nullptr);
}

namespace
{

using PointsBuilder = VectorTileGeometryBuilder::PointsBuilder;
using PolylinesBuilder = VectorTileGeometryBuilder::PolylinesBuilder;
using PolygonsBuilder = VectorTileGeometryBuilder::PolygonsBuilder;

template<typename Builder>
void add_points(Builder& builder, std::span<const lm::dvec3> points)
{
    for (const lm::dvec3& point : points)
    {
        builder.add_point(point);
    }
}

template<typename Builder>
void add_points(Builder& builder, std::initializer_list<lm::dvec3> points)
{
    add_points(builder, std::span<const lm::dvec3>(points.begin(), points.size()));
}

} // anonymous namespace

class TileGeometryBuilder : public testing::Test
{
protected:
    void SetUp() override { _allocator = hrz::blobs::create_allocator(1024 * 1024, true); }

    void TearDown() override { hrz::blobs::destroy_allocator(_allocator); }

    std::optional<VectorTileGeometry> build(
        hrz::function_ref<bool(VectorTileGeometryBuilder&)> callback)
    {
        VectorTileGeometryBuilder builder(_allocator);
        if (!callback(builder)) return std::nullopt;
        return std::move(builder).finalize();
    }

    hrz::BlobAllocator* _allocator = nullptr;
};

TEST_F(TileGeometryBuilder, empty)
{
    const auto geometry = build([](VectorTileGeometryBuilder&) { return true; });
    ASSERT_TRUE(geometry);

    EXPECT_EQ(geometry->features.size(), 0);
    EXPECT_EQ(geometry->geometry_parts.size(), 0);
    EXPECT_EQ(geometry->points.size(), 0);
    EXPECT_EQ(geometry->linestring_sizes.size(), 0);
}

TEST_F(TileGeometryBuilder, point)
{
    const auto geometry = build(
        [](VectorTileGeometryBuilder& builder)
        {
            if (!builder.add_points_feature(
                    [](PointsBuilder& points)
                    {
                        points.add_point({1, 2, 3});
                        return true;
                    }))
                return false;
            return true;
        });
    ASSERT_TRUE(geometry);

    const Feature expected_features[] = {
        {0, 1, {}, 0.0F, FeatureFlags::make_simple_feature(hrz_proto::POINT_GEOMETRY)},
    };
    const lm::dvec3 expected_points[] = {{1, 2, 3}};

    expect_features_eq(geometry->features, expected_features);
    expect_parts_eq(geometry->geometry_parts, {});
    expect_points_eq(geometry->points, expected_points);
    expect_uints_eq(geometry->linestring_sizes, {});
}

TEST_F(TileGeometryBuilder, multi_point)
{
    const auto geometry = build(
        [](VectorTileGeometryBuilder& builder)
        {
            if (!builder.add_points_feature(
                    [](PointsBuilder& points)
                    {
                        add_points(points, {{1, 1, 0}, {2, 2, 0}, {3, 3, 0}});
                        return true;
                    }))
                return false;
            return true;
        });
    ASSERT_TRUE(geometry);

    const Feature expected_features[] = {
        {0, 3, {}, 0.0F, FeatureFlags::make_simple_feature(hrz_proto::POINT_GEOMETRY)},
    };
    const lm::dvec3 expected_points[] = {{1, 1, 0}, {2, 2, 0}, {3, 3, 0}};

    expect_features_eq(geometry->features, expected_features);
    expect_parts_eq(geometry->geometry_parts, {});
    expect_points_eq(geometry->points, expected_points);
    expect_uints_eq(geometry->linestring_sizes, {});
}

void expect_linestring_tile(const VectorTileGeometry& tile)
{
    const Feature expected_features[] = {
        {0, 3, {}, 0.0F, FeatureFlags::make_simple_feature(hrz_proto::POLYLINE_GEOMETRY)},
    };
    const lm::dvec3 expected_points[] = {{1, 1, 0}, {2, 2, 0}, {3, 3, 0}};

    expect_features_eq(tile.features, expected_features);
    expect_parts_eq(tile.geometry_parts, {});
    expect_points_eq(tile.points, expected_points);
    expect_uints_eq(tile.linestring_sizes, {});
}

TEST_F(TileGeometryBuilder, linestring)
{
    const auto geometry = build(
        [](VectorTileGeometryBuilder& builder)
        {
            if (!builder.add_polylines_feature(
                    [](PolylinesBuilder& polylines)
                    {
                        add_points(polylines, {{1, 1, 0}, {2, 2, 0}, {3, 3, 0}});
                        EXPECT_TRUE(polylines.end_linestring());
                        return true;
                    }))
                return false;
            return true;
        });
    ASSERT_TRUE(geometry);
    expect_linestring_tile(*geometry);
}

TEST_F(TileGeometryBuilder, linestring_implicit_end)
{
    const auto geometry = build(
        [](VectorTileGeometryBuilder& builder)
        {
            if (!builder.add_polylines_feature(
                    [](PolylinesBuilder& polylines)
                    {
                        add_points(polylines, {{1, 1, 0}, {2, 2, 0}, {3, 3, 0}});
                        return true;
                    }))
                return false;
            return true;
        });
    ASSERT_TRUE(geometry);
    expect_linestring_tile(*geometry);
}

void expect_multi_linestring_tile(const VectorTileGeometry& tile)
{
    const Feature expected_features[] = {
        {0, 1, {}, 0.0F, collection_of({hrz_proto::POLYLINE_GEOMETRY})},
    };
    const GeometryPart expected_parts[] = {
        {hrz_proto::POLYLINE_GEOMETRY, 0, 5, 0, 2},
    };
    const lm::dvec3 expected_points[] = {{1, 1, 0}, {2, 2, 0}, {3, 3, 0}, {4, 4, 0}, {5, 5, 0}};
    const uint32_t expected_linestring_sizes[] = {2, 3};

    expect_features_eq(tile.features, expected_features);
    expect_parts_eq(tile.geometry_parts, expected_parts);
    expect_points_eq(tile.points, expected_points);
    expect_uints_eq(tile.linestring_sizes, expected_linestring_sizes);
}

TEST_F(TileGeometryBuilder, multi_linestring)
{
    const auto geometry = build(
        [](VectorTileGeometryBuilder& builder)
        {
            if (!builder.add_polylines_feature(
                    [](PolylinesBuilder& polylines)
                    {
                        add_points(polylines, {{1, 1, 0}, {2, 2, 0}});
                        EXPECT_TRUE(polylines.end_linestring());
                        add_points(polylines, {{3, 3, 0}, {4, 4, 0}, {5, 5, 0}});
                        EXPECT_TRUE(polylines.end_linestring());
                        return true;
                    }))
                return false;
            return true;
        });
    ASSERT_TRUE(geometry);
    expect_multi_linestring_tile(*geometry);
}

TEST_F(TileGeometryBuilder, multi_linestring_implicit_end)
{
    const auto geometry = build(
        [](VectorTileGeometryBuilder& builder)
        {
            if (!builder.add_polylines_feature(
                    [](PolylinesBuilder& polylines)
                    {
                        add_points(polylines, {{1, 1, 0}, {2, 2, 0}});
                        EXPECT_TRUE(polylines.end_linestring());
                        add_points(polylines, {{3, 3, 0}, {4, 4, 0}, {5, 5, 0}});
                        return true;
                    }))
                return false;
            return true;
        });
    ASSERT_TRUE(geometry);
    expect_multi_linestring_tile(*geometry);
}

void expect_polygon_tile(const VectorTileGeometry& tile)
{
    const Feature expected_features[] = {
        {0, 3, {}, 0.0F, FeatureFlags::make_simple_feature(hrz_proto::POLYGON_GEOMETRY)},
    };
    const lm::dvec3 expected_points[] = {{0, 0, 0}, {4, 0, 0}, {0, 4, 0}};

    expect_features_eq(tile.features, expected_features);
    expect_parts_eq(tile.geometry_parts, {});
    expect_points_eq(tile.points, expected_points);
    expect_uints_eq(tile.linestring_sizes, {});
}

TEST_F(TileGeometryBuilder, polygon)
{
    const auto geometry = build(
        [](VectorTileGeometryBuilder& builder)
        {
            if (!builder.add_polygons_feature(
                    [](PolygonsBuilder& polygons)
                    {
                        add_points(polygons, {{0, 0, 0}, {4, 0, 0}, {0, 4, 0}});
                        EXPECT_TRUE(polygons.end_ring());
                        EXPECT_TRUE(polygons.end_polygon());
                        return true;
                    }))
                return false;
            return true;
        });
    ASSERT_TRUE(geometry);
    expect_polygon_tile(*geometry);
}

TEST_F(TileGeometryBuilder, polygon_implicit_end_ring)
{
    const auto geometry = build(
        [](VectorTileGeometryBuilder& builder)
        {
            if (!builder.add_polygons_feature(
                    [](PolygonsBuilder& polygons)
                    {
                        add_points(polygons, {{0, 0, 0}, {4, 0, 0}, {0, 4, 0}});
                        EXPECT_TRUE(polygons.end_polygon());
                        return true;
                    }))
                return false;
            return true;
        });
    ASSERT_TRUE(geometry);
    expect_polygon_tile(*geometry);
}

TEST_F(TileGeometryBuilder, polygon_implicit_end_polygon)
{
    const auto geometry = build(
        [](VectorTileGeometryBuilder& builder)
        {
            if (!builder.add_polygons_feature(
                    [](PolygonsBuilder& polygons)
                    {
                        add_points(polygons, {{0, 0, 0}, {4, 0, 0}, {0, 4, 0}});
                        EXPECT_TRUE(polygons.end_ring());
                        return true;
                    }))
                return false;
            return true;
        });
    ASSERT_TRUE(geometry);
    expect_polygon_tile(*geometry);
}

TEST_F(TileGeometryBuilder, polygon_implicit_end_ring_and_end_polygon)
{
    const auto geometry = build(
        [](VectorTileGeometryBuilder& builder)
        {
            if (!builder.add_polygons_feature(
                    [](PolygonsBuilder& polygons)
                    {
                        add_points(polygons, {{0, 0, 0}, {4, 0, 0}, {0, 4, 0}});
                        return true;
                    }))
                return false;
            return true;
        });
    ASSERT_TRUE(geometry);
    expect_polygon_tile(*geometry);
}

void expect_polygon_with_holes_tile(const VectorTileGeometry& tile)
{
    const Feature expected_features[] = {
        {0, 1, {}, 0.0F, collection_of({hrz_proto::POLYGON_GEOMETRY})},
    };
    const GeometryPart expected_parts[] = {
        {hrz_proto::POLYGON_GEOMETRY, 0, 10, 0, 3},
    };
    // clang-format off
    const lm::dvec3 expected_points[] = {
        {0, 0, 0}, {8, 0, 0}, {0, 8, 0},            // Exterior
        {1, 1, 0}, {1, 2, 0}, {2, 2, 0}, {2, 1, 0}, // Hole 0
        {3, 1, 0}, {3, 2, 0}, {4, 1, 0},            // Hole 1
    };
    // clang-format on
    const uint32_t expected_linestring_sizes[] = {3, 4, 3};

    expect_features_eq(tile.features, expected_features);
    expect_parts_eq(tile.geometry_parts, expected_parts);
    expect_points_eq(tile.points, expected_points);
    expect_uints_eq(tile.linestring_sizes, expected_linestring_sizes);
}

TEST_F(TileGeometryBuilder, polygon_with_holes)
{
    const auto geometry = build(
        [](VectorTileGeometryBuilder& builder)
        {
            if (!builder.add_polygons_feature(
                    [](PolygonsBuilder& polygons)
                    {
                        add_points(polygons, {{0, 0, 0}, {8, 0, 0}, {0, 8, 0}});
                        EXPECT_TRUE(polygons.end_ring());
                        add_points(polygons, {{1, 1, 0}, {1, 2, 0}, {2, 2, 0}, {2, 1, 0}});
                        EXPECT_TRUE(polygons.end_ring());
                        add_points(polygons, {{3, 1, 0}, {3, 2, 0}, {4, 1, 0}});
                        EXPECT_TRUE(polygons.end_ring());
                        EXPECT_TRUE(polygons.end_polygon());
                        return true;
                    }))
                return false;
            return true;
        });
    ASSERT_TRUE(geometry);
    expect_polygon_with_holes_tile(*geometry);
}

TEST_F(TileGeometryBuilder, polygon_with_holes_implicit_end)
{
    const auto geometry = build(
        [](VectorTileGeometryBuilder& builder)
        {
            if (!builder.add_polygons_feature(
                    [](PolygonsBuilder& polygons)
                    {
                        add_points(polygons, {{0, 0, 0}, {8, 0, 0}, {0, 8, 0}});
                        EXPECT_TRUE(polygons.end_ring());
                        add_points(polygons, {{1, 1, 0}, {1, 2, 0}, {2, 2, 0}, {2, 1, 0}});
                        EXPECT_TRUE(polygons.end_ring());
                        add_points(polygons, {{3, 1, 0}, {3, 2, 0}, {4, 1, 0}});
                        return true;
                    }))
                return false;
            return true;
        });
    ASSERT_TRUE(geometry);
    expect_polygon_with_holes_tile(*geometry);
}

void expect_multi_polygon_tile(const VectorTileGeometry& tile)
{
    const Feature expected_features[] = {
        {0, 3, {}, 0.0F, collection_of({hrz_proto::POLYGON_GEOMETRY})},
    };
    const GeometryPart expected_parts[] = {
        {hrz_proto::POLYGON_GEOMETRY, 0, 3, 0, 1},
        {hrz_proto::POLYGON_GEOMETRY, 3, 7, 0, 2},
        {hrz_proto::POLYGON_GEOMETRY, 10, 4, 0, 1},
    };
    // clang-format off
    const lm::dvec3 expected_points[] = {
        {0, 0, 0}, {4, 0, 0}, {0, 4, 0},                // Polygon 0, exterior
        {10, 0, 0}, {18, 0, 0}, {10, 8, 0},             // Polygon 1, exterior
        {11, 1, 0}, {11, 2, 0}, {12, 2, 0}, {12, 1, 0}, // Polygon 1, hole
        {20, 0, 0}, {21, 0, 0}, {21, 1, 0}, {20, 1, 0}, // Polygon 2, exterior
    };
    // clang-format on
    const uint32_t expected_linestring_sizes[] = {3, 4};

    expect_features_eq(tile.features, expected_features);
    expect_parts_eq(tile.geometry_parts, expected_parts);
    expect_points_eq(tile.points, expected_points);
    expect_uints_eq(tile.linestring_sizes, expected_linestring_sizes);
}

TEST_F(TileGeometryBuilder, multi_polygon)
{
    const auto geometry = build(
        [](VectorTileGeometryBuilder& builder)
        {
            if (!builder.add_polygons_feature(
                    [](PolygonsBuilder& polygons)
                    {
                        add_points(polygons, {{0, 0, 0}, {4, 0, 0}, {0, 4, 0}});
                        EXPECT_TRUE(polygons.end_ring());
                        EXPECT_TRUE(polygons.end_polygon());

                        add_points(polygons, {{10, 0, 0}, {18, 0, 0}, {10, 8, 0}});
                        EXPECT_TRUE(polygons.end_ring());
                        add_points(polygons, {{11, 1, 0}, {11, 2, 0}, {12, 2, 0}, {12, 1, 0}});
                        EXPECT_TRUE(polygons.end_ring());
                        EXPECT_TRUE(polygons.end_polygon());

                        add_points(polygons, {{20, 0, 0}, {21, 0, 0}, {21, 1, 0}, {20, 1, 0}});
                        EXPECT_TRUE(polygons.end_ring());
                        EXPECT_TRUE(polygons.end_polygon());
                        return true;
                    }))
                return false;
            return true;
        });
    ASSERT_TRUE(geometry);
    expect_multi_polygon_tile(*geometry);
}

TEST_F(TileGeometryBuilder, multi_polygon_implicit_end)
{
    const auto geometry = build(
        [](VectorTileGeometryBuilder& builder)
        {
            if (!builder.add_polygons_feature(
                    [](PolygonsBuilder& polygons)
                    {
                        add_points(polygons, {{0, 0, 0}, {4, 0, 0}, {0, 4, 0}});
                        EXPECT_TRUE(polygons.end_polygon());

                        add_points(polygons, {{10, 0, 0}, {18, 0, 0}, {10, 8, 0}});
                        EXPECT_TRUE(polygons.end_ring());
                        add_points(polygons, {{11, 1, 0}, {11, 2, 0}, {12, 2, 0}, {12, 1, 0}});
                        EXPECT_TRUE(polygons.end_polygon());

                        add_points(polygons, {{20, 0, 0}, {21, 0, 0}, {21, 1, 0}, {20, 1, 0}});
                        return true;
                    }))
                return false;
            return true;
        });
    ASSERT_TRUE(geometry);
    expect_multi_polygon_tile(*geometry);
}

TEST_F(TileGeometryBuilder, implicit_ends_between_consecutive_features)
{
    const auto geometry = build(
        [](VectorTileGeometryBuilder& builder)
        {
            if (!builder.add_polylines_feature(
                    [](PolylinesBuilder& polylines)
                    {
                        add_points(polylines, {{0, 0, 0}, {1, 0, 0}});
                        EXPECT_TRUE(polylines.end_linestring());
                        add_points(polylines, {{2, 0, 0}, {3, 0, 0}, {4, 0, 0}});
                        return true;
                    }))
                return false;
            if (!builder.add_polygons_feature(
                    [](PolygonsBuilder& polygons)
                    {
                        add_points(polygons, {{0, 0, 0}, {4, 0, 0}, {0, 4, 0}});
                        EXPECT_TRUE(polygons.end_ring());
                        add_points(polygons, {{1, 1, 0}, {1, 2, 0}, {2, 2, 0}, {2, 1, 0}});
                        return true;
                    }))
                return false;
            if (!builder.add_geometry_collection(
                    [](VectorTileGeometryBuilder& collection)
                    {
                        if (!collection.add_polylines_feature(
                                [](PolylinesBuilder& polylines)
                                {
                                    add_points(polylines, {{5, 5, 0}, {6, 6, 0}});
                                    EXPECT_TRUE(polylines.end_linestring());
                                    add_points(polylines, {{7, 7, 0}, {8, 8, 0}});
                                    return true;
                                }))
                            return false;
                        if (!collection.add_polygons_feature(
                                [](PolygonsBuilder& polygons)
                                {
                                    add_points(polygons, {{0, 0, 0}, {4, 0, 0}, {0, 4, 0}});
                                    return true;
                                }))
                            return false;
                        return true;
                    }))
                return false;
            if (!builder.add_polylines_feature(
                    [](PolylinesBuilder& polylines)
                    {
                        add_points(polylines, {{9, 9, 0}, {10, 10, 0}});
                        return true;
                    }))
                return false;
            return true;
        });
    ASSERT_TRUE(geometry);

    const Feature expected_features[] = {
        {0, 1, {}, 0.0F, collection_of({hrz_proto::POLYLINE_GEOMETRY})},
        {1, 1, {}, 0.0F, collection_of({hrz_proto::POLYGON_GEOMETRY})},
        {2,
         2,
         {},
         0.0F,
         collection_of({hrz_proto::POLYLINE_GEOMETRY, hrz_proto::POLYGON_GEOMETRY})},
        {19, 2, {}, 0.0F, FeatureFlags::make_simple_feature(hrz_proto::POLYLINE_GEOMETRY)},
    };
    const GeometryPart expected_parts[] = {
        {hrz_proto::POLYLINE_GEOMETRY, 0, 5, 0, 2},
        {hrz_proto::POLYGON_GEOMETRY, 5, 7, 2, 2},
        {hrz_proto::POLYLINE_GEOMETRY, 12, 4, 4, 2},
        {hrz_proto::POLYGON_GEOMETRY, 16, 3, 0, 1},
    };
    // clang-format off
    const lm::dvec3 expected_points[] = {
        {0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {3, 0, 0}, {4, 0, 0},                       // Feature 0
        {0, 0, 0}, {4, 0, 0}, {0, 4, 0}, {1, 1, 0}, {1, 2, 0}, {2, 2, 0}, {2, 1, 0}, // Feature 1
        {5, 5, 0}, {6, 6, 0}, {7, 7, 0}, {8, 8, 0}, {0, 0, 0}, {4, 0, 0}, {0, 4, 0}, // Feature 2
        {9, 9, 0}, {10, 10, 0},                                                      // Feature 3
    };
    const uint32_t expected_linestring_sizes[] = {
        2, 3, // Feature 0
        3, 4, // Feature 1
        2, 2, // Feature 2, polylines
    };
    // clang-format on

    expect_features_eq(geometry->features, expected_features);
    expect_parts_eq(geometry->geometry_parts, expected_parts);
    expect_points_eq(geometry->points, expected_points);
    expect_uints_eq(geometry->linestring_sizes, expected_linestring_sizes);
}

TEST_F(TileGeometryBuilder, geometry_collection)
{
    const auto geometry = build(
        [](VectorTileGeometryBuilder& builder)
        {
            if (!builder.add_geometry_collection(
                    [](VectorTileGeometryBuilder& collection)
                    {
                        if (!collection.add_points_feature(
                                [](PointsBuilder& points)
                                {
                                    points.add_point({1, 1, 0});
                                    return true;
                                }))
                            return false;

                        if (!collection.add_points_feature(
                                [](PointsBuilder& points)
                                {
                                    add_points(points, {{2, 2, 0}, {3, 3, 0}});
                                    return true;
                                }))
                            return false;

                        if (!collection.add_polylines_feature(
                                [](PolylinesBuilder& polylines)
                                {
                                    add_points(polylines, {{0, 0, 0}, {1, 0, 0}});
                                    EXPECT_TRUE(polylines.end_linestring());
                                    add_points(polylines, {{0, 1, 0}, {1, 1, 0}, {2, 1, 0}});
                                    EXPECT_TRUE(polylines.end_linestring());
                                    return true;
                                }))
                            return false;

                        if (!collection.add_polygons_feature(
                                [](PolygonsBuilder& polygons)
                                {
                                    add_points(polygons, {{0, 0, 0}, {4, 0, 0}, {0, 4, 0}});
                                    EXPECT_TRUE(polygons.end_ring());
                                    EXPECT_TRUE(polygons.end_polygon());
                                    return true;
                                }))
                            return false;

                        if (!collection.add_polygons_feature(
                                [](PolygonsBuilder& polygons)
                                {
                                    add_points(polygons, {{0, 0, 0}, {8, 0, 0}, {0, 8, 0}});
                                    EXPECT_TRUE(polygons.end_ring());
                                    add_points(
                                        polygons, {{1, 1, 0}, {1, 2, 0}, {2, 2, 0}, {2, 1, 0}});
                                    EXPECT_TRUE(polygons.end_ring());
                                    EXPECT_TRUE(polygons.end_polygon());
                                    return true;
                                }))
                            return false;

                        if (!collection.add_polygons_feature(
                                [](PolygonsBuilder& polygons)
                                {
                                    add_points(polygons, {{0, 0, 0}, {4, 0, 0}, {0, 4, 0}});
                                    EXPECT_TRUE(polygons.end_ring());
                                    EXPECT_TRUE(polygons.end_polygon());
                                    add_points(polygons, {{10, 0, 0}, {18, 0, 0}, {10, 8, 0}});
                                    EXPECT_TRUE(polygons.end_ring());
                                    add_points(
                                        polygons, {{11, 1, 0}, {11, 2, 0}, {12, 2, 0}, {12, 1, 0}});
                                    EXPECT_TRUE(polygons.end_ring());
                                    EXPECT_TRUE(polygons.end_polygon());
                                    return true;
                                }))
                            return false;

                        if (!collection.add_polylines_feature(
                                [](PolylinesBuilder& polylines)
                                {
                                    add_points(polylines, {{5, 5, 0}, {6, 6, 0}});
                                    EXPECT_TRUE(polylines.end_linestring());
                                    return true;
                                }))
                            return false;
                        return true;
                    }))
                return false;
            return true;
        });
    ASSERT_TRUE(geometry);

    const Feature expected_features[] = {
        {0,
         8,
         {},
         0.0F,
         collection_of(
             {hrz_proto::POINT_GEOMETRY, hrz_proto::POLYLINE_GEOMETRY,
              hrz_proto::POLYGON_GEOMETRY})},
    };
    const GeometryPart expected_parts[] = {
        {hrz_proto::POINT_GEOMETRY, 0, 1, 0, 0},    {hrz_proto::POINT_GEOMETRY, 1, 2, 0, 0},
        {hrz_proto::POLYLINE_GEOMETRY, 3, 5, 0, 2}, {hrz_proto::POLYGON_GEOMETRY, 8, 3, 0, 1},
        {hrz_proto::POLYGON_GEOMETRY, 11, 7, 2, 2}, {hrz_proto::POLYGON_GEOMETRY, 18, 3, 0, 1},
        {hrz_proto::POLYGON_GEOMETRY, 21, 7, 4, 2}, {hrz_proto::POLYLINE_GEOMETRY, 28, 2, 0, 1},
    };
    // clang-format off
    const lm::dvec3 expected_points[] = {
        {1, 1, 0},                                                                   // Point
        {2, 2, 0}, {3, 3, 0},                                                        // Multi-point
        {0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}, {2, 1, 0},                       // Multi-linestring
        {0, 0, 0}, {4, 0, 0}, {0, 4, 0},                                             // Polygon
        {0, 0, 0}, {8, 0, 0}, {0, 8, 0}, {1, 1, 0}, {1, 2, 0}, {2, 2, 0}, {2, 1, 0}, // Polygon with hole
        {0, 0, 0}, {4, 0, 0}, {0, 4, 0}, {10, 0, 0}, {18, 0, 0}, {10, 8, 0},
        {11, 1, 0}, {11, 2, 0}, {12, 2, 0}, {12, 1, 0},                              // Multi-polygon
        {5, 5, 0}, {6, 6, 0},                                                        // Linestring
    };
    const uint32_t expected_linestring_sizes[] = {
        2, 3,   // Multi-linestring
        3, 4,   // Polygon with hole
        3, 4,   // Multi-polygon, polygon 1
    };
    // clang-format on

    expect_features_eq(geometry->features, expected_features);
    expect_parts_eq(geometry->geometry_parts, expected_parts);
    expect_points_eq(geometry->points, expected_points);
    expect_uints_eq(geometry->linestring_sizes, expected_linestring_sizes);
}

TEST_F(TileGeometryBuilder, empty_geometry_collection)
{
    const auto geometry = build(
        [](VectorTileGeometryBuilder& builder)
        {
            if (!builder.add_geometry_collection([](VectorTileGeometryBuilder&) { return true; }))
                return false;
            return true;
        });
    ASSERT_TRUE(geometry);

    const Feature expected_features[] = {
        {0, 0, {}, 0.0F, collection_of({})},
    };

    expect_features_eq(geometry->features, expected_features);
    expect_parts_eq(geometry->geometry_parts, {});
    expect_points_eq(geometry->points, {});
    expect_uints_eq(geometry->linestring_sizes, {});
}

TEST_F(TileGeometryBuilder, nested_empty_geometry_collections)
{
    const auto geometry = build(
        [](VectorTileGeometryBuilder& builder)
        {
            return builder.add_geometry_collection(
                [](VectorTileGeometryBuilder& outer)
                {
                    return outer.add_geometry_collection([](VectorTileGeometryBuilder&)
                                                         { return true; });
                });
        });
    ASSERT_TRUE(geometry);

    const Feature expected_features[] = {
        {0, 0, {}, 0.0F, collection_of({})},
    };

    expect_features_eq(geometry->features, expected_features);
    expect_parts_eq(geometry->geometry_parts, {});
    expect_points_eq(geometry->points, {});
    expect_uints_eq(geometry->linestring_sizes, {});
}

TEST_F(TileGeometryBuilder, empty_nested_geometry_collection_adds_nothing)
{
    const auto geometry = build(
        [](VectorTileGeometryBuilder& builder)
        {
            return builder.add_geometry_collection(
                [](VectorTileGeometryBuilder& outer)
                {
                    if (!outer.add_geometry_collection([](VectorTileGeometryBuilder&)
                                                       { return true; }))
                        return false;
                    if (!outer.add_points_feature(
                            [](PointsBuilder& points)
                            {
                                points.add_point({1, 1, 0});
                                return true;
                            }))
                        return false;
                    return outer.add_polylines_feature(
                        [](PolylinesBuilder& polylines)
                        {
                            add_points(polylines, {{2, 2, 0}, {3, 3, 0}});
                            return true;
                        });
                });
        });
    ASSERT_TRUE(geometry);

    const Feature expected_features[] = {
        {0, 2, {}, 0.0F, collection_of({hrz_proto::POINT_GEOMETRY, hrz_proto::POLYLINE_GEOMETRY})},
    };
    const GeometryPart expected_parts[] = {
        {hrz_proto::POINT_GEOMETRY, 0, 1, 0, 0},
        {hrz_proto::POLYLINE_GEOMETRY, 1, 2, 0, 1},
    };
    const lm::dvec3 expected_points[] = {{1, 1, 0}, {2, 2, 0}, {3, 3, 0}};

    expect_features_eq(geometry->features, expected_features);
    expect_parts_eq(geometry->geometry_parts, expected_parts);
    expect_points_eq(geometry->points, expected_points);
    expect_uints_eq(geometry->linestring_sizes, {});
}

TEST_F(TileGeometryBuilder, empty_geometry_collection_between_features)
{
    const auto geometry = build(
        [](VectorTileGeometryBuilder& builder)
        {
            if (!builder.add_points_feature(
                    [](PointsBuilder& points)
                    {
                        points.add_point({1, 1, 0});
                        return true;
                    }))
                return false;
            if (!builder.add_geometry_collection([](VectorTileGeometryBuilder&) { return true; }))
                return false;
            return builder.add_points_feature(
                [](PointsBuilder& points)
                {
                    points.add_point({2, 2, 0});
                    return true;
                });
        });
    ASSERT_TRUE(geometry);

    const Feature expected_features[] = {
        {0, 1, {}, 0.0F, FeatureFlags::make_simple_feature(hrz_proto::POINT_GEOMETRY)},
        {1, 0, {}, 0.0F, collection_of({})},
        {1, 1, {}, 0.0F, FeatureFlags::make_simple_feature(hrz_proto::POINT_GEOMETRY)},
    };
    const lm::dvec3 expected_points[] = {{1, 1, 0}, {2, 2, 0}};

    expect_features_eq(geometry->features, expected_features);
    expect_parts_eq(geometry->geometry_parts, {});
    expect_points_eq(geometry->points, expected_points);
    expect_uints_eq(geometry->linestring_sizes, {});
}

TEST_F(TileGeometryBuilder, discarded_empty_geometry_collection_between_features)
{
    const auto geometry = build(
        [](VectorTileGeometryBuilder& builder)
        {
            if (!builder.add_points_feature(
                    [](PointsBuilder& points)
                    {
                        points.add_point({1, 1, 0});
                        return true;
                    }))
                return false;

            const auto feature_count_before = builder.feature_count();
            if (!builder.add_geometry_collection(
                    [](VectorTileGeometryBuilder&) { return true; },
                    VectorTileGeometryBuilder::EmptyCollection::Discard))
                return false;
            EXPECT_EQ(builder.feature_count(), feature_count_before);

            return builder.add_points_feature(
                [](PointsBuilder& points)
                {
                    points.add_point({2, 2, 0});
                    return true;
                });
        });
    ASSERT_TRUE(geometry);

    const Feature expected_features[] = {
        {0, 1, {}, 0.0F, FeatureFlags::make_simple_feature(hrz_proto::POINT_GEOMETRY)},
        {1, 1, {}, 0.0F, FeatureFlags::make_simple_feature(hrz_proto::POINT_GEOMETRY)},
    };
    const lm::dvec3 expected_points[] = {{1, 1, 0}, {2, 2, 0}};

    expect_features_eq(geometry->features, expected_features);
    expect_parts_eq(geometry->geometry_parts, {});
    expect_points_eq(geometry->points, expected_points);
    expect_uints_eq(geometry->linestring_sizes, {});
}

TEST_F(TileGeometryBuilder, discarded_geometry_collection_with_empty_contents)
{
    const auto geometry = build(
        [](VectorTileGeometryBuilder& builder)
        {
            return builder.add_geometry_collection(
                [](VectorTileGeometryBuilder& collection)
                {
                    if (!collection.add_points_feature([](PointsBuilder&) { return true; }))
                        return false;
                    if (!collection.add_polylines_feature([](PolylinesBuilder&) { return true; }))
                        return false;
                    if (!collection.add_polygons_feature([](PolygonsBuilder&) { return true; }))
                        return false;
                    return collection.add_geometry_collection([](VectorTileGeometryBuilder&)
                                                              { return true; });
                },
                VectorTileGeometryBuilder::EmptyCollection::Discard);
        });
    ASSERT_TRUE(geometry);

    expect_features_eq(geometry->features, {});
    expect_parts_eq(geometry->geometry_parts, {});
    expect_points_eq(geometry->points, {});
    expect_uints_eq(geometry->linestring_sizes, {});
}

TEST_F(TileGeometryBuilder, discard_keeps_non_empty_geometry_collection)
{
    const auto geometry = build(
        [](VectorTileGeometryBuilder& builder)
        {
            return builder.add_geometry_collection(
                [](VectorTileGeometryBuilder& collection)
                {
                    if (!collection.add_points_feature([](PointsBuilder&) { return true; }))
                        return false;
                    return collection.add_polylines_feature(
                        [](PolylinesBuilder& polylines)
                        {
                            add_points(polylines, {{1, 1, 0}, {2, 2, 0}});
                            return true;
                        });
                },
                VectorTileGeometryBuilder::EmptyCollection::Discard);
        });
    ASSERT_TRUE(geometry);

    const Feature expected_features[] = {
        {0, 2, {}, 0.0F, FeatureFlags::make_simple_feature(hrz_proto::POLYLINE_GEOMETRY)},
    };
    const lm::dvec3 expected_points[] = {{1, 1, 0}, {2, 2, 0}};

    expect_features_eq(geometry->features, expected_features);
    expect_parts_eq(geometry->geometry_parts, {});
    expect_points_eq(geometry->points, expected_points);
    expect_uints_eq(geometry->linestring_sizes, {});
}

TEST_F(TileGeometryBuilder, nested_geometry_collections_are_flattened)
{
    const auto geometry = build(
        [](VectorTileGeometryBuilder& builder)
        {
            if (!builder.add_geometry_collection(
                    [](VectorTileGeometryBuilder& outer)
                    {
                        if (!outer.add_points_feature(
                                [](PointsBuilder& points)
                                {
                                    points.add_point({1, 1, 0});
                                    return true;
                                }))
                            return false;
                        if (!outer.add_geometry_collection(
                                [](VectorTileGeometryBuilder& inner)
                                {
                                    if (!inner.add_points_feature(
                                            [](PointsBuilder& points)
                                            {
                                                add_points(points, {{2, 2, 0}, {3, 3, 0}});
                                                return true;
                                            }))
                                        return false;
                                    if (!inner.add_geometry_collection(
                                            [](VectorTileGeometryBuilder& innermost)
                                            {
                                                if (!innermost.add_polygons_feature(
                                                        [](PolygonsBuilder& polygons)
                                                        {
                                                            add_points(
                                                                polygons,
                                                                {{0, 0, 0}, {8, 0, 0}, {0, 8, 0}});
                                                            EXPECT_TRUE(polygons.end_ring());
                                                            add_points(
                                                                polygons,
                                                                {{1, 1, 0},
                                                                 {1, 2, 0},
                                                                 {2, 2, 0},
                                                                 {2, 1, 0}});
                                                            EXPECT_TRUE(polygons.end_ring());
                                                            EXPECT_TRUE(polygons.end_polygon());
                                                            return true;
                                                        }))
                                                    return false;
                                                return true;
                                            }))
                                        return false;
                                    return true;
                                }))
                            return false;
                        if (!outer.add_points_feature(
                                [](PointsBuilder& points)
                                {
                                    points.add_point({4, 4, 0});
                                    return true;
                                }))
                            return false;
                        return true;
                    }))
                return false;
            if (!builder.add_points_feature(
                    [](PointsBuilder& points)
                    {
                        points.add_point({5, 5, 0});
                        return true;
                    }))
                return false;
            return true;
        });
    ASSERT_TRUE(geometry);

    const Feature expected_features[] = {
        {0, 4, {}, 0.0F, collection_of({hrz_proto::POINT_GEOMETRY, hrz_proto::POLYGON_GEOMETRY})},
        {11, 1, {}, 0.0F, FeatureFlags::make_simple_feature(hrz_proto::POINT_GEOMETRY)},
    };
    const GeometryPart expected_parts[] = {
        {hrz_proto::POINT_GEOMETRY, 0, 1, 0, 0},
        {hrz_proto::POINT_GEOMETRY, 1, 2, 0, 0},
        {hrz_proto::POLYGON_GEOMETRY, 3, 7, 0, 2},
        {hrz_proto::POINT_GEOMETRY, 10, 1, 0, 0},
    };
    // clang-format off
    const lm::dvec3 expected_points[] = {
        {1, 1, 0},
        {2, 2, 0}, {3, 3, 0},
        {0, 0, 0}, {8, 0, 0}, {0, 8, 0}, {1, 1, 0}, {1, 2, 0}, {2, 2, 0}, {2, 1, 0},
        {4, 4, 0},
        {5, 5, 0},
    };
    // clang-format on
    const uint32_t expected_linestring_sizes[] = {3, 4};

    expect_features_eq(geometry->features, expected_features);
    expect_parts_eq(geometry->geometry_parts, expected_parts);
    expect_points_eq(geometry->points, expected_points);
    expect_uints_eq(geometry->linestring_sizes, expected_linestring_sizes);
}

TEST_F(TileGeometryBuilder, reference_tile)
{
    const std::span<const lm::dvec3> pts(reference_tile::points);

    const auto geometry = build(
        [pts](VectorTileGeometryBuilder& builder)
        {
            // Feature 0
            if (!builder.add_points_feature(
                    [pts](PointsBuilder& points)
                    {
                        points.add_point(pts[0]);
                        return true;
                    }))
                return false;

            // Feature 1
            if (!builder.add_points_feature(
                    [pts](PointsBuilder& points)
                    {
                        add_points(points, pts.subspan(1, 3));
                        return true;
                    }))
                return false;

            // Feature 2
            if (!builder.add_polylines_feature(
                    [pts](PolylinesBuilder& polylines)
                    {
                        add_points(polylines, pts.subspan(4, 3));
                        EXPECT_TRUE(polylines.end_linestring());
                        return true;
                    }))
                return false;

            // Feature 3
            if (!builder.add_polylines_feature(
                    [pts](PolylinesBuilder& polylines)
                    {
                        add_points(polylines, pts.subspan(7, 2));
                        EXPECT_TRUE(polylines.end_linestring());
                        add_points(polylines, pts.subspan(9, 3));
                        EXPECT_TRUE(polylines.end_linestring());
                        return true;
                    }))
                return false;

            // Feature 4
            if (!builder.add_polygons_feature(
                    [pts](PolygonsBuilder& polygons)
                    {
                        add_points(polygons, pts.subspan(12, 3));
                        EXPECT_TRUE(polygons.end_ring());
                        EXPECT_TRUE(polygons.end_polygon());
                        return true;
                    }))
                return false;

            // Feature 5
            if (!builder.add_polygons_feature(
                    [pts](PolygonsBuilder& polygons)
                    {
                        add_points(polygons, pts.subspan(15, 3));
                        EXPECT_TRUE(polygons.end_ring());
                        add_points(polygons, pts.subspan(18, 4));
                        EXPECT_TRUE(polygons.end_ring());
                        EXPECT_TRUE(polygons.end_polygon());
                        return true;
                    }))
                return false;

            // Feature 6
            if (!builder.add_polygons_feature(
                    [pts](PolygonsBuilder& polygons)
                    {
                        add_points(polygons, pts.subspan(22, 3));
                        EXPECT_TRUE(polygons.end_ring());
                        EXPECT_TRUE(polygons.end_polygon());
                        add_points(polygons, pts.subspan(25, 4));
                        EXPECT_TRUE(polygons.end_ring());
                        EXPECT_TRUE(polygons.end_polygon());
                        return true;
                    }))
                return false;

            // Feature 7
            if (!builder.add_geometry_collection(
                    [pts](VectorTileGeometryBuilder& collection)
                    {
                        if (!collection.add_points_feature(
                                [pts](PointsBuilder& points)
                                {
                                    points.add_point(pts[29]);
                                    return true;
                                }))
                            return false;
                        if (!collection.add_polylines_feature(
                                [pts](PolylinesBuilder& polylines)
                                {
                                    add_points(polylines, pts.subspan(30, 2));
                                    EXPECT_TRUE(polylines.end_linestring());
                                    add_points(polylines, pts.subspan(32, 2));
                                    EXPECT_TRUE(polylines.end_linestring());
                                    return true;
                                }))
                            return false;
                        return true;
                    }))
                return false;
            return true;
        });
    ASSERT_TRUE(geometry);

    expect_features_eq(geometry->features, reference_tile::features);
    expect_parts_eq(geometry->geometry_parts, reference_tile::geometry_parts);
    expect_points_eq(geometry->points, reference_tile::points);
    expect_uints_eq(geometry->linestring_sizes, reference_tile::linestring_sizes);
}

TEST_F(TileGeometryBuilder, bounds)
{
    const auto geometry = build(
        [](VectorTileGeometryBuilder& builder)
        {
            if (!builder.add_points_feature(
                    [](PointsBuilder& points)
                    {
                        points.add_point({1, 2, 100});
                        return true;
                    }))
                return false;
            if (!builder.add_polylines_feature(
                    [](PolylinesBuilder& polylines)
                    {
                        add_points(polylines, {{-3, 5, 7}, {4, -1, -7}});
                        EXPECT_TRUE(polylines.end_linestring());
                        return true;
                    }))
                return false;
            if (!builder.add_geometry_collection(
                    [](VectorTileGeometryBuilder& collection)
                    {
                        if (!collection.add_polygons_feature(
                                [](PolygonsBuilder& polygons)
                                {
                                    add_points(polygons, {{10, 0, 0}, {11, 0, 0}, {10, 20, 0}});
                                    EXPECT_TRUE(polygons.end_ring());
                                    EXPECT_TRUE(polygons.end_polygon());
                                    return true;
                                }))
                            return false;
                        return true;
                    }))
                return false;
            return true;
        });
    ASSERT_TRUE(geometry);

    EXPECT_DOUBLE_EQ(geometry->bounds.min.x, -3);
    EXPECT_DOUBLE_EQ(geometry->bounds.min.y, -1);
    EXPECT_DOUBLE_EQ(geometry->bounds.max.x, 11);
    EXPECT_DOUBLE_EQ(geometry->bounds.max.y, 20);
}

TEST_F(TileGeometryBuilder, end_exterior_ring_on_first_ring_is_simple_polygon)
{
    const auto geometry = build(
        [](VectorTileGeometryBuilder& builder)
        {
            if (!builder.add_polygons_feature(
                    [](PolygonsBuilder& polygons)
                    {
                        add_points(polygons, {{0, 0, 0}, {4, 0, 0}, {0, 4, 0}});
                        EXPECT_TRUE(polygons.end_exterior_ring());
                        return true;
                    }))
                return false;
            return true;
        });
    ASSERT_TRUE(geometry);
    expect_polygon_tile(*geometry);
}

TEST_F(TileGeometryBuilder, end_polygon_after_end_exterior_ring)
{
    const auto geometry = build(
        [](VectorTileGeometryBuilder& builder)
        {
            if (!builder.add_polygons_feature(
                    [](PolygonsBuilder& polygons)
                    {
                        add_points(polygons, {{0, 0, 0}, {4, 0, 0}, {0, 4, 0}});
                        EXPECT_TRUE(polygons.end_exterior_ring());
                        EXPECT_TRUE(polygons.end_polygon());
                        return true;
                    }))
                return false;
            return true;
        });
    ASSERT_TRUE(geometry);
    expect_polygon_tile(*geometry);
}

TEST_F(TileGeometryBuilder, end_exterior_ring_builds_multi_polygon)
{
    const auto geometry = build(
        [](VectorTileGeometryBuilder& builder)
        {
            if (!builder.add_polygons_feature(
                    [](PolygonsBuilder& polygons)
                    {
                        add_points(polygons, {{0, 0, 0}, {4, 0, 0}, {0, 4, 0}});
                        EXPECT_TRUE(polygons.end_exterior_ring());

                        add_points(polygons, {{10, 0, 0}, {18, 0, 0}, {10, 8, 0}});
                        EXPECT_TRUE(polygons.end_exterior_ring());
                        add_points(polygons, {{11, 1, 0}, {11, 2, 0}, {12, 2, 0}, {12, 1, 0}});
                        EXPECT_TRUE(polygons.end_ring());

                        add_points(polygons, {{20, 0, 0}, {21, 0, 0}, {21, 1, 0}, {20, 1, 0}});
                        EXPECT_TRUE(polygons.end_exterior_ring());
                        return true;
                    }))
                return false;
            return true;
        });
    ASSERT_TRUE(geometry);
    expect_multi_polygon_tile(*geometry);
}

void expect_two_triangles_tile(const VectorTileGeometry& tile)
{
    const Feature expected_features[] = {
        {0, 2, {}, 0.0F, collection_of({hrz_proto::POLYGON_GEOMETRY})},
    };
    const GeometryPart expected_parts[] = {
        {hrz_proto::POLYGON_GEOMETRY, 0, 3, 0, 1},
        {hrz_proto::POLYGON_GEOMETRY, 3, 3, 0, 1},
    };
    const lm::dvec3 expected_points[] = {{0, 0, 0},  {4, 0, 0},  {0, 4, 0},
                                         {10, 0, 0}, {11, 0, 0}, {10, 1, 0}};

    expect_features_eq(tile.features, expected_features);
    expect_parts_eq(tile.geometry_parts, expected_parts);
    expect_points_eq(tile.points, expected_points);
    expect_uints_eq(tile.linestring_sizes, {});
}

TEST_F(TileGeometryBuilder, end_exterior_ring_after_simple_polygon_ring)
{
    const auto geometry = build(
        [](VectorTileGeometryBuilder& builder)
        {
            if (!builder.add_polygons_feature(
                    [](PolygonsBuilder& polygons)
                    {
                        add_points(polygons, {{0, 0, 0}, {4, 0, 0}, {0, 4, 0}});
                        EXPECT_TRUE(polygons.end_ring());
                        add_points(polygons, {{10, 0, 0}, {11, 0, 0}, {10, 1, 0}});
                        EXPECT_TRUE(polygons.end_exterior_ring());
                        return true;
                    }))
                return false;
            return true;
        });
    ASSERT_TRUE(geometry);
    expect_two_triangles_tile(*geometry);
}

TEST_F(TileGeometryBuilder, end_exterior_ring_after_end_polygon)
{
    const auto geometry = build(
        [](VectorTileGeometryBuilder& builder)
        {
            if (!builder.add_polygons_feature(
                    [](PolygonsBuilder& polygons)
                    {
                        add_points(polygons, {{0, 0, 0}, {4, 0, 0}, {0, 4, 0}});
                        EXPECT_TRUE(polygons.end_polygon());
                        add_points(polygons, {{10, 0, 0}, {11, 0, 0}, {10, 1, 0}});
                        EXPECT_TRUE(polygons.end_exterior_ring());
                        return true;
                    }))
                return false;
            return true;
        });
    ASSERT_TRUE(geometry);
    expect_two_triangles_tile(*geometry);
}

TEST_F(TileGeometryBuilder, end_exterior_ring_after_polygon_with_holes)
{
    const auto geometry = build(
        [](VectorTileGeometryBuilder& builder)
        {
            if (!builder.add_polygons_feature(
                    [](PolygonsBuilder& polygons)
                    {
                        add_points(polygons, {{0, 0, 0}, {8, 0, 0}, {0, 8, 0}});
                        EXPECT_TRUE(polygons.end_exterior_ring());
                        add_points(polygons, {{1, 1, 0}, {1, 2, 0}, {2, 2, 0}, {2, 1, 0}});
                        EXPECT_TRUE(polygons.end_ring());
                        add_points(polygons, {{3, 1, 0}, {3, 2, 0}, {4, 1, 0}});
                        EXPECT_TRUE(polygons.end_ring());

                        add_points(polygons, {{10, 0, 0}, {11, 0, 0}, {10, 1, 0}});
                        EXPECT_TRUE(polygons.end_exterior_ring());
                        return true;
                    }))
                return false;
            return true;
        });
    ASSERT_TRUE(geometry);

    const Feature expected_features[] = {
        {0, 2, {}, 0.0F, collection_of({hrz_proto::POLYGON_GEOMETRY})},
    };
    const GeometryPart expected_parts[] = {
        {hrz_proto::POLYGON_GEOMETRY, 0, 10, 0, 3},
        {hrz_proto::POLYGON_GEOMETRY, 10, 3, 0, 1},
    };
    // clang-format off
    const lm::dvec3 expected_points[] = {
        {0, 0, 0}, {8, 0, 0}, {0, 8, 0},            // Polygon 0, exterior
        {1, 1, 0}, {1, 2, 0}, {2, 2, 0}, {2, 1, 0}, // Polygon 0, hole 0
        {3, 1, 0}, {3, 2, 0}, {4, 1, 0},            // Polygon 0, hole 1
        {10, 0, 0}, {11, 0, 0}, {10, 1, 0},         // Polygon 1, exterior
    };
    // clang-format on
    const uint32_t expected_linestring_sizes[] = {3, 4, 3};

    expect_features_eq(geometry->features, expected_features);
    expect_parts_eq(geometry->geometry_parts, expected_parts);
    expect_points_eq(geometry->points, expected_points);
    expect_uints_eq(geometry->linestring_sizes, expected_linestring_sizes);
}

TEST_F(TileGeometryBuilder, end_exterior_ring_without_points_does_nothing)
{
    const auto geometry = build(
        [](VectorTileGeometryBuilder& builder)
        {
            if (!builder.add_polygons_feature(
                    [](PolygonsBuilder& polygons)
                    {
                        add_points(polygons, {{0, 0, 0}, {8, 0, 0}, {0, 8, 0}});
                        EXPECT_TRUE(polygons.end_ring());
                        EXPECT_TRUE(polygons.end_exterior_ring());
                        add_points(polygons, {{1, 1, 0}, {1, 2, 0}, {2, 2, 0}, {2, 1, 0}});
                        EXPECT_TRUE(polygons.end_ring());
                        add_points(polygons, {{3, 1, 0}, {3, 2, 0}, {4, 1, 0}});
                        EXPECT_TRUE(polygons.end_ring());
                        return true;
                    }))
                return false;
            return true;
        });
    ASSERT_TRUE(geometry);
    expect_polygon_with_holes_tile(*geometry);
}

TEST_F(TileGeometryBuilder, end_exterior_ring_in_geometry_collection)
{
    const auto geometry = build(
        [](VectorTileGeometryBuilder& builder)
        {
            if (!builder.add_geometry_collection(
                    [](VectorTileGeometryBuilder& collection)
                    {
                        if (!collection.add_points_feature(
                                [](PointsBuilder& points)
                                {
                                    points.add_point({1, 1, 0});
                                    return true;
                                }))
                            return false;

                        if (!collection.add_polygons_feature(
                                [](PolygonsBuilder& polygons)
                                {
                                    add_points(polygons, {{0, 0, 0}, {4, 0, 0}, {0, 4, 0}});
                                    EXPECT_TRUE(polygons.end_exterior_ring());
                                    add_points(polygons, {{10, 0, 0}, {18, 0, 0}, {10, 8, 0}});
                                    EXPECT_TRUE(polygons.end_exterior_ring());
                                    add_points(
                                        polygons, {{11, 1, 0}, {11, 2, 0}, {12, 2, 0}, {12, 1, 0}});
                                    EXPECT_TRUE(polygons.end_ring());
                                    return true;
                                }))
                            return false;

                        if (!collection.add_polygons_feature(
                                [](PolygonsBuilder& polygons)
                                {
                                    add_points(
                                        polygons, {{20, 0, 0}, {21, 0, 0}, {21, 1, 0}, {20, 1, 0}});
                                    EXPECT_TRUE(polygons.end_exterior_ring());
                                    return true;
                                }))
                            return false;
                        return true;
                    }))
                return false;

            if (!builder.add_polygons_feature(
                    [](PolygonsBuilder& polygons)
                    {
                        add_points(polygons, {{30, 0, 0}, {34, 0, 0}, {30, 4, 0}});
                        EXPECT_TRUE(polygons.end_exterior_ring());
                        return true;
                    }))
                return false;
            return true;
        });
    ASSERT_TRUE(geometry);

    const Feature expected_features[] = {
        {0, 4, {}, 0.0F, collection_of({hrz_proto::POINT_GEOMETRY, hrz_proto::POLYGON_GEOMETRY})},
        {15, 3, {}, 0.0F, FeatureFlags::make_simple_feature(hrz_proto::POLYGON_GEOMETRY)},
    };
    const GeometryPart expected_parts[] = {
        {hrz_proto::POINT_GEOMETRY, 0, 1, 0, 0},
        {hrz_proto::POLYGON_GEOMETRY, 1, 3, 0, 1},
        {hrz_proto::POLYGON_GEOMETRY, 4, 7, 0, 2},
        {hrz_proto::POLYGON_GEOMETRY, 11, 4, 0, 1},
    };
    // clang-format off
    const lm::dvec3 expected_points[] = {
        {1, 1, 0},                                      // Point
        {0, 0, 0}, {4, 0, 0}, {0, 4, 0},                // Polygon 0
        {10, 0, 0}, {18, 0, 0}, {10, 8, 0},             // Polygon 1, exterior
        {11, 1, 0}, {11, 2, 0}, {12, 2, 0}, {12, 1, 0}, // Polygon 1, hole
        {20, 0, 0}, {21, 0, 0}, {21, 1, 0}, {20, 1, 0}, // Polygon 2
        {30, 0, 0}, {34, 0, 0}, {30, 4, 0},             // Top-level polygon
    };
    // clang-format on
    const uint32_t expected_linestring_sizes[] = {3, 4};

    expect_features_eq(geometry->features, expected_features);
    expect_parts_eq(geometry->geometry_parts, expected_parts);
    expect_points_eq(geometry->points, expected_points);
    expect_uints_eq(geometry->linestring_sizes, expected_linestring_sizes);
}

TEST_F(TileGeometryBuilder, end_exterior_ring_from_winding_order)
{
    // clang-format off
    static const std::vector<lm::dvec3> rings[] = {
        {{0, 0, 0}, {8, 0, 0}, {0, 8, 0}},              // Polygon A, exterior (CCW)
        {{1, 1, 0}, {1, 2, 0}, {2, 2, 0}, {2, 1, 0}},   // Polygon A, hole (CW)
        {{10, 0, 0}, {12, 0, 0}, {12, 2, 0}, {10, 2, 0}}, // Polygon B, exterior (CCW)
        {{20, 0, 0}, {30, 0, 0}, {20, 10, 0}},          // Polygon C, exterior (CCW)
        {{21, 1, 0}, {21, 2, 0}, {22, 2, 0}, {22, 1, 0}}, // Polygon C, hole 0 (CW)
        {{23, 1, 0}, {23, 2, 0}, {24, 1, 0}},           // Polygon C, hole 1 (CW)
    };
    // clang-format on

    const auto geometry = build(
        [](VectorTileGeometryBuilder& builder)
        {
            if (!builder.add_polygons_feature(
                    [](PolygonsBuilder& polygons)
                    {
                        for (const auto& ring : rings)
                        {
                            double signed_area = 0.0;
                            for (size_t i = 0; i < ring.size(); ++i)
                            {
                                const auto& a = ring[i];
                                const auto& b = ring[(i + 1) % ring.size()];
                                signed_area += a.x * b.y - b.x * a.y;
                            }

                            add_points(polygons, ring);
                            if (signed_area > 0.0)
                            {
                                EXPECT_TRUE(polygons.end_exterior_ring());
                            }
                            else
                            {
                                EXPECT_TRUE(polygons.end_ring());
                            }
                        }
                        return true;
                    }))
                return false;
            return true;
        });
    ASSERT_TRUE(geometry);

    const Feature expected_features[] = {
        {0, 3, {}, 0.0F, collection_of({hrz_proto::POLYGON_GEOMETRY})},
    };
    const GeometryPart expected_parts[] = {
        {hrz_proto::POLYGON_GEOMETRY, 0, 7, 0, 2},
        {hrz_proto::POLYGON_GEOMETRY, 7, 4, 0, 1},
        {hrz_proto::POLYGON_GEOMETRY, 11, 10, 2, 3},
    };
    std::vector<lm::dvec3> expected_points;
    for (const auto& ring : rings)
    {
        expected_points.insert(expected_points.end(), ring.begin(), ring.end());
    }
    const uint32_t expected_linestring_sizes[] = {
        3, 4,    // Polygon A
        3, 4, 3, // Polygon C
    };

    expect_features_eq(geometry->features, expected_features);
    expect_parts_eq(geometry->geometry_parts, expected_parts);
    expect_points_eq(geometry->points, expected_points);
    expect_uints_eq(geometry->linestring_sizes, expected_linestring_sizes);
}

TEST_F(TileGeometryBuilder, callback_failure_is_propagated)
{
    VectorTileGeometryBuilder builder(_allocator);

    EXPECT_FALSE(builder.add_points_feature([](PointsBuilder&) { return false; }));
    EXPECT_FALSE(builder.add_polylines_feature([](PolylinesBuilder&) { return false; }));
    EXPECT_FALSE(builder.add_polygons_feature([](PolygonsBuilder&) { return false; }));
    EXPECT_FALSE(builder.add_geometry_collection([](VectorTileGeometryBuilder&) { return false; }));
    EXPECT_FALSE(builder.add_geometry_collection(
        [](VectorTileGeometryBuilder& collection)
        { return collection.add_polygons_feature([](PolygonsBuilder&) { return false; }); }));
}

TEST_F(TileGeometryBuilder, build_fails_when_callback_fails)
{
    EXPECT_FALSE(build([](VectorTileGeometryBuilder&) { return false; }));
}

TEST(TileGeometryFeatureFlags, has_geometry)
{
    const hrz_proto::VectorGeometryType types[] = {
        hrz_proto::POINT_GEOMETRY, hrz_proto::POLYLINE_GEOMETRY, hrz_proto::POLYGON_GEOMETRY
    };

    for (const auto type : types)
    {
        SCOPED_TRACE(testing::Message() << "simple " << hrz_proto::VectorGeometryType_Name(type));
        const auto flags = FeatureFlags::make_simple_feature(type);
        for (const auto other : types)
        {
            EXPECT_EQ(flags.has_geometry(other), other == type);
        }
    }

    const auto empty = collection_of({});
    EXPECT_FALSE(empty.has_geometry(hrz_proto::POINT_GEOMETRY));
    EXPECT_FALSE(empty.has_geometry(hrz_proto::POLYLINE_GEOMETRY));
    EXPECT_FALSE(empty.has_geometry(hrz_proto::POLYGON_GEOMETRY));

    const auto polylines = collection_of({hrz_proto::POLYLINE_GEOMETRY});
    EXPECT_FALSE(polylines.has_geometry(hrz_proto::POINT_GEOMETRY));
    EXPECT_TRUE(polylines.has_geometry(hrz_proto::POLYLINE_GEOMETRY));
    EXPECT_FALSE(polylines.has_geometry(hrz_proto::POLYGON_GEOMETRY));

    const auto points_and_polygons =
        collection_of({hrz_proto::POINT_GEOMETRY, hrz_proto::POLYGON_GEOMETRY});
    EXPECT_TRUE(points_and_polygons.has_geometry(hrz_proto::POINT_GEOMETRY));
    EXPECT_FALSE(points_and_polygons.has_geometry(hrz_proto::POLYLINE_GEOMETRY));
    EXPECT_TRUE(points_and_polygons.has_geometry(hrz_proto::POLYGON_GEOMETRY));

    const auto all = collection_of(
        {hrz_proto::POINT_GEOMETRY, hrz_proto::POLYLINE_GEOMETRY, hrz_proto::POLYGON_GEOMETRY});
    EXPECT_TRUE(all.has_geometry(hrz_proto::POINT_GEOMETRY));
    EXPECT_TRUE(all.has_geometry(hrz_proto::POLYLINE_GEOMETRY));
    EXPECT_TRUE(all.has_geometry(hrz_proto::POLYGON_GEOMETRY));

    // The simple feature geometry type is ignored for collections.
    auto points_with_stale_type = collection_of({hrz_proto::POINT_GEOMETRY});
    points_with_stale_type.feature_type_int = static_cast<uint8_t>(hrz_proto::POLYGON_GEOMETRY);
    EXPECT_TRUE(points_with_stale_type.has_geometry(hrz_proto::POINT_GEOMETRY));
    EXPECT_FALSE(points_with_stale_type.has_geometry(hrz_proto::POLYGON_GEOMETRY));
}

namespace
{

std::vector<uint32_t> to_vector(const LinestringSizes& sizes)
{
    return std::vector<uint32_t>(sizes.begin(), sizes.end());
}

} // anonymous namespace

TEST(NormalizeLinestringSizes, points_ignore_linestring_sizes)
{
    const uint32_t sizes[] = {5, 7};
    LinestringSizes out;
    EXPECT_TRUE(normalize_linestring_sizes(hrz_proto::POINT_GEOMETRY, 3, sizes, &out));
    EXPECT_EQ(to_vector(out), (std::vector<uint32_t>{3}));
}

TEST(NormalizeLinestringSizes, empty_sizes_are_a_single_linestring)
{
    LinestringSizes out;

    EXPECT_TRUE(normalize_linestring_sizes(hrz_proto::POLYLINE_GEOMETRY, 4, {}, &out));
    EXPECT_EQ(to_vector(out), (std::vector<uint32_t>{4}));

    EXPECT_TRUE(normalize_linestring_sizes(hrz_proto::POLYGON_GEOMETRY, 5, {}, &out));
    EXPECT_EQ(to_vector(out), (std::vector<uint32_t>{5}));
}

TEST(NormalizeLinestringSizes, explicit_sizes)
{
    const uint32_t sizes[] = {2, 3};
    LinestringSizes out;
    EXPECT_TRUE(normalize_linestring_sizes(hrz_proto::POLYLINE_GEOMETRY, 5, sizes, &out));
    EXPECT_EQ(to_vector(out), (std::vector<uint32_t>{2, 3}));
}

TEST(NormalizeLinestringSizes, extra_points_are_allowed)
{
    const uint32_t sizes[] = {3};
    LinestringSizes out;
    EXPECT_TRUE(normalize_linestring_sizes(hrz_proto::POLYGON_GEOMETRY, 5, sizes, &out));
    EXPECT_EQ(to_vector(out), (std::vector<uint32_t>{3}));
}

TEST(NormalizeLinestringSizes, too_few_points_is_invalid)
{
    const uint32_t sizes[] = {3, 3};
    LinestringSizes out;
    out.push_back(42);
    EXPECT_FALSE(normalize_linestring_sizes(hrz_proto::POLYGON_GEOMETRY, 5, sizes, &out));
    EXPECT_TRUE(out.empty());
}

TEST(NormalizeLinestringSizes, empty_linestrings_are_removed)
{
    const uint32_t sizes[] = {0, 2, 0, 3};
    LinestringSizes out;
    EXPECT_TRUE(normalize_linestring_sizes(hrz_proto::POLYLINE_GEOMETRY, 5, sizes, &out));
    EXPECT_EQ(to_vector(out), (std::vector<uint32_t>{2, 3}));
}

TEST(NormalizeLinestringSizes, no_points)
{
    LinestringSizes out;

    EXPECT_TRUE(normalize_linestring_sizes(hrz_proto::POINT_GEOMETRY, 0, {}, &out));
    EXPECT_TRUE(out.empty());

    EXPECT_TRUE(normalize_linestring_sizes(hrz_proto::POLYLINE_GEOMETRY, 0, {}, &out));
    EXPECT_TRUE(out.empty());
}
