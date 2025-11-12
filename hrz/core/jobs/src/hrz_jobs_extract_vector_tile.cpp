#include "hrz_jobs_declarations.h"

#include <hrz_common_attributes.h>
#include <hrz_common_blob_vector.h>
#include <hrz_common_profiling.h>
#include <hrz_common_vector_data.h>
#include <hrz_fnd_inlined_vector.h>
#include <hrz_fnd_log.h>

#include <optional>

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
    *out = {};

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
} // namespace

hrz::JobResult run(
    const hrz::vector_data::VectorTileExtractionParams& params,
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

    auto src_features = params.source_data.geometry.features.get_cdata();
    auto src_points = params.source_data.geometry.points.get_cdata();
    auto src_linestring_sizes = params.source_data.geometry.linestring_sizes.get_cdata();
    auto aabb_tree = params.aabb_tree.aabb_tree.get_cdata();

    auto ba = context.get_blob_allocator();

    auto features =
        hrz::BlobVector<hrz::vector_data::VectorTileGeometry::Feature>(ba, InitialFeatureCapacity);
    auto points = hrz::BlobVector<lm::dvec3>(ba, InitialPointCapacity);
    auto linestring_sizes = hrz::BlobVector<uint32_t>(ba, InitialLinestringSizeCapacity);

    points.register_blob_metadata("contents"_ss, "vector geometry points"_ss);
    linestring_sizes.register_blob_metadata("contents"_ss, "vector geometry linestring sizes"_ss);
    features.register_blob_metadata("contents"_ss, "vector geometry features"_ss);

    points.register_blob_owner(context.get_resource_owner());
    linestring_sizes.register_blob_owner(context.get_resource_owner());
    features.register_blob_owner(context.get_resource_owner());

    std::vector<uint32_t> feature_src_indices;
    std::vector<double> deviations; // Reused between features

    // Points with deviation values under this threshold should be simplified away.
    const auto tolerance = hrz::vector_data::compute_vector_tile_tolerance(params.coords.lod)
        * std::pow(2.0F, params.tolerance);

    uint32_t tile_point_count = 0;
    uint32_t tile_linestring_size_count = 0;
    bool tile_has_full_detail = true;

    lm::dbbox2 tile_geometry_bounds = lm::dbbox2::invalid();

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
        }
        else
        {
            const auto& src_feature = src_features[node.feature_index];

            hrz::vector_data::VectorTileGeometry::Feature feature{};

            // We don't update the anchor of simplified features, which makes their position remain
            // the same between LODs.
            feature.anchor = src_feature.anchor;
            feature.anchor_angle = src_feature.anchor_angle;

            feature.type = src_feature.type;
            feature.first_point = tile_point_count;
            feature.first_linestring_size = tile_linestring_size_count;

            if (feature.type == hrz_proto::VectorGeometryType::POINT_GEOMETRY)
            {
                size_t point_count = 0;
                for (size_t j = 0; j < src_feature.point_count; j++)
                {
                    const auto& point = src_points[src_feature.first_point + j];
                    if (lm::contains(tile_bounds, point.xy))
                    {
                        const auto& point = src_points[src_feature.first_point + j];
                        points.push_back(point);
                        tile_geometry_bounds = lm::expand(tile_geometry_bounds, point.xy);
                        point_count++;
                    }
                }

                feature.point_count = point_count;
                feature.linestring_count = 0;
            }
            else if (
                feature.type == hrz_proto::VectorGeometryType::POLYLINE_GEOMETRY
                || feature.type == hrz_proto::VectorGeometryType::POLYGON_GEOMETRY)
            {
                const bool is_polygon =
                    feature.type == hrz_proto::VectorGeometryType::POLYGON_GEOMETRY;
                bool feature_is_simplified = false;

                ClippedFeature clip_data;

                // Simplify the linestrings
                size_t src_linestring_first_point = src_feature.first_point;
                for (size_t j = 0; j < src_feature.linestring_count; ++j)
                {
                    auto src_linestring_size =
                        src_linestring_sizes[src_feature.first_linestring_size + j];

                    const std::span<const lm::dvec3> src_linestring_points_span =
                        src_points.as_span().subspan(
                            src_linestring_first_point, src_linestring_size);

                    src_linestring_first_point += src_linestring_size;

                    lm::dbbox2 linestring_bbox = lm::dbbox2::invalid();
                    for (const auto& point : src_linestring_points_span)
                    {
                        linestring_bbox = lm::expand(linestring_bbox, point.xy);
                    }
                    if (lm::area(linestring_bbox) < tolerance)
                    {
                        // The bounding-box is too small, discard the linestring.
                        feature_is_simplified = true;
                        tile_has_full_detail = false;
                        continue;
                    }

                    deviations.clear();
                    deviations.resize(src_linestring_size);
                    compute_linestring_deviations(src_linestring_points_span, deviations);

                    // Don't simplify away the first and last points.
                    clip_data.push_point(src_linestring_points_span[0]);
                    for (size_t k = 1; k < src_linestring_size - 1; ++k)
                    {
                        if (deviations[k] >= tolerance)
                        {
                            clip_data.push_point(src_linestring_points_span[k]);
                        }
                        else if (src_linestring_size >= 3)
                        {
                            feature_is_simplified = true;
                        }
                    }
                    clip_data.push_point(src_linestring_points_span[src_linestring_size - 1]);

                    if (is_polygon && clip_data.current_linestring_size < 3)
                    {
                        clip_data.clear_linestring();
                        tile_has_full_detail = false;
                    }
                    else
                    {
                        clip_data.end_linestring();
                    }
                }

                ClippedFeature clipped;

                clip_feature(
                    clip_data, &clipped, tile_bounds.min.x, tile_bounds.max.x, 0, is_polygon);
                std::swap(clip_data, clipped);
                clip_feature(
                    clip_data, &clipped, tile_bounds.min.y, tile_bounds.max.y, 1, is_polygon);

                // Data is now ready
                size_t clipped_linestring_first_point = 0;
                for (const auto& clipped_linestring_size : clipped.linestring_sizes)
                {
                    if (clipped_linestring_size == 0)
                    {
                        continue;
                    }

                    for (size_t k = 0; k < clipped_linestring_size; ++k)
                    {
                        const auto& point = clipped.points[clipped_linestring_first_point + k];
                        points.push_back(point);
                        tile_geometry_bounds = lm::expand(tile_geometry_bounds, point.xy);
                    }

                    linestring_sizes.push_back(clipped_linestring_size);
                    feature.linestring_count++;
                    feature.point_count += clipped_linestring_size;

                    if (feature_is_simplified)
                    {
                        tile_has_full_detail = false;
                    }

                    clipped_linestring_first_point += clipped_linestring_size;
                }
            }
            else
            {
                assert(false && "Unhandled");
                return hrz::JobResult::FAILURE;
            }

            if (feature.point_count > 0)
            {
                features.push_back(feature);
                feature_src_indices.push_back(node.feature_index);

                tile_point_count += feature.point_count;
                tile_linestring_size_count += feature.linestring_count;
            }
        }
    }

    extracted_tile.coords = params.coords;

    auto points_array_opt = points.to_blob_array();
    auto linestring_sizes_array_opt = linestring_sizes.to_blob_array();
    auto features_array_opt = features.to_blob_array();

    if (!points_array_opt.has_value() || !linestring_sizes_array_opt.has_value()
        || !features_array_opt.has_value())
    {
        return hrz::JobResult::FAILURE;
    }

    auto points_array = points_array_opt.value();
    auto linestring_sizes_array = linestring_sizes_array_opt.value();
    auto features_array = features_array_opt.value();

    extracted_tile.geometry.points = std::move(points_array);
    extracted_tile.geometry.linestring_sizes = std::move(linestring_sizes_array);
    extracted_tile.geometry.features = std::move(features_array);
    extracted_tile.geometry.bounds = tile_geometry_bounds;
    extracted_tile.geometry.has_full_detail = tile_has_full_detail;

    {
        std::vector<hrz::vector_data::AttributeValuesBuilder> attributes;
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
                return hrz::JobResult::FAILURE;
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

        auto feature_count = features_array.size();
        hrz::BlobVector<hrz::vector_data::FeatureIdHash> hashes(ba, feature_count);
        hashes.register_blob_metadata("contents"_ss, "feature ID hashes"_ss);
        hashes.register_blob_owner(context.get_resource_owner());
        hashes.resize(feature_count);

        auto hashes_opt = hashes.to_blob_array();
        if (!hashes_opt.has_value())
        {
            return hrz::JobResult::FAILURE;
        }

        auto feature_ids = hrz::vector_data::FeatureIds::make(
            feature_id_attribute_values, std::move(hashes_opt.value()));
        if (!feature_ids.has_value())
        {
            return hrz::JobResult::FAILURE;
        }

        extracted_tile.feature_ids = std::move(feature_ids.value());
    }
    else
    {
        extracted_tile.feature_ids = {};
    }

    return hrz::JobResult::SUCCESS;
}
} // namespace hrz_jobs::extract_vector_tile
