// SPDX-FileCopyrightText: Copyright 2021 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/core/shape_editor.h"

#include "hrz/common/color.h"
#include "hrz/common/geo.h"
#include "hrz/common/maths.h"
#include "hrz/common/monitoring_defs.h"
#include "hrz/common/profiling.h"
#include "hrz/common/triangulation.h" // IWYU pragma: keep
#include "hrz/core/camera_height.h"
#include "hrz/core/client_message_queue.h"
#include "hrz/core/client_messages.h"
#include "hrz/core/events.h"
#include "hrz/core/gestures.h"
#include "hrz/core/global_flags.h"
#include "hrz/core/picking_id_allocator.h"
#include "hrz/core/picking_system.h"
#include "hrz/core/planet/geometry.h"
#include "hrz/core/render/context.h"
#include "hrz/core/render/defs.h"
#include "hrz/core/render/resource_context.h"
#include "hrz/core/render/resources.h"
#include "hrz/core/shaders/collection.h"
#include "hrz/fnd/array_view.h"
#include "hrz/fnd/flat_hash_map.h"
#include "hrz/fnd/flat_hash_set.h"
#include "hrz/fnd/gen_object_pool.h"
#include "hrz/fnd/log.h"
#include "hrz/fnd/maths.h"
#include "hrz/fnd/mem.h"
#include "hrz/protocol/path_builder/layer/editable_shape_layer.h"
#include "hrz/protocol/shape_editor/message.pb.h"

#include <earcut.hpp>

