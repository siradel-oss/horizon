// SPDX-FileCopyrightText: Copyright 2025 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/common/blob_array.h"
#include "hrz/fnd/function_ref.h"
#include "hrz/protocol/vector/geometry_type.pb.h"

#include <lin_maths.h>

#include <bit>
#include <cstdint>

namespace hrz::vector_data
{

// clang-format off
//
// This is how geometry is encoded:
//
// | Geometry type  | parts_count | point_count | linestring_count |
// |----------------|-------------|-------------|------------------|
// | Point          | 1           | 1           |                  |
// | Multi-point    | 1           | N           |                  |
// | Polyline       | 1           | N           | 1                |
// | Multi-polyline | 1           | N           | M                |
// | Polygon        | 1           | N           | M                |
// | Multi-polygon  | K           | N           | M                |
// | Collection     | K           |             |                  |
//
// But the `Feature` structure also supports collections, and this allows
// optimising some top-level features (that are not part of a collection). (Note
// that "collection" here is analogous to GeoJSON's `GeometryCollection`, not
// `FeatureCollection`.)
//
// Points, multi-points, polylines, and polygons without holes can be encoded as
// just a span of points and a type, so we don't need to allocate a part: we can
// use first_geometry_part and geometry_part_count as first_point and
// point_count directly on the feature.
//
// | Geometry type    | parts_count | point_count | linestring_count | is_collection |
// |------------------|-------------|-------------|------------------|---------------|
// | Point            | 1           |             |                  | false         |
// | Multi-point      | N           |             |                  | false         |
// | Polyline         | N           |             |                  | false         |
// | Multi-polyline   | 1           | N           | M                | true          |
// | Simple polygon   | N           |             |                  | false         |
// | Polygon w/ holes | 1           | N           | M                | true          |
// | Multi-polygon    | K           | N           | M                | true          |
// | Collection       | K           |             |                  | true          |
//
// Of course, simple geometries inside collections still need to be encoded with
// parts, but this simpler encoding is very useful for the most common cases.
//
// clang-format on
struct VectorTileGeometry
{
    struct GeometryPart
    {
        hrz_proto::VectorGeometryType type;

        // Index of first point in "points".
        uint32_t first_point;
        // Sum of all points for polygons, polylines, and multi-geometries.
        uint32_t point_count;

        // Index of first linestring size in "linestring_sizes".
        // If linestring_count = 1, this is not used, point_count is sufficient.
        // A linestring size is the number of points for each linestring.
        // Linestrings (also called rings for polygons) must not be closed.
        // For polygons, exterior linestring must be counter-clockwise, and interior ones must
        // be clockwise.
        uint32_t first_linestring_size;
        // Number of linestrings for polygons and polylines.
        // Sum of all linestrings for multi-polygons.
        uint32_t linestring_count;
    };

    struct FeatureFlags
    {
        bool is_collection : 1;

        // Geometry type of a simple feature. Not used for collections.
        uint8_t feature_type_int : 2;

        // For collections, one bit per geometry type contained in the collection,
        // indexed by hrz_proto::VectorGeometryType. Not used for simple features.
        uint8_t collection_geometry_types : 3;

        static_assert(hrz_proto::VectorGeometryType_MIN == 0);
        static_assert(hrz_proto::VectorGeometryType_MAX == 2);

        static FeatureFlags make_collection()
        {
            FeatureFlags flags{};
            flags.is_collection = true;
            return flags;
        }

        static FeatureFlags make_simple_feature(hrz_proto::VectorGeometryType type)
        {
            FeatureFlags flags{};
            flags.is_collection = false;
            flags.feature_type_int = static_cast<uint8_t>(type);
            return flags;
        }

        // Returns the primitive geometry type of simple features.
        // If it is a collection of a single geometry type, return that type.
        // Otherwise return nullopt.
        constexpr std::optional<hrz_proto::VectorGeometryType> primitive_geometry_type() const
        {
            if (!is_collection)
            {
                return static_cast<hrz_proto::VectorGeometryType>(feature_type_int);
            }

            if (std::has_single_bit(collection_geometry_types))
            {
                return static_cast<hrz_proto::VectorGeometryType>(
                    std::countr_zero(collection_geometry_types));
            }

            return std::nullopt;
        }

        constexpr bool has_geometry(hrz_proto::VectorGeometryType type) const
        {
            if (!is_collection) return primitive_geometry_type() == type;
            return ((collection_geometry_types >> static_cast<uint8_t>(type)) & 1U) != 0;
        }
    };

    struct Feature
    {
        // Index of first geometry part in "geometry_parts".
        // If is_collection is false, this is equivalent to first_point and point_count.
        // This can be the case for points, multi-points, polylines, and polygons without holes.
        uint32_t first_geometry_part{};
        uint32_t geometry_part_count{};

        // Anchor of the feature in Web Mercator.
        lm::dvec3 anchor;

        // Angle of the feature's anchor in radians, counter-clockwise, 0 is east.
        // This is only non-zero for polylines.
        float anchor_angle{};

        FeatureFlags flags{};
    };

    // Bounds of the geometry, in Web Mercator metres.
    lm::dbbox2 bounds;

    hrz::BlobArray<Feature> features;

    hrz::BlobArray<GeometryPart> geometry_parts;

    // All coordinates must be in Web Mercator (EPSG 3857)
    hrz::BlobArray<lm::dvec3> points;

    // Linestring sizes for polygon and polyline features.
    hrz::BlobArray<uint32_t> linestring_sizes;

    // True if the four children tiles, if they exist, would not provide more information
    // than this tile.
    bool has_full_detail{};

    struct PointGeometry
    {
        lm::dvec3 point;
        uint32_t point_index;
    };

    struct PolylineGeometry
    {
        std::span<const lm::dvec3> points;
        uint32_t first_point_index;
    };

    struct PolygonGeometry
    {
        std::span<const lm::dvec3> points;
        std::span<const uint32_t> ring_sizes;
        uint32_t first_point_index;
    };

    using Geometry = std::variant<PointGeometry, PolylineGeometry, PolygonGeometry>;

    class Reader
    {
        friend struct VectorTileGeometry;

        hrz::BlobArray<Feature>::Data _features;
        hrz::BlobArray<GeometryPart>::Data _geometry_parts;
        hrz::BlobArray<lm::dvec3>::Data _points;
        hrz::BlobArray<uint32_t>::Data _linestring_sizes;

        explicit Reader(const VectorTileGeometry& geometry) :
            _features(geometry.features.get_cdata()),
            _geometry_parts(geometry.geometry_parts.get_cdata()),
            _points(geometry.points.get_cdata()),
            _linestring_sizes(geometry.linestring_sizes.get_cdata())
        {
        }

        void iterate_simple_geometries_for_part(
            uint32_t part_index,
            hrz::function_ref<void(const Geometry&)> callback) const;

    public:
        // Iterate over all simple geometries for a feature, calling the callback for each one.
        void iterate_geometries_for_feature(
            uint32_t feature_index,
            hrz::function_ref<void(const Geometry&)> callback) const;

        const Feature* get_feature(uint32_t feature_index) const;
    };

    Reader make_reader() const { return Reader(*this); }
};

// Computes the anchor and anchor angle of a feature from its geometry.
// The spans must encompass the entire tile, and not be subspans.
void compute_feature_anchor(
    VectorTileGeometry::Feature& feature,
    std::span<const lm::dvec3> points,
    std::span<const uint32_t> linestring_sizes,
    std::span<const VectorTileGeometry::GeometryPart> geometry_parts);

} // namespace hrz::vector_data
