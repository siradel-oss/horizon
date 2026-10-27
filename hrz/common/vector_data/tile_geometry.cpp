// SPDX-FileCopyrightText: Copyright 2026 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/common/vector_data/tile_geometry.h"

#include "hrz/common/vector_data/geometry_utils.h"
#include "hrz/protocol/vector/geometry_type.pb.h"

#include <limits>

namespace hrz::vector_data
{

void compute_feature_anchor(
    VectorTileGeometry::Feature& feature,
    std::span<const lm::dvec3> points,
    std::span<const uint32_t> linestring_sizes,
    std::span<const VectorTileGeometry::GeometryPart> geometry_parts)
{
    feature.anchor = lm::dvec3(std::numeric_limits<double>::quiet_NaN());
    feature.anchor_angle = 0.0F;

    if (feature.geometry_part_count == 0) return;

    if (!feature.flags.is_collection)
    {
        const auto feature_points =
            points.subspan(feature.first_geometry_part, feature.geometry_part_count);
        switch (feature.flags.primitive_geometry_type().value())
        {
            case hrz_proto::VectorGeometryType::POINT_GEOMETRY:
            {
                feature.anchor = compute_points_center(feature_points);
                break;
            }
            case hrz_proto::VectorGeometryType::POLYLINE_GEOMETRY:
            {
                const uint32_t linestring_size[] = {feature.geometry_part_count};
                compute_linestring_middle_and_angle(
                    feature_points, linestring_size, &feature.anchor, &feature.anchor_angle);
                break;
            }
            case hrz_proto::VectorGeometryType::POLYGON_GEOMETRY:
            {
                feature.anchor = compute_ring_average(feature_points);
                break;
            }
            default: assert(false && "Unhandled case"); break;
        }
    }
    else if (
        feature.flags.primitive_geometry_type() == hrz_proto::POLYGON_GEOMETRY
        && feature.geometry_part_count > 1)
    {
        // For "pure" multi-polygons, we try to be smart and compute the anchor
        // for the polygon with the largest exterior ring.
        auto largest_area = 0.0;
        auto anchor = lm::dvec3(std::numeric_limits<double>::quiet_NaN());

        for (uint32_t i = 0; i < feature.geometry_part_count; ++i)
        {
            const auto& geometry_part = geometry_parts[feature.first_geometry_part + i];
            if (geometry_part.linestring_count == 0) continue;

            const uint32_t first_ring_size = geometry_part.linestring_count < 2
                ? geometry_part.point_count
                : linestring_sizes[geometry_part.first_linestring_size];

            const auto ring_points = points.subspan(geometry_part.first_point, first_ring_size);

            auto ring_bbox = lm::dbbox2::invalid();
            for (const auto& point : ring_points)
            {
                ring_bbox = lm::expand(ring_bbox, point.xy);
            }

            const double area = lm::area(ring_bbox);
            if (lm::is_valid(ring_bbox) && area > largest_area)
            {
                largest_area = area;
                anchor = compute_ring_average(ring_points);
            }
        }

        feature.anchor = anchor;
    }
    else
    {
        // For geometry collections, we do pretty much the same thing, but only
        // for the first part.
        const auto& geometry_part = geometry_parts[feature.first_geometry_part];
        switch (geometry_part.type)
        {
            case hrz_proto::VectorGeometryType::POINT_GEOMETRY:
            {
                const auto feature_points =
                    points.subspan(geometry_part.first_point, geometry_part.point_count);
                feature.anchor = compute_points_center(feature_points);
                break;
            }
            case hrz_proto::VectorGeometryType::POLYLINE_GEOMETRY:
            {
                const auto feature_points =
                    points.subspan(geometry_part.first_point, geometry_part.point_count);
                const uint32_t single_linestring_size[] = {geometry_part.point_count};
                const auto feature_linestring_sizes = geometry_part.linestring_count < 2
                    ? std::span<const uint32_t>(single_linestring_size)
                    : linestring_sizes.subspan(
                          geometry_part.first_linestring_size, geometry_part.linestring_count);

                compute_linestring_middle_and_angle(
                    feature_points, feature_linestring_sizes, &feature.anchor,
                    &feature.anchor_angle);
                break;
            }
            case hrz_proto::VectorGeometryType::POLYGON_GEOMETRY:
            {
                const uint32_t first_ring_size = geometry_part.linestring_count < 2
                    ? geometry_part.point_count
                    : linestring_sizes[geometry_part.first_linestring_size];
                const auto feature_points =
                    points.subspan(geometry_part.first_point, first_ring_size);
                feature.anchor = compute_ring_average(feature_points);
                break;
            }
            default: assert(false && "Unhandled case"); break;
        }
    }
}

const VectorTileGeometry::Feature* VectorTileGeometry::Reader::get_feature(
    uint32_t feature_index) const
{
    if (feature_index >= _features.size())
    {
        return nullptr;
    }

    return &_features[feature_index];
}

void VectorTileGeometry::Reader::iterate_geometries_for_feature(
    uint32_t feature_index,
    hrz::function_ref<void(const Geometry&)> callback) const
{
    if (feature_index >= _features.size())
    {
        return;
    }

    const auto& feature = _features[feature_index];

    if (!feature.flags.is_collection)
    {
        if (feature.geometry_part_count == 0) return;

        switch (feature.flags.primitive_geometry_type().value())
        {
            case hrz_proto::VectorGeometryType::POINT_GEOMETRY:
                for (uint32_t i = 0; i < feature.geometry_part_count; ++i)
                {
                    const PointGeometry point_geom{
                        .point = _points[feature.first_geometry_part + i],
                        .point_index = feature.first_geometry_part + i
                    };
                    callback(point_geom);
                }
                break;
            case hrz_proto::VectorGeometryType::POLYLINE_GEOMETRY:
            {
                const PolylineGeometry polyline_geom{
                    .points = std::span<const lm::dvec3>(
                        &_points[feature.first_geometry_part], feature.geometry_part_count),
                    .first_point_index = feature.first_geometry_part
                };
                callback(polyline_geom);
                break;
            }
            case hrz_proto::VectorGeometryType::POLYGON_GEOMETRY:
            {
                const PolygonGeometry polygon_geom{
                    .points = std::span<const lm::dvec3>(
                        &_points[feature.first_geometry_part], feature.geometry_part_count),
                    .ring_sizes = std::span<const uint32_t>(&feature.geometry_part_count, 1),
                    .first_point_index = feature.first_geometry_part
                };
                callback(polygon_geom);
                break;
            }
            default: assert(false && "Unhandled case"); break;
        }
    }
    else
    {
        for (uint32_t i = 0; i < feature.geometry_part_count; ++i)
        {
            iterate_simple_geometries_for_part(feature.first_geometry_part + i, callback);
        }
    }
}

void VectorTileGeometry::Reader::iterate_simple_geometries_for_part(
    uint32_t part_index,
    hrz::function_ref<void(const Geometry&)> callback) const
{
    if (part_index >= _geometry_parts.size())
    {
        return;
    }

    const auto& part = _geometry_parts[part_index];

    if (part.point_count == 0) return;

    switch (part.type)
    {
        case hrz_proto::POINT_GEOMETRY:
        {
            for (uint32_t i = 0; i < part.point_count; ++i)
            {
                const PointGeometry point_geom{
                    .point = _points[part.first_point + i],
                    .point_index = part.first_point + i
                };
                callback(point_geom);
            }
            break;
        }
        case hrz_proto::POLYLINE_GEOMETRY:
        {
            if (part.linestring_count == 1)
            {
                const auto* first_ptr = &_points[part.first_point];
                const PolylineGeometry polyline_geom{
                    .points = std::span<const lm::dvec3>(first_ptr, part.point_count),
                    .first_point_index = part.first_point
                };
                callback(polyline_geom);
            }
            else
            {
                uint32_t linestring_first_point = part.first_point;
                for (uint32_t i = 0; i < part.linestring_count; ++i)
                {
                    const auto linestring_size = _linestring_sizes[part.first_linestring_size + i];
                    const PolylineGeometry polyline_geom{
                        .points = std::span<const lm::dvec3>(
                            &_points[linestring_first_point], linestring_size),
                        .first_point_index = linestring_first_point
                    };
                    callback(polyline_geom);
                    linestring_first_point += linestring_size;
                }
            }
            break;
        }
        case hrz_proto::POLYGON_GEOMETRY:
        {
            if (part.linestring_count == 1)
            {
                const PolygonGeometry polygon_geom{
                    .points =
                        std::span<const lm::dvec3>(&_points[part.first_point], part.point_count),
                    .ring_sizes = std::span<const uint32_t>(&part.point_count, 1),
                    .first_point_index = part.first_point
                };
                callback(polygon_geom);
            }
            else
            {
                const auto* first_point_ptr = &_points[part.first_point];
                const auto* first_linestring_size_ptr =
                    &_linestring_sizes[part.first_linestring_size];
                const PolygonGeometry polygon_geom{
                    .points = std::span<const lm::dvec3>(first_point_ptr, part.point_count),
                    .ring_sizes =
                        std::span<const uint32_t>(first_linestring_size_ptr, part.linestring_count),
                    .first_point_index = part.first_point
                };
                callback(polygon_geom);
            }
            break;
        }
        default: assert(false && "Unhandled case"); break;
    }
}

} // namespace hrz::vector_data
