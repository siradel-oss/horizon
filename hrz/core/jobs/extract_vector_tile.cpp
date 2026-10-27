// SPDX-FileCopyrightText: Copyright 2024 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/common/blob_vector.h"
#include "hrz/common/profiling.h"
#include "hrz/common/vector_data/geometry_utils.h"
#include "hrz/common/vector_data/packed_attribute_values_builder.h"
#include "hrz/common/vector_data/tile_geometry.h"
#include "hrz/common/vector_data/tile_geometry_builder.h"
#include "hrz/core/jobs/jobs_declarations.h"
#include "hrz/core/jobs/vector_data_jobs_params.h"
#include "hrz/fnd/inlined_vector.h"

#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace hrz_jobs::extract_vector_tile
{
namespace
{

static constexpr size_t InitialFeatureCapacity = 1024;
static constexpr size_t InitialPointCapacity = 4096;
static constexpr size_t InitialLinestringSizeCapacity = 1024;

// Copyright © 2020 Inigo Quilez
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions: The above copyright
// notice and this permission notice shall be included in all copies or
// substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS IS",
// WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
// TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
// This includes a fix for the case when the segment is a point, which is not
// handled by the original code, but is important for us.
inline double point_to_segment_squared_distance(
    const lm::dvec3& p,
    const lm::dvec3& a,
    const lm::dvec3& b)
{
    const lm::dvec3 ba = b - a;
    const lm::dvec3 pa = p - a;
    const double ba2 = lm::length2(ba);

    if (ba2 > 0.0)
    {
        const double h = hrz::clamp(lm::dot(pa, ba) / ba2, 0.0, 1.0);
        return lm::length2(pa - h * ba);
    }
    else
    {
        return lm::length2(pa);
    }
}

// Douglas–Peucker simplification
void compute_linestring_deviations(std::span<const lm::dvec3> points, std::span<double> deviations)
{
    if (points.size() <= 2) return;

    const auto& a = points.front();
    const auto& b = points.back();

    double max_squared_distance = 0;
    size_t split_index = 0;

    const size_t middle = points.size() / 2;
    size_t min_distance_to_middle = points.size();

    for (size_t i = 1; i < points.size() - 1; i++)
    {
        const auto& p = points[i];
        const double squared_distance = point_to_segment_squared_distance(p, a, b);

        if (squared_distance > max_squared_distance)
        {
            max_squared_distance = squared_distance;
            split_index = i;
        }
        // Splitting the linestring as close to its middle as possible helps decreasing
        // recursion for treacherous inputs that loop over themselves (which is why we end up with
        // identical squared distances).
        // See https://github.com/mapbox/geojson-vt/issues/104
        else if (squared_distance == max_squared_distance)
        {
            const size_t to_middle = (i > middle) ? i - middle : middle - i;
            if (to_middle < min_distance_to_middle)
            {
                min_distance_to_middle = to_middle;
                split_index = i;
            }
        }
    }

    if (split_index > 0)
    {
        compute_linestring_deviations(
            points.subspan(0, split_index + 1), deviations.subspan(0, split_index + 1));
        compute_linestring_deviations(points.subspan(split_index), deviations.subspan(split_index));

        // Only assign a deviation to the most deviated point.
        // The recursive calls will handle the other points as needed.
        deviations[split_index] = max_squared_distance;
    }
}

inline lm::dvec3 intersect(const lm::dvec3& a, const lm::dvec3& b, uint32_t axis, double k)
{
    const double t = (k - a.m[axis]) / (b.m[axis] - a.m[axis]);
    assert(!std::isinf(t));

    auto res = lm::mix(a, b, t);

    // When polygons are clipped, they are shrunk tightly to the tile
    // border. In some cases, such as when there are holes, with inner
    // linestrings, multiple points can be clipped to the same position.
    // Some representation systems, such as those that triangulate
    // polygons, rely on these similar positions having the same exact
    // X and Y coordinates.
    // The operations below ensure the exact tile border values are used.

    res.m[axis] = k;

    for (uint32_t i = 0; i < 3; ++i)
    {
        if (i != axis)
        {
            if (a.m[i] == b.m[i])
            {
                res.m[i] = a.m[i];
            }
        }
    }

    return res;
}

struct ClippedFeature
{
    std::vector<lm::dvec3> points;
    std::vector<size_t> linestring_sizes;
    size_t current_linestring_size = 0;
    std::optional<lm::dvec3> previous_point = std::nullopt; // On the current linestring

    void reset()
    {
        points.clear();
        linestring_sizes.clear();
        current_linestring_size = 0;
        previous_point = std::nullopt;
    }

    inline void push_point(lm::dvec3 point)
    {
        if (previous_point.has_value()
            && lm::length2(point - previous_point.value())
                <= std::numeric_limits<double>::epsilon())
        {
            return;
        }

        points.push_back(point);
        current_linestring_size++;
        previous_point = {point};
    }

    inline void clear_linestring()
    {
        if (current_linestring_size > 0)
        {
            points.resize(points.size() - current_linestring_size);
            current_linestring_size = 0;
            previous_point = std::nullopt;
        }
    }

    inline void end_linestring()
    {
        if (current_linestring_size > 0)
        {
            linestring_sizes.push_back(current_linestring_size);
            current_linestring_size = 0;
            previous_point = std::nullopt;
        }
    }
};

// Clips a set of linestrings in vertical or horizontal "strip".
// When working with polylines, new linestrings must be created when one leaves then
// re-enters the strip.
//
// Adapted from https://github.com/mapbox/geojson-vt/blob/main/src/clip.js
// Though they store the deviation values in the Z component, which we need to keep track
// of point altitudes. So we need to keep track of the original index of clipped points.
void clip_feature(
    const ClippedFeature& in,
    ClippedFeature* out,
    double min,
    double max,
    uint32_t axis,
    bool polygon)
{
    out->reset();

    // Each linestring of the feature should be clipped individually.
    size_t linestring_first_point = 0;
    for (const auto linestring_size : in.linestring_sizes)
    {
        if (linestring_size == 0)
        {
            continue;
        }

        const auto points =
            std::span<const lm::dvec3>(&in.points[linestring_first_point], linestring_size);
        const auto segment_count = (polygon) ? points.size() : (points.size() - 1);

        for (size_t i0 = 0; i0 < segment_count; i0++)
        {
            const auto i1 = (i0 + 1) % points.size();

            const auto& p0 = points[i0];
            const auto& p1 = points[i1];

            if (p0.m[axis] < min)
            {
                // --|-->  |
                if (p1.m[axis] > min)
                {
                    out->push_point(intersect(p0, p1, axis, min));
                }
            }
            else if (p0.m[axis] > max)
            {
                //   |  <--|---
                if (p1.m[axis] < max)
                {
                    out->push_point(intersect(p0, p1, axis, max));
                }
            }
            else
            {
                out->push_point(p0);
            }

            // If a linestring leaves the boundaries, it needs to be split.
            // <-|---  |
            if (p0.m[axis] > min && p1.m[axis] < min)
            {
                out->push_point(intersect(p0, p1, axis, min));
                if (!polygon)
                {
                    out->end_linestring();
                }
            }
            //   |  ---|-->
            else if (p0.m[axis] < max && p1.m[axis] > max)
            {
                out->push_point(intersect(p0, p1, axis, max));
                if (!polygon)
                {
                    out->end_linestring();
                }
            }
        }

        if (!polygon)
        {
            const auto& last = points.back();
            if (last.m[axis] >= min && last.m[axis] <= max)
            {
                out->push_point(last);
            }
        }

        out->end_linestring();
        linestring_first_point += linestring_size;
    }
}

// Simplifies a linestring and appends it to out as a new linestring.
// Returns false if the linestring was discarded entirely.
bool simplify_linestring(
    std::span<const lm::dvec3> src_points,
    double tolerance,
    bool is_polygon,
    std::vector<double>& deviations,
    ClippedFeature* out,
    bool* is_simplified)
{
    if (src_points.empty()) return false;

    lm::dbbox2 linestring_bbox = lm::dbbox2::invalid();
    for (const auto& point : src_points)
    {
        linestring_bbox = lm::expand(linestring_bbox, point.xy);
    }
    if (lm::area(linestring_bbox) < tolerance)
    {
        // The bounding-box is too small, discard the linestring.
        *is_simplified = true;
        return false;
    }

    deviations.clear();
    deviations.resize(src_points.size());
    compute_linestring_deviations(src_points, deviations);

    // Don't simplify away the first and last points.
    out->push_point(src_points.front());
    for (size_t k = 1; k + 1 < src_points.size(); ++k)
    {
        if (deviations[k] >= tolerance)
        {
            out->push_point(src_points[k]);
        }
        else if (src_points.size() >= 3)
        {
            *is_simplified = true;
        }
    }
    out->push_point(src_points.back());

    if (is_polygon && out->current_linestring_size < 3)
    {
        out->clear_linestring();
        *is_simplified = true;
        return false;
    }

    out->end_linestring();
    return true;
}

// Clips all the linestrings of a feature to the tile bounds.
// Polylines can be split in multiple linestrings when they leave and re-enter the tile.
void clip_to_bounds(ClippedFeature* in, const lm::dbbox2& bounds, bool is_polygon)
{
    ClippedFeature tmp;
    clip_feature(*in, &tmp, bounds.min.x, bounds.max.x, 0, is_polygon);
    in->reset();
    clip_feature(tmp, in, bounds.min.y, bounds.max.y, 1, is_polygon);
}

// Simplifies and clips a polyline, then adds the resulting linestrings.
[[nodiscard]]
bool add_extracted_polyline(
    std::span<const lm::dvec3> src_points,
    const lm::dbbox2& tile_bounds,
    double tolerance,
    std::vector<double>& deviations,
    hrz::vector_data::VectorTileGeometryBuilder::PolylinesBuilder& polylines_builder,
    bool* is_simplified)
{
    ClippedFeature clipped;
    if (!simplify_linestring(src_points, tolerance, false, deviations, &clipped, is_simplified))
    {
        return true;
    }

    clip_to_bounds(&clipped, tile_bounds, false);

    size_t first_point = 0;
    for (const size_t linestring_size : clipped.linestring_sizes)
    {
        for (size_t k = 0; k < linestring_size; ++k)
        {
            polylines_builder.add_point(clipped.points[first_point + k]);
        }
        if (!polylines_builder.end_linestring()) return false;
        first_point += linestring_size;
    }

    return true;
}

// Simplifies and clips the rings of a polygon, then adds the resulting polygon.
// The polygon is discarded if its exterior ring is simplified or clipped away.
[[nodiscard]]
bool add_extracted_polygon(
    std::span<const lm::dvec3> src_points,
    std::span<const uint32_t> src_ring_sizes,
    const lm::dbbox2& tile_bounds,
    double tolerance,
    std::vector<double>& deviations,
    hrz::vector_data::VectorTileGeometryBuilder::PolygonsBuilder& polygons_builder,
    bool* is_simplified)
{
    ClippedFeature clipped;

    size_t ring_first_point = 0;
    for (size_t ring_index = 0; ring_index < src_ring_sizes.size(); ++ring_index)
    {
        const uint32_t ring_size = src_ring_sizes[ring_index];
        const auto ring_points = src_points.subspan(ring_first_point, ring_size);
        ring_first_point += ring_size;

        clipped.reset();
        if (!simplify_linestring(ring_points, tolerance, true, deviations, &clipped, is_simplified))
        {
            // Without its exterior ring, the polygon's holes are meaningless.
            if (ring_index == 0) return true;
            continue;
        }

        clip_to_bounds(&clipped, tile_bounds, true);
        if (clipped.linestring_sizes.empty())
        {
            if (ring_index == 0) return true;
            continue;
        }

        // Clipping a ring with a convex shape always results in a single ring.
        for (const auto& point : clipped.points)
        {
            polygons_builder.add_point(point);
        }
        if (!polygons_builder.end_ring()) return false;
    }

    return polygons_builder.end_polygon();
}

// Adds the extracted geometries of a source feature to the current feature of the builder.
// Geometries are grouped by type, so that multi-geometries keep a compact encoding.
[[nodiscard]]
bool add_extracted_feature_geometries(
    const hrz::vector_data::VectorTileGeometry::Reader& src_geometry_reader,
    uint32_t src_feature_index,
    const hrz::vector_data::VectorTileGeometry::FeatureFlags& src_flags,
    const lm::dbbox2& tile_bounds,
    double tolerance,
    std::vector<double>& deviations,
    hrz::vector_data::VectorTileGeometryBuilder& feature_builder,
    bool* is_simplified)
{
    using hrz::vector_data::VectorTileGeometry;
    using hrz::vector_data::VectorTileGeometryBuilder;

    if (src_flags.has_geometry(hrz_proto::VectorGeometryType::POINT_GEOMETRY))
    {
        const bool success = feature_builder.add_points_feature(
            [&](VectorTileGeometryBuilder::PointsBuilder& points_builder)
            {
                src_geometry_reader.iterate_geometries_for_feature(
                    src_feature_index,
                    [&](const VectorTileGeometry::Geometry& src_geometry)
                    {
                        const auto* point =
                            std::get_if<VectorTileGeometry::PointGeometry>(&src_geometry);
                        if (point && lm::contains(tile_bounds, point->point.xy))
                        {
                            points_builder.add_point(point->point);
                        }
                    });
                return true;
            });

        if (!success)
        {
            return false;
        }
    }

    if (src_flags.has_geometry(hrz_proto::VectorGeometryType::POLYLINE_GEOMETRY))
    {
        const bool success = feature_builder.add_polylines_feature(
            [&](VectorTileGeometryBuilder::PolylinesBuilder& polylines_builder)
            {
                bool success = true;
                src_geometry_reader.iterate_geometries_for_feature(
                    src_feature_index,
                    [&](const VectorTileGeometry::Geometry& src_geometry)
                    {
                        if (!success) return;

                        const auto* polyline =
                            std::get_if<VectorTileGeometry::PolylineGeometry>(&src_geometry);
                        if (polyline)
                        {
                            success = add_extracted_polyline(
                                polyline->points, tile_bounds, tolerance, deviations,
                                polylines_builder, is_simplified);
                        }
                    });
                return success;
            });
        if (!success)
        {
            return false;
        }
    }

    if (src_flags.has_geometry(hrz_proto::VectorGeometryType::POLYGON_GEOMETRY))
    {
        const bool success = feature_builder.add_polygons_feature(
            [&](VectorTileGeometryBuilder::PolygonsBuilder& polygons_builder)
            {
                bool success = true;
                src_geometry_reader.iterate_geometries_for_feature(
                    src_feature_index,
                    [&](const VectorTileGeometry::Geometry& src_geometry)
                    {
                        if (!success) return;

                        const auto* polygon =
                            std::get_if<VectorTileGeometry::PolygonGeometry>(&src_geometry);
                        if (polygon)
                        {
                            success = add_extracted_polygon(
                                polygon->points, polygon->ring_sizes, tile_bounds, tolerance,
                                deviations, polygons_builder, is_simplified);
                        }
                    });
                return success;
            });
        if (!success)
        {
            return false;
        }
    }

    return true;
}

} // namespace

hrz_jobs::JobResult run(
    const hrz_jobs::VectorTileExtractionParams& params,
    hrz::vector_data::DecodedVectorTile& extracted_tile,
    const JobContext& context)
{
    HRZ_SCOPED_SAMPLE("extract vector tile job");

    lm::dbbox2 tile_bounds = hrz::mercator_tile_bbox_meters(params.coords);
    if (params.include_clip_margin)
    {
        const lm::dvec2 tile_center = lm::center(tile_bounds);

        lm::dvec2 size = lm::size(tile_bounds);
        size *= 1.1;

        tile_bounds = lm::expand(tile_bounds, tile_center - size * 0.5);
        tile_bounds = lm::expand(tile_bounds, tile_center + size * 0.5);
    }
    tile_bounds = lm::intersection(tile_bounds, hrz::geo_to_web_mercator(params.bounds));

    using hrz::vector_data::VectorTileGeometry;

    const auto src_geometry_reader = params.source_data.geometry.make_reader();
    auto aabb_tree = params.aabb_tree.aabb_tree.get_cdata();

    auto ba = context.get_blob_allocator();

    hrz::vector_data::VectorTileGeometryBuilder builder(
        ba, InitialFeatureCapacity, InitialPointCapacity, InitialLinestringSizeCapacity);
    builder.register_blobs_owner(context.get_resource_owner());

    std::vector<uint32_t> feature_src_indices;
    std::vector<double> deviations; // Reused between features

    // Points with deviation values under this threshold should be simplified away.
    const auto tolerance = hrz::vector_data::compute_vector_tile_tolerance(params.coords.lod)
        * std::pow(2.0F, params.tolerance);

    bool tile_has_full_detail = true;

    hrz::InlinedVector<size_t, 32> search_stack;
    if (!aabb_tree.empty())
    {
        search_stack.push_back(0);
    }

    while (!search_stack.empty())
    {
        const size_t node_index = search_stack.back();
        search_stack.pop_back();

        const auto& node = aabb_tree.at(node_index);

        if (!lm::intersect(node.bbox, tile_bounds))
        {
            continue;
        }

        if (node.has_children)
        {
            search_stack.push_back(node.child_indices[0]);
            search_stack.push_back(node.child_indices[1]);
            continue;
        }

        const auto* src_feature = src_geometry_reader.get_feature(node.feature_index);
        if (!src_feature) continue;

        bool feature_is_simplified = false;

        const auto feature_count_before_opt = builder.feature_count();
        if (!feature_count_before_opt.has_value()) return hrz_jobs::JobResult::FAILURE;

        // Each source feature produces at most one feature, and none if all its geometry was
        // simplified or clipped away.
        const bool success = builder.add_geometry_collection(
            [&](hrz::vector_data::VectorTileGeometryBuilder& feature_builder)
            {
                return add_extracted_feature_geometries(
                    src_geometry_reader, node.feature_index, src_feature->flags, tile_bounds,
                    tolerance, deviations, feature_builder, &feature_is_simplified);
            },
            hrz::vector_data::VectorTileGeometryBuilder::EmptyCollection::Discard);
        if (!success) return hrz_jobs::JobResult::FAILURE;

        if (feature_is_simplified)
        {
            tile_has_full_detail = false;
        }

        const auto feature_count_after_opt = builder.feature_count();
        if (!feature_count_after_opt.has_value()) return hrz_jobs::JobResult::FAILURE;

        assert(feature_count_after_opt.value() - feature_count_before_opt.value() <= 1);
        if (feature_count_after_opt.value() > feature_count_before_opt.value())
        {
            feature_src_indices.push_back(node.feature_index);
        }
    }

    extracted_tile.coords = params.coords;

    auto geometry_opt = std::move(builder).finalize();
    if (!geometry_opt.has_value())
    {
        return hrz_jobs::JobResult::FAILURE;
    }

    // We don't update the anchor of simplified features, which makes their position remain
    // the same between LODs.
    {
        auto features_data = geometry_opt->features.get_mutable_data();
        assert(features_data.size() == feature_src_indices.size());

        for (size_t i = 0; i < features_data.size(); ++i)
        {
            const auto* src_feature = src_geometry_reader.get_feature(feature_src_indices[i]);
            if (!src_feature) continue;

            features_data[i].anchor = src_feature->anchor;
            features_data[i].anchor_angle = src_feature->anchor_angle;
        }
    }

    extracted_tile.geometry = std::move(geometry_opt.value());
    extracted_tile.geometry.has_full_detail = tile_has_full_detail;

    {
        std::vector<hrz::vector_data::PackedAttributeValuesBuilder> attributes;
        attributes.reserve(params.source_data.attributes.size());

        for (size_t i = 0; i < params.source_data.attributes.size(); ++i)
        {
            attributes.emplace_back(feature_src_indices.size(), ba, context.get_resource_owner());
        }

        for (size_t i = 0; i < params.source_data.attributes.size(); ++i)
        {
            auto src_attribute = params.source_data.attributes[i].get_reader();
            for (const uint32_t feature_src_index : feature_src_indices)
            {
                attributes[i].push_ref(src_attribute.as_ref(feature_src_index));
            }
        }

        for (size_t i = 0; i < attributes.size(); ++i)
        {
            auto finalized_attribute =
                attributes[i].finalize(params.source_data.attributes[i].attribute_id);
            if (!finalized_attribute.has_value())
            {
                return hrz_jobs::JobResult::FAILURE;
            }

            extracted_tile.attributes.push_back(std::move(finalized_attribute.value()));
        }
    }

    if (!params.source_data.feature_ids.empty())
    {
        hrz::InlinedVector<hrz::vector_data::AttributeValues, 2> feature_id_attribute_values;

        for (const auto& attribute : extracted_tile.attributes)
        {
            if (params.source_data.feature_ids.has_attribute(attribute.attribute_id))
            {
                feature_id_attribute_values.push_back(attribute);
            }
        }

        auto feature_count = extracted_tile.geometry.features.size();
        hrz::BlobVector<hrz::vector_data::FeatureIdHash> hashes(ba, feature_count);
        hashes.register_blob_metadata("contents"_ss, "feature ID hashes"_ss);
        hashes.register_blob_owner(context.get_resource_owner());
        hashes.resize(feature_count);

        auto hashes_opt = hashes.to_blob_array();
        if (!hashes_opt.has_value())
        {
            return hrz_jobs::JobResult::FAILURE;
        }

        auto feature_ids = hrz::vector_data::FeatureIds::make(
            feature_id_attribute_values, std::move(hashes_opt.value()));
        if (!feature_ids.has_value())
        {
            return hrz_jobs::JobResult::FAILURE;
        }

        extracted_tile.feature_ids = std::move(feature_ids.value());
    }
    else
    {
        extracted_tile.feature_ids = {};
    }

    return hrz_jobs::JobResult::SUCCESS;
}

} // namespace hrz_jobs::extract_vector_tile