#include <cassert>
#include <cmath>
#include <deque>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace
{

struct Edge
{
    size_t from;
    size_t to;

    constexpr bool operator ==(const Edge& other) const = default;

    static Edge create(size_t index0, size_t index1)
    {
        Edge edge;
        edge.from = std::min(index0, index1);
        edge.to = std::max(index0, index1);
        return edge;
    }

    template<typename H>
    friend H AbslHashValue(H h, const Edge& edge)
    {
        return H::combine(std::move(h), edge.from, edge.to);
    }
};

constexpr double MaxSegmentLength = 100000; // metres
constexpr double MaxSegmentAngularLength = lm::radians(4.0);
constexpr size_t MaxPolygonTriangles = (size_t)1 << 20;
// Low enough to avoid artefacts at min zoom.
constexpr double MeshDownOffset = -2000; // metres
// High enough to avoid clipping Mount Everest.
constexpr double MeshUpOffset = 10000; // metres
// No need to compensate for the planet's curvature here.
constexpr double ControlBboxDownOffset = -500; // metres
constexpr double ControlBboxUpOffset = 9000;   // metres
constexpr uint32_t NoControlPoint = 0xffffffff;
constexpr float ClickMaxDistance = 24; // pixels
constexpr float ClickMaxDistanceSquared = ClickMaxDistance * ClickMaxDistance;

enum class LineType
{
    Geodesics,
    RhumbLines,
    RhumbLinesNotAcrossAntimeridian,
};

hrz::GeoPosition2 normalize_to_wmerc(const hrz::GeoPosition2& geo)
{
    return {
        hrz::clamp(geo.lat, -hrz::MERCATOR_MAX_LAT, hrz::MERCATOR_MAX_LAT),
        hrz::normalize_longitude(geo.lon)
    };
}

hrz::GeoPosition2 normalize_position(const hrz::GeoPosition2& geo, LineType line_type)
{
    if (line_type == LineType::RhumbLines || line_type == LineType::RhumbLinesNotAcrossAntimeridian)
    {
        return normalize_to_wmerc(geo);
    }
    else
    {
        return hrz::normalize(geo);
    }
}

double angular_distance(
    const hrz::GeoPosition2& geo_a,
    const hrz::GeoPosition2& geo_b,
    bool allow_antimeridian_crossing)
{
    auto ll_a = lm::dvec2(geo_a.lon, geo_a.lat);
    auto ll_b = lm::dvec2(geo_b.lon, geo_b.lat);

    auto diff = ll_b - ll_a;

    if (allow_antimeridian_crossing && std::abs(geo_b.lon - geo_a.lon) > lm::radians(180.0))
    {
        // The segment goes across the antimeridian.
        diff.x = std::abs(std::abs(diff.x) - lm::radians(360.0));
    }

    return lm::length(diff);
}

bool segment_crosses_antimeridian(
    const hrz::GeoPosition2& geo_a,
    const hrz::GeoPosition2& geo_b,
    LineType line_type)
{
    if (line_type == LineType::RhumbLinesNotAcrossAntimeridian) return false;

    return std::abs(geo_b.lon - geo_a.lon) > lm::radians(180.0);
}

struct Mesh
{
    std::vector<lm::vec3> vertex_data;
    std::vector<uint32_t> indices;
    lm::dvec3 center = {0, 0, 0};
    double radius = 0;
    bool is_degenerate = true;
};

Mesh generate_point_mesh(const hrz::GeoPosition2& position)
{
    HRZ_SCOPED_SAMPLE("generate point mesh");

    constexpr size_t subdivisions = 16;
    constexpr size_t point_count = subdivisions * 2 + 2;

    std::vector<lm::vec3> vbo_data(point_count * 5);

    hrz::ArrayView<lm::vec3> ecef_positions_low(
        vbo_data.data() + 0, point_count, sizeof(lm::vec3) * 5);
    hrz::ArrayView<lm::vec3> ecef_positions_high(
        vbo_data.data() + 1, point_count, sizeof(lm::vec3) * 5);
    hrz::ArrayView<lm::vec3> normals(vbo_data.data() + 2, point_count, sizeof(lm::vec3) * 5);
    hrz::ArrayView<lm::vec3> bisectors(vbo_data.data() + 3, point_count, sizeof(lm::vec3) * 5);
    hrz::ArrayView<lm::vec3> extrusion_params(
        vbo_data.data() + 4, point_count, sizeof(lm::vec3) * 5);

    // ECEF positions
    lm::dvec3 ground_normal = hrz::geo_to_normal(position);
    lm::dvec3 down_offset = ground_normal * MeshDownOffset;
    lm::dvec3 up_offset = ground_normal * MeshUpOffset;

    lm::dvec3 ecef_center = hrz::geo_to_ecef(position);
    lm::dvec3 ecef_center_bottom = ecef_center + down_offset;
    lm::dvec3 ecef_center_top = ecef_center + up_offset;

    {
        lm::vec3 ecef_center_bottom_low = {0, 0, 0};
        lm::vec3 ecef_center_bottom_high = {0, 0, 0};
        hrz::split_double(
            ecef_center_bottom.x, ecef_center_bottom_low.x, ecef_center_bottom_high.x);
        hrz::split_double(
            ecef_center_bottom.y, ecef_center_bottom_low.y, ecef_center_bottom_high.y);
        hrz::split_double(
            ecef_center_bottom.z, ecef_center_bottom_low.z, ecef_center_bottom_high.z);

        lm::vec3 ecef_center_top_low = {0, 0, 0};
        lm::vec3 ecef_center_top_high = {0, 0, 0};
        hrz::split_double(ecef_center_top.x, ecef_center_top_low.x, ecef_center_top_high.x);
        hrz::split_double(ecef_center_top.y, ecef_center_top_low.y, ecef_center_top_high.y);
        hrz::split_double(ecef_center_top.z, ecef_center_top_low.z, ecef_center_top_high.z);

        ecef_positions_low[0] = ecef_center_bottom_low;
        ecef_positions_high[0] = ecef_center_bottom_high;

        ecef_positions_low[1] = ecef_center_top_low;
        ecef_positions_high[1] = ecef_center_top_high;

        for (size_t i = 0; i < subdivisions; ++i)
        {
            ecef_positions_low[2 + i] = ecef_center_bottom_low;
            ecef_positions_high[2 + i] = ecef_center_bottom_high;

            ecef_positions_low[2 + subdivisions + i] = ecef_center_top_low;
            ecef_positions_high[2 + subdivisions + i] = ecef_center_top_high;
        }
    }

    // Normals, bisectors, and extrusion params
    lm::dmat4 enu_to_ecef = hrz::enu_to_ecef_transform_for_geo(position);

    normals[0] = {0, 0, 0};
    normals[1] = {0, 0, 0};

    bisectors[0] = {0, 0, 0};
    bisectors[1] = {0, 0, 0};

    extrusion_params[0] = {0, 0, 0};
    extrusion_params[1] = {0, 0, 0};

    for (size_t i = 0; i < subdivisions; ++i)
    {
        double angle = ((lm::PI * 2) / subdivisions) * i;
        double x = std::cos(angle);
        double y = std::sin(angle);
        lm::vec3 normal_ecef = lm::vec3((enu_to_ecef * lm::dvec4(x, y, 0, 0)).xyz);

        normals[2 + i] = normal_ecef;
        normals[2 + subdivisions + i] = normal_ecef;

        bisectors[2 + i] = normal_ecef;
        bisectors[2 + subdivisions + i] = normal_ecef;

        extrusion_params[2 + i] = {1, 0, 0};
        extrusion_params[2 + subdivisions + i] = {1, 0, 0};
    }

    // Indices
    std::vector<uint32_t> indices;
    indices.reserve((subdivisions * 4) * 3);

    for (size_t i = 0; i < subdivisions; ++i)
    {
        uint32_t center_bottom = 0;
        uint32_t center_top = 1;
        uint32_t bottom = 2 + i;
        uint32_t next_bottom = 2 + ((i + 1) % subdivisions);
        uint32_t top = bottom + subdivisions;
        uint32_t next_top = next_bottom + subdivisions;

        indices.push_back(bottom);
        indices.push_back(next_bottom);
        indices.push_back(next_top);

        indices.push_back(bottom);
        indices.push_back(next_top);
        indices.push_back(top);

        indices.push_back(center_bottom);
        indices.push_back(next_bottom);
        indices.push_back(bottom);

        indices.push_back(center_top);
        indices.push_back(top);
        indices.push_back(next_top);
    }

    lm::dvec3 ecef_positions[] = {ecef_center_bottom, ecef_center_top};
    auto b_sphere = hrz::compute_bounding_sphere(std::span<const lm::dvec3>{ecef_positions, 2});

    Mesh mesh;
    mesh.vertex_data = std::move(vbo_data);
    mesh.indices = std::move(indices);
    mesh.center = b_sphere.center;
    mesh.radius = b_sphere.radius;
    mesh.is_degenerate = false;

    return mesh;
}

template<typename TCollector>
void subdivide_rhumb_line(
    const hrz::GeoPosition2& geo_a,
    const hrz::GeoPosition2& geo_b,
    const lm::dvec2& wmerc_a,
    const lm::dvec2& wmerc_b,
    bool allow_antimeridian_crossing,
    TCollector position_collector)
{
    double distance = angular_distance(geo_a, geo_b, allow_antimeridian_crossing);

    if (distance > MaxSegmentAngularLength)
    {
        lm::dvec2 midpoint_wmerc;
        if (!allow_antimeridian_crossing
            || std::abs(wmerc_b.x - wmerc_a.x) <= hrz::HALF_MERCATOR_RANGE)
        {
            midpoint_wmerc = lm::mix(wmerc_a, wmerc_b, 0.5);
        }
        else
        {
            // The segment goes across the antimeridian.

            if (wmerc_a.x < wmerc_b.x)
            {
                auto a = wmerc_a;
                a.x += hrz::MERCATOR_RANGE;
                midpoint_wmerc = lm::mix(a, wmerc_b, 0.5);
            }
            else
            {
                auto b = wmerc_b;
                b.x += hrz::MERCATOR_RANGE;
                midpoint_wmerc = lm::mix(wmerc_a, b, 0.5);
            }

            if (midpoint_wmerc.x > hrz::HALF_MERCATOR_RANGE)
            {
                midpoint_wmerc.x -= hrz::MERCATOR_RANGE;
            }
            else if (midpoint_wmerc.x < -hrz::HALF_MERCATOR_RANGE)
            {
                midpoint_wmerc.x += hrz::MERCATOR_RANGE;
            }
        }

        hrz::GeoPosition2 midpoint_geo = hrz::web_mercator_to_geo2(midpoint_wmerc);

        subdivide_rhumb_line(
            geo_a, midpoint_geo, wmerc_a, midpoint_wmerc, allow_antimeridian_crossing,
            position_collector);
        subdivide_rhumb_line(
            midpoint_geo, geo_b, midpoint_wmerc, wmerc_b, allow_antimeridian_crossing,
            position_collector);
    }
    else
    {
        position_collector.append(geo_b);
    }
}

template<typename TCollector>
void subdivide_rhumb_line(
    const hrz::GeoPosition2& geo_a,
    const hrz::GeoPosition2& geo_b,
    TCollector position_collector,
    bool allow_antimeridian_crossing,
    bool append_first_position = true)
{
    if (append_first_position)
    {
        position_collector.append(geo_a);
    }

    subdivide_rhumb_line(
        geo_a, geo_b, hrz::geo_to_web_mercator(geo_a), hrz::geo_to_web_mercator(geo_b),
        allow_antimeridian_crossing, position_collector);
}

struct GeoPositionVectorCollector
{
    std::vector<hrz::GeoPosition2>& geo_positions;

    void append(const hrz::GeoPosition2& p) { geo_positions.push_back(p); }
};

Mesh generate_polyline_mesh(
    std::span<const hrz::GeoPosition2> input_positions,
    bool loop,
    LineType line_type)
{
    HRZ_SCOPED_SAMPLE("generate polyline mesh");

    // Subdivide each segment into short enough sub-segments that the curvature
    // of the planet is not an issue.
    // Each point is duplicated four times. Original input points are duplicated
    // eight times.
    // Normals are computed for each point. It gives the direction in which the
    // points should be moved in order to give width to the line.
    // Bisectors are computed, they average the normals of the two neighbouring
    // sub-segments and are used to enhance the extrusion when the line makes
    // an angle.
    // Then for each quadruplet, two points are extruded downwards, the other
    // two are extruded upwards. This create a band that follows the curvature
    // of the planet, always intersecting the surface.
    // The indices are computed in order to wrap each sub-segment by four quads.
    // End caps are added if the line does not loop.
    // In the vertex shader, the points are moved along their normals, or the
    // bisectors.

    if (input_positions.empty())
    {
        Mesh mesh;
        mesh.is_degenerate = true;
        return mesh;
    }

    if (input_positions.size() == 1)
    {
        return generate_point_mesh(input_positions[0]);
    }

    Mesh mesh;

    bool do_loop = loop && input_positions.size() >= 3;

    // Geodesic subdivision
    auto subdivide_geodesic = [](const hrz::GeoPosition2& geo_a, const hrz::GeoPosition2& geo_b,
                                 std::vector<hrz::GeoPosition2>& positions)
    {
        positions.push_back(geo_a);

        double distance = hrz::geodesic_distance(geo_a, geo_b);
        size_t segments = std::ceil(distance / MaxSegmentLength);

        for (size_t i = 1; i < segments; ++i)
        {
            double t = (double)i / (double)segments;
            positions.push_back(hrz::geodesic_interpolation(geo_a, geo_b, t));
        }

        positions.push_back(geo_b);
    };

    auto subdivide = [&](const hrz::GeoPosition2& geo_a, const hrz::GeoPosition2& geo_b,
                         std::vector<hrz::GeoPosition2>& positions)
    {
        if (line_type == LineType::RhumbLines
            || line_type == LineType::RhumbLinesNotAcrossAntimeridian)
        {
            GeoPositionVectorCollector collector{positions};
            subdivide_rhumb_line<GeoPositionVectorCollector>(
                geo_a, geo_b, collector, line_type == LineType::RhumbLines);
        }
        else
        {
            subdivide_geodesic(geo_a, geo_b, positions);
        }
    };

    std::vector<hrz::GeoPosition2> positions_geo;
    std::vector<size_t> line_boundary_indices = {0};

    for (size_t i = 0; i < input_positions.size() - 1; ++i)
    {
        const auto& from = input_positions[i];
        const auto& to = input_positions[i + 1];

        if (from == to) continue;

        subdivide(from, to, positions_geo);
        line_boundary_indices.push_back(positions_geo.size());
    }

    if (positions_geo.empty())
    {
        mesh.is_degenerate = true;
        return mesh;
    }

    if (do_loop)
    {
        subdivide(input_positions.back(), input_positions.front(), positions_geo);
        line_boundary_indices.push_back(positions_geo.size());
    }

    size_t latlon_point_count = positions_geo.size();
    std::vector<lm::vec3> vbo_data(latlon_point_count * 4 * 5);

    // Lat-lon -> ECEF
    std::vector<lm::dvec3> ecef_positions;
    ecef_positions.resize(latlon_point_count * 4);

    for (size_t i = 0; i < latlon_point_count; ++i)
    {
        const hrz::GeoPosition2& geo = positions_geo.at(i);
        const lm::dvec3 ecef = hrz::geo_to_ecef(geo);

        ecef_positions[i * 4 + 0] = ecef;
        ecef_positions[i * 4 + 1] = ecef;
        ecef_positions[i * 4 + 2] = ecef;
        ecef_positions[i * 4 + 3] = ecef;
    }

    //
    //               ╱
    //     normal   ╱
    //          ⇖  ╱
    //            ×─────────
    //          ⇙ ⇓
    // bisector   normal

    // Normals
    hrz::ArrayView<lm::vec3> normals(
        vbo_data.data() + 2, latlon_point_count * 4, sizeof(lm::vec3) * 5);
    hrz::ArrayView<lm::vec3> bisectors(
        vbo_data.data() + 3, latlon_point_count * 4, sizeof(lm::vec3) * 5);
    hrz::ArrayView<lm::vec3> extrusion_params(
        vbo_data.data() + 4, latlon_point_count * 4, sizeof(lm::vec3) * 5);

    // Extrusion params:
    //  * x: 0: No extrusion,
    //       1: Extrude vertex.
    //  * y: 0: Do not limit bisector extrusion,
    //       1: Limit bisector extrusion (outer angles),
    //      -1: Do not use bisectors, only normals (inner angles).
    //  * z: unused.

    for (size_t l = 0; l < line_boundary_indices.size() - 1; ++l)
    {
        size_t line_start = line_boundary_indices.at(l);
        size_t line_end = line_boundary_indices.at(l + 1);

        for (size_t i = line_start; i < line_end; ++i)
        {
            size_t index = i == line_end - 1 ? i - 1 : i;

            const lm::dvec3& ecef = ecef_positions.at(index * 4);
            const lm::dvec3& next_ecef = ecef_positions.at((index + 1) * 4);

            lm::dvec3 ground_normal = hrz::geo_to_normal(positions_geo.at(i));

            lm::vec3 normal = lm::vec3(lm::normalize(lm::cross(next_ecef - ecef, ground_normal)));

            if (std::isnan(normal.x) || std::isnan(normal.y) || std::isnan(normal.z))
            {
                // This can happen if two points are at the same positions.
                normal = {0, 0, 0};
            }

            normals[i * 4 + 0] = normal;
            normals[i * 4 + 1] = normal;
            normals[i * 4 + 2] = -normal;
            normals[i * 4 + 3] = -normal;

            bisectors[i * 4 + 0] = normal;
            bisectors[i * 4 + 1] = normal;
            bisectors[i * 4 + 2] = -normal;
            bisectors[i * 4 + 3] = -normal;

            extrusion_params[i * 4 + 0] = {1, 0, 0};
            extrusion_params[i * 4 + 1] = {1, 0, 0};
            extrusion_params[i * 4 + 2] = {1, 0, 0};
            extrusion_params[i * 4 + 3] = {1, 0, 0};
        }
    }

    // Bisectors

    auto compute_bisector =
        [&](size_t index, size_t next_index, size_t top_index, size_t top_next_index)
    {
        if (normals.at(index) == lm::vec3{0, 0, 0}) return;

        lm::vec3 normal = normals.at(index);
        lm::vec3 next_normal = normals.at(next_index);
        lm::vec3 bisector = lm::normalize(normal + next_normal);

        if (std::isnan(bisector.x) || std::isnan(bisector.y) || std::isnan(bisector.z))
        {
            // This can happen if the line goes back onto itself.
            if (normal != lm::vec3{0, 0, 0})
            {
                bisector = normal;
            }
            else if (next_normal != lm::vec3{0, 0, 0})
            {
                bisector = next_normal;
            }
            else
            {
                bisector = {1, 0, 0};
            }
        }

        bisectors[index] = bisector;
        bisectors[next_index] = bisector;
        bisectors[top_index] = bisector;
        bisectors[top_next_index] = bisector;

        bisectors[index + 2] = -bisector;
        bisectors[next_index + 2] = -bisector;
        bisectors[top_index + 2] = -bisector;
        bisectors[top_next_index + 2] = -bisector;

        const lm::dvec3& ecef = ecef_positions.at(index);
        const lm::dvec3& prev_ecef = ecef_positions.at(index - 4);

        bool left_turn = lm::dot(lm::normalize(ecef - prev_ecef), bisector) > 0;

        extrusion_params[index].y = left_turn ? 1.0F : -1.0F;
        extrusion_params[top_index].y = left_turn ? 1.0F : -1.0F;
        extrusion_params[next_index].y = left_turn ? 1.0F : -1.0F;
        extrusion_params[top_next_index].y = left_turn ? 1.0F : -1.0F;

        extrusion_params[index + 2].y = left_turn ? -1.0F : 1.0F;
        extrusion_params[top_index + 2].y = left_turn ? -1.0F : 1.0F;
        extrusion_params[next_index + 2].y = left_turn ? -1.0F : 1.0F;
        extrusion_params[top_next_index + 2].y = left_turn ? -1.0F : 1.0F;
    };

    for (size_t l = 1; l < line_boundary_indices.size() - 1; ++l)
    {
        size_t line_start = line_boundary_indices.at(l);

        size_t index = (line_start * 4) - 4;
        size_t next_index = index + 4;
        size_t top_index = index + 1;
        size_t top_next_index = next_index + 1;

        compute_bisector(index, next_index, top_index, top_next_index);
    }

    if (do_loop)
    {
        compute_bisector((latlon_point_count - 1) * 4, 0, (latlon_point_count - 1) * 4 + 1, 1);
    }

    // Offsets from ground for floor and roof
    {
        for (size_t i = 0; i < positions_geo.size(); ++i)
        {
            lm::dvec3 normal = hrz::geo_to_normal(positions_geo.at(i));
            lm::dvec3 down_offset = normal * MeshDownOffset;
            lm::dvec3 up_offset = normal * MeshUpOffset;
            ecef_positions.at(i * 4 + 0) += down_offset;
            ecef_positions.at(i * 4 + 1) += up_offset;
            ecef_positions.at(i * 4 + 2) += down_offset;
            ecef_positions.at(i * 4 + 3) += up_offset;
        }
    }

    // ECEF positions split
    hrz::ArrayView<lm::vec3> ecef_positions_low(
        vbo_data.data() + 0, latlon_point_count * 4, sizeof(lm::vec3) * 5);
    hrz::ArrayView<lm::vec3> ecef_positions_high(
        vbo_data.data() + 1, latlon_point_count * 4, sizeof(lm::vec3) * 5);

    for (size_t i = 0; i < ecef_positions.size(); ++i)
    {
        const lm::dvec3& ecef = ecef_positions.at(i);
        hrz::split_double(ecef.x, ecef_positions_low[i].x, ecef_positions_high[i].x);
        hrz::split_double(ecef.y, ecef_positions_low[i].y, ecef_positions_high[i].y);
        hrz::split_double(ecef.z, ecef_positions_low[i].z, ecef_positions_high[i].z);
    }

    // Indices
    std::vector<uint32_t> indices;
    indices.reserve((latlon_point_count * 8 + 4) * 3);

    // Line segments
    for (size_t i = 0; i < latlon_point_count - 1; ++i)
    {
        // Right wall
        indices.push_back((i + 0) * 4 + 0);
        indices.push_back((i + 1) * 4 + 0);
        indices.push_back((i + 1) * 4 + 1);
        indices.push_back((i + 0) * 4 + 0);
        indices.push_back((i + 1) * 4 + 1);
        indices.push_back((i + 0) * 4 + 1);

        // Roof
        indices.push_back((i + 0) * 4 + 1);
        indices.push_back((i + 1) * 4 + 1);
        indices.push_back((i + 1) * 4 + 3);
        indices.push_back((i + 0) * 4 + 1);
        indices.push_back((i + 1) * 4 + 3);
        indices.push_back((i + 0) * 4 + 3);

        // Left wall
        indices.push_back((i + 1) * 4 + 2);
        indices.push_back((i + 0) * 4 + 2);
        indices.push_back((i + 0) * 4 + 3);
        indices.push_back((i + 1) * 4 + 2);
        indices.push_back((i + 0) * 4 + 3);
        indices.push_back((i + 1) * 4 + 3);

        // Floor
        indices.push_back((i + 1) * 4 + 0);
        indices.push_back((i + 0) * 4 + 0);
        indices.push_back((i + 0) * 4 + 2);
        indices.push_back((i + 1) * 4 + 0);
        indices.push_back((i + 0) * 4 + 2);
        indices.push_back((i + 1) * 4 + 2);
    }

    // Ends
    if (do_loop)
    {
        size_t last = (latlon_point_count - 1) * 4;

        // Right wall
        indices.push_back(last + 0);
        indices.push_back(0);
        indices.push_back(1);
        indices.push_back(last + 0);
        indices.push_back(1);
        indices.push_back(last + 1);

        // Roof
        indices.push_back(last + 1);
        indices.push_back(1);
        indices.push_back(3);
        indices.push_back(last + 1);
        indices.push_back(3);
        indices.push_back(last + 3);

        // Left wall
        indices.push_back(2);
        indices.push_back(last + 2);
        indices.push_back(last + 3);
        indices.push_back(2);
        indices.push_back(last + 3);
        indices.push_back(3);

        // Floor
        indices.push_back(0);
        indices.push_back(last + 0);
        indices.push_back(last + 2);
        indices.push_back(0);
        indices.push_back(last + 2);
        indices.push_back(2);
    }
    else
    {
        // First end cap
        indices.push_back(2);
        indices.push_back(0);
        indices.push_back(1);
        indices.push_back(2);
        indices.push_back(1);
        indices.push_back(3);

        // Second end cap
        size_t index = (latlon_point_count - 1) * 4;
        indices.push_back(index + 0);
        indices.push_back(index + 2);
        indices.push_back(index + 3);
        indices.push_back(index + 0);
        indices.push_back(index + 3);
        indices.push_back(index + 1);
    }

    auto b_sphere = hrz::compute_bounding_sphere(
        std::span<const lm::dvec3>{ecef_positions.data(), ecef_positions.size()});

    mesh.vertex_data = std::move(vbo_data);
    mesh.indices = std::move(indices);
    mesh.center = b_sphere.center;
    mesh.radius = b_sphere.radius;
    mesh.is_degenerate = false;

    return mesh;
}

// Return true if the border of the triangulated mesh is exclusively made of segments
// joining two neighbouring points (wrt their indices).
// If it isn't the case, it means that Earcut hasn't succeeded in creating a mesh that
// uses the input positions as expected, usually because they describe a degenerate
// polygon.
bool check_triangulation(
    std::span<uint32_t> triangulation_indices,
    uint32_t position_count,
    std::span<const size_t> linestring_sizes)
{
    HRZ_SCOPED_SAMPLE("check triangulation");

    if (triangulation_indices.size() < 3) return false;

    // The boolean value indicates whether the edge has been inverted or not
    // before its insertion in the map.
    // (The edges can be inverted in order to guarantee equality and hash,
    // needed by the map, regardless of the order of the indices.
    hrz::flat_hash_map<Edge, bool> edges;

    auto add_edge = [&](uint32_t from, uint32_t to)
    {
        auto edge = Edge::create(from, to);
        auto it = edges.find(edge);

        if (it != edges.end())
        {
            // If an edge was already in the set, it means it participates in two triangles,
            // and isn't on the border of the mesh.
            edges.erase(it);
        }
        else
        {
            edges.insert({edge, to < from});
        }
    };

    for (size_t i = 0; i + 2 < triangulation_indices.size(); i += 3)
    {
        add_edge(triangulation_indices[i + 0], triangulation_indices[i + 1]);
        add_edge(triangulation_indices[i + 1], triangulation_indices[i + 2]);
        add_edge(triangulation_indices[i + 2], triangulation_indices[i + 0]);
    }

    // Now only edges on the border remain.

    constexpr uint32_t no_index = std::numeric_limits<uint32_t>::max();
    std::vector<uint32_t> index_to_next(position_count, no_index);

    for (auto it : edges)
    {
        const auto& edge = it.first;
        bool inverted = it.second;
        uint32_t index = inverted ? edge.to : edge.from;
        uint32_t next = inverted ? edge.from : edge.to;

        if (index_to_next.at(index) != no_index)
        {
            // One point participates in two rings, this is invalid.
            return false;
        }

        index_to_next[index] = next;
    }

    uint32_t current_linestring_start = 0;
    for (size_t linestring_index = 0; linestring_index < linestring_sizes.size();
         ++linestring_index)
    {
        uint32_t linestring_size = (uint32_t)linestring_sizes[linestring_index];
        uint32_t linestring_end = current_linestring_start + linestring_size;

        for (uint32_t index = current_linestring_start; index < linestring_end; ++index)
        {
            uint32_t next = index_to_next.at(index);

            if (next == no_index) return false;

            uint32_t diff = (uint32_t)std::abs((int)next - (int)index);

            if (diff != 1 && diff != linestring_size - 1) return false;
        }

        current_linestring_start += linestring_size;
    }

    return true;
}

void make_wall_segments(
    uint32_t from,
    uint32_t to,
    std::vector<uint32_t>& indices,
    const hrz::flat_hash_map<Edge, size_t>& edges_to_indices,
    size_t floor_position_count,
    bool clockwise_outer_linestring)
{
    auto it = edges_to_indices.find(Edge::create(from, to));
    if (it != edges_to_indices.end())
    {
        // Subdivided edge, create segments for both subdivisions
        make_wall_segments(
            from, it->second, indices, edges_to_indices, floor_position_count,
            clockwise_outer_linestring);
        make_wall_segments(
            it->second, to, indices, edges_to_indices, floor_position_count,
            clockwise_outer_linestring);
    }
    else
    {
        if (clockwise_outer_linestring)
        {
            std::swap(from, to);
        }

        // Not subdivided, create wall segment
        indices.push_back(from);
        indices.push_back(to);
        indices.push_back(to + floor_position_count);
        indices.push_back(from);
        indices.push_back(to + floor_position_count);
        indices.push_back(from + floor_position_count);
    }
}

Mesh generate_polygon_mesh(
    std::span<const hrz::GeoPosition2> input_positions_geo,
    std::span<const size_t> input_linestring_sizes,
    LineType line_type)
{
    HRZ_SCOPED_SAMPLE("generate polygon mesh");

    // Triangulate the flat polygon (including holes).
    // Subdivide triangles so that no edge is longer than a given distance.
    // (To be able to follow the curvature of the planet.)
    // Create a bottom mesh, lower it under the surface of the planet.
    // Create a top mesh, raise it above the highest mountains.
    // Add walls (outer and inner).

    if (input_positions_geo.size() < 3 || input_linestring_sizes.empty()
        || input_linestring_sizes[0] < 3)
    {
        Mesh mesh;
        mesh.is_degenerate = true;
        return mesh;
    }

    std::vector<size_t> linestring_sizes;
    linestring_sizes.reserve(input_linestring_sizes.size());
    std::vector<hrz::GeoPosition2> positions_geo;
    positions_geo.reserve(input_positions_geo.size());

    // Remove duplicate points
    {
        uint32_t current_linestring_start = 0;
        for (size_t r = 0; r < input_linestring_sizes.size(); ++r)
        {
            uint32_t linestring_size = input_linestring_sizes[r];
            uint32_t linestring_end = current_linestring_start + linestring_size;

            uint32_t actual_linestring_size = 0;
            hrz::GeoPosition2 previous_geo = {
                std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()
            };

            for (size_t p = current_linestring_start;
                 p < linestring_end && p < input_positions_geo.size(); ++p)
            {
                auto geo = input_positions_geo[p];
                if (geo != previous_geo)
                {
                    actual_linestring_size += 1;
                    positions_geo.push_back(geo);
                }
                previous_geo = geo;
            }

            if (actual_linestring_size > 0)
            {
                linestring_sizes.push_back(actual_linestring_size);
            }

            current_linestring_start += linestring_size;
        }
    }

    // Triangulate

    std::vector<std::span<const hrz::GeoPosition2>> linestrings;
    {
        size_t current_position = 0;
        for (size_t i = 0; i < linestring_sizes.size(); ++i)
        {
            size_t linestring_size = linestring_sizes.at(i);

            if (linestring_size < 3) continue;

            linestrings.push_back(
                std::span<const hrz::GeoPosition2>(positions_geo)
                    .subspan(current_position, linestring_size));
            current_position += linestring_size;
        }
    }

    // Whatever the winding order of the input positions, Earcut always returns
    // indices for counter-clockwise triangles.
    std::span<const std::span<const hrz::GeoPosition2>> linestring_span = {
        linestrings.data(), linestrings.size()
    };
    auto triangulation_indices = mapbox::earcut<uint32_t>(linestring_span);

    if (!check_triangulation(triangulation_indices, positions_geo.size(), linestring_sizes))
    {
        // The polygon could not be properly triangulated,
        // due to bad conditions, such as self-intersections.
        Mesh mesh;
        mesh.is_degenerate = true;
        return mesh;
    }

    // Depending on the winding order of the input positions, the walls must
    // be drawn in one direction or the other, in order to preserve correct
    // triangle winding order.
    // `linestring_span` contains positions in lat-lon, whose winding order is opposite
    // to ECEF's, hence the negation.
    // The computation is made here to avoid an invalidation of the span when new
    // geographic positions are added.
    bool clockwise_outer_linestring = !hrz::is_clockwise(linestring_span[0]);

    // Subdivide
    // "Inspired" by
    // https://github.com/CesiumGS/cesium/blob/94e5646e6c970a83b604652cd908064033ef9870/Source/Core/PolygonPipeline.js#L97

    std::vector<uint32_t>& indices_to_subdivide = triangulation_indices;
    std::vector<uint32_t> subdivided_indices;
    hrz::flat_hash_map<Edge, size_t> edges_to_indices;

    for (size_t triangle = 0; triangle < indices_to_subdivide.size(); triangle += 3)
    {
        if (indices_to_subdivide.size() > MaxPolygonTriangles)
        {
            // The polygon has too many triangles, probably because it could not
            // be properly subdivided, and the subdivision procedure is stuck in
            // an infinite loop.
            // This can happen in some cases when using rhumb-lines that cross
            // the antimeridian when the polygon includes a pole.
            Mesh mesh;
            mesh.is_degenerate = true;
            return mesh;
        }

        auto get_distance = [&](hrz::GeoPosition2 ia_geo, hrz::GeoPosition2 ib_geo)
        {
            if (line_type == LineType::RhumbLines
                || line_type == LineType::RhumbLinesNotAcrossAntimeridian)
            {
                bool allow_antimeridian_crossing = line_type == LineType::RhumbLines;

                return angular_distance(ia_geo, ib_geo, allow_antimeridian_crossing);
            }
            else
            {
                return hrz::geodesic_distance(ia_geo, ib_geo);
            }
        };

        auto get_midpoint = [&](uint32_t ia, uint32_t ib, hrz::GeoPosition2 ia_geo,
                                hrz::GeoPosition2 ib_geo) -> uint32_t
        {
            auto edge = Edge::create(ia, ib);
            auto it = edges_to_indices.find(edge);
            if (it != edges_to_indices.end())
            {
                return it->second;
            }
            else
            {
                hrz::GeoPosition2 i_geo;
                switch (line_type)
                {
                    case LineType::Geodesics: i_geo = hrz::geodesic_midpoint(ia_geo, ib_geo); break;
                    case LineType::RhumbLines:
                        i_geo = hrz::rhumb_line_midpoint(ia_geo, ib_geo, true);
                        break;
                    case LineType::RhumbLinesNotAcrossAntimeridian:
                        i_geo = hrz::rhumb_line_midpoint(ia_geo, ib_geo, false);
                        break;
                    default: assert(false && "Unhandled case"); break;
                }

                size_t i = positions_geo.size();
                positions_geo.push_back(i_geo);
                edges_to_indices.insert({edge, i});
                return i;
            }
        };

        size_t i0 = indices_to_subdivide.at(triangle + 0);
        size_t i1 = indices_to_subdivide.at(triangle + 1);
        size_t i2 = indices_to_subdivide.at(triangle + 2);

        hrz::GeoPosition2 i0_geo = positions_geo.at(i0);
        hrz::GeoPosition2 i1_geo = positions_geo.at(i1);
        hrz::GeoPosition2 i2_geo = positions_geo.at(i2);

        double i0i1 = get_distance(i0_geo, i1_geo);
        double i1i2 = get_distance(i1_geo, i2_geo);
        double i2i0 = get_distance(i2_geo, i0_geo);

        double max_distance = std::max(i0i1, std::max(i1i2, i2i0));
        double max_allowed_distance =
            line_type == LineType::Geodesics ? MaxSegmentLength : MaxSegmentAngularLength;

        if (max_distance <= max_allowed_distance)
        {
            subdivided_indices.push_back(i0);
            subdivided_indices.push_back(i1);
            subdivided_indices.push_back(i2);

            continue;
        }

        if (i0i1 > max_allowed_distance && i0i1 == max_distance)
        {
            uint32_t i = get_midpoint(i0, i1, i0_geo, i1_geo);

            indices_to_subdivide.push_back(i0);
            indices_to_subdivide.push_back(i);
            indices_to_subdivide.push_back(i2);
            indices_to_subdivide.push_back(i);
            indices_to_subdivide.push_back(i1);
            indices_to_subdivide.push_back(i2);

            continue;
        }

        if (i1i2 > max_allowed_distance && i1i2 == max_distance)
        {
            uint32_t i = get_midpoint(i1, i2, i1_geo, i2_geo);

            indices_to_subdivide.push_back(i0);
            indices_to_subdivide.push_back(i1);
            indices_to_subdivide.push_back(i);
            indices_to_subdivide.push_back(i0);
            indices_to_subdivide.push_back(i);
            indices_to_subdivide.push_back(i2);

            continue;
        }

        if (i2i0 > max_allowed_distance && i2i0 == max_distance)
        {
            uint32_t i = get_midpoint(i2, i0, i2_geo, i0_geo);

            indices_to_subdivide.push_back(i);
            indices_to_subdivide.push_back(i1);
            indices_to_subdivide.push_back(i2);
            indices_to_subdivide.push_back(i0);
            indices_to_subdivide.push_back(i1);
            indices_to_subdivide.push_back(i);

            continue;
        }
    }

    std::vector<lm::dvec3> ecef_positions;
    ecef_positions.reserve(positions_geo.size() * 2);

    std::vector<uint32_t> indices;
    indices.reserve(subdivided_indices.size() * 2);

    // Floor
    {
        for (const auto& latlon : positions_geo)
        {
            ecef_positions.push_back(hrz::geo_to_ecef(latlon));
        }

        for (size_t i = 0; i < subdivided_indices.size(); i += 3)
        {
            // The triangles from Earcut are CCW in lat-lon space.
            // This makes them CW in ECEF.
            // We want them to face down, because we're making the
            // bottom of the mesh.
            // So the triangles are inverted twice.
            indices.push_back(subdivided_indices.at(i + 0));
            indices.push_back(subdivided_indices.at(i + 1));
            indices.push_back(subdivided_indices.at(i + 2));
        }
    }

    size_t floor_position_count = ecef_positions.size();

    // Roof
    {
        for (const auto& latlon : positions_geo)
        {
            ecef_positions.push_back(hrz::geo_to_ecef(latlon));
        }

        for (size_t i = 0; i < subdivided_indices.size(); i += 3)
        {
            // Here we only have to invert the triangles once.
            indices.push_back(subdivided_indices.at(i + 0) + floor_position_count);
            indices.push_back(subdivided_indices.at(i + 2) + floor_position_count);
            indices.push_back(subdivided_indices.at(i + 1) + floor_position_count);
        }
    }

    // Offsets from ground for floor and roof
    {
        for (size_t i = 0; i < positions_geo.size(); ++i)
        {
            lm::dvec3 normal = hrz::geo_to_normal(positions_geo.at(i));
            lm::dvec3 down_offset = normal * MeshDownOffset;
            lm::dvec3 up_offset = normal * MeshUpOffset;
            ecef_positions.at(i) += down_offset;
            ecef_positions.at(i + floor_position_count) += up_offset;
        }
    }

    // Walls
    {
        size_t current_position = 0;
        for (size_t linestring_size : linestring_sizes)
        {
            for (size_t i = 0; i < linestring_size; ++i)
            {
                make_wall_segments(
                    current_position + i, current_position + ((i + 1) % linestring_size), indices,
                    edges_to_indices, floor_position_count, clockwise_outer_linestring);
            }
            current_position += linestring_size;
        }
    }

    std::vector<lm::vec3> split_positions;
    split_positions.resize(ecef_positions.size() * 2);

    for (size_t i = 0; i < ecef_positions.size(); ++i)
    {
        const lm::dvec3& ecef = ecef_positions.at(i);
        hrz::split_double(ecef.x, split_positions[i * 2 + 0].x, split_positions[i * 2 + 1].x);
        hrz::split_double(ecef.y, split_positions[i * 2 + 0].y, split_positions[i * 2 + 1].y);
        hrz::split_double(ecef.z, split_positions[i * 2 + 0].z, split_positions[i * 2 + 1].z);
    }

    auto b_sphere = hrz::compute_bounding_sphere(
        std::span<const lm::dvec3>{ecef_positions.data(), ecef_positions.size()});

    Mesh mesh;
    mesh.vertex_data = std::move(split_positions);
    mesh.indices = std::move(indices);
    mesh.center = b_sphere.center;
    mesh.radius = b_sphere.radius;
    mesh.is_degenerate = false;

    return mesh;
}

Mesh generate_sphere_mesh(const lm::vec3& center, float radius, size_t subdivision_steps = 0)
{
    // Start from an icosahedron, then subdivide each triangle into four triangles,
    // as many times as desired, while moving all the new points to the unit sphere.
    // Finally, apply scale and translation.
    // Values for the icosahedron taken from
    // https://www.danielsieger.com/blog/2021/01/03/generating-platonic-solids.html

    float phi = (1.0F + std::sqrt(5.0F)) * 0.5F; // Golden ratio
    float a = 1.0F;
    float b = 1.0F / phi;

    std::vector<lm::vec3> vertices = {{0, b, -a}, {b, a, 0},   {-b, a, 0},  {0, b, a},
                                      {0, -b, a}, {-a, 0, b},  {0, -b, -a}, {a, 0, -b},
                                      {a, 0, b},  {-a, 0, -b}, {b, -a, 0},  {-b, -a, 0}};

    std::vector<uint32_t> indices = {2, 1, 0, 1,  2,  3,  5,  4,  3, 4, 8,  3, 7,  6, 0,
                                     6, 9, 0, 11, 10, 4,  10, 11, 6, 9, 5,  2, 5,  9, 11,
                                     8, 7, 1, 7,  8,  10, 2,  5,  3, 8, 1,  3, 9,  2, 0,
                                     1, 7, 0, 11, 9,  6,  7,  10, 6, 5, 11, 4, 10, 8, 4};

    for (size_t v = 0; v < vertices.size(); ++v)
    {
        vertices[v] = lm::normalize(vertices[v]);
    }

    hrz::flat_hash_map<lm::uvec2, uint32_t> edges_to_indices;

    auto get_or_create_vertex = [&](uint32_t i0, uint32_t i1)
    {
        if (i1 < i0) std::swap(i0, i1);

        lm::uvec2 edge = {i0, i1};

        auto it = edges_to_indices.find(edge);
        if (it != edges_to_indices.end())
        {
            return it->second;
        }

        uint32_t i = vertices.size();
        vertices.push_back((vertices.at(i0) + vertices.at(i1)) * 0.5F);
        edges_to_indices.insert({edge, i});
        return i;
    };

    std::vector<uint32_t> new_indices;

    for (size_t s = 0; s < subdivision_steps; ++s)
    {
        new_indices.clear();
        new_indices.reserve(indices.size() * 4);

        size_t prev_vertex_count = vertices.size();
        size_t index_count = indices.size();

        assert(index_count % 3 == 0);

        for (size_t t = 0; t < index_count; t += 3)
        {
            uint32_t i0 = indices.at(t + 0);
            uint32_t i1 = indices.at(t + 1);
            uint32_t i2 = indices.at(t + 2);

            uint32_t i01 = get_or_create_vertex(i0, i1);
            uint32_t i12 = get_or_create_vertex(i1, i2);
            uint32_t i20 = get_or_create_vertex(i2, i0);

            new_indices.push_back(i0);
            new_indices.push_back(i01);
            new_indices.push_back(i20);

            new_indices.push_back(i01);
            new_indices.push_back(i1);
            new_indices.push_back(i12);

            new_indices.push_back(i01);
            new_indices.push_back(i12);
            new_indices.push_back(i20);

            new_indices.push_back(i20);
            new_indices.push_back(i12);
            new_indices.push_back(i2);
        }

        std::swap(indices, new_indices);

        size_t new_vertex_count = vertices.size();

        for (size_t v = prev_vertex_count; v < new_vertex_count; ++v)
        {
            vertices[v] = lm::normalize(vertices[v]);
        }
    }

    Mesh mesh;
    mesh.indices = indices;
    mesh.center = lm::dvec3(center);
    mesh.radius = radius;

    mesh.vertex_data.reserve(vertices.size() * 2);
    for (const auto& vertex : vertices)
    {
        mesh.vertex_data.push_back(vertex * radius + center);
        mesh.vertex_data.push_back(vertex); // normal
    }

    return mesh;
}

enum
{
    ShapeParamsUbo = hrz::UboCustomStart,
    ControlParamsUbo = hrz::UboCustomStart,
    PlanetParamsUbo,

    PositionLowInputStream = 0,
    PositionHighInputStream = 1,
    NormalInputStream = 2,
    BisectorInputStream = 3,
    ExtrusionParamsInputStream = 4,

    WmercPositionLowInputStream = 2,
    WmercPositionHighInputStream,
    GroundNormalInputStream,
    LocalPositionInputStream,

    DtmIndirectionSampler = 0,
    DtmAtlasSampler = 1,
};

struct ShapeUniformData
{
    lm::vec4 color;
    lm::uvec2 object_reference;
    float line_width;
    uint32_t _padding[1];
};

HRZ_CHECK_UBO_SIZE(ShapeUniformData);

struct ControlUniformData
{
    lm::uvec2 object_reference;
    uint32_t selected_control_point_id;
    hrz::bool32 pick_selected_control_point;
    hrz::bool32 pick_midpoint_control_points;
    float control_point_size;
    hrz::bool32 show_midpoint_control_points;
    uint32_t _padding[1];
    lm::vec4 control_point_color;
    lm::vec4 midpoint_control_point_color;
    lm::vec4 selected_control_point_color;
};

HRZ_CHECK_UBO_SIZE(ControlUniformData);

struct RenderableShape : public my::Renderer::Renderable
{
    my::ResourceHandle vertex_buffer = my::ResourceHandle::null();
    my::ResourceHandle index_buffer = my::ResourceHandle::null();

    struct RenderData
    {
        my::ResourceHandle vertex_input = my::ResourceHandle::null();
        my::ResourceHandle fullscreen_vertex_input = my::ResourceHandle::null();
        uint32_t vertex_count = 0;
        my::ResourceHandle stencil_shader = my::ResourceHandle::null();
        my::ResourceHandle visual_shader = my::ResourceHandle::null();
        my::ResourceHandle picking_shader = my::ResourceHandle::null();
        my::ResourceHandle uniform_buffer = my::ResourceHandle::null();
        uint32_t scene_views_bitset;
    } data;

    my::Renderer::BinMask bin_mask;

    lm::dvec3 center;
    double radius;
    uint32_t z_index;

    bool is_degenerate;

    static void render_callback(
        uint32_t render_type,
        my::RenderContext* r,
        my::ResourceBinder* rb,
        const void* raw_user_data,
        const void* raw_data)
    {
        auto data = (const RenderData*)raw_data;

        auto user_data = (const hrz::SceneViewRenderGraphUserData*)raw_user_data;
        if (((1 << user_data->scene_view) & data->scene_views_bitset) == 0) return;

        my::ResourceHandle shader;
        switch (render_type)
        {
            case hrz::RenderDecal: shader = data->visual_shader; break;
            case hrz::RenderPicking: shader = data->picking_shader; break;
            default: return;
        }

        // Clear the stencil buffer to the value at the center of its range.
        static const my::ClearTarget clear_targets[] = {{
            my::Attachment::Stencil,
            my::ClearValue::make_stencil(128),
        }};

        r->clear(clear_targets);

        rb->push_state();

        my::UboBinding ubo_bindings[] = {
            {ShapeParamsUbo, data->uniform_buffer, 0, sizeof(ShapeUniformData)}
        };
        rb->bind(ubo_bindings);

        auto state = rb->get_current_state();

        auto batch = my::DrawBatchInfo(my::PrimitiveType::TriangleList, data->vertex_count)
                         .indexed(my::IndexType::UInt);

        // Draw the actual mesh geometry.
        // No action is undertaken when the depth test succeeds.
        // When it fails, the value in the stencil buffer is decremented for front-facing
        // polygons and incremented for back-facing ones.
        // In effect the stencil buffer has values greater than the starting value for
        // pixels which are at the depth of the depth buffer and inside the mesh that has
        // just been drawn.
        // For pixels that are outside the mesh, either they are not affected by the draw
        // call at all, or they are behind (or in front of) an equal number of front- and
        // back- facing triangles. Their stencil value stays the same as before the draw
        // call.
        r->draw(batch, data->stencil_shader, data->vertex_input, state.ubos, state.textures);

        batch = my::DrawBatchInfo(my::PrimitiveType::TriangleList, 3);

        // Draw a full-screen triangle.
        // Only draw pixels for which the stencil value is not the original value (at
        // clear time).
        // This makes the intersection of the depth buffer and the mesh be visible.
        r->draw(batch, shader, data->fullscreen_vertex_input, state.ubos, state.textures);

        rb->pop_state();
    }

    void collect_render_info(my::Renderer::Queue& queue, const my::Renderer::Culler& culler)
        const override
    {
        if (culler.is_visible_in_any_view(center, radius))
        {
            queue.enqueue(bin_mask, render_callback, data, center, radius, z_index);
        }
    }
};

void collect_gpu_data(RenderableShape& renderable, std::vector<my::ResourceHandle>& resources)
{
    resources.push_back(renderable.data.vertex_input);
    resources.push_back(renderable.index_buffer);
    resources.push_back(renderable.vertex_buffer);
    resources.push_back(renderable.data.uniform_buffer);
}

struct RenderableControl : public my::Renderer::Renderable
{
    my::ResourceHandle vertex_buffer = my::ResourceHandle::null();
    my::ResourceHandle index_buffer = my::ResourceHandle::null();

    struct RenderData
    {
        my::ResourceHandle vertex_input = my::ResourceHandle::null();
        uint32_t vertex_count = 0;
        uint32_t instance_count = 0;
        my::ResourceHandle visual_shader = my::ResourceHandle::null();
        my::ResourceHandle picking_shader = my::ResourceHandle::null();
        my::ResourceHandle uniform_buffer = my::ResourceHandle::null();
        hrz::planet::GeometryResources planet_resources;
        uint32_t scene_views_bitset;
    } data;

    my::Renderer::BinMask bin_mask;

    lm::dvec3 center;
    double radius;

    static void render_callback(
        uint32_t render_type,
        my::RenderContext* r,
        my::ResourceBinder* rb,
        const void* raw_user_data,
        const void* raw_data)
    {
        auto data = (const RenderData*)raw_data;

        auto user_data = (const hrz::SceneViewRenderGraphUserData*)raw_user_data;
        if (((1 << user_data->scene_view) & data->scene_views_bitset) == 0) return;

        my::ResourceHandle shader;
        switch (render_type)
        {
            case hrz::RenderVisual: shader = data->visual_shader; break;
            case hrz::RenderPicking: shader = data->picking_shader; break;
            default: return;
        }

        auto batch = my::DrawBatchInfo(my::PrimitiveType::TriangleList, data->vertex_count)
                         .indexed(my::IndexType::UInt)
                         .instanced(data->instance_count);

        rb->push_state();

        my::UboBinding ubo_bindings[] = {
            {ControlParamsUbo, data->uniform_buffer, 0, sizeof(ControlUniformData)},
            {PlanetParamsUbo, data->planet_resources.planet_params.buffer,
             data->planet_resources.planet_params.offset, data->planet_resources.planet_params.size}
        };
        rb->bind(ubo_bindings);

        my::TextureBinding texture_bindings[] = {
            {DtmIndirectionSampler, data->planet_resources.dtm_indirection.texture,
             data->planet_resources.dtm_indirection.sampler},
            {DtmAtlasSampler, data->planet_resources.dtm_atlas.texture,
             data->planet_resources.dtm_atlas.sampler},
        };
        rb->bind(texture_bindings);

        auto state = rb->get_current_state();

        r->draw(batch, shader, data->vertex_input, state.ubos, state.textures);

        rb->pop_state();
    }

    void collect_render_info(my::Renderer::Queue& queue, const my::Renderer::Culler& culler)
        const override
    {
        if (culler.is_visible_in_any_view(center, radius))
        {
            queue.enqueue(bin_mask, render_callback, data, center, radius);
        }
    }
};

void collect_gpu_data(RenderableControl& renderable, std::vector<my::ResourceHandle>& resources)
{
    resources.push_back(renderable.data.vertex_input);
    resources.push_back(renderable.vertex_buffer);
    resources.push_back(renderable.data.uniform_buffer);
}

struct Shape
{
    using Handle = uint32_t;

    enum class Kind
    {
        Polyline,
        Polygon,
    };

    Handle handle;
    uint64_t global_layer_id;
    Kind kind;
    LineType line_type;
    size_t max_point_count; // 1 for points, 2 for lines
    std::vector<hrz::GeoPosition2> points;
    std::vector<size_t> linestring_sizes; // Only for polygons
    lm::vec4 stroke_color;
    lm::vec4 fill_color;
    float stroke_width;
    lm::vec4 selected_stroke_color;
    lm::vec4 selected_fill_color;
    float selected_stroke_width;
    float control_point_size;
    lm::vec4 control_point_color;
    lm::vec4 midpoint_control_point_color;
    lm::vec4 selected_control_point_color;
    bool show_midpoint_control_points;
    bool always_show_control_points;

    uint32_t z_index;

    hrz::picking::ObjectReference object_ref;

    std::vector<hrz::GeoPosition2> control_points;

    bool geometry_updated = false;
    bool style_updated = false;
    bool control_style_updated = false;
    bool z_index_updated = false;
    bool visibility_updated = false;
    bool scene_views_updated = false;
    bool model_update_frequency_updated = false;

    bool is_visible = true;
    uint32_t scene_views_bitset = 0;
    bool extend_to_mouse_pointer = false;
    bool generate_renderable = false;
    bool regenerate_renderable = false;
    bool generate_control_renderable = false;
    bool regenerate_control_renderable = false;
    bool update_ubo = false;
    bool update_control_ubo = false;
    hrz_proto::EditableShapeModelUpdateFrequency model_update_frequency =
        hrz_proto::DEFAULT_MODEL_UPDATES;

    RenderableShape renderable;
    std::vector<RenderableShape> renderable_outlines; // Only for polygons, one per linestring
    RenderableControl renderable_control;
};

} // namespace

