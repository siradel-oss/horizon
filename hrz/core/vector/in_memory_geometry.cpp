// SPDX-FileCopyrightText: Copyright 2026 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/core/vector/in_memory_geometry.h"

#include "hrz/common/vector_data/geometry_utils.h"
#include "hrz/fnd/inlined_vector.h"
#include "hrz/fnd/log.h"
#include "hrz/protocol/vector/geometry.pb.h"
#include "hrz/protocol/vector/geometry_type.pb.h"

#include <limits>
#include <numeric>
#include <optional>

namespace hrz::vector_data::in_memory
{
namespace
{

std::optional<uint32_t> get_minimum_point_count(hrz_proto::VectorGeometryType type)
{
    switch (type)
    {
        case hrz_proto::VectorGeometryType::POINT_GEOMETRY: return 1;
        case hrz_proto::VectorGeometryType::POLYLINE_GEOMETRY: return 2;
        case hrz_proto::VectorGeometryType::POLYGON_GEOMETRY: return 3;
        default: return std::nullopt;
    }
}

struct DecodedGeometry
{
    hrz_proto::VectorGeometryType type;
    uint32_t first_point;
    uint32_t point_count;
    uint32_t first_linestring_size; // In the decoded linestring sizes
    uint32_t linestring_count;
};

} // anonymous namespace

VectorTileGeometry::Feature make_empty_feature()
{
    VectorTileGeometry::Feature feature{};
    feature.anchor = lm::dvec3(std::numeric_limits<double>::quiet_NaN());
    feature.flags = VectorTileGeometry::FeatureFlags::make_collection();
    return feature;
}

FeatureGeometry decode_feature_geometry(
    const hrz_proto::VectorFeature& feature,
    hrz::function_ref<void(std::span<lm::dvec3>)> project)
{
    FeatureGeometry result;
    auto& arrays = result.arrays;

    hrz::InlinedVector<DecodedGeometry, 4> decoded_geometries;
    std::vector<uint32_t> decoded_linestring_sizes;
    LinestringSizes linestring_sizes;

    for (const auto& geometry : feature.geometry())
    {
        const auto type = geometry.type();
        const auto point_count = (uint32_t)(geometry.coords_size() / 3);

        const auto minimum_point_count = get_minimum_point_count(type);
        if (!minimum_point_count.has_value())
        {
            HRZ_LOG_WARNING(
                "Ignoring an in-memory vector geometry with an unknown type: {}", (int)type);
            continue;
        }

        if (point_count < minimum_point_count.value())
        {
            HRZ_LOG_WARNING(
                "Ignoring an in-memory vector geometry with too few points: {}", point_count);
            continue;
        }

        if (!normalize_linestring_sizes(
                type, point_count,
                std::span<const uint32_t>(
                    geometry.linestring_sizes().data(), (size_t)geometry.linestring_sizes_size()),
                &linestring_sizes))
        {
            HRZ_LOG_WARNING(
                "Ignoring an in-memory vector geometry with fewer points than expected by its "
                "linestring sizes.");
            continue;
        }

        if (linestring_sizes.empty()) continue;

        const auto used_point_count =
            std::accumulate(linestring_sizes.begin(), linestring_sizes.end(), 0U);

        decoded_geometries.push_back({
            .type = type,
            .first_point = (uint32_t)arrays.points.size(),
            .point_count = used_point_count,
            .first_linestring_size = (uint32_t)decoded_linestring_sizes.size(),
            .linestring_count = (uint32_t)linestring_sizes.size(),
        });

        for (uint32_t i = 0; i < used_point_count; ++i)
        {
            arrays.points.emplace_back(
                geometry.coords((int)(i * 3 + 0)), geometry.coords((int)(i * 3 + 1)),
                geometry.coords((int)(i * 3 + 2)));
        }

        decoded_linestring_sizes.insert(
            decoded_linestring_sizes.end(), linestring_sizes.begin(), linestring_sizes.end());
    }

    if (decoded_geometries.empty()) return result;

    project(arrays.points);

    for (const auto& point : arrays.points)
    {
        result.bbox = lm::expand(result.bbox, point);
    }

    auto& encoded = result.encoded;
    encoded = VectorTileGeometry::Feature{};

    // Single points, multi-points, single linestrings, and polygons without holes
    // don't need a geometry part.
    if (decoded_geometries.size() == 1 && decoded_geometries[0].linestring_count == 1)
    {
        const auto& decoded = decoded_geometries[0];
        encoded.first_geometry_part = decoded.first_point;
        encoded.geometry_part_count = decoded.point_count;
        encoded.flags = VectorTileGeometry::FeatureFlags::make_simple_feature(decoded.type);
    }
    else
    {
        encoded.first_geometry_part = 0;
        encoded.geometry_part_count = (uint32_t)decoded_geometries.size();
        encoded.flags = VectorTileGeometry::FeatureFlags::make_collection();

        for (const auto& decoded : decoded_geometries)
        {
            const auto first_linestring_size = (uint32_t)arrays.sizes.size();

            // When there is a single linestring, its size isn't stored.
            if (decoded.linestring_count > 1)
            {
                const auto sizes =
                    std::span<const uint32_t>(decoded_linestring_sizes)
                        .subspan(decoded.first_linestring_size, decoded.linestring_count);
                arrays.sizes.insert(arrays.sizes.end(), sizes.begin(), sizes.end());
            }

            // Like the tile geometry builder, point parts have no linestrings.
            const bool is_point = decoded.type == hrz_proto::VectorGeometryType::POINT_GEOMETRY;

            arrays.parts.push_back({
                .type = decoded.type,
                .first_point = decoded.first_point,
                .point_count = decoded.point_count,
                .first_linestring_size = first_linestring_size,
                .linestring_count = is_point ? 0 : decoded.linestring_count,
            });

            encoded.flags.collection_geometry_types =
                (uint8_t)(encoded.flags.collection_geometry_types | (1U << (uint8_t)decoded.type));
        }
    }

    compute_feature_anchor(encoded, arrays.points, arrays.sizes, arrays.parts);

    return result;
}

VectorTileGeometry::Feature append_feature_geometry(
    const FeatureGeometry& geometry,
    GeometryArrays* dst)
{
    const auto& src = geometry.arrays;
    VectorTileGeometry::Feature feature = geometry.encoded;

    if (!feature.flags.is_collection)
    {
        feature.first_geometry_part = (uint32_t)dst->points.size();

        const auto src_points =
            std::span<const lm::dvec3>(src.points)
                .subspan(
                    geometry.encoded.first_geometry_part, geometry.encoded.geometry_part_count);
        dst->points.insert(dst->points.end(), src_points.begin(), src_points.end());

        return feature;
    }

    feature.first_geometry_part = (uint32_t)dst->parts.size();

    for (uint32_t i = 0; i < geometry.encoded.geometry_part_count; ++i)
    {
        auto part = src.parts[geometry.encoded.first_geometry_part + i];

        const auto src_points =
            std::span<const lm::dvec3>(src.points).subspan(part.first_point, part.point_count);
        part.first_point = (uint32_t)dst->points.size();
        dst->points.insert(dst->points.end(), src_points.begin(), src_points.end());

        // When there is a single linestring, its size isn't stored.
        if (part.linestring_count > 1)
        {
            const auto src_sizes = std::span<const uint32_t>(src.sizes).subspan(
                part.first_linestring_size, part.linestring_count);
            part.first_linestring_size = (uint32_t)dst->sizes.size();
            dst->sizes.insert(dst->sizes.end(), src_sizes.begin(), src_sizes.end());
        }
        else
        {
            part.first_linestring_size = (uint32_t)dst->sizes.size();
        }

        dst->parts.push_back(part);
    }

    return feature;
}

} // namespace hrz::vector_data::in_memory