namespace hrz
{

struct ShapeEditor
{
    enum class Mode
    {
        NoAction,
        AppendPoint,
        DragPoint,
    };

    struct Event
    {
        enum class Kind
        {
            ModeSwitch,
            ShapeSelection,
            ControlPointSelection,
            MouseMove,
            MouseButtonDown,
            MouseButtonUp,
            MouseClick,
            Delete,
        };

        struct Mouse
        {
            std::optional<lm::ivec2> initial_screen_position;
            std::optional<lm::ivec2> screen_position;
            bool is_in_viewport;
            hrz_proto::SceneViewIndex view_index;
            std::optional<platform::Event::MouseButton> button;
            std::optional<picking::PositionTicket> picking_ticket;
            bool has_picking_results;
            std::optional<lm::dvec3> ecef_position;
            std::optional<lm::uvec2> picked_id;
        };

        using Payload = std::variant<std::monostate, Mouse, Mode, std::optional<uint64_t>>;

        Kind kind;
        Payload payload;

        bool is_mouse_event() const
        {
            return kind == Kind::MouseMove || kind == Kind::MouseButtonDown
                || kind == Kind::MouseButtonUp || kind == Kind::MouseClick;
        }

        Mouse& mouse() { return std::get<Mouse>(payload); }

        Mode& mode() { return std::get<Mode>(payload); }

        std::optional<uint64_t> layer_id() const
        {
            return std::get<std::optional<uint64_t>>(payload);
        }

        std::optional<uint64_t> control_point_index() const
        {
            return std::get<std::optional<uint64_t>>(payload);
        }
    };

    uint8_t picking_id;
    bool disabled;

    std::deque<Event> events;

    // Help determine when mouse button down/up events make a click.
    std::optional<Event> last_button_down_events[SCENE_VIEW_COUNT];

    // Used when we need to generate a new mouse move event.
    std::optional<Event> last_mouse_move_event;

    std::optional<hrz::gestures::GestureId> button_down_gesture_id;
    std::optional<hrz::gestures::GestureId> drag_gesture_id;

    std::optional<size_t> selected_shape;
    bool shape_selection_is_locked = false;
    std::optional<size_t> selected_control_point;
    Mode mode = Mode::NoAction;

    // These variables are used for making drag actions.
    std::optional<Event::Mouse> current_drag_mouse_start;
    std::optional<Shape::Handle> current_mouse_down_shape;
    std::optional<Shape::Handle> current_dragged_control_point;

    my::ResourceHandle polyline_stencil_shader = my::ResourceHandle::null();
    my::ResourceHandle polygon_stencil_shader = my::ResourceHandle::null();
    my::ResourceHandle shape_visual_shader = my::ResourceHandle::null();
    my::ResourceHandle shape_picking_shader = my::ResourceHandle::null();
    my::ResourceHandle control_visual_shader = my::ResourceHandle::null();
    my::ResourceHandle control_picking_shader = my::ResourceHandle::null();
    my::ResourceHandle control_vertex_buffer = my::ResourceHandle::null();
    size_t control_vertex_count = 0;
    my::ResourceHandle control_index_buffer = my::ResourceHandle::null();
    my::ResourceHandle fullscreen_vertex_buffer = my::ResourceHandle::null();
    my::ResourceHandle fullscreen_vertex_input = my::ResourceHandle::null();

    // Use only 24 bits, so that the handle can also serve as the picking
    // complementary ID for the shape.
    using IndexPool = GenIndexPool<Shape::Handle, 4, 20>;
    using ShapePool = GenObjectPool<Shape, IndexPool, 16>;

    ShapePool shape_pool;
    hrz::flat_hash_map<uint64_t, Shape::Handle> layer_ids_to_shape_handles;

    hrz::flat_hash_set<Shape::Handle> updated_layers;
    hrz::flat_hash_set<Shape::Handle> shapes_to_update_on_gpu;
    hrz::flat_hash_set<Shape::Handle> renderable_shapes;
    hrz::flat_hash_set<Shape::Handle> unregistered_layers;

    std::vector<my::ResourceHandle> unused_resources;

    std::vector<hrz_proto::ShapeEditorMessage> pending_messages;
};

namespace editor
{
namespace
{

// Update the model after the shape has been modified by the user
// with mouse and keyboard events.
void update_shape_model(
    Shape& shape,
    ShapeEditor* editor,
    SceneModel* scene_model,
    ClientMessageQueue* mq)
{
    hrz_proto::LayerHandle handle;
    handle.set_opaque(shape.global_layer_id);

    hrz_proto::EditableShapeLayerPathBuilder<hrz::SceneModelAccessor> builder(scene_model, handle);

    hrz_proto::EditableShapeLayer layer = builder.clone().get();
    auto geometry = layer.mutable_geometry();

    auto coords = geometry->mutable_coords();
    auto linestring_sizes = geometry->mutable_linestring_sizes();

    coords->Clear();
    linestring_sizes->Clear();

    std::optional<size_t> skipped_point_index = std::nullopt;

    for (size_t i = 0; i < shape.points.size(); ++i)
    {
        const auto& point = shape.points.at(i);

        if (editor->selected_shape == shape.handle && editor->selected_control_point == i * 2
            && shape.extend_to_mouse_pointer
            && shape.model_update_frequency < hrz_proto::APPEND_MODEL_UPDATES)
        {
            // Do not include the point that follows the mouse cursor, at it
            // hasn't been really added by the user yet.
            skipped_point_index = {i};
            continue;
        }

        coords->Add(lm::degrees(point.lat));
        coords->Add(lm::degrees(point.lon));
    }

    if (shape.kind == Shape::Kind::Polygon)
    {
        size_t point_count = 0;

        for (size_t linestring_size : shape.linestring_sizes)
        {
            if (skipped_point_index.has_value()
                && point_count + linestring_size > skipped_point_index.value())
            {
                // Do not count the point that follows the mouse cursor.
                linestring_size -= 1;
                skipped_point_index = std::nullopt;
            }

            linestring_sizes->Add(linestring_size);
            point_count += linestring_size;
        }
    }

    std::move(builder).set(layer);

    hrz_proto::ShapeEditorMessage message;
    message.mutable_shape_geometry_update()->mutable_layer()->set_opaque(shape.global_layer_id);
    hrz::client_message_queue::enqueue_shape_editor_update_message(mq, std::move(message));
}

// Create, delete, and reposition control points where they are needed,
// after the shape geometry has been modified.
void update_control_points(Shape& shape)
{
    HRZ_SCOPED_SAMPLE("update control points");

    auto midpoint = [&](size_t p0, size_t p1)
    {
        auto geo0 = shape.points.at(p0);
        auto geo1 = shape.points.at(p1);

        if (shape.line_type == LineType::RhumbLines)
        {
            return hrz::rhumb_line_midpoint(geo0, geo1, true);
        }
        else if (shape.line_type == LineType::RhumbLinesNotAcrossAntimeridian)
        {
            return hrz::rhumb_line_midpoint(geo0, geo1, false);
        }
        else
        {
            return hrz::geodesic_midpoint(geo0, geo1);
        }
    };

    shape.control_points.clear();

    if (shape.kind == Shape::Kind::Polyline)
    {
        size_t max_index = std::min(shape.points.size(), shape.max_point_count);

        for (size_t point_index = 0; point_index < max_index; ++point_index)
        {
            if (point_index > 0)
            {
                shape.control_points.push_back(midpoint(point_index - 1, point_index));
            }

            shape.control_points.push_back(shape.points.at(point_index));
        }
    }
    else if (shape.kind == Shape::Kind::Polygon)
    {
        size_t linestring_start = 0;
        for (size_t i = 0; i < shape.linestring_sizes.size(); ++i)
        {
            size_t linestring_size = shape.linestring_sizes.at(i);

            if (linestring_size == 0) continue;

            size_t linestring_end = linestring_start + linestring_size - 1;

            for (size_t point_index = linestring_start; point_index <= linestring_end;
                 ++point_index)
            {
                if (point_index > linestring_start)
                {
                    shape.control_points.push_back(midpoint(point_index - 1, point_index));
                }

                shape.control_points.push_back(shape.points.at(point_index));
            }

            if (linestring_size >= 3)
            {
                shape.control_points.push_back(midpoint(linestring_end, linestring_start));
            }

            linestring_start += linestring_size;
        }
    }

    shape.regenerate_control_renderable = true;
}

bool control_point_can_be_selected(Shape& shape, size_t control_point_index)
{
    if (control_point_index >= shape.control_points.size())
    {
        return false;
    }

    if (shape.kind == Shape::Kind::Polyline && shape.max_point_count == 2
        && control_point_index == 1)
    {
        // This is a simple line, the midpoint control point is hidden.
        return false;
    }

    return true;
}

void refresh_shape_after_point_update(
    Shape& shape,
    ShapeEditor* editor,
    SceneModel* scene_model,
    ClientMessageQueue* mq,
    bool update_model = true)
{
    update_control_points(shape);

    if (update_model)
    {
        update_shape_model(shape, editor, scene_model, mq);
    }

    shape.regenerate_renderable = true;

    editor->shapes_to_update_on_gpu.insert(shape.handle);
}

void insert_point(
    ShapeEditor* editor,
    Shape& shape,
    size_t index,
    hrz::GeoPosition2 position,
    SceneModel* scene_model,
    ClientMessageQueue* mq,
    bool update_model = true)
{
    HRZ_SCOPED_SAMPLE("insert point");

    assert(index <= shape.points.size());

    size_t linestring = 0;

    if (shape.kind == Shape::Kind::Polygon)
    {
        size_t linestring_start = 0;
        for (size_t i = 0; i < shape.linestring_sizes.size(); ++i)
        {
            size_t linestring_size = shape.linestring_sizes.at(i);

            if (index <= linestring_start + linestring_size)
            {
                linestring = i;
                break;
            }

            linestring_start += linestring_size;
        }

        if (linestring_start == shape.points.size())
        {
            // Start new ring
            shape.linestring_sizes.push_back(0);
            linestring = shape.linestring_sizes.size() - 1;
        }
    }

    shape.points.insert(shape.points.begin() + index, position);

    if (shape.kind == Shape::Kind::Polygon)
    {
        shape.linestring_sizes[linestring] += 1;
    }

    refresh_shape_after_point_update(shape, editor, scene_model, mq, update_model);
}

void remove_point(
    ShapeEditor* editor,
    Shape& shape,
    size_t index,
    SceneModel* scene_model,
    ClientMessageQueue* mq,
    bool update_model = true)
{
    HRZ_SCOPED_SAMPLE("remove point");

    assert(index < shape.points.size());

    bool first_point = index == 0;
    bool last_point = index == shape.points.size() - 1;
    size_t linestring = 0;

    if (shape.kind == Shape::Kind::Polygon)
    {
        size_t linestring_start = 0;
        for (size_t i = 0; i < shape.linestring_sizes.size(); ++i)
        {
            size_t linestring_size = shape.linestring_sizes.at(i);
            size_t linestring_end = linestring_start + linestring_size - 1;

            if (index >= linestring_start && index <= linestring_end)
            {
                linestring = i;

                if (linestring_start == index)
                {
                    first_point = true;
                }
                if (linestring_end == index)
                {
                    last_point = true;
                }

                if (first_point || last_point) break;
            }

            linestring_start += linestring_size;
        }
    }

    shape.points.erase(shape.points.begin() + index);

    if (shape.kind == Shape::Kind::Polygon)
    {
        // Update linestring size
        shape.linestring_sizes[linestring] -= 1;
        if (shape.linestring_sizes[linestring] == 0)
        {
            shape.linestring_sizes.erase(shape.linestring_sizes.begin() + linestring);
        }
    }

    refresh_shape_after_point_update(shape, editor, scene_model, mq, update_model);
}

void set_point_position(
    ShapeEditor* editor,
    Shape& shape,
    size_t index,
    hrz::GeoPosition2 position,
    SceneModel* scene_model,
    ClientMessageQueue* mq,
    bool update_model = true)
{
    assert(index < shape.points.size());

    shape.points[index] = normalize_position(position, shape.line_type);

    refresh_shape_after_point_update(shape, editor, scene_model, mq, update_model);
}

void collect_gpu_data(Shape& shape, std::vector<my::ResourceHandle>& resources)
{
    collect_gpu_data(shape.renderable, resources);

    for (auto& renderable : shape.renderable_outlines)
    {
        collect_gpu_data(renderable, resources);
    }

    collect_gpu_data(shape.renderable_control, resources);
}

void init_render(ShapeEditor* editor, hrz::Render* render)
{
    editor->polyline_stencil_shader =
        render->rc->retrieve_shader(hrz_shaders::ShapeEditorPolyline_stencil_name);
    editor->polygon_stencil_shader =
        render->rc->retrieve_shader(hrz_shaders::ShapeEditorPolygon_stencil_name);
    editor->shape_visual_shader =
        render->rc->retrieve_shader(hrz_shaders::ShapeEditorShape_visual_name);
    editor->shape_picking_shader =
        render->rc->retrieve_shader(hrz_shaders::ShapeEditorShape_picking_name);
    editor->control_visual_shader =
        render->rc->retrieve_shader(hrz_shaders::ShapeEditorControl_visual_name);
    editor->control_picking_shader =
        render->rc->retrieve_shader(hrz_shaders::ShapeEditorControl_picking_name);

    {
        auto mesh = generate_sphere_mesh({0, 0, 0}, 1.0F, 1);

        my::BufferResource vb_res(my::BufferResource::BufferType::Vertex);
        vb_res.size = mesh.vertex_data.size() * sizeof(lm::vec3);
        vb_res.usage = my::UsageHint::Static;
        vb_res.data = mesh.vertex_data.data();

        editor->control_vertex_buffer =
            render->rc->alloc(&vb_res, monitoring::systems::ShapeEditor);

        my::BufferResource ib_res(my::BufferResource::BufferType::Index);
        ib_res.size = mesh.indices.size() * sizeof(uint32_t);
        ib_res.usage = my::UsageHint::Dynamic;
        ib_res.data = mesh.indices.data();

        editor->control_index_buffer = render->rc->alloc(&ib_res, monitoring::systems::ShapeEditor);

        editor->control_vertex_count = mesh.indices.size();
    }

    {
        const lm::vec2 positions[] = {{-1, -1}, {4, -1}, {-1, 4}};

        my::BufferResource vb_res(my::BufferResource::BufferType::Vertex);
        vb_res.size = sizeof(positions);
        vb_res.usage = my::UsageHint::Static;
        vb_res.data = positions;

        editor->fullscreen_vertex_buffer =
            render->rc->alloc(&vb_res, monitoring::systems::ShapeEditor);

        my::VertexInputStream streams[] = {
            {0, editor->fullscreen_vertex_buffer, my::VertexFormat::Float32_2, 0, 0,
             my::VertexRate::PerVertex}
        };

        my::VertexInputResource vi_res;
        vi_res.attribs = streams;

        editor->fullscreen_vertex_input =
            render->rc->alloc(&vi_res, hrz::monitoring::systems::ShapeEditor);
    }
}

void deinit_render(ShapeEditor* editor, hrz::Render* render)
{
    render->rc->dealloc(editor->control_vertex_buffer);
    render->rc->dealloc(editor->control_index_buffer);
    render->rc->dealloc(editor->fullscreen_vertex_input);
    render->rc->dealloc(editor->fullscreen_vertex_buffer);

    for (auto resource : editor->unused_resources)
    {
        render->rc->dealloc(resource);
    }
}

// Return the mode the editor will be in once the events are applied.
ShapeEditor::Mode get_mode_after_events(ShapeEditor* editor)
{
    for (auto it = editor->events.rbegin(); it != editor->events.rend(); ++it)
    {
        if (it->kind == ShapeEditor::Event::Kind::ModeSwitch)
        {
            return it->mode();
        }
    }

    return editor->mode;
}

} // namespace

ShapeEditor* create_editor(PickingIdAllocator* pia)
{
    assert(pia);

    auto editor = new ShapeEditor();
    editor->picking_id = picking::allocate_system_id(pia);
    editor->disabled = !get_flag(Flag::EnableTerrain);

    return editor;
}

void destroy_editor(ShapeEditor* editor, PickingIdAllocator* pia, Render* render)
{
    assert(editor && pia && render);

    picking::release_system_id(pia, editor->picking_id);

    deinit_render(editor, render);

    for (auto it : editor->layer_ids_to_shape_handles)
    {
        auto shape = editor->shape_pool.get_object(it.second);
        collect_gpu_data(*shape, editor->unused_resources);
    }

    for (auto resource : editor->unused_resources)
    {
        render->rc->dealloc(resource);
    }

    delete editor;
}

void initialize_rendering(ShapeEditor* editor, Render* render)
{
    assert(editor && render);

    init_render(editor, render);
}

void register_layer(ShapeEditor* editor, SceneModel* scene_model, uint64_t global_layer_id)
{
    assert(editor && scene_model);

    if (editor->layer_ids_to_shape_handles.find(global_layer_id)
        != std::end(editor->layer_ids_to_shape_handles))
    {
        // Layer is already registered.
        return;
    }

    auto shape_handle = editor->shape_pool.alloc();
    Shape* shape = editor->shape_pool.get_object(shape_handle);
    shape->handle = shape_handle;
    shape->global_layer_id = global_layer_id;
    shape->object_ref = picking::ObjectReference{editor->picking_id, shape_handle, 0};

    shape->kind = Shape::Kind::Polyline;
    shape->line_type = LineType::Geodesics;
    shape->max_point_count = std::numeric_limits<size_t>::max();
    shape->stroke_color = {1.0, 0.0, 0.0, 1.0};
    shape->fill_color = {0.5, 0.0, 0.0, 0.5};
    shape->stroke_width = 8.0;
    shape->selected_stroke_color = {0.8, 0.0, 0.8, 0.8};
    shape->selected_fill_color = {0.5, 0.0, 0.5, 0.5};
    shape->selected_stroke_width = 8.0;
    shape->control_point_size = 12;
    shape->control_point_color = {1, 0, 0, 1};
    shape->midpoint_control_point_color = {0, 1, 0, 1};
    shape->selected_control_point_color = {1, 0, 1, 1};
    shape->show_midpoint_control_points = true;
    shape->always_show_control_points = false;
    shape->z_index = 0;
    shape->is_visible = true;
    shape->scene_views_bitset = (1 << hrz::SCENE_VIEW_COUNT) - 1;

    editor->layer_ids_to_shape_handles.insert({global_layer_id, shape_handle});

    shape->generate_renderable = true;
    shape->generate_control_renderable = true;
    editor->shapes_to_update_on_gpu.insert(shape_handle);

    hrz_proto::PathRoot root;
    root.mutable_editable_shape_layer()->set_opaque(global_layer_id);
    scene_model::register_element(scene_model, root);

    // Default data
    hrz_proto::EditableShapeLayer data;
    data.mutable_geometry()->set_type(hrz_proto::EditableShapeGeometryType::EDITABLE_POLYLINE);

    auto set_model_color = [](hrz_proto::Color* model, const lm::vec4 color)
    {
        model->set_r(color.r);
        model->set_g(color.g);
        model->set_b(color.b);
        model->set_a(color.a);
    };

    set_model_color(data.mutable_stroke_color(), shape->stroke_color);
    set_model_color(data.mutable_fill_color(), shape->fill_color);
    data.set_stroke_width(shape->stroke_width);
    set_model_color(data.mutable_selected_stroke_color(), shape->selected_stroke_color);
    set_model_color(data.mutable_selected_fill_color(), shape->selected_fill_color);
    data.set_selected_stroke_width(shape->selected_stroke_width);
    data.set_control_point_size(shape->control_point_size);
    set_model_color(data.mutable_control_point_color(), shape->control_point_color);
    set_model_color(
        data.mutable_midpoint_control_point_color(), shape->midpoint_control_point_color);
    set_model_color(
        data.mutable_selected_control_point_color(), shape->selected_control_point_color);
    data.set_show_midpoint_control_points(shape->show_midpoint_control_points);
    data.set_always_show_control_points(shape->always_show_control_points);

    data.set_z_index(shape->z_index);

    data.set_visible(shape->is_visible);
    data.mutable_scene_views()->set_bits(shape->scene_views_bitset);

    hrz_proto::EditableShapeLayerPathBuilder<SceneModelAccessor>(
        scene_model, root.editable_shape_layer())
        .set(data);
}

void unregister_layer(ShapeEditor* editor, uint64_t global_layer_id)
{
    assert(editor);

    auto it = editor->layer_ids_to_shape_handles.find(global_layer_id);
    if (it == editor->layer_ids_to_shape_handles.end()) return;

    auto handle = it->second;
    if (editor->selected_shape == handle)
    {
        if (editor->mode != ShapeEditor::Mode::NoAction)
        {
            editor->mode = ShapeEditor::Mode::NoAction;

            hrz_proto::ShapeEditorMessage message;
            message.set_mode_switch(get_current_mode(editor));
            editor->pending_messages.push_back(message);
        }

        editor->selected_shape = std::nullopt;
        editor->selected_control_point = std::nullopt;

        hrz_proto::ShapeEditorMessage message1;
        message1.mutable_control_point_selection();
        editor->pending_messages.push_back(message1);

        hrz_proto::ShapeEditorMessage message2;
        message2.mutable_shape_selection();
        editor->pending_messages.push_back(message2);
    }

    editor->unregistered_layers.insert(it->second);
    editor->layer_ids_to_shape_handles.erase(it);
}

void notify_model_update(
    ShapeEditor* editor,
    uint64_t global_layer_id,
    scene_model::UpdateType,
    const scene_model::EditableShapeLayerPath& path)
{
    assert(editor);

    auto it = editor->layer_ids_to_shape_handles.find(global_layer_id);
    if (it == editor->layer_ids_to_shape_handles.end()) return;

    auto shape_handle = it->second;
    auto shape = editor->shape_pool.get_object(it->second);

    if (path.leaf() || path.is_geometry())
    {
        shape->geometry_updated = true;
    }

    if (path.leaf() || path.is_stroke_color() || path.is_fill_color() || path.is_stroke_width()
        || path.is_selected_stroke_color() || path.is_selected_fill_color()
        || path.is_selected_stroke_width())
    {
        shape->style_updated = true;
    }

    if (path.leaf() || path.is_control_point_size() || path.is_control_point_color()
        || path.is_midpoint_control_point_color() || path.is_selected_control_point_color()
        || path.is_show_midpoint_control_points() || path.is_always_show_control_points())
    {
        shape->control_style_updated = true;
    }

    if (path.leaf() || path.is_z_index())
    {
        shape->z_index_updated = true;
    }

    if (path.leaf() || path.is_visible())
    {
        shape->visibility_updated = true;
    }

    if (path.leaf() || path.is_scene_views())
    {
        shape->scene_views_updated = true;
    }

    if (path.leaf() || path.is_model_update_frequency())
    {
        shape->model_update_frequency_updated = true;
    }

    editor->updated_layers.insert(shape_handle);
}

void pick(
    const ShapeEditor* editor,
    const picking::ObjectReference& ref,
    const lm::dvec3&,
    hrz_proto::PickResults& picking_results)
{
    assert(editor);

    if (ref.system_id != editor->picking_id) return;

    auto shape_handle = ref.complementary_id;
    const auto shape = editor->shape_pool.get_object(shape_handle);

    if (shape != nullptr && shape->is_visible)
    {
        hrz_proto::PickLayerResult* res = picking_results.mutable_results()->Add();
        res->mutable_layer()->set_type(hrz_proto::LayerType::EDITABLE_SHAPE);
        res->mutable_layer()->mutable_handle()->set_opaque(shape->global_layer_id);
        *res->mutable_editable_shape() = hrz_proto::Void();
    }
}

std::pair<size_t, size_t> make_typed_object_references(
    const ShapeEditor* editor,
    std::span<const picking::ObjectReference> refs,
    std::span<hrz_proto::TypedObjectReference> output)
{
    assert(refs.size() <= output.size());

    size_t in_cursor = 0;
    size_t out_cursor = 0;

    uint32_t last_complementary_id = 0;
    const Shape* shape = nullptr;

    // We assume that refs are sorted, thus if we find an object reference
    // without the correct system id, we assume there are no more inputs with
    // the same system id.
    while (in_cursor < refs.size() && refs[in_cursor].system_id == editor->picking_id)
    {
        const auto& ref = refs[in_cursor++];
        if (!shape || last_complementary_id != ref.complementary_id)
        {
            shape = editor->shape_pool.get_object(ref.complementary_id);
            last_complementary_id = ref.complementary_id;

            // Since there is no object ids for editable shapes, it's fine to only
            // insert in the output list once per complementery_id contiguous
            // sequence since the input references are assumed to be sorted.
            if (shape && shape->is_visible)
            {
                auto& typed_ref = output[out_cursor++];
                typed_ref.mutable_editable_shape()->set_opaque(shape->global_layer_id);
            }
        }
    }

    return std::make_pair(in_cursor, out_cursor);
}

namespace
{

std::optional<Shape::Handle> get_shape_handle_for_picking_id(
    ShapeEditor* editor,
    uint32_t complementary_picking_id)
{
    if (editor->shape_pool.is_valid(complementary_picking_id))
    {
        return {(Shape::Handle)complementary_picking_id};
    }

    return std::nullopt;
}

RenderRequest unregister_layers(ShapeEditor* editor, SceneModel* scene_model)
{
    HRZ_SCOPED_SAMPLE("unregister layers");

    RenderRequest render_request;

    for (auto shape_handle : editor->unregistered_layers)
    {
        auto shape = editor->shape_pool.get_object(shape_handle);

        if (shape)
        {
            hrz_proto::PathRoot root;
            root.mutable_editable_shape_layer()->set_opaque(shape->global_layer_id);
            scene_model::unregister_element(scene_model, root);

            collect_gpu_data(*shape, editor->unused_resources);

            editor->shape_pool.release(shape_handle);

            render_request.request_visual_render();
        }

        editor->updated_layers.erase(shape_handle);
        editor->shapes_to_update_on_gpu.erase(shape_handle);
        editor->renderable_shapes.erase(shape_handle);
    }

    editor->unregistered_layers.clear();

    return render_request;
}

/**
 * Apply changes that come from the model.
 */
RenderRequest work_shapes(ShapeEditor* editor, SceneModel* scene_model, ClientMessageQueue* mq)
{
    HRZ_SCOPED_SAMPLE("work shapes");

    RenderRequest render_request;

    auto switch_to_no_action_mode = [&]()
    {
        ShapeEditor::Event event;
        event.kind = ShapeEditor::Event::Kind::ModeSwitch;
        event.payload = ShapeEditor::Mode::NoAction;
        editor->events.push_front(event);
    };

    for (auto shape_handle : editor->updated_layers)
    {
        auto shape_ptr = editor->shape_pool.get_object(shape_handle);

        if (shape_ptr == nullptr) continue;

        auto& shape = *shape_ptr;

        hrz_proto::LayerHandle handle;
        handle.set_opaque(shape.global_layer_id);

        hrz_proto::EditableShapeLayerPathBuilder<SceneModelAccessor> builder(scene_model, handle);

        if (shape.geometry_updated)
        {
            shape.points.clear();
            shape.control_points.clear();
            shape.linestring_sizes.clear();

            shape.kind = Shape::Kind::Polyline;
            shape.max_point_count = std::numeric_limits<size_t>::max();
            auto type = builder.clone().geometry().type().get();
            switch (type)
            {
                case hrz_proto::EditableShapeGeometryType::EDITABLE_POINT:
                    shape.kind = Shape::Kind::Polyline;
                    shape.max_point_count = 1;
                    break;
                case hrz_proto::EditableShapeGeometryType::EDITABLE_LINE:
                    shape.kind = Shape::Kind::Polyline;
                    shape.max_point_count = 2;
                    break;
                case hrz_proto::EditableShapeGeometryType::EDITABLE_POLYLINE:
                    shape.kind = Shape::Kind::Polyline;
                    break;
                case hrz_proto::EditableShapeGeometryType::EDITABLE_POLYGON:
                    shape.kind = Shape::Kind::Polygon;
                    break;
                default: assert(false && "Unhandled case"); break;
            }

            shape.line_type = LineType::Geodesics;
            auto line_type = builder.clone().geometry().line_type().get();
            switch (line_type)
            {
                case hrz_proto::EditableShapeLineType::EDITABLE_SHAPE_LINE_GEODESIC:
                    shape.line_type = LineType::Geodesics;
                    break;
                case hrz_proto::EditableShapeLineType::EDITABLE_SHAPE_LINE_RHUMB_LINE:
                    shape.line_type = LineType::RhumbLines;
                    break;
                case hrz_proto::EditableShapeLineType::
                    EDITABLE_SHAPE_LINE_RHUMB_LINE_NOT_ACROSS_ANTIMERIDIAN:
                    shape.line_type = LineType::RhumbLinesNotAcrossAntimeridian;
                    break;
                default: assert(false && "Unhandled case"); break;
            }

            auto model = builder.clone().get();
            auto geometry = model.geometry();

            for (int i = 0; i + 1 < geometry.coords_size(); i += 2)
            {
                GeoPosition2 point{
                    lm::radians(geometry.coords(i + 0)), lm::radians(geometry.coords(i + 1))
                };
                shape.points.push_back(normalize_position(point, shape.line_type));
            }

            if (shape.kind == Shape::Kind::Polygon)
            {
                if (!shape.points.empty())
                {
                    if (geometry.linestring_sizes_size() > 0)
                    {
                        size_t linestring_start = 0;
                        for (int i = 0; i < geometry.linestring_sizes_size(); ++i)
                        {
                            size_t linestring_size = geometry.linestring_sizes(i);

                            if (linestring_start + linestring_size >= shape.points.size())
                            {
                                shape.linestring_sizes.push_back(
                                    shape.points.size() - linestring_start);
                                break;
                            }

                            shape.linestring_sizes.push_back(linestring_size);

                            linestring_start += linestring_size;
                        }
                    }
                    else
                    {
                        // If no linestring sizes have been given, as a fallback,
                        // create a single outer polygon from all the points.
                        shape.linestring_sizes.push_back(shape.points.size());

                        update_shape_model(shape, editor, scene_model, mq);
                    }
                }
            }

            update_control_points(shape);

            if (editor->selected_shape == shape.handle
                && editor->selected_control_point.has_value())
            {
                if (!control_point_can_be_selected(shape, editor->selected_control_point.value()))
                {
                    // The selected control point isn’t valid any more, because
                    // the shape has been reduced in effective length.

                    editor->selected_control_point = std::nullopt;

                    hrz_proto::ShapeEditorMessage message;
                    message.mutable_control_point_selection();
                    client_message_queue::enqueue_shape_editor_update_message(
                        mq, std::move(message));
                }
            }

            shape.extend_to_mouse_pointer = false;

            shape.show_midpoint_control_points =
                builder.clone().show_midpoint_control_points().get()
                && !(shape.kind == Shape::Kind::Polyline && shape.max_point_count <= 2);

            collect_gpu_data(shape, editor->unused_resources);
            shape.renderable_outlines.clear();

            shape.generate_renderable = true;

            editor->shapes_to_update_on_gpu.insert(shape.handle);
            editor->renderable_shapes.erase(shape.handle);

            hrz_proto::ShapeEditorMessage message;
            message.mutable_shape_geometry_update()->mutable_layer()->set_opaque(
                shape.global_layer_id);
            hrz::client_message_queue::enqueue_shape_editor_update_message(mq, std::move(message));

            shape.geometry_updated = false;

            switch_to_no_action_mode();
        }

        if (shape.style_updated)
        {
            shape.stroke_color = hrz::srgb_to_linear(
                hrz::convert_proto_color_to_float(builder.clone().stroke_color().get()));
            shape.stroke_color.rgb *= shape.stroke_color.a;
            shape.fill_color = hrz::srgb_to_linear(
                hrz::convert_proto_color_to_float(builder.clone().fill_color().get()));
            shape.fill_color.rgb *= shape.fill_color.a;
            shape.stroke_width = builder.clone().stroke_width().get();
            shape.selected_stroke_color = hrz::srgb_to_linear(
                hrz::convert_proto_color_to_float(builder.clone().selected_stroke_color().get()));
            shape.selected_stroke_color.rgb *= shape.selected_stroke_color.a;
            shape.selected_fill_color = hrz::srgb_to_linear(
                hrz::convert_proto_color_to_float(builder.clone().selected_fill_color().get()));
            shape.selected_fill_color.rgb *= shape.selected_fill_color.a;
            shape.selected_stroke_width = builder.clone().selected_stroke_width().get();
            shape.update_ubo = true;
            shape.style_updated = false;

            editor->shapes_to_update_on_gpu.insert(shape_handle);
        }

        if (shape.control_style_updated)
        {
            shape.control_point_size = builder.clone().control_point_size().get();
            shape.control_point_color = hrz::srgb_to_linear(
                hrz::convert_proto_color_to_float(builder.clone().control_point_color().get()));
            shape.midpoint_control_point_color = hrz::srgb_to_linear(
                hrz::convert_proto_color_to_float(
                    builder.clone().midpoint_control_point_color().get()));
            shape.selected_control_point_color = hrz::srgb_to_linear(
                hrz::convert_proto_color_to_float(
                    builder.clone().selected_control_point_color().get()));
            shape.show_midpoint_control_points =
                builder.clone().show_midpoint_control_points().get()
                && !(shape.kind == Shape::Kind::Polyline && shape.max_point_count <= 2);
            shape.always_show_control_points = builder.clone().always_show_control_points().get();
            shape.update_control_ubo = true;
            shape.control_style_updated = false;

            editor->shapes_to_update_on_gpu.insert(shape_handle);

            switch_to_no_action_mode();
        }

        if (shape.z_index_updated)
        {
            shape.z_index = builder.clone().z_index().get();
            shape.renderable.z_index = shape.z_index << 1;
            for (auto& renderable : shape.renderable_outlines)
            {
                renderable.z_index = (shape.z_index << 1) + 1;
            }
            shape.z_index_updated = false;

            render_request.request_visual_render();
        }

        if (shape.visibility_updated)
        {
            shape.is_visible = builder.clone().visible().get();

            if (!shape.is_visible && editor->selected_shape == shape.handle)
            {
                editor->selected_shape = std::nullopt;
                editor->selected_control_point = std::nullopt;
                shape.update_ubo = true;

                editor->shapes_to_update_on_gpu.insert(shape_handle);

                hrz_proto::ShapeEditorMessage message1;
                message1.mutable_control_point_selection();
                client_message_queue::enqueue_shape_editor_update_message(mq, std::move(message1));

                hrz_proto::ShapeEditorMessage message2;
                message2.mutable_shape_selection();
                client_message_queue::enqueue_shape_editor_update_message(mq, std::move(message2));
            }

            shape.visibility_updated = false;

            switch_to_no_action_mode();
            render_request.request_visual_render();
        }

        if (shape.scene_views_updated)
        {
            shape.scene_views_bitset = builder.clone().scene_views().bits().get();
            shape.renderable.data.scene_views_bitset = shape.scene_views_bitset;
            for (auto& renderable : shape.renderable_outlines)
            {
                renderable.data.scene_views_bitset = shape.scene_views_bitset;
            }
            shape.renderable_control.data.scene_views_bitset = shape.scene_views_bitset;
            shape.scene_views_updated = false;

            switch_to_no_action_mode();
            render_request.request_visual_render();
        }

        if (shape.model_update_frequency_updated)
        {
            shape.model_update_frequency = builder.clone().model_update_frequency().get();
            shape.model_update_frequency_updated = false;
        }
    }

    editor->updated_layers.clear();

    return render_request;
}

/**
 * Apply changes that come from the events.
 *
 * Return true if there are still events to work on.
 */
bool work_events(ShapeEditor* editor, SceneModel* scene_model, ClientMessageQueue* mq)
{
    HRZ_SCOPED_SAMPLE("work events");

    std::vector<ShapeEditor::Event> new_events;

    auto get_shape = [&](Shape::Handle shape_handle) -> Shape&
    {
        assert(editor->shape_pool.is_valid(shape_handle));
        return *editor->shape_pool.get_object(shape_handle);
    };

    auto send_control_point_selection_message = [&]()
    {
        hrz_proto::ShapeEditorMessage message;
        message.mutable_control_point_selection();

        if (editor->selected_control_point.has_value())
        {
            message.mutable_control_point_selection()->mutable_control_point_index()->set_value(
                (uint32_t)editor->selected_control_point.value());
        }

        client_message_queue::enqueue_shape_editor_update_message(mq, std::move(message));
    };

    // Returns true if success
    auto select_shape =
        [&](std::optional<Shape::Handle> shape_handle, bool allow_even_if_locked = false)
    {
        if (editor->shape_selection_is_locked && !allow_even_if_locked) return false;

        auto update_ubo = [&](Shape& shape)
        {
            shape.update_ubo = true;
            editor->shapes_to_update_on_gpu.insert(shape.handle);
        };

        if (editor->selected_shape == shape_handle) return true;

        if (editor->selected_shape.has_value())
        {
            auto& selected_shape = get_shape(editor->selected_shape.value());
            update_ubo(selected_shape);
        }

        bool success = true;
        if (shape_handle.has_value() && !editor->shape_pool.is_valid(shape_handle.value()))
        {
            shape_handle = std::nullopt;
            success = false;
        }

        editor->selected_control_point = std::nullopt;

        send_control_point_selection_message();

        editor->selected_shape = shape_handle;

        hrz_proto::ShapeEditorMessage message;
        message.mutable_shape_selection();

        if (editor->selected_shape.has_value())
        {
            auto& selected_shape = get_shape(editor->selected_shape.value());
            update_ubo(selected_shape);

            message.mutable_shape_selection()->mutable_layer()->set_opaque(
                selected_shape.global_layer_id);
        }

        client_message_queue::enqueue_shape_editor_update_message(mq, std::move(message));

        return success;
    };

    // Returns true if success
    auto select_control_point = [&](std::optional<size_t> control_point_index)
    {
        if (editor->selected_control_point == control_point_index) return true;

        if (!editor->selected_shape.has_value())
        {
            editor->selected_control_point = std::nullopt;
            send_control_point_selection_message();
            return false;
        }

        auto& selected_shape = get_shape(editor->selected_shape.value());

        bool success = true;
        if (control_point_index.has_value()
            && !control_point_can_be_selected(selected_shape, control_point_index.value()))
        {
            control_point_index = std::nullopt;
            success = false;
        }

        editor->selected_control_point = control_point_index;

        send_control_point_selection_message();

        selected_shape.update_control_ubo = true;

        editor->shapes_to_update_on_gpu.insert(selected_shape.handle);

        return success;
    };

    auto make_mouse_move_event = [&]()
    {
        ShapeEditor::Event event;
        event.kind = ShapeEditor::Event::Kind::MouseMove;
        ShapeEditor::Event::Mouse mouse;

        if (editor->last_mouse_move_event.has_value())
        {
            assert(editor->last_mouse_move_event.value().is_mouse_event());
            const auto& last_mouse_move = editor->last_mouse_move_event.value().mouse();
            mouse.screen_position = last_mouse_move.screen_position;
            mouse.is_in_viewport = last_mouse_move.is_in_viewport;
            mouse.view_index = last_mouse_move.view_index;
        }
        else
        {
            mouse.screen_position = std::nullopt;
            mouse.is_in_viewport = false;
            mouse.view_index = hrz_proto::SceneViewIndex::SCENE_VIEW_0;
        }

        mouse.has_picking_results = false;
        mouse.picking_ticket = std::nullopt;
        mouse.ecef_position = std::nullopt;
        event.payload = mouse;

        return event;
    };

    auto discard_mouse_drag = [&]()
    {
        editor->current_drag_mouse_start = std::nullopt;
        editor->current_mouse_down_shape = std::nullopt;
        editor->current_dragged_control_point = std::nullopt;
    };

    auto send_mode_switch_message = [&]()
    {
        hrz_proto::ShapeEditorMessage message;
        message.set_mode_switch(get_current_mode(editor));
        hrz::client_message_queue::enqueue_shape_editor_update_message(mq, std::move(message));
    };

    auto switch_mode = [&](ShapeEditor::Mode mode)
    {
        editor->mode = mode;
        send_mode_switch_message();

        if (editor->selected_shape.has_value())
        {
            auto& selected_shape = get_shape(editor->selected_shape.value());

            // Needed to trigger update of `pick_selected_control_point`
            // and `pick_midpoint_control_points`.
            selected_shape.update_control_ubo = true;

            editor->shapes_to_update_on_gpu.insert(selected_shape.handle);
        }
    };

    auto switch_to_append_mode = [&]()
    {
        auto& shape = get_shape(editor->selected_shape.value());

        // Cannot append from a mid-point control point.
        bool non_midpoint_point_selected = editor->selected_control_point.has_value()
            && editor->selected_control_point.value() % 2 == 0;

        // Cannot append to a shape that has reach its max point count.
        bool shape_can_be_extended = shape.points.size() < shape.max_point_count;

        if (non_midpoint_point_selected && shape_can_be_extended)
        {
            assert(editor->selected_control_point.value() < shape.control_points.size());

            switch_mode(ShapeEditor::Mode::AppendPoint);

            size_t control_point = editor->selected_control_point.value();
            size_t point = control_point / 2;

            if (shape.kind == Shape::Kind::Polyline && shape.points.size() > 1
                && control_point == 0)
            {
                // Append from the front
                insert_point(editor, shape, 0, shape.points.front(), scene_model, mq, false);
                select_control_point(0);
            }
            else
            {
                insert_point(
                    editor, shape, point + 1, shape.points.at(point), scene_model, mq, false);
                select_control_point((point + 1) * 2);
            }

            shape.extend_to_mouse_pointer = true;

            // Don't wait for the mouse to move before
            // visually extending the line to the pointer.
            // The shape model will be updated (if necessary)
            // thanks to this event.
            new_events.push_back(make_mouse_move_event());
        }
        else if (shape.points.empty())
        {
            // There are no points currently, so appending must be
            // possible even with no selected control point.
            switch_mode(ShapeEditor::Mode::AppendPoint);
        }
    };

    auto move_extension_to_pointer = [&](Shape& shape, const hrz::GeoPosition2 geo)
    {
        assert(editor->selected_control_point.value() < shape.control_points.size());

        size_t control_point = editor->selected_control_point.value();
        size_t point = control_point / 2;

        set_point_position(
            editor, shape, point, geo, scene_model, mq,
            shape.model_update_frequency >= hrz_proto::APPEND_MODEL_UPDATES);
    };

    auto remove_extension_to_pointer = [&](Shape& shape)
    {
        assert(shape.extend_to_mouse_pointer);
        assert(editor->selected_control_point.has_value());
        assert(editor->selected_control_point.value() < shape.control_points.size());

        size_t control_point = editor->selected_control_point.value();
        size_t point = control_point / 2;

        remove_point(
            editor, shape, point, scene_model, mq,
            shape.model_update_frequency >= hrz_proto::APPEND_MODEL_UPDATES);
        shape.extend_to_mouse_pointer = false;

        if (shape.control_points.empty())
        {
            select_control_point(std::nullopt);
        }
        else if (editor->selected_control_point.value() != 0)
        {
            select_control_point({control_point - 2});
        }
        else
        {
            select_control_point({0});
        }
    };

    auto stop_append = [&]()
    {
        if (editor->selected_shape.has_value())
        {
            auto& shape = get_shape(editor->selected_shape.value());

            if (shape.extend_to_mouse_pointer)
            {
                remove_extension_to_pointer(shape);
            }
        }
    };

    auto append_position = [&](Shape& shape, const hrz::GeoPosition2& geo)
    {
        if (shape.extend_to_mouse_pointer)
        {
            assert(editor->selected_control_point.has_value());
            assert(editor->selected_control_point.value() < shape.control_points.size());

            size_t control_point = editor->selected_control_point.value();
            size_t point = control_point / 2;

            set_point_position(editor, shape, point, geo, scene_model, mq, false);

            bool switch_modes = false;

            if (shape.points.size() < shape.max_point_count)
            {
                size_t new_point = point == 0 ? 0 : point + 1;
                insert_point(editor, shape, new_point, geo, scene_model, mq, false);
                select_control_point(new_point * 2);
            }
            else
            {
                // Line is full
                shape.extend_to_mouse_pointer = false;
                switch_modes = true;

                assert(shape.points.size() == shape.max_point_count);
            }

            update_shape_model(shape, editor, scene_model, mq);

            if (switch_modes)
            {
                switch_mode(ShapeEditor::Mode::NoAction);
            }
        }
        else if (shape.points.empty())
        {
            insert_point(editor, shape, 0, geo, scene_model, mq, false);

            bool switch_modes = false;

            if (shape.max_point_count > 1)
            {
                // We can continue appending points.
                insert_point(editor, shape, 1, geo, scene_model, mq, false);
                select_control_point(2);
                shape.extend_to_mouse_pointer = true;
            }
            else
            {
                // The shape is of type point, we must stop here.
                shape.extend_to_mouse_pointer = false;
                switch_modes = true;
            }

            update_shape_model(shape, editor, scene_model, mq);

            if (switch_modes)
            {
                switch_mode(ShapeEditor::Mode::NoAction);
            }
        }
    };

    for (auto it = editor->events.begin(); it != editor->events.end();)
    {
        auto& event = *it;

        if (event.kind == ShapeEditor::Event::Kind::ModeSwitch)
        {
            auto new_mode = event.mode();

            if (editor->mode != new_mode)
            {
                if (new_mode == ShapeEditor::Mode::AppendPoint)
                {
                    if (editor->selected_shape.has_value())
                    {
                        switch_to_append_mode();
                    }
                }
                else if (new_mode == ShapeEditor::Mode::NoAction)
                {
                    stop_append();
                    switch_mode(ShapeEditor::Mode::NoAction);
                }
                else
                {
                    assert(false && "Unhandled case");
                }

                discard_mouse_drag();
            }
        }
        else if (event.kind == ShapeEditor::Event::Kind::ShapeSelection)
        {
            auto layer_id = event.layer_id();
            std::optional<Shape::Handle> handle = std::nullopt;

            if (layer_id.has_value())
            {
                auto it = editor->layer_ids_to_shape_handles.find(layer_id.value());

                if (it != editor->layer_ids_to_shape_handles.end())
                {
                    handle = {it->second};
                }
                else
                {
                    HRZ_LOG_WARNING(
                        "Tried to select the editable shape of an unknown layer: {}",
                        event.layer_id().value());
                }
            }

            if (handle != editor->selected_shape)
            {
                stop_append();
            }

            select_shape(handle, true);
            discard_mouse_drag();
            switch_mode(ShapeEditor::Mode::NoAction);
        }
        else if (event.kind == ShapeEditor::Event::Kind::ControlPointSelection)
        {
            std::optional<size_t> control_point_index = event.control_point_index().has_value()
                ? std::optional<size_t>(event.control_point_index().value())
                : std::nullopt;

            if (!select_control_point(control_point_index))
            {
                HRZ_LOG_WARNING(
                    "Tried to select an invalid control point: {}",
                    event.control_point_index().value());
            }

            discard_mouse_drag();
            switch_mode(ShapeEditor::Mode::NoAction);
        }
        else if (event.is_mouse_event())
        {
            auto& mouse = event.mouse();

            if (!mouse.has_picking_results) break;

            if (event.kind == ShapeEditor::Event::Kind::MouseButtonDown)
            {
                if (mouse.button == platform::Event::MouseButton::Left)
                {
                    if (editor->mode == ShapeEditor::Mode::NoAction)
                    {
                        if (mouse.screen_position.has_value() && mouse.picked_id.has_value())
                        {
                            editor->current_drag_mouse_start = mouse;

                            editor->current_mouse_down_shape =
                                get_shape_handle_for_picking_id(editor, mouse.picked_id.value().x);

                            if (mouse.picked_id.value().y != NoControlPoint)
                            {
                                editor->current_dragged_control_point = {mouse.picked_id.value().y};
                            }
                            else
                            {
                                editor->current_dragged_control_point = std::nullopt;
                            }
                        }
                        else
                        {
                            editor->current_drag_mouse_start = std::nullopt;
                            editor->current_mouse_down_shape = std::nullopt;
                        }
                    }
                }
            }
            else if (event.kind == ShapeEditor::Event::Kind::MouseButtonUp)
            {
                if (mouse.button == platform::Event::MouseButton::Left)
                {
                    if (editor->mode == ShapeEditor::Mode::DragPoint)
                    {
                        assert(editor->selected_shape.has_value());
                        auto& shape =
                            *editor->shape_pool.get_object(editor->selected_shape.value());
                        update_shape_model(shape, editor, scene_model, mq);

                        switch_mode(ShapeEditor::Mode::NoAction);
                    }

                    discard_mouse_drag();
                }
            }
            else if (event.kind == ShapeEditor::Event::Kind::MouseClick)
            {
                if (editor->mode == ShapeEditor::Mode::NoAction)
                {
                    if (mouse.button == platform::Event::MouseButton::Left)
                    {
                        if (mouse.picked_id.has_value())
                        {
                            auto picked_id = mouse.picked_id.value();
                            auto picked_shape_handle =
                                get_shape_handle_for_picking_id(editor, picked_id.x);
                            select_shape(picked_shape_handle);

                            if (picked_id.y != NoControlPoint)
                            {
                                select_control_point({picked_id.y});
                            }
                            else
                            {
                                select_control_point(std::nullopt);
                            }
                        }
                        else
                        {
                            select_shape(std::nullopt);
                        }
                    }
                }
                else if (editor->mode == ShapeEditor::Mode::AppendPoint)
                {
                    if (mouse.button == platform::Event::MouseButton::Left)
                    {
                        bool event_handled = false;
                        auto& shape = get_shape(editor->selected_shape.value());

                        if (mouse.picked_id.has_value())
                        {
                            auto picked_id = mouse.picked_id.value();
                            auto picked_shape_handle =
                                get_shape_handle_for_picking_id(editor, picked_id.x);

                            if (picked_shape_handle.has_value()
                                && picked_shape_handle.value() == shape.handle)
                            {
                                assert(editor->selected_control_point.has_value());

                                auto abs_diff = [](size_t a, size_t b)
                                { return a >= b ? a - b : b - a; };

                                // If the index difference between the selected control point
                                // and the picked control point is 2, it means that the picked
                                // control point is the one of the point of the shape that is
                                // the closest to the one being appended.
                                if (abs_diff(picked_id.y, editor->selected_control_point.value())
                                    == 2)
                                {
                                    stop_append();
                                    switch_mode(ShapeEditor::Mode::NoAction);
                                    event_handled = true;
                                }
                            }
                        }

                        if (!event_handled && mouse.ecef_position.has_value())
                        {
                            append_position(shape, hrz::ecef_to_geo2(mouse.ecef_position.value()));
                        }
                    }
                    else if (mouse.button == platform::Event::MouseButton::Right)
                    {
                        stop_append();
                        switch_mode(ShapeEditor::Mode::NoAction);
                    }
                }
            }
            else if (event.kind == ShapeEditor::Event::Kind::MouseMove)
            {
                if (editor->mode == ShapeEditor::Mode::AppendPoint)
                {
                    if (editor->selected_shape.has_value()
                        && editor->selected_control_point.has_value()
                        && mouse.ecef_position.has_value())
                    {
                        auto& shape = get_shape(editor->selected_shape.value());
                        auto latlon = hrz::ecef_to_geo2(mouse.ecef_position.value());
                        move_extension_to_pointer(shape, latlon);
                    }
                }
                else if (editor->mode == ShapeEditor::Mode::NoAction)
                {
                    if (mouse.screen_position.has_value() && mouse.ecef_position.has_value()
                        && editor->current_drag_mouse_start.has_value()
                        && editor->current_drag_mouse_start.value().view_index == mouse.view_index
                        && editor->current_mouse_down_shape.has_value()
                        && editor->current_dragged_control_point.has_value()
                        && lm::length2(
                               editor->current_drag_mouse_start.value().screen_position.value()
                               - mouse.screen_position.value())
                            > ClickMaxDistanceSquared)
                    {
                        select_shape(editor->current_mouse_down_shape);

                        if (editor->current_dragged_control_point.value() % 2 != 0)
                        {
                            // The control point is a midpoint.

                            auto& shape = get_shape(editor->selected_shape.value());
                            size_t new_point_index =
                                editor->current_dragged_control_point.value() / 2 + 1;

                            assert(new_point_index <= shape.points.size());
                            assert(
                                shape.max_point_count >= 3
                                && shape.points.size() < shape.max_point_count);

                            hrz::GeoPosition2 geo = hrz::ecef_to_geo2(mouse.ecef_position.value());
                            insert_point(editor, shape, new_point_index, geo, scene_model, mq);

                            // The midpoint has been converted into an actual point, that
                            // makes a control point appear just before it, shift the index
                            // of the dragged point by one.
                            editor->current_dragged_control_point = {
                                editor->current_dragged_control_point.value() + 1
                            };
                        }

                        select_control_point(editor->current_dragged_control_point);

                        switch_mode(ShapeEditor::Mode::DragPoint);
                    }
                }
                else if (editor->mode == ShapeEditor::Mode::DragPoint)
                {
                    if (mouse.view_index == editor->current_drag_mouse_start.value().view_index
                        && mouse.ecef_position.has_value())
                    {
                        auto& shape = get_shape(editor->selected_shape.value());
                        size_t point_index = editor->current_dragged_control_point.value() / 2;

                        hrz::GeoPosition2 geo = hrz::ecef_to_geo2(mouse.ecef_position.value());
                        set_point_position(
                            editor, shape, point_index, geo, scene_model, mq,
                            shape.model_update_frequency >= hrz_proto::DRAG_MODEL_UPDATES);
                    }
                }

                if (editor->mode != ShapeEditor::Mode::DragPoint
                    && editor->current_drag_mouse_start.has_value()
                    && (!mouse.screen_position.has_value()
                        || (mouse.screen_position.has_value()
                            && lm::length2(
                                   editor->current_drag_mouse_start.value().screen_position.value()
                                   - mouse.screen_position.value())
                                > ClickMaxDistanceSquared)))
                {
                    discard_mouse_drag();
                }
            }
        }
        else if (event.kind == ShapeEditor::Event::Kind::Delete)
        {
            if (editor->mode == ShapeEditor::Mode::AppendPoint)
            {
                stop_append();
                switch_mode(ShapeEditor::Mode::NoAction);
            }
            else if (
                editor->selected_shape.has_value() && editor->selected_control_point.has_value())
            {
                if (editor->mode == ShapeEditor::Mode::DragPoint)
                {
                    switch_mode(ShapeEditor::Mode::NoAction);
                }

                // Check that the control point is not a mid-point control point.
                if (editor->selected_control_point.value() % 2 == 0)
                {
                    // Delete the point.
                    auto& shape = get_shape(editor->selected_shape.value());
                    size_t point_index = editor->selected_control_point.value() / 2;

                    remove_point(editor, shape, point_index, scene_model, mq);

                    select_control_point(std::nullopt);
                }
            }

            discard_mouse_drag();
        }

        it = editor->events.erase(it);
    }

    if (!new_events.empty())
    {
        for (const auto& event : new_events)
        {
            editor->events.push_back(event);
        }
        return true;
    }
    else
    {
        return false;
    }
}

} // namespace

RenderRequest work(ShapeEditor* editor, SceneModel* scene_model, ClientMessageQueue* mq)
{
    HRZ_SCOPED_SAMPLE("shape editor work");

    assert(editor && scene_model && mq);

    assert(!editor->selected_shape || editor->shape_pool.is_valid(editor->selected_shape.value()));

    if (editor->disabled) return {};

    RenderRequest render_request;

    for (auto& message : editor->pending_messages)
    {
        hrz::client_message_queue::enqueue_shape_editor_update_message(mq, std::move(message));
    }
    editor->pending_messages.clear();

    render_request |= unregister_layers(editor, scene_model);

    render_request |= work_shapes(editor, scene_model, mq);

    // `work_events()` can create new events, so call it in a loop
    // until the event stack is empty.
    while (work_events(editor, scene_model, mq))
    {
    }

    return render_request;
}

/**
 * Resolve picking for mouse events.
 */
void work_picking(ShapeEditor* editor, PickingSystem* picking, hrz_proto::SceneViewIndex view_index)
{
    HRZ_SCOPED_SAMPLE("shape editor work picking");

    assert(editor && picking);

    if (editor->disabled) return;

    for (auto& event : editor->events)
    {
        if (event.is_mouse_event() && event.mouse().view_index == view_index)
        {
            auto& mouse = event.mouse();
            if (!mouse.has_picking_results)
            {
                // Don't make pickings for mouse move events when
                // there is no need for them.
                if ((event.kind == ShapeEditor::Event::Kind::MouseMove
                     && !(
                         editor->current_drag_mouse_start != std::nullopt
                         || editor->mode == ShapeEditor::Mode::AppendPoint
                         || editor->mode == ShapeEditor::Mode::DragPoint))
                    || !mouse.is_in_viewport || !mouse.screen_position.has_value())
                {
                    mouse.has_picking_results = true;
                }
                else if (!mouse.picking_ticket.has_value())
                {
                    auto position = mouse.initial_screen_position.has_value()
                        ? mouse.initial_screen_position.value()
                        : mouse.screen_position.value();
                    mouse.picking_ticket = picking::schedule_pick(picking, position);
                    mouse.has_picking_results = false;
                }
                else
                {
                    hrz::picking::PositionResult pick_result;

                    if (hrz::picking::retrieve_result(
                            picking, mouse.picking_ticket.value(), &pick_result))
                    {
                        if (pick_result.ref.system_id != 0 && pick_result.position.has_value())
                        {
                            mouse.ecef_position = {lm::dvec3(
                                pick_result.position->x, pick_result.position->y,
                                pick_result.position->z)};
                        }

                        if (pick_result.ref.system_id == editor->picking_id)
                        {
                            mouse.picked_id = {
                                pick_result.ref.complementary_id, pick_result.ref.object_id
                            };
                        }

                        mouse.has_picking_results = true;
                    }
                }
            }
        }
    }
}

namespace
{

bool consume_mouse_event(ShapeEditor* editor, hrz_proto::SceneViewIndex view_index)
{
    return editor->current_drag_mouse_start.has_value()
        && editor->current_drag_mouse_start.value().view_index == view_index
        && (get_mode_after_events(editor) == ShapeEditor::Mode::DragPoint
            || (editor->current_mouse_down_shape.has_value()
                && editor->current_dragged_control_point.has_value()));
}

bool handle_platform_event(
    ShapeEditor* editor,
    const platform::Event& event,
    bool is_in_viewport,
    hrz_proto::SceneViewIndex view_index,
    const CameraViewInfo& view_info)
{
    bool consume_event = false;

    switch (event.kind)
    {
        case platform::Event::Kind::MouseButtonDown:
            if (is_in_viewport)
            {
                auto screen_position = lm::ivec2(event.mouse_button.x, event.mouse_button.y);

                ShapeEditor::Event editor_event;
                editor_event.kind = ShapeEditor::Event::Kind::MouseButtonDown;
                ShapeEditor::Event::Mouse mouse;
                mouse.initial_screen_position = {screen_position};
                mouse.screen_position = {screen_position};
                mouse.is_in_viewport = is_in_viewport;
                mouse.view_index = view_index;
                mouse.button = {event.mouse_button.button};
                mouse.has_picking_results = false;
                mouse.picking_ticket = std::nullopt;
                mouse.ecef_position = std::nullopt;
                editor_event.payload = mouse;
                editor->events.push_back(editor_event);
                editor->last_button_down_events[view_index] = {editor_event};

                consume_event = consume_mouse_event(editor, view_index);
            }
            break;
        case platform::Event::Kind::MouseButtonUp:
        {
            ShapeEditor::Event editor_event;
            editor_event.kind = ShapeEditor::Event::Kind::MouseButtonUp;
            ShapeEditor::Event::Mouse mouse;
            mouse.screen_position = {lm::ivec2(event.mouse_button.x, event.mouse_button.y)};
            mouse.is_in_viewport = is_in_viewport;
            mouse.view_index = view_index;
            mouse.button = {event.mouse_button.button};
            mouse.has_picking_results = false;
            mouse.picking_ticket = std::nullopt;
            mouse.ecef_position = std::nullopt;
            editor_event.payload = mouse;
            editor->events.push_back(editor_event);
        }

            if (editor->last_button_down_events[view_index].has_value())
            {
                auto& button_down = editor->last_button_down_events[view_index].value().mouse();
                auto screen_position = lm::ivec2(event.mouse_button.x, event.mouse_button.y);
                if (lm::length2(button_down.screen_position.value() - screen_position)
                        <= ClickMaxDistanceSquared
                    && view_index == button_down.view_index
                    && button_down.button == event.mouse_button.button)
                {
                    ShapeEditor::Event editor_event;
                    editor_event.kind = ShapeEditor::Event::Kind::MouseClick;
                    ShapeEditor::Event::Mouse mouse;
                    mouse.screen_position = {screen_position};
                    mouse.is_in_viewport = is_in_viewport;
                    mouse.view_index = view_index;
                    mouse.button = {event.mouse_button.button};
                    mouse.has_picking_results = false;
                    mouse.picking_ticket = std::nullopt;
                    mouse.ecef_position = std::nullopt;
                    editor_event.payload = mouse;
                    editor->events.push_back(editor_event);
                }
            }

            if (editor->last_button_down_events[view_index].has_value())
            {
                editor->last_button_down_events[view_index] = std::nullopt;
            }

            break;
        case platform::Event::Kind::MouseButtonDoubleClick:
        {
            auto mode_after = get_mode_after_events(editor);

            if (is_in_viewport && mode_after == ShapeEditor::Mode::AppendPoint)
            {
                ShapeEditor::Event editor_event;
                editor_event.kind = ShapeEditor::Event::Kind::ModeSwitch;
                editor_event.payload = ShapeEditor::Mode::NoAction;
                editor->events.push_back(editor_event);

                consume_event = true;
            }
            break;
        }
        case platform::Event::Kind::MouseMove:
        {
            if (is_in_viewport)
            {
                auto screen_position = lm::ivec2(event.mouse_move.x, event.mouse_move.y);

                ShapeEditor::Event editor_event;
                editor_event.kind = ShapeEditor::Event::Kind::MouseMove;
                ShapeEditor::Event::Mouse mouse;
                mouse.screen_position = {screen_position};
                mouse.is_in_viewport = is_in_viewport;
                mouse.view_index = view_index;
                mouse.has_picking_results = false;
                mouse.picking_ticket = std::nullopt;
                mouse.ecef_position = std::nullopt;
                editor_event.payload = mouse;

                if (!editor->events.empty())
                {
                    auto& last_event = editor->events.back();
                    if (last_event.kind == ShapeEditor::Event::Kind::MouseMove
                        && !last_event.mouse().picking_ticket.has_value()
                        && !last_event.mouse().has_picking_results
                        && last_event.mouse().view_index == view_index)
                    {
                        // Replace last mouse move event, to avoid too many
                        // picking requests
                        editor->events.pop_back();
                    }
                }

                editor->events.push_back(editor_event);

                editor->last_mouse_move_event = editor_event;

                consume_event = consume_mouse_event(editor, view_index);
            }
            else if (
                editor->last_mouse_move_event.has_value()
                && editor->last_mouse_move_event.value().mouse().view_index == view_index)
            {
                editor->last_mouse_move_event = std::nullopt;
            }

            if (editor->last_button_down_events[view_index].has_value())
            {
                float distance = is_in_viewport
                    ? lm::length(
                          lm::ivec2(event.mouse_move.x, event.mouse_move.y)
                          - editor->last_button_down_events[view_index]
                                .value()
                                .mouse()
                                .screen_position.value())
                    : std::numeric_limits<float>::max();
                if (distance > ClickMaxDistance)
                {
                    editor->last_button_down_events[view_index] = std::nullopt;
                }
            }
        }
        break;
        case platform::Event::Kind::MouseLeave:
        {
            ShapeEditor::Event editor_event;
            editor_event.kind = ShapeEditor::Event::Kind::MouseMove;
            ShapeEditor::Event::Mouse mouse;
            mouse.screen_position = std::nullopt;
            mouse.is_in_viewport = is_in_viewport;
            mouse.view_index = view_index;
            mouse.has_picking_results = false;
            mouse.picking_ticket = std::nullopt;
            mouse.ecef_position = std::nullopt;
            editor_event.payload = mouse;
            editor->events.push_back(editor_event);

            editor->last_mouse_move_event = std::nullopt;
        }
        break;
        default: break;
    }

    return consume_event;
}

bool handle_gesture_event(
    ShapeEditor* editor,
    const gestures::Event& event,
    bool is_in_viewport,
    hrz_proto::SceneViewIndex view_index,
    const CameraViewInfo&)
{
    bool consume_event = false;

    switch (event.kind)
    {
        case gestures::Event::Kind::New: break;
        case gestures::Event::Kind::Qualification:
        {
            if (is_in_viewport && event.finger_count() == hrz::gestures::FingerCount::One)
            {
                const auto& single_finger_gesture = event.single_finger_gesture();
                auto screen_position =
                    lm::ivec2(single_finger_gesture.position.x, single_finger_gesture.position.y);
                auto initial_screen_position = lm::ivec2(
                    single_finger_gesture.initial_position.x,
                    single_finger_gesture.initial_position.y);

                {
                    ShapeEditor::Event editor_event;
                    editor_event.kind = ShapeEditor::Event::Kind::MouseButtonDown;
                    ShapeEditor::Event::Mouse mouse;
                    mouse.initial_screen_position = {initial_screen_position};
                    mouse.screen_position = {screen_position};
                    mouse.is_in_viewport = is_in_viewport;
                    mouse.view_index = view_index;
                    mouse.button = platform::Event::MouseButton::Left;
                    mouse.has_picking_results = false;
                    mouse.picking_ticket = std::nullopt;
                    mouse.ecef_position = std::nullopt;
                    editor_event.payload = mouse;
                    editor->events.push_back(editor_event);
                    editor->button_down_gesture_id = {single_finger_gesture.id};
                }

                if (single_finger_gesture.type == hrz::gestures::SingleFingerGesture::Type::Tap)
                {
                    ShapeEditor::Event editor_event;
                    editor_event.kind = ShapeEditor::Event::Kind::MouseClick;
                    ShapeEditor::Event::Mouse mouse;
                    mouse.screen_position = {screen_position};
                    mouse.is_in_viewport = is_in_viewport;
                    mouse.view_index = view_index;
                    mouse.button = platform::Event::MouseButton::Left;
                    mouse.has_picking_results = false;
                    mouse.picking_ticket = std::nullopt;
                    mouse.ecef_position = std::nullopt;
                    editor_event.payload = mouse;
                    editor->events.push_back(editor_event);
                }
                else if (
                    single_finger_gesture.type == hrz::gestures::SingleFingerGesture::Type::Drag
                    && !editor->drag_gesture_id.has_value())
                {
                    editor->drag_gesture_id = {single_finger_gesture.id};

                    ShapeEditor::Event editor_event;
                    editor_event.kind = ShapeEditor::Event::Kind::MouseMove;
                    ShapeEditor::Event::Mouse mouse;
                    mouse.screen_position = {screen_position};
                    mouse.is_in_viewport = is_in_viewport;
                    mouse.view_index = view_index;
                    mouse.has_picking_results = false;
                    mouse.picking_ticket = std::nullopt;
                    mouse.ecef_position = std::nullopt;
                    editor_event.payload = mouse;
                    editor->events.push_back(editor_event);

                    // @Note(qdebroise): When using touch events the planet can move a bit before
                    // the shape editor responds to the dragging events. In order to prevent this we
                    // would need to catpure the qualification event here when relevant but doing
                    // this would force us to do the work_event() here which isn't ideal.
                }

                consume_event = consume_mouse_event(editor, view_index);
            }
            break;
        }
        case hrz::gestures::Event::Kind::Move:
        {
            if (event.finger_count() == hrz::gestures::FingerCount::One)
            {
                const auto& single_finger_gesture = event.single_finger_gesture();
                auto screen_position =
                    lm::ivec2(single_finger_gesture.position.x, single_finger_gesture.position.y);
                if (editor->drag_gesture_id == single_finger_gesture.id)
                {
                    if (is_in_viewport)
                    {
                        ShapeEditor::Event editor_event;
                        editor_event.kind = ShapeEditor::Event::Kind::MouseMove;
                        ShapeEditor::Event::Mouse mouse;
                        mouse.screen_position = {screen_position};
                        mouse.is_in_viewport = is_in_viewport;
                        mouse.view_index = view_index;
                        mouse.has_picking_results = false;
                        mouse.picking_ticket = std::nullopt;
                        mouse.ecef_position = std::nullopt;
                        editor_event.payload = mouse;

                        if (!editor->events.empty())
                        {
                            auto& last_event = editor->events.back();
                            if (last_event.kind == ShapeEditor::Event::Kind::MouseMove
                                && !last_event.mouse().picking_ticket.has_value()
                                && !last_event.mouse().has_picking_results
                                && last_event.mouse().view_index == view_index)
                            {
                                // Replace last mouse move event, to avoid too many
                                // picking requests
                                editor->events.pop_back();
                            }
                        }

                        editor->events.push_back(editor_event);

                        editor->last_mouse_move_event = editor_event;

                        consume_event = consume_mouse_event(editor, view_index);
                    }
                    else if (
                        editor->last_mouse_move_event.has_value()
                        && editor->last_mouse_move_event.value().mouse().view_index == view_index)
                    {
                        editor->last_mouse_move_event = std::nullopt;
                    }
                }
            }
            break;
        }
        case hrz::gestures::Event::Kind::End:
        {
            if (event.finger_count() == hrz::gestures::FingerCount::One)
            {
                const auto& single_finger_gesture = event.single_finger_gesture();
                auto screen_position =
                    lm::ivec2(single_finger_gesture.position.x, single_finger_gesture.position.y);

                if (single_finger_gesture.id == editor->button_down_gesture_id)
                {
                    ShapeEditor::Event editor_event;
                    editor_event.kind = ShapeEditor::Event::Kind::MouseButtonUp;
                    ShapeEditor::Event::Mouse mouse;
                    mouse.screen_position = {screen_position};
                    mouse.is_in_viewport = is_in_viewport;
                    mouse.view_index = view_index;
                    mouse.button = platform::Event::MouseButton::Left;
                    mouse.has_picking_results = false;
                    mouse.picking_ticket = std::nullopt;
                    mouse.ecef_position = std::nullopt;
                    editor_event.payload = mouse;
                    editor->events.push_back(editor_event);

                    editor->last_mouse_move_event = std::nullopt;
                    editor->button_down_gesture_id = std::nullopt;
                }

                if (editor->drag_gesture_id == single_finger_gesture.id)
                {
                    editor->drag_gesture_id = std::nullopt;
                }
            }
            break;
        }
        default: assert(false && "Unhandled case"); break;
    }

    return consume_event;
}

} // namespace

bool handle_event(
    ShapeEditor* editor,
    const ViewportEvent& event,
    hrz_proto::SceneViewIndex view_index,
    const CameraViewInfo& view_info)
{
    HRZ_SCOPED_SAMPLE("shape editor handle event");

    assert(editor);

    if (editor->disabled) return false;

    switch (event.kind)
    {
        case Event::Kind::Platform:
            return handle_platform_event(
                editor, event.platform, event.is_in_viewport, view_index, view_info);
        case Event::Kind::Gesture:
            return handle_gesture_event(
                editor, event.gesture, event.is_in_viewport, view_index, view_info);
        default: break;
    }

    return false;
}

std::optional<uint64_t> get_selected_shape(const ShapeEditor* editor)
{
    assert(editor);

    if (editor->selected_shape.has_value())
    {
        auto shape = editor->shape_pool.get_object(editor->selected_shape.value());
        if (shape != nullptr)
        {
            return {shape->global_layer_id};
        }
    }

    return std::nullopt;
}

void select_shape(ShapeEditor* editor, std::optional<uint64_t> layer_id)
{
    assert(editor);

    ShapeEditor::Event event;
    event.kind = ShapeEditor::Event::Kind::ShapeSelection;
    event.payload = layer_id;
    assert(event.layer_id() == layer_id);
    editor->events.push_back(event);
}

void lock_shape_selection(ShapeEditor* editor)
{
    assert(editor);

    editor->shape_selection_is_locked = true;
}

void unlock_shape_selection(ShapeEditor* editor)
{
    assert(editor);

    editor->shape_selection_is_locked = false;
}

std::optional<uint32_t> get_selected_control_point(const ShapeEditor* editor)
{
    assert(editor);

    if (editor->selected_shape.has_value() && editor->selected_control_point.has_value())
    {
        return {(uint32_t)editor->selected_control_point.value()};
    }

    return std::nullopt;
}

void select_control_point(ShapeEditor* editor, std::optional<uint32_t> control_point_index)
{
    assert(editor);

    std::optional<uint64_t> payload = control_point_index.has_value()
        ? std::optional<uint64_t>(control_point_index.value())
        : std::nullopt;

    ShapeEditor::Event event;
    event.kind = ShapeEditor::Event::Kind::ControlPointSelection;
    event.payload = payload;
    editor->events.push_back(event);
}

hrz_proto::ShapeEditorMode get_current_mode(const ShapeEditor* editor)
{
    assert(editor);

    switch (editor->mode)
    {
        case ShapeEditor::Mode::AppendPoint: return hrz_proto::ShapeEditorMode::APPEND_MODE;
        default: return hrz_proto::ShapeEditorMode::SELECTION_MODE;
    }
}

void set_mode(ShapeEditor* editor, hrz_proto::ShapeEditorMode mode)
{
    assert(editor);

    ShapeEditor::Event editor_event;
    editor_event.kind = ShapeEditor::Event::Kind::ModeSwitch;

    switch (mode)
    {
        case hrz_proto::ShapeEditorMode::SELECTION_MODE:
            editor_event.payload = ShapeEditor::Mode::NoAction;
            break;
        case hrz_proto::ShapeEditorMode::APPEND_MODE:
            editor_event.payload = ShapeEditor::Mode::AppendPoint;
            break;
        default:
            assert(false && "Unhandled case");
            editor_event.payload = ShapeEditor::Mode::NoAction;
            break;
    }

    editor->events.push_back(editor_event);
}

namespace
{

// Area preserving projection
lm::dvec2 project_to_sinusoidal(const hrz::GeoPosition2& geo)
{
    constexpr double lon_length = hrz::EARTH_RADIUS; // length in metres of 1 rad
    constexpr double lat_length = lon_length * hrz::WGS84_AXES_LENGTH_RATIO;
    return lm::dvec2(geo.lon * std::cos(geo.lat) * lon_length, geo.lat * lat_length);
}

struct SinusoidalVectorCollector
{
    std::vector<lm::dvec2>& sinusoidal_positions;

    void append(const hrz::GeoPosition2& p)
    {
        sinusoidal_positions.push_back(project_to_sinusoidal(p));
    }
};

} // namespace

hrz_proto::ShapeInformation get_shape_information(ShapeEditor* editor, uint64_t global_layer_id)
{
    assert(editor);

    hrz_proto::ShapeInformation info;
    info.set_length(0);
    info.set_area(0);

    auto it = editor->layer_ids_to_shape_handles.find(global_layer_id);
    if (it == editor->layer_ids_to_shape_handles.end())
    {
        return info;
    }

    auto normalize_angle = [](double angle, double lower_bound)
    {
        constexpr double TAU = lm::PI * 2;
        while (angle < lower_bound)
        {
            angle += TAU;
        }
        while (angle > lower_bound + TAU)
        {
            angle -= TAU;
        }
        return angle;
    };

    auto compute_polyline_length =
        [&](std::span<const hrz::GeoPosition2> points, bool closed, LineType line_type)
    {
        if (points.size() <= 1)
        {
            info.add_segment_counts(0);
            info.add_polyline_lengths(0);
            return 0.0;
        }

        info.add_segment_counts(points.size() - 1);

        double length = 0;
        double first_bearing = 0;

        auto compute_bearing = [&](const GeoPosition2& from, const GeoPosition2& to)
        {
            switch (line_type)
            {
                case LineType::Geodesics: return hrz::bearing_between_points(from, to);
                case LineType::RhumbLines:
                    return hrz::rhumb_line_bearing_between_points(from, to, true);
                case LineType::RhumbLinesNotAcrossAntimeridian:
                    return hrz::rhumb_line_bearing_between_points(from, to, false);
                default: assert(false && "Unhandled case"); return 0.0;
            }
        };

        auto compute_segment_length = [&](const GeoPosition2& from, const GeoPosition2& to)
        {
            switch (line_type)
            {
                case LineType::Geodesics: return hrz::geodesic_distance(from, to);
                case LineType::RhumbLines: return hrz::rhumb_line_distance(from, to, true);
                case LineType::RhumbLinesNotAcrossAntimeridian:
                    return hrz::rhumb_line_distance(from, to, false);
                default: assert(false && "Unhandled case"); return 0.0;
            }
        };

        for (size_t i = 0; i < points.size() - (closed ? 0 : 1); ++i)
        {
            const auto& from = points[i];
            const auto& to = points[(i + 1) % points.size()];
            double segment_length = compute_segment_length(from, to);

            info.add_segment_lengths(segment_length);

            length += segment_length;

            double forward_bearing = compute_bearing(from, to);
            info.add_segment_bearings(normalize_angle(forward_bearing, 0));

            if (i == 0)
            {
                first_bearing = forward_bearing;
            }
            else
            {
                const auto& previous = points[i - 1];
                // The forward bearing from the previous position is not necessarily
                // the same as the backward bearing from this position plus a half-turn.
                // (Like when the segment goes over the pole.)
                double backward_bearing = compute_bearing(from, previous) + lm::PI;
                double angle = forward_bearing - backward_bearing;
                info.add_segment_angles(normalize_angle(angle, -lm::PI));
            }
        }

        if (points.size() >= 2 && closed)
        {
            double backward_bearing = compute_bearing(points.front(), points.back()) + lm::PI;
            double angle = first_bearing - backward_bearing;
            info.add_segment_angles(normalize_angle(angle, -lm::PI));
        }

        info.add_polyline_lengths(length);

        return length;
    };

    const auto& shape = *editor->shape_pool.get_object(it->second);

    if (shape.kind == Shape::Kind::Polyline)
    {
        auto point_count = std::min(shape.points.size(), shape.max_point_count);
        info.set_length(
            compute_polyline_length({shape.points.data(), point_count}, false, shape.line_type));
    }
    else if (shape.kind == Shape::Kind::Polygon)
    {
        // See https://stackoverflow.com/a/451482
        auto compute_polygon_area = [](std::span<const lm::dvec2> points)
        {
            double area = 0;
            size_t point_count = points.size();
            for (size_t i = 0; i < point_count; ++i)
            {
                size_t j = (i + 1) % point_count;
                area += points[i].x * points[j].y - points[i].y * points[j].x;
            }
            return std::abs(area) * 0.5;
        };

        double total_perimeter = 0;
        double total_area = 0;
        bool area_can_be_computed = true;

        if (!shape.linestring_sizes.empty() && shape.linestring_sizes.front() > 0)
        {
            if (shape.points.size() > 1)
            {
                // Perimeter

                std::span<const hrz::GeoPosition2> points_span = {
                    shape.points.data(), shape.points.size()
                };

                size_t outer_linestring_size = shape.linestring_sizes.front();
                total_perimeter += compute_polyline_length(
                    points_span.subspan(0, outer_linestring_size), true, shape.line_type);

                size_t current_linestring_start = outer_linestring_size;
                for (size_t i = 1; i < shape.linestring_sizes.size(); ++i)
                {
                    size_t linestring_size = shape.linestring_sizes[i];

                    total_perimeter += compute_polyline_length(
                        points_span.subspan(current_linestring_start, linestring_size), true,
                        shape.line_type);

                    current_linestring_start += linestring_size;
                }
            }

            if (shape.points.size() > 2)
            {
                // Area

                // To compute the area of a polygon that uses rhumb lines, the polygon
                // edges are subdivided then the area is calculated like a polygon with
                // geodesics.
                // This subdivision makes the total point count as well as the point
                // count of each linestring different from what is in the shape model.
                // (This is different from the perimeter, where a dedicated function to
                // compute lengths of rhumb lines allows using the same points and line-
                // string sizes.)

                std::vector<lm::dvec2> sinusoidal_points;
                sinusoidal_points.reserve(shape.points.size());

                std::optional<std::vector<size_t>> linestring_sizes_opt;
                std::span<const size_t> linestring_sizes_span;

                if (shape.line_type == LineType::RhumbLines
                    || shape.line_type == LineType::RhumbLinesNotAcrossAntimeridian)
                {
                    SinusoidalVectorCollector collector{sinusoidal_points};

                    linestring_sizes_opt = {std::vector<size_t>{}};
                    auto& linestring_sizes = linestring_sizes_opt.value();
                    size_t current_linestring_start = 0;

                    for (size_t linestring_size : shape.linestring_sizes)
                    {
                        if (linestring_size < 2 || !area_can_be_computed) continue;

                        size_t point_count_before = sinusoidal_points.size();

                        collector.append(shape.points[current_linestring_start]);

                        for (size_t i = current_linestring_start;
                             i < current_linestring_start + linestring_size - 1
                             && i < shape.points.size() - 1;
                             ++i)
                        {
                            if (segment_crosses_antimeridian(
                                    shape.points[i], shape.points[i + 1], shape.line_type))
                            {
                                area_can_be_computed = false;
                                break;
                            }

                            subdivide_rhumb_line(
                                shape.points[i], shape.points[i + 1], collector,
                                shape.line_type == LineType::RhumbLines);
                        }

                        linestring_sizes.push_back(sinusoidal_points.size() - point_count_before);
                        current_linestring_start += linestring_size;
                    }

                    linestring_sizes_span = std::span<const size_t>{
                        linestring_sizes_opt.value().data(), linestring_sizes_opt.value().size()
                    };
                }
                else
                {
                    size_t current_linestring_start = 0;

                    for (size_t linestring_size : shape.linestring_sizes)
                    {
                        if (linestring_size < 2 || !area_can_be_computed) continue;

                        sinusoidal_points.push_back(
                            project_to_sinusoidal(shape.points[current_linestring_start]));

                        for (size_t i = current_linestring_start;
                             i < current_linestring_start + linestring_size - 1
                             && i < shape.points.size() - 1;
                             ++i)
                        {
                            if (segment_crosses_antimeridian(
                                    shape.points[i], shape.points[i + 1], shape.line_type))
                            {
                                area_can_be_computed = false;
                                break;
                            }

                            sinusoidal_points.push_back(project_to_sinusoidal(shape.points[i + 1]));
                        }

                        current_linestring_start += linestring_size;
                    }

                    linestring_sizes_span = std::span<const size_t>{
                        shape.linestring_sizes.data(), shape.linestring_sizes.size()
                    };
                }

                std::span<const lm::dvec2> sinusoidal_points_span = {
                    sinusoidal_points.data(), sinusoidal_points.size()
                };

                size_t outer_linestring_size = linestring_sizes_span.front();

                if (area_can_be_computed)
                {
                    double outer_area = compute_polygon_area(
                        sinusoidal_points_span.subspan(0, outer_linestring_size));
                    info.add_polygon_areas(outer_area);
                    total_area += outer_area;

                    size_t current_linestring_start = outer_linestring_size;
                    for (size_t i = 1; i < linestring_sizes_span.size(); ++i)
                    {
                        size_t linestring_size = linestring_sizes_span[i];

                        double inner_area = compute_polygon_area(sinusoidal_points_span.subspan(
                            current_linestring_start, linestring_size));
                        info.add_polygon_areas(inner_area);
                        total_area -= inner_area;

                        current_linestring_start += linestring_size;
                    }
                }
                else
                {
                    total_area = 0.0;
                }
            }
        }

        info.set_length(total_perimeter);
        info.set_area(total_area);
    }

    return info;
}

void delete_selected_control_point(ShapeEditor* editor)
{
    assert(editor);
    ShapeEditor::Event editor_event;
    editor_event.kind = ShapeEditor::Event::Kind::Delete;
    editor->events.push_back(editor_event);
}

namespace
{

ShapeUniformData make_shape_uniforms(const Shape& shape, const ShapeEditor* editor)
{
    bool selected = editor->selected_shape == shape.handle;

    ShapeUniformData uniforms;

    if (shape.kind == Shape::Kind::Polyline)
    {
        uniforms.color = selected ? shape.selected_stroke_color : shape.stroke_color;
    }
    else if (shape.kind == Shape::Kind::Polygon)
    {
        uniforms.color = selected ? shape.selected_fill_color : shape.fill_color;
    }

    uniforms.line_width = selected ? shape.selected_stroke_width : shape.stroke_width;
    uniforms.object_reference = shape.object_ref.to_uvec2();

    return uniforms;
}

ShapeUniformData make_outline_uniforms(const Shape& shape, const ShapeEditor* editor)
{
    bool selected = editor->selected_shape == shape.handle;

    ShapeUniformData uniforms;
    uniforms.color = selected ? shape.selected_stroke_color : shape.stroke_color;
    uniforms.line_width = shape.stroke_width;
    uniforms.object_reference = shape.object_ref.to_uvec2();

    return uniforms;
}

ControlUniformData make_control_uniforms(const Shape& shape, const ShapeEditor* editor)
{
    ControlUniformData uniforms;
    uniforms.object_reference = shape.object_ref.to_uvec2();

    if (editor->selected_shape == shape.handle && editor->selected_control_point.has_value())
    {
        uniforms.selected_control_point_id = editor->selected_control_point.value();
    }
    else
    {
        uniforms.selected_control_point_id = NoControlPoint;
    }

    // Avoids picking the control point that is being used.
    uniforms.pick_selected_control_point = editor->mode == ShapeEditor::Mode::NoAction;

    // Avoids picking the midpoint control point when trying to click on the previous point.
    // (Midpoint control points are unused when appending anyway.)
    uniforms.pick_midpoint_control_points = editor->mode != ShapeEditor::Mode::AppendPoint;

    uniforms.control_point_size = shape.control_point_size;
    uniforms.control_point_color = shape.control_point_color;
    uniforms.midpoint_control_point_color = shape.midpoint_control_point_color;
    uniforms.selected_control_point_color = shape.selected_control_point_color;
    uniforms.show_midpoint_control_points = shape.show_midpoint_control_points;

    return uniforms;
}

void generate_polyline_renderable(
    uint64_t shape_layer_id,
    std::span<const hrz::GeoPosition2> points,
    bool loop,
    LineType line_type,
    uint32_t z_index,
    uint32_t scene_views_bitset,
    const ShapeUniformData& uniforms,
    RenderableShape& renderable,
    ShapeEditor* editor,
    Render* render)
{
    HRZ_SCOPED_SAMPLE("generate polyline renderable");

    auto mesh = generate_polyline_mesh(points, loop, line_type);

    my::BufferResource vb_res(my::BufferResource::BufferType::Vertex);
    vb_res.size = mesh.vertex_data.size() * sizeof(lm::vec3);
    vb_res.usage = my::UsageHint::Dynamic;
    vb_res.data = mesh.vertex_data.data();

    my::ResourceHandle vertex_buffer = render->rc->alloc(
        &vb_res, monitoring::systems::ShapeEditor, shape_layer_id,
        {{"contents"_ss, "polyline vertex data"_ss}});

    my::BufferResource ib_res(my::BufferResource::BufferType::Index);
    ib_res.size = mesh.indices.size() * sizeof(uint32_t);
    ib_res.usage = my::UsageHint::Dynamic;
    ib_res.data = mesh.indices.data();

    my::ResourceHandle index_buffer = render->rc->alloc(
        &ib_res, monitoring::systems::ShapeEditor, shape_layer_id,
        {{"contents"_ss, "polyline indices"_ss}});

    my::VertexInputStream streams[] = {
        {PositionLowInputStream, vertex_buffer, my::VertexFormat::Float32_3, 0,
         sizeof(lm::vec3) * 5, my::VertexRate::PerVertex},
        {PositionHighInputStream, vertex_buffer, my::VertexFormat::Float32_3, sizeof(lm::vec3),
         sizeof(lm::vec3) * 5, my::VertexRate::PerVertex},
        {NormalInputStream, vertex_buffer, my::VertexFormat::Float32_3, sizeof(lm::vec3) * 2,
         sizeof(lm::vec3) * 5, my::VertexRate::PerVertex},
        {BisectorInputStream, vertex_buffer, my::VertexFormat::Float32_3, sizeof(lm::vec3) * 3,
         sizeof(lm::vec3) * 5, my::VertexRate::PerVertex},
        {ExtrusionParamsInputStream, vertex_buffer, my::VertexFormat::Float32_3,
         sizeof(lm::vec3) * 4, sizeof(lm::vec3) * 5, my::VertexRate::PerVertex}
    };

    my::VertexInputResource vi_res;
    vi_res.indices = index_buffer;
    vi_res.attribs = streams;
    my::ResourceHandle vertex_input =
        render->rc->alloc(&vi_res, monitoring::systems::ShapeEditor, shape_layer_id);

    my::BufferResource ub_res(my::BufferResource::BufferType::Uniform);
    ub_res.size = sizeof(ShapeUniformData);
    ub_res.usage = my::UsageHint::Updatable;
    ub_res.data = &uniforms;

    my::ResourceHandle uniform_buffer = render->rc->alloc(
        &ub_res, monitoring::systems::ShapeEditor, shape_layer_id,
        {{"contents"_ss, "polyline uniforms"_ss}});

    renderable.vertex_buffer = vertex_buffer;
    renderable.index_buffer = index_buffer;
    renderable.data.fullscreen_vertex_input = editor->fullscreen_vertex_input;
    renderable.data.vertex_input = vertex_input;
    renderable.data.vertex_count = mesh.indices.size();
    renderable.data.stencil_shader = editor->polyline_stencil_shader;
    renderable.data.visual_shader = editor->shape_visual_shader;
    renderable.data.picking_shader = editor->shape_picking_shader;
    renderable.data.uniform_buffer = uniform_buffer;
    renderable.data.scene_views_bitset = scene_views_bitset;
    renderable.bin_mask = hrz::RenderDecalBin;

    renderable.z_index = z_index;
    renderable.center = mesh.center;
    renderable.radius = mesh.radius;

    renderable.is_degenerate = mesh.is_degenerate;
}

void generate_polyline_renderable(Shape& shape, ShapeEditor* editor, Render* render)
{
    assert(shape.kind == Shape::Kind::Polyline);

    std::span<const hrz::GeoPosition2> points = {
        shape.points.data(), std::min(shape.points.size(), shape.max_point_count)
    };
    generate_polyline_renderable(
        shape.global_layer_id, points, false, shape.line_type, shape.z_index << 1,
        shape.scene_views_bitset, make_shape_uniforms(shape, editor), shape.renderable, editor,
        render);
}

void regenerate_polyline_renderable(
    std::span<const hrz::GeoPosition2> points,
    bool loop,
    LineType line_type,
    RenderableShape& renderable,
    Render* render)
{
    HRZ_SCOPED_SAMPLE("regenerate polyline renderable");

    auto mesh = generate_polyline_mesh(points, loop, line_type);

    my::BufferResource vb_res(my::BufferResource::BufferType::Vertex);
    vb_res.size = mesh.vertex_data.size() * sizeof(lm::vec3);
    vb_res.usage = my::UsageHint::Dynamic;
    vb_res.data = mesh.vertex_data.data();

    render->rc->realloc_buffer(renderable.vertex_buffer, &vb_res);

    my::BufferResource ib_res(my::BufferResource::BufferType::Index);
    ib_res.size = mesh.indices.size() * sizeof(uint32_t);
    ib_res.usage = my::UsageHint::Dynamic;
    ib_res.data = mesh.indices.data();

    render->rc->realloc_buffer(renderable.index_buffer, &ib_res);

    renderable.data.vertex_count = mesh.indices.size();

    renderable.center = mesh.center;
    renderable.radius = mesh.radius;

    renderable.is_degenerate = mesh.is_degenerate;
}

void regenerate_polyline_renderable(Shape& shape, Render* render)
{
    assert(shape.kind == Shape::Kind::Polyline);

    std::span<const hrz::GeoPosition2> points = {
        shape.points.data(), std::min(shape.points.size(), shape.max_point_count)
    };

    regenerate_polyline_renderable(points, false, shape.line_type, shape.renderable, render);
}

void generate_polygon_renderable(
    Shape& shape,
    uint32_t z_index,
    ShapeEditor* editor,
    Render* render)
{
    HRZ_SCOPED_SAMPLE("generate polygon renderable");

    assert(shape.kind == Shape::Kind::Polygon);

    auto mesh = generate_polygon_mesh(
        {shape.points.data(), shape.points.size()},
        {shape.linestring_sizes.data(), shape.linestring_sizes.size()}, shape.line_type);

    my::BufferResource vb_res(my::BufferResource::BufferType::Vertex);
    vb_res.size = mesh.vertex_data.size() * sizeof(lm::vec3);
    vb_res.usage = my::UsageHint::Dynamic;
    vb_res.data = mesh.vertex_data.data();

    my::ResourceHandle vertex_buffer = render->rc->alloc(
        &vb_res, monitoring::systems::ShapeEditor, shape.global_layer_id,
        {{"contents"_ss, "polygon vertex data"_ss}});

    my::BufferResource ib_res(my::BufferResource::BufferType::Index);
    ib_res.size = mesh.indices.size() * sizeof(uint32_t);
    ib_res.usage = my::UsageHint::Dynamic;
    ib_res.data = mesh.indices.data();

    my::ResourceHandle index_buffer = render->rc->alloc(
        &ib_res, monitoring::systems::ShapeEditor, shape.global_layer_id,
        {{"contents"_ss, "polygon indices"_ss}});

    my::VertexInputStream streams[] = {
        {PositionLowInputStream, vertex_buffer, my::VertexFormat::Float32_3, 0,
         sizeof(lm::vec3) * 2, my::VertexRate::PerVertex},
        {PositionHighInputStream, vertex_buffer, my::VertexFormat::Float32_3, sizeof(lm::vec3),
         sizeof(lm::vec3) * 2, my::VertexRate::PerVertex}
    };

    my::VertexInputResource vi_res;
    vi_res.indices = index_buffer;
    vi_res.attribs = streams;
    my::ResourceHandle vertex_input =
        render->rc->alloc(&vi_res, monitoring::systems::ShapeEditor, shape.global_layer_id);

    auto uniforms = make_shape_uniforms(shape, editor);

    my::BufferResource ub_res(my::BufferResource::BufferType::Uniform);
    ub_res.size = sizeof(ShapeUniformData);
    ub_res.usage = my::UsageHint::Updatable;
    ub_res.data = &uniforms;

    my::ResourceHandle uniform_buffer = render->rc->alloc(
        &ub_res, monitoring::systems::ShapeEditor, shape.global_layer_id,
        {{"contents"_ss, "polygon uniforms"_ss}});

    auto& renderable = shape.renderable;
    renderable.vertex_buffer = vertex_buffer;
    renderable.index_buffer = index_buffer;
    renderable.data.vertex_input = vertex_input;
    renderable.data.vertex_count = mesh.indices.size();
    renderable.data.stencil_shader = editor->polygon_stencil_shader;
    renderable.data.visual_shader = editor->shape_visual_shader;
    renderable.data.picking_shader = editor->shape_picking_shader;
    renderable.data.uniform_buffer = uniform_buffer;
    renderable.data.scene_views_bitset = shape.scene_views_bitset;
    renderable.bin_mask = hrz::RenderDecalBin;

    renderable.z_index = z_index;
    renderable.center = mesh.center;
    renderable.radius = mesh.radius;

    renderable.is_degenerate = mesh.is_degenerate;
}

void generate_polygon_renderables(Shape& shape, ShapeEditor* editor, Render* render)
{
    HRZ_SCOPED_SAMPLE("generate polygon renderables");

    assert(shape.kind == Shape::Kind::Polygon);

    generate_polygon_renderable(shape, shape.z_index << 1, editor, render);
    shape.renderable.data.fullscreen_vertex_input = editor->fullscreen_vertex_input;

    std::span<hrz::GeoPosition2> points = {shape.points.data(), shape.points.size()};

    auto outline_uniforms = make_outline_uniforms(shape, editor);

    size_t current_linestring_start = 0;
    for (size_t linestring_size : shape.linestring_sizes)
    {
        shape.renderable_outlines.push_back({});

        generate_polyline_renderable(
            shape.global_layer_id, points.subspan(current_linestring_start, linestring_size), true,
            shape.line_type, (shape.z_index << 1) + 1, shape.scene_views_bitset, outline_uniforms,
            shape.renderable_outlines.back(), editor, render);

        current_linestring_start += linestring_size;
    }
}

void regenerate_polygon_renderable(Shape& shape, Render* render)
{
    HRZ_SCOPED_SAMPLE("regenerate polygon renderable");

    assert(shape.kind == Shape::Kind::Polygon);

    auto mesh = generate_polygon_mesh(
        {shape.points.data(), shape.points.size()},
        {shape.linestring_sizes.data(), shape.linestring_sizes.size()}, shape.line_type);

    my::BufferResource vb_res(my::BufferResource::BufferType::Vertex);
    vb_res.size = mesh.vertex_data.size() * sizeof(lm::vec3);
    vb_res.usage = my::UsageHint::Dynamic;
    vb_res.data = mesh.vertex_data.data();

    auto& renderable = shape.renderable;

    render->rc->realloc_buffer(renderable.vertex_buffer, &vb_res);

    my::BufferResource ib_res(my::BufferResource::BufferType::Index);
    ib_res.size = mesh.indices.size() * sizeof(uint32_t);
    ib_res.usage = my::UsageHint::Dynamic;
    ib_res.data = mesh.indices.data();

    render->rc->realloc_buffer(renderable.index_buffer, &ib_res);

    renderable.data.vertex_count = mesh.indices.size();

    renderable.center = mesh.center;
    renderable.radius = mesh.radius;

    renderable.is_degenerate = mesh.is_degenerate;
}

void regenerate_polygon_renderables(Shape& shape, ShapeEditor* editor, Render* render)
{
    HRZ_SCOPED_SAMPLE("regenerate polygon renderables");

    regenerate_polygon_renderable(shape, render);

    std::span<hrz::GeoPosition2> points = {shape.points.data(), shape.points.size()};

    // Update existing outline renderables
    size_t current_linestring_start = 0;
    for (size_t i = 0; i < shape.linestring_sizes.size() && i < shape.renderable_outlines.size();
         ++i)
    {
        size_t linestring_size = shape.linestring_sizes.at(i);

        regenerate_polyline_renderable(
            points.subspan(current_linestring_start, linestring_size), true, shape.line_type,
            shape.renderable_outlines.at(i), render);

        current_linestring_start += linestring_size;
    }

    // Create new outline renderables
    auto outline_uniforms = make_outline_uniforms(shape, editor);

    for (size_t i = shape.renderable_outlines.size(); i < shape.linestring_sizes.size(); ++i)
    {
        size_t linestring_size = shape.linestring_sizes.at(i);

        shape.renderable_outlines.push_back({});

        generate_polyline_renderable(
            shape.global_layer_id, points.subspan(current_linestring_start, linestring_size), true,
            shape.line_type, 1, shape.scene_views_bitset, outline_uniforms,
            shape.renderable_outlines.back(), editor, render);

        current_linestring_start += linestring_size;
    }

    // Delete unused outline renderables
    for (auto i = shape.linestring_sizes.size(); i < shape.renderable_outlines.size(); ++i)
    {
        collect_gpu_data(shape.renderable_outlines.at(i), editor->unused_resources);
    }

    shape.renderable_outlines.resize(shape.linestring_sizes.size());
}

std::vector<lm::vec3> generate_control_points_instance_buffer(
    Shape& shape,
    lm::dvec3& out_center,
    double& out_radius,
    size_t& out_control_point_count)
{
    HRZ_SCOPED_SAMPLE("generate control points instance buffer");

    size_t control_point_count = shape.control_points.size();

    std::vector<lm::vec3> vbo_data(control_point_count * 5);

    if (control_point_count == 0)
    {
        return vbo_data;
    }

    // Lat-lon to ECEF and web Mercator, ground normals
    std::vector<lm::dvec3> ecef_positions;
    ecef_positions.reserve(control_point_count);

    std::vector<lm::dvec3> web_mercator_positions;
    web_mercator_positions.reserve(control_point_count);

    ArrayView<lm::vec3> ground_normals(
        vbo_data.data() + 4, control_point_count, sizeof(lm::vec3) * 5);

    for (size_t i = 0; i < shape.control_points.size(); ++i)
    {
        const auto& position = shape.control_points.at(i);
        ecef_positions.push_back(hrz::geo_to_ecef(position));
        web_mercator_positions.emplace_back(hrz::geo_to_web_mercator_pixels(position), 0);
        ground_normals[i] = lm::vec3(hrz::geo_to_normal(position));
    }

    // ECEF and web Mercator positions split
    ArrayView<lm::vec3> ecef_positions_low(
        vbo_data.data() + 0, control_point_count, sizeof(lm::vec3) * 5);
    ArrayView<lm::vec3> ecef_positions_high(
        vbo_data.data() + 1, control_point_count, sizeof(lm::vec3) * 5);

    ArrayView<lm::vec3> wmerc_positions_low(
        vbo_data.data() + 2, control_point_count, sizeof(lm::vec3) * 5);
    ArrayView<lm::vec3> wmerc_positions_high(
        vbo_data.data() + 3, control_point_count, sizeof(lm::vec3) * 5);

    for (size_t i = 0; i < control_point_count; ++i)
    {
        const lm::dvec3& ecef = ecef_positions.at(i);
        hrz::split_double(ecef.x, ecef_positions_low[i].x, ecef_positions_high[i].x);
        hrz::split_double(ecef.y, ecef_positions_low[i].y, ecef_positions_high[i].y);
        hrz::split_double(ecef.z, ecef_positions_low[i].z, ecef_positions_high[i].z);

        const lm::dvec3& wmerc = web_mercator_positions.at(i);
        hrz::split_double(wmerc.x, wmerc_positions_low[i].x, wmerc_positions_high[i].x);
        hrz::split_double(wmerc.y, wmerc_positions_low[i].y, wmerc_positions_high[i].y);
        // wmerc.z is unused
        wmerc_positions_low[i].z = 0;
        wmerc_positions_high[i].z = 0;
    }

    // Bounding sphere
    std::vector<lm::dvec3> bbox_ecef_positions;
    bbox_ecef_positions.reserve(control_point_count * 2);

    for (size_t i = 0; i < control_point_count; ++i)
    {
        const auto& ecef = ecef_positions.at(i);
        const auto& ground_normal = ground_normals.at(i);

        bbox_ecef_positions.push_back(ecef + ground_normal * ControlBboxDownOffset);
        bbox_ecef_positions.push_back(ecef + ground_normal * ControlBboxUpOffset);
    }

    auto b_sphere = hrz::compute_bounding_sphere(
        std::span<const lm::dvec3>{bbox_ecef_positions.data(), bbox_ecef_positions.size()});

    out_center = b_sphere.center;
    out_radius = b_sphere.radius;
    out_control_point_count = control_point_count;

    return vbo_data;
}

void generate_control_points_renderable(Shape& shape, ShapeEditor* editor, Render* render)
{
    HRZ_SCOPED_SAMPLE("generate control points renderable");

    lm::dvec3 center = {0, 0, 0};
    double radius = 0;
    size_t control_point_count = 0;
    auto vbo_data =
        generate_control_points_instance_buffer(shape, center, radius, control_point_count);

    my::BufferResource vb_res(my::BufferResource::BufferType::Vertex);
    vb_res.size = vbo_data.size() * sizeof(lm::vec3);
    vb_res.usage = my::UsageHint::Dynamic;
    vb_res.data = vbo_data.data();

    my::ResourceHandle instance_buffer = render->rc->alloc(
        &vb_res, monitoring::systems::ShapeEditor, shape.global_layer_id,
        {{"contents"_ss, "control point instance data"_ss}});

    my::VertexInputStream streams[] = {
        {PositionLowInputStream, instance_buffer, my::VertexFormat::Float32_3, sizeof(lm::vec3) * 0,
         sizeof(lm::vec3) * 5, my::VertexRate::PerInstance},
        {PositionHighInputStream, instance_buffer, my::VertexFormat::Float32_3,
         sizeof(lm::vec3) * 1, sizeof(lm::vec3) * 5, my::VertexRate::PerInstance},
        {WmercPositionLowInputStream, instance_buffer, my::VertexFormat::Float32_2,
         sizeof(lm::vec3) * 2, sizeof(lm::vec3) * 5, my::VertexRate::PerInstance},
        {WmercPositionHighInputStream, instance_buffer, my::VertexFormat::Float32_2,
         sizeof(lm::vec3) * 3, sizeof(lm::vec3) * 5, my::VertexRate::PerInstance},
        {GroundNormalInputStream, instance_buffer, my::VertexFormat::Float32_3,
         sizeof(lm::vec3) * 4, sizeof(lm::vec3) * 5, my::VertexRate::PerInstance},
        {LocalPositionInputStream, editor->control_vertex_buffer, my::VertexFormat::Float32_3, 0,
         sizeof(lm::vec3) * 2, my::VertexRate::PerVertex}
    };

    my::VertexInputResource vi_res;
    vi_res.indices = editor->control_index_buffer;
    vi_res.attribs = streams;
    my::ResourceHandle vertex_input =
        render->rc->alloc(&vi_res, monitoring::systems::ShapeEditor, shape.global_layer_id);

    auto uniforms = make_control_uniforms(shape, editor);

    my::BufferResource ub_res(my::BufferResource::BufferType::Uniform);
    ub_res.size = sizeof(ControlUniformData);
    ub_res.usage = my::UsageHint::Updatable;
    ub_res.data = &uniforms;

    my::ResourceHandle uniform_buffer = render->rc->alloc(
        &ub_res, monitoring::systems::ShapeEditor, shape.global_layer_id,
        {{"contents"_ss, "control point uniforms"_ss}});

    auto& renderable = shape.renderable_control;
    renderable.vertex_buffer = instance_buffer;
    renderable.index_buffer = editor->control_index_buffer;
    renderable.data.vertex_input = vertex_input;
    renderable.data.vertex_count = editor->control_vertex_count;
    renderable.data.instance_count = control_point_count;
    renderable.data.visual_shader = editor->control_visual_shader;
    renderable.data.picking_shader = editor->control_picking_shader;
    renderable.data.uniform_buffer = uniform_buffer;
    renderable.data.scene_views_bitset = shape.scene_views_bitset;
    renderable.bin_mask = hrz::RenderInWorldBin;
    renderable.center = center;
    renderable.radius = radius;
}

void regenerate_control_points_renderable(Shape& shape, ShapeEditor* editor, Render* render)
{
    HRZ_SCOPED_SAMPLE("regenerate control points renderable");

    lm::dvec3 center = {0, 0, 0};
    double radius = 0;
    size_t control_point_count = 0;
    auto vbo_data =
        generate_control_points_instance_buffer(shape, center, radius, control_point_count);

    my::BufferResource vb_res(my::BufferResource::BufferType::Vertex);
    vb_res.size = vbo_data.size() * sizeof(lm::vec3);
    vb_res.usage = my::UsageHint::Dynamic;
    vb_res.data = vbo_data.data();

    auto& renderable = shape.renderable_control;

    render->rc->realloc_buffer(renderable.vertex_buffer, &vb_res);

    renderable.data.instance_count = control_point_count;

    renderable.center = center;
    renderable.radius = radius;
}

void update_shape_ubo(Shape& shape, ShapeEditor* editor, Render* render)
{
    {
        auto uniforms = make_shape_uniforms(shape, editor);

        render->my->update_buffer(
            shape.renderable.data.uniform_buffer, 0, sizeof(ShapeUniformData), &uniforms);
    }

    auto outline_uniforms = make_outline_uniforms(shape, editor);

    for (size_t i = 0; i < shape.renderable_outlines.size(); ++i)
    {
        render->my->update_buffer(
            shape.renderable_outlines.at(i).data.uniform_buffer, 0, sizeof(ShapeUniformData),
            &outline_uniforms);
    }
}

void update_control_points_ubo(Shape& shape, ShapeEditor* editor, Render* render)
{
    auto uniforms = make_control_uniforms(shape, editor);
    render->my->update_buffer(
        shape.renderable_control.data.uniform_buffer, 0, sizeof(ControlUniformData), &uniforms);
}

} // namespace

RenderRequest work_gpu(ShapeEditor* editor, Render* render)
{
    HRZ_SCOPED_SAMPLE("shape editor work gpu");

    assert(editor && render);

    if (editor->disabled) return {};

    RenderRequest render_request;

    for (auto shape_handle : editor->shapes_to_update_on_gpu)
    {
        auto shape_ptr = editor->shape_pool.get_object(shape_handle);

        if (shape_ptr == nullptr) continue;

        auto& shape = *shape_ptr;

        if (shape.generate_renderable)
        {
            if (shape.kind == Shape::Kind::Polyline)
            {
                generate_polyline_renderable(shape, editor, render);
            }
            else if (shape.kind == Shape::Kind::Polygon)
            {
                generate_polygon_renderables(shape, editor, render);
            }

            generate_control_points_renderable(shape, editor, render);

            editor->renderable_shapes.insert(shape_handle);
        }
        else if (shape.regenerate_renderable)
        {
            if (shape.kind == Shape::Kind::Polyline)
            {
                regenerate_polyline_renderable(shape, render);
            }
            else if (shape.kind == Shape::Kind::Polygon)
            {
                regenerate_polygon_renderables(shape, editor, render);
            }
        }

        if (shape.generate_control_renderable)
        {
            generate_control_points_renderable(shape, editor, render);
        }
        else if (shape.regenerate_control_renderable)
        {
            regenerate_control_points_renderable(shape, editor, render);
        }

        shape.generate_renderable = false;
        shape.regenerate_renderable = false;
        shape.generate_control_renderable = false;
        shape.regenerate_control_renderable = false;

        if (shape.update_control_ubo || shape.update_ubo)
        {
            update_control_points_ubo(shape, editor, render);

            shape.update_control_ubo = false;
        }

        if (shape.update_ubo)
        {
            update_shape_ubo(shape, editor, render);

            shape.update_ubo = false;
        }

        render_request.request_visual_render();
    }

    editor->shapes_to_update_on_gpu.clear();

    for (auto resource : editor->unused_resources)
    {
        render->rc->dealloc(resource);
    }
    editor->unused_resources.clear();

    return render_request;
}

void draw(
    ShapeEditor* editor,
    Render* render,
    const planet::GeometryResources& planet_geometry_resources)
{
    HRZ_SCOPED_SAMPLE("shape editor draw");

    assert(editor && render);

    for (auto shape_handle : editor->renderable_shapes)
    {
        auto shape_ptr = editor->shape_pool.get_object(shape_handle);

        if (shape_ptr == nullptr) continue;

        auto& shape = *shape_ptr;

        if (shape.is_visible)
        {
            if (!shape.renderable.is_degenerate)
            {
                render->rd->collect_renderable(shape.renderable);
            }

            for (const auto& renderable : shape.renderable_outlines)
            {
                if (!renderable.is_degenerate)
                {
                    render->rd->collect_renderable(renderable);
                }
            }

            if (editor->selected_shape == shape_handle || shape.always_show_control_points)
            {
                shape.renderable_control.data.planet_resources = planet_geometry_resources;
                render->rd->collect_renderable(shape.renderable_control);
            }
        }
    }
}

void collect_shaders(hrz::GpuResourceContext* rc)
{
    {
        static const my::IndexName polyline_attribs[] = {
            {PositionLowInputStream, "i_position_low"},
            {PositionHighInputStream, "i_position_high"},
            {NormalInputStream, "i_normal"},
            {BisectorInputStream, "i_bisector"},
            {ExtrusionParamsInputStream, "i_extrusion_params"},
        };

        static const my::IndexName ubos[] = {
            {hrz::UboFrame, "Frame"},
            {ShapeParamsUbo, "Shape"},
        };

        static const my::IndexName samplers[] = {
            {hrz::SamplerCameraHeight, "u_camera_height"},
        };

        my::ShaderResource res{};
        res.name = hrz_shaders::ShapeEditorPolyline_stencil_name;
        res.vertex_source_len = hrz_shaders::ShapeEditorPolyline_stencil_vert_len;
        res.vertex_source = hrz_shaders::ShapeEditorPolyline_stencil_vert;
        res.fragment_source_len = hrz_shaders::ShapeEditorPolyline_stencil_frag_len;
        res.fragment_source = hrz_shaders::ShapeEditorPolyline_stencil_frag;
        res.attribs = polyline_attribs;
        res.uniform_blocks = ubos;
        res.samplers = samplers;
        res.outputs = {};
        res.initial_state.color_blend.enable = false;
        res.initial_state.depth.test = true;
        res.initial_state.depth.compare = my::DepthState::LessEqual;
        res.initial_state.depth.write = false;
        res.initial_state.rasterization.cull_mode = my::RasterizationState::None;
        res.initial_state.stencil.enable = true;
        res.initial_state.stencil.front.compare = my::StencilState::Always;
        res.initial_state.stencil.front.reference = 128;
        res.initial_state.stencil.front.compare_mask = 0xff;
        res.initial_state.stencil.front.write_mask = 0xff;
        res.initial_state.stencil.front.fail_op = my::StencilState::Keep;
        res.initial_state.stencil.front.depth_fail_op = my::StencilState::DecrementWrap;
        res.initial_state.stencil.front.depth_pass_op = my::StencilState::Keep;
        res.initial_state.stencil.back.compare = my::StencilState::Always;
        res.initial_state.stencil.back.reference = 128;
        res.initial_state.stencil.back.compare_mask = 0xff;
        res.initial_state.stencil.back.write_mask = 0xff;
        res.initial_state.stencil.back.fail_op = my::StencilState::Keep;
        res.initial_state.stencil.back.depth_fail_op = my::StencilState::IncrementWrap;
        res.initial_state.stencil.back.depth_pass_op = my::StencilState::Keep;
        rc->alloc(&res, hrz::monitoring::systems::ShapeEditor);

        static const my::IndexName polygon_attribs[] = {
            {PositionLowInputStream, "i_position_low"},
            {PositionHighInputStream, "i_position_high"},
        };

        res.name = hrz_shaders::ShapeEditorPolygon_stencil_name;
        res.vertex_source_len = hrz_shaders::ShapeEditorPolygon_stencil_vert_len;
        res.vertex_source = hrz_shaders::ShapeEditorPolygon_stencil_vert;
        res.fragment_source_len = hrz_shaders::ShapeEditorPolygon_stencil_frag_len;
        res.fragment_source = hrz_shaders::ShapeEditorPolygon_stencil_frag;
        res.attribs = polygon_attribs;
        res.samplers = {};
        rc->alloc(&res, hrz::monitoring::systems::ShapeEditor);
    }

    {
        static const my::IndexName attribs[] = {
            {0, "i_pos"},
        };

        static const my::IndexName ubos[] = {
            {ShapeParamsUbo, "Shape"},
        };

        const char* visual_outputs[] = {"o_color"};

        my::ShaderResource res{};
        res.name = hrz_shaders::ShapeEditorShape_visual_name;
        res.vertex_source_len = hrz_shaders::ShapeEditorShape_visual_vert_len;
        res.vertex_source = hrz_shaders::ShapeEditorShape_visual_vert;
        res.fragment_source_len = hrz_shaders::ShapeEditorShape_visual_frag_len;
        res.fragment_source = hrz_shaders::ShapeEditorShape_visual_frag;
        res.attribs = attribs;
        res.uniform_blocks = ubos;
        res.samplers = {};
        res.outputs = visual_outputs;
        res.initial_state.color_blend.enable = true;
        res.initial_state.color_blend.color.src = my::ColorBlendState::One;
        res.initial_state.color_blend.color.dst = my::ColorBlendState::OneMinusSrcAlpha;
        res.initial_state.color_blend.alpha.src = my::ColorBlendState::One;
        res.initial_state.color_blend.alpha.dst = my::ColorBlendState::OneMinusSrcAlpha;
        res.initial_state.depth.test = false;
        res.initial_state.depth.write = false;
        res.initial_state.rasterization.cull_mode = my::RasterizationState::Back;
        res.initial_state.stencil.enable = true;
        res.initial_state.stencil.front.compare = my::StencilState::NotEqual;
        res.initial_state.stencil.front.reference = 128;
        res.initial_state.stencil.front.compare_mask = 0xff;
        res.initial_state.stencil.front.write_mask = 0;
        res.initial_state.stencil.front.fail_op = my::StencilState::Keep;
        res.initial_state.stencil.front.depth_fail_op = my::StencilState::Keep;
        res.initial_state.stencil.front.depth_pass_op = my::StencilState::Keep;
        res.initial_state.stencil.back.compare = my::StencilState::NotEqual;
        res.initial_state.stencil.back.reference = 128;
        res.initial_state.stencil.back.compare_mask = 0xff;
        res.initial_state.stencil.back.write_mask = 0;
        res.initial_state.stencil.back.fail_op = my::StencilState::Keep;
        res.initial_state.stencil.back.depth_fail_op = my::StencilState::Keep;
        res.initial_state.stencil.back.depth_pass_op = my::StencilState::Keep;
        rc->alloc(&res, hrz::monitoring::systems::ShapeEditor);

        const char* picking_color_outputs[] = {"o_object_reference"};

        res.name = hrz_shaders::ShapeEditorShape_picking_name;
        res.vertex_source_len = hrz_shaders::ShapeEditorShape_picking_vert_len;
        res.vertex_source = hrz_shaders::ShapeEditorShape_picking_vert;
        res.fragment_source_len = hrz_shaders::ShapeEditorShape_picking_frag_len;
        res.fragment_source = hrz_shaders::ShapeEditorShape_picking_frag;
        res.outputs = picking_color_outputs;
        res.initial_state.color_blend.enable = false;
        rc->alloc(&res, hrz::monitoring::systems::ShapeEditor);
    }

    {
        static const my::IndexName attribs[] = {
            {PositionLowInputStream, "i_position_low"},
            {PositionHighInputStream, "i_position_high"},
            {WmercPositionLowInputStream, "i_wmerc_low"},
            {WmercPositionHighInputStream, "i_wmerc_high"},
            {GroundNormalInputStream, "i_ground_normal"},
            {LocalPositionInputStream, "i_local_position"},
        };

        static const my::IndexName ubos[] = {
            {hrz::UboFrame, "Frame"},
            {ControlParamsUbo, "Control"},
            {PlanetParamsUbo, "PlanetParams"},
        };

        static const my::IndexName samplers[] = {
            {DtmIndirectionSampler, "u_dtm_indirection"},
            {DtmAtlasSampler, "u_dtm_atlas"},
        };

        const char* color_outputs[] = {"o_color"};

        my::ShaderResource res{};
        res.name = hrz_shaders::ShapeEditorControl_visual_name;
        res.vertex_source_len = hrz_shaders::ShapeEditorControl_visual_vert_len;
        res.vertex_source = hrz_shaders::ShapeEditorControl_visual_vert;
        res.fragment_source_len = hrz_shaders::ShapeEditorControl_visual_frag_len;
        res.fragment_source = hrz_shaders::ShapeEditorControl_visual_frag;
        res.attribs = attribs;
        res.uniform_blocks = ubos;
        res.samplers = samplers;
        res.outputs = color_outputs;
        res.initial_state.rasterization.cull_mode = my::RasterizationState::Back;
        res.initial_state.color_blend.enable = false;
        res.initial_state.depth.test = true;
        res.initial_state.depth.compare = my::DepthState::LessEqual;
        res.initial_state.depth.write = true;
        res.initial_state.stencil.enable = false;
        rc->alloc(&res, hrz::monitoring::systems::ShapeEditor);

        const char* picking_color_outputs[] = {"o_object_reference", "o_depth"};

        res.name = hrz_shaders::ShapeEditorControl_picking_name;
        res.vertex_source_len = hrz_shaders::ShapeEditorControl_picking_vert_len;
        res.vertex_source = hrz_shaders::ShapeEditorControl_picking_vert;
        res.fragment_source_len = hrz_shaders::ShapeEditorControl_picking_frag_len;
        res.fragment_source = hrz_shaders::ShapeEditorControl_picking_frag;
        res.outputs = picking_color_outputs;
        rc->alloc(&res, hrz::monitoring::systems::ShapeEditor);
    }
}

} // namespace editor
} // namespace hrz
