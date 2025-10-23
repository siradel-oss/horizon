#include "hrz_jobs_clipping.h"
#include "hrz_jobs_declarations.h"
#include "hrz_jobs_feature_clamping.h"
#include "hrz_jobs_vector_repr_common.h"

#include <hrz_common_blob_array.h>
#include <hrz_common_blob_vector.h>
#include <hrz_common_profiling.h>
#include <hrz_common_proj.h>
#include <hrz_common_triangulation.h>
#include <hrz_common_vector_data.h>
#include <hrz_common_vector_tiles.h>
#include <hrz_common_vertex_utils.h>
#include <hrz_fnd_flat_hash_map.h>
#include <hrz_fnd_string_utils.h>

#include <earcut.hpp>

#include <limits>
#include <span>

namespace hrz_jobs::bake_extruded_vector_geometry
{
namespace
{
static constexpr double kMaxSegmentAngularLength = lm::radians(4.0); // in radians

static constexpr size_t kInitialVertexCapacity = 512;
static constexpr size_t kInitialPositionCapacity = 2048;
static constexpr size_t kInitialIndexCapacity = 2048;

static constexpr double kInvSqrt2 = 0.7071067811865475; // 1 / sqrt(2)

// Above this angle, we just generate smooth normals instead of a bevel, when
// bevels are enabled.
static constexpr double kBevelMaxAngle = lm::radians(160.0);

// Walls below this threshold are too short to do anything useful with.
static constexpr double kBevelMinWallLength = 1e-6;

// Below this bevel width, we don't do any beveling.
static constexpr float kBevelMinWidth = 1e-2F;

using Vertex = hrz::vt::ExtrudedVectorGeometry::Vertex;
using VertexBuffer = hrz::BlobVector<Vertex>;

struct GeometryBuilder
{
    // Baked positions for later reprojection
    hrz::BlobVector<lm::dvec3> positions;

    // Actual GPU data
    VertexBuffer vertices;
    hrz::BlobVector<uint32_t> indices;

    explicit GeometryBuilder(hrz::BlobAllocator* ba) :
        positions(ba, kInitialPositionCapacity),
        vertices(ba, kInitialVertexCapacity),
        indices(ba, kInitialIndexCapacity)
    {
    }

    inline uint32_t vertices_count() const
    {
        return static_cast<uint32_t>(vertices.size().value_or(0));
    }

    void append_vertex(
        lm::dvec3 position,
        lm::vec3 normal,
        lm::ubvec4 color,
        uint32_t feature_index)
    {
        vertices.push_back({{}, hrz::octahedral_compress_normal(normal), color, feature_index});
        positions.push_back(position);
    }

    void append_index(uint32_t index) { indices.push_back(index); }
};

struct TileInfo
{
    lm::dmat3 normal_matrix;
    hrz::TileCoords tile_coords;
    lm::dbbox2 wmerc_tile_bounds;
    hrz::GeoBounds geo_data_bounds;

    bool clip_to_tile{};
    float bevel_width{};
};

struct FeatureInfo : public TileInfo
{
    explicit FeatureInfo(const TileInfo& ti) : TileInfo{ti} {}

    uint32_t feature_index{};

    lm::ubvec4 roof_color_srgb;
    lm::ubvec4 wall_top_color_srgb;
    lm::ubvec4 wall_bottom_color_srgb;

    bool double_sided_roof{};
    bool double_sided_walls{};
    bool roof_bevel{};
    bool invert_walls_winding{};

    // Used to subdivide walls and roofs so that they don't intersect the planet.
    double max_angular_distance{};
};

struct VertexWithBevelInfo
{
    // Original point location.
    lm::dvec3 pos;

    // Inset position for the roof when beveled.
    lm::dvec3 inset;

    // Normals with the points before and after this one.
    lm::vec3 normal0;
    lm::vec3 normal1;

    bool has_bevel{};

    // Position of the beveled point with the face "before".
    lm::dvec3 bevel_pos0;

    // Position of the beveled point with the face "after".
    lm::dvec3 bevel_pos1;
};

void make_linestring_bevel_info(
    std::span<const lm::dvec3> points,
    std::span<VertexWithBevelInfo> out_vertex_info,
    float bevel_width,
    bool is_closed,
    bool use_z,
    bool invert_normals)
{
    for (size_t i = 0; i < points.size(); ++i)
    {
        lm::dvec3 p0 = points[i == 0 ? (is_closed ? points.size() - 1 : 0) : i - 1];
        lm::dvec3 p1 = points[i];
        lm::dvec3 p2 = points[i + 1 == points.size() ? (is_closed ? 0 : points.size() - 1) : i + 1];

        if (!use_z)
        {
            p0.z = 0.0;
            p1.z = 0.0;
            p2.z = 0.0;
        }

        const lm::dvec3 diff0 = p1 - p0;
        const lm::dvec3 diff1 = p2 - p1;

        const double len0 = lm::length(diff0);
        const double len1 = lm::length(diff1);

        out_vertex_info[i].pos = p1;
        out_vertex_info[i].inset = p1;

        out_vertex_info[i].normal0 = len0 > 0
            ? lm::vec3(lm::normalize(lm::cross(diff0, lm::dvec3(0, 0, 1))))
            : lm::vec3(0, 0, 1);
        out_vertex_info[i].normal1 = len1 > 0
            ? lm::vec3(lm::normalize(lm::cross(diff1, lm::dvec3(0, 0, 1))))
            : lm::vec3(0, 0, 1);

        if (invert_normals)
        {
            out_vertex_info[i].normal0 = -out_vertex_info[i].normal0;
            out_vertex_info[i].normal1 = -out_vertex_info[i].normal1;
        }

        out_vertex_info[i].has_bevel = false;

        if (len0 < kBevelMinWallLength || len1 < kBevelMinWallLength
            || bevel_width < kBevelMinWidth)
        {
            continue;
        }

        const float angle = std::acos(lm::dot(-diff0, diff1) / (len0 * len1));
        const float sin_half_angle = std::sin(angle / 2.0F);

        // Angle too sharp -> no bevel.
        if (sin_half_angle < 1e-3F)
        {
            continue;
        }

        const float offset = bevel_width / (2.0F * sin_half_angle);

        const lm::vec3 inset_dir(
            -lm::normalize(out_vertex_info[i].normal0 + out_vertex_info[i].normal1));

        out_vertex_info[i].inset =
            p1 + lm::dvec3(inset_dir) * kInvSqrt2 * bevel_width / sin_half_angle;

        // Edges too short -> no bevel.
        if (len0 < 2 * offset || len1 < 2 * offset)
        {
            continue;
        }

        // Angle too low -> generate smooth normals, no bevel.
        if (angle > kBevelMaxAngle)
        {
            out_vertex_info[i].normal0 = -inset_dir;
            out_vertex_info[i].normal1 = -inset_dir;
            continue;
        }

        out_vertex_info[i].has_bevel = true;

        out_vertex_info[i].bevel_pos0 = p1 - lm::dvec3(diff0 / len0) * offset;
        out_vertex_info[i].bevel_pos1 = p1 + lm::dvec3(diff1 / len1) * offset;
    }
}

// The longer the angular length, and the closer to the ground,
// the more a segment must be subdivided so that it does not
// intersect the planet.
double max_segment_angular_length_for_altitude(
    double altitude,
    const hrz::TileCoords& tile_coords,
    const hrz::GeoBounds& geo_data_bounds)
{
    if (tile_coords.lod >= 10) return std::numeric_limits<double>::infinity();

    double max_angular_distance = kMaxSegmentAngularLength;
    if (altitude > 0.0)
    {
        max_angular_distance = std::max(
            2.0 * std::acos(hrz::EARTH_RADIUS / (hrz::EARTH_RADIUS + altitude)),
            std::abs(geo_data_bounds.east - geo_data_bounds.west) / 32.0);
    }

    return max_angular_distance;
}

void generate_roof_geometry(
    std::span<const lm::dvec3> feature_span,
    std::span<const std::span<const lm::dvec3>> linestrings,
    GeometryBuilder& builder,
    double altitude,
    const FeatureInfo& info)
{
    if (!builder.vertices.is_valid()) return;

    const uint32_t first_vertex_index = builder.vertices_count();

    std::vector<lm::dvec3> vertex_positions;
    vertex_positions.reserve(feature_span.size());

    auto generate_vertex = [&builder, altitude, &info,
                            &vertex_positions](const lm::dvec3& pt) -> uint32_t
    {
        const lm::dvec3 position = pt + lm::dvec3(0, 0, altitude);
        const lm::dvec3 normal = info.normal_matrix.z;

        auto index = vertex_positions.size();

        vertex_positions.push_back(pt);

        builder.append_vertex(position, lm::vec3(normal), info.roof_color_srgb, info.feature_index);

        if (info.double_sided_roof)
        {
            builder.append_vertex(
                position, lm::vec3(-normal), info.roof_color_srgb, info.feature_index);
        }

        return index;
    };

    auto triangulation_indices = mapbox::earcut<uint32_t>(linestrings);

    if (info.clip_to_tile)
    {
        std::vector<uint32_t> clipped_indices;
        auto feature_span_view_vec2 = hrz::ArrayView<const lm::dvec2>(
            (const lm::dvec2*)feature_span.data(), feature_span.size(), sizeof(lm::dvec3));

        for (size_t i = 0; i < triangulation_indices.size(); i += 3)
        {
            const uint32_t i0 = triangulation_indices[i + 0];
            const uint32_t i1 = triangulation_indices[i + 1];
            const uint32_t i2 = triangulation_indices[i + 2];

            const double p0z = feature_span[i0].z;
            const double p1z = feature_span[i1].z;
            const double p2z = feature_span[i2].z;

            hrz::clip_triangle<double, uint32_t>(
                feature_span_view_vec2, i0, i1, i2, info.wmerc_tile_bounds,
                [&clipped_indices](uint32_t i0, uint32_t i1, uint32_t i2)
                {
                    clipped_indices.push_back(i0);
                    clipped_indices.push_back(i1);
                    clipped_indices.push_back(i2);
                },
                [p0z, p1z, p2z, &generate_vertex](const lm::dvec2& p, const lm::dvec3& w)
                {
                    const double z = p0z * w.x + p1z * w.y + p2z * w.z;
                    return generate_vertex(lm::dvec3(p, z));
                });
        }

        triangulation_indices = std::move(clipped_indices);
    }
    else
    {
        for (const auto& p : feature_span)
        {
            generate_vertex(p);
        }
    }

    if (std::isfinite(info.max_angular_distance))
    {
        hrz::flat_hash_map<lm::uvec2, uint32_t> midpoints_indices;

        for (size_t i = 0; i < triangulation_indices.size();)
        {
            const uint32_t i0 = triangulation_indices[i + 0];
            const uint32_t i1 = triangulation_indices[i + 1];
            const uint32_t i2 = triangulation_indices[i + 2];

            const lm::dvec3 p0 = vertex_positions[i0];
            const lm::dvec3 p1 = vertex_positions[i1];
            const lm::dvec3 p2 = vertex_positions[i2];

            const auto geo0 = hrz::web_mercator_to_geo2(p0.xy);
            const auto geo1 = hrz::web_mercator_to_geo2(p1.xy);
            const auto geo2 = hrz::web_mercator_to_geo2(p2.xy);

            const double edge_lengths[] = {
                lm::length(lm::dvec2{geo1.lon, geo1.lat} - lm::dvec2{geo0.lon, geo0.lat}),
                lm::length(lm::dvec2{geo2.lon, geo2.lat} - lm::dvec2{geo1.lon, geo1.lat}),
                lm::length(lm::dvec2{geo0.lon, geo0.lat} - lm::dvec2{geo2.lon, geo2.lat}),
            };

            size_t longest_edge = 0;
            if (edge_lengths[1] > edge_lengths[0])
            {
                if (edge_lengths[2] > edge_lengths[1])
                {
                    longest_edge = 2;
                }
                else
                {
                    longest_edge = 1;
                }
            }
            else if (edge_lengths[2] > edge_lengths[0])
            {
                longest_edge = 2;
            }

            if (edge_lengths[longest_edge] > info.max_angular_distance)
            {
                lm::uvec2 edge_index_pair;

                switch (longest_edge)
                {
                    case 0: edge_index_pair = {std::min(i0, i1), std::max(i0, i1)}; break;
                    case 1: edge_index_pair = {std::min(i1, i2), std::max(i1, i2)}; break;
                    case 2: edge_index_pair = {std::min(i2, i0), std::max(i2, i0)}; break;
                    default:
                        assert(false && "Invalid state");
                        edge_index_pair = {0, 0};
                        break;
                }

                uint32_t new_i = 0;

                auto it = midpoints_indices.find(edge_index_pair);
                if (it != midpoints_indices.end())
                {
                    new_i = it->second;
                }
                else
                {
                    lm::dvec3 new_p;

                    switch (longest_edge)
                    {
                        case 0: new_p = lm::mix(p0, p1, 0.5); break;
                        case 1: new_p = lm::mix(p1, p2, 0.5); break;
                        case 2: new_p = lm::mix(p2, p0, 0.5); break;
                        default:
                            assert(false && "Invalid state");
                            new_p = {0, 0, 0};
                            break;
                    }

                    new_i = generate_vertex(new_p);
                    midpoints_indices.insert({edge_index_pair, new_i});
                }

                switch (longest_edge)
                {
                    case 0:
                        assert(triangulation_indices[i + 1] != new_i);
                        triangulation_indices[i + 1] = new_i;

                        triangulation_indices.push_back(new_i);
                        triangulation_indices.push_back(i1);
                        triangulation_indices.push_back(i2);
                        break;
                    case 1:
                        assert(triangulation_indices[i + 2] != new_i);
                        triangulation_indices[i + 2] = new_i;

                        triangulation_indices.push_back(new_i);
                        triangulation_indices.push_back(i2);
                        triangulation_indices.push_back(i0);
                        break;
                    case 2:
                        assert(triangulation_indices[i + 2] != new_i);
                        triangulation_indices[i + 2] = new_i;

                        triangulation_indices.push_back(i1);
                        triangulation_indices.push_back(i2);
                        triangulation_indices.push_back(new_i);
                        break;
                    default: assert(false && "Invalid state"); break;
                }
            }
            else
            {
                assert(edge_lengths[0] <= info.max_angular_distance);
                assert(edge_lengths[1] <= info.max_angular_distance);
                assert(edge_lengths[2] <= info.max_angular_distance);

                i += 3;
            }
        }
    }

    if (info.double_sided_roof)
    {
        const size_t index_count = triangulation_indices.size();
        assert(index_count % 3 == 0);
        for (size_t i = 0; i < index_count; i += 3)
        {
            triangulation_indices[i + 0] *= 2;
            triangulation_indices[i + 1] *= 2;
            triangulation_indices[i + 2] *= 2;

            triangulation_indices.push_back(triangulation_indices[i + 2] + 1);
            triangulation_indices.push_back(triangulation_indices[i + 1] + 1);
            triangulation_indices.push_back(triangulation_indices[i + 0] + 1);
        }
    }

    for (const uint32_t idx : triangulation_indices)
    {
        builder.append_index(idx + first_vertex_index);
    }
}

void generate_roof_geometry(
    std::span<const VertexWithBevelInfo> vbis,
    std::span<const std::pair<size_t, size_t>> linestrings,
    GeometryBuilder& builder,
    double roof_altitude,
    const FeatureInfo& info)
{
    std::vector<lm::dvec3> roof_vertices;
    std::vector<std::span<const lm::dvec3>> rings;

    if (!info.roof_bevel)
    {
        // No roof bevel, generate a flat roof with all the side vertices,
        // including the beveled ones.

        size_t count = 0;
        for (const auto& vbi : vbis)
        {
            count += vbi.has_bevel ? 2 : 1;
        }

        roof_vertices.resize(count);
        rings.reserve(linestrings.size());

        size_t v_index = 0;
        for (const auto [start_v_index, linestring_size] : linestrings)
        {
            for (size_t i = 0; i < linestring_size; ++i)
            {
                const auto& vbi = vbis[start_v_index + i];
                if (vbi.has_bevel)
                {
                    roof_vertices[v_index++] = vbi.bevel_pos0;
                    roof_vertices[v_index++] = vbi.bevel_pos1;
                }
                else
                {
                    roof_vertices[v_index++] = vbi.pos;
                }
            }

            rings.emplace_back(roof_vertices.data() + start_v_index, v_index - start_v_index);
        }
    }
    else
    {
        // When the roof is beveled, inset the roof polygon by the bevel offset.

        roof_vertices.resize(vbis.size());
        rings.reserve(linestrings.size());

        size_t v_index = 0;
        for (const auto [start_v_index, linestring_size] : linestrings)
        {
            const std::span<const VertexWithBevelInfo> vbis_ring(
                vbis.data() + start_v_index, linestring_size);

            for (size_t i = 0; i < linestring_size; ++i)
            {
                roof_vertices[v_index++] = vbis_ring[i].inset;
            }

            rings.emplace_back(roof_vertices.data() + start_v_index, v_index - start_v_index);
        }
    }

    generate_roof_geometry(roof_vertices, rings, builder, roof_altitude, info);
}

void generate_wall_geometry(
    lm::dvec2 pp0,
    lm::dvec2 pp1,
    double floor0,
    double floor1,
    double roof0,
    double roof1,
    lm::vec3 n0,
    lm::vec3 n1,
    GeometryBuilder& builder,
    const FeatureInfo& info)
{
    if (!builder.vertices.is_valid()) return;

    bool in_bounds = true;

    if (info.clip_to_tile)
    {
        in_bounds = false;

        const bool in_bounds_0 = lm::contains(info.wmerc_tile_bounds, pp0);
        const bool in_bounds_1 = lm::contains(info.wmerc_tile_bounds, pp1);

        in_bounds = in_bounds_0 || in_bounds_1;

        if (!in_bounds_0 || !in_bounds_1)
        {
            hrz::clip_segment<double>(
                pp0, pp1, info.wmerc_tile_bounds,
                [&](const lm::dvec2& a, const lm::dvec2& b, double ta, double tb)
                {
                    pp0 = a;
                    pp1 = b;

                    std::tie(floor0, floor1) = hrz::lerp_two(floor0, floor1, ta, tb);
                    std::tie(roof0, roof1) = hrz::lerp_two(roof0, roof1, ta, tb);

                    auto nn0 = lm::mix(n0, n1, static_cast<float>(ta));
                    auto nn1 = lm::mix(n0, n1, static_cast<float>(tb));

                    n0 = lm::normalize(nn0);
                    n1 = lm::normalize(nn1);

                    in_bounds = true;
                });
        }
    }

    if (!in_bounds)
    {
        return;
    }

    if (std::isfinite(info.max_angular_distance))
    {
        const auto geo0 = hrz::web_mercator_to_geo2(pp0);
        const auto geo1 = hrz::web_mercator_to_geo2(pp1);

        const double segment_length =
            lm::length(lm::dvec2{geo1.lon, geo1.lat} - lm::dvec2{geo0.lon, geo0.lat});

        if (segment_length > info.max_angular_distance)
        {
            const lm::dvec2 midpoint = lm::mix(pp0, pp1, 0.5);
            const double midpoint_floor = hrz::lerp(floor0, floor1, 0.5);
            const double midpoint_roof = hrz::lerp(roof0, roof1, 0.5);

            const auto midpoint_normal = lm::normalize(lm::vec3(n0 + n1));

            generate_wall_geometry(
                pp0, midpoint, floor0, midpoint_floor, roof0, midpoint_roof, n0, midpoint_normal,
                builder, info);
            generate_wall_geometry(
                midpoint, pp1, midpoint_floor, floor1, midpoint_roof, roof1, midpoint_normal, n1,
                builder, info);

            return;
        }
    }

    const uint32_t first_wall_index = builder.vertices_count();

    n0 = lm::vec3(info.normal_matrix * n0);
    n1 = lm::vec3(info.normal_matrix * n1);

    builder.append_vertex(
        lm::dvec3(pp0, floor0), n0, info.wall_bottom_color_srgb, info.feature_index);
    builder.append_vertex(lm::dvec3(pp0, roof0), n0, info.wall_top_color_srgb, info.feature_index);
    builder.append_vertex(
        lm::dvec3(pp1, floor1), n1, info.wall_bottom_color_srgb, info.feature_index);
    builder.append_vertex(lm::dvec3(pp1, roof1), n1, info.wall_top_color_srgb, info.feature_index);

    builder.append_index(first_wall_index + 0);
    builder.append_index(first_wall_index + (info.invert_walls_winding ? 1 : 2));
    builder.append_index(first_wall_index + (info.invert_walls_winding ? 2 : 1));
    builder.append_index(first_wall_index + 1);
    builder.append_index(first_wall_index + (info.invert_walls_winding ? 3 : 2));
    builder.append_index(first_wall_index + (info.invert_walls_winding ? 2 : 3));

    if (info.double_sided_walls)
    {
        builder.append_vertex(
            lm::dvec3(pp0, floor0), -n0, info.wall_bottom_color_srgb, info.feature_index);
        builder.append_vertex(
            lm::dvec3(pp0, roof0), -n0, info.wall_top_color_srgb, info.feature_index);
        builder.append_vertex(
            lm::dvec3(pp1, floor1), -n1, info.wall_bottom_color_srgb, info.feature_index);
        builder.append_vertex(
            lm::dvec3(pp1, roof1), -n1, info.wall_top_color_srgb, info.feature_index);

        builder.append_index(first_wall_index + 4);
        builder.append_index(first_wall_index + (info.invert_walls_winding ? 6 : 5));
        builder.append_index(first_wall_index + (info.invert_walls_winding ? 5 : 6));
        builder.append_index(first_wall_index + 5);
        builder.append_index(first_wall_index + (info.invert_walls_winding ? 6 : 7));
        builder.append_index(first_wall_index + (info.invert_walls_winding ? 7 : 6));
    }
}

// Vertex structure used for polygon generation and clipping.
// "Interp" because we can interpolate its values.
struct VertexInterp
{
    lm::dvec3 pos;
    lm::vec3 normal;
    lm::vec4 color;
};

void generate_polygon(
    std::span<const VertexInterp> input_verts,
    GeometryBuilder& builder,
    const FeatureInfo& info)
{
    hrz::InlinedVector<VertexInterp, 16> clipped_verts;

    if (info.clip_to_tile)
    {
        hrz::InlinedVector<lm::dvec2, 16> positions;
        hrz::InlinedVector<VertexInterp, 16> tmp_verts;

        positions.reserve(input_verts.size());
        tmp_verts.reserve(input_verts.size());

        for (const auto& v : input_verts)
        {
            positions.push_back(v.pos.xy);
            tmp_verts.push_back(v);
        }

        hrz::clip_convex_polygon<double>(
            positions, info.wmerc_tile_bounds,
            [&tmp_verts](const lm::dvec2&, int i0, int i1, double t) -> int
            {
                const auto& a = tmp_verts[(size_t)i0];
                const auto& b = tmp_verts[(size_t)i1];

                tmp_verts.push_back({
                    lm::mix(a.pos, b.pos, t),
                    lm::normalize(lm::mix(a.normal, b.normal, static_cast<float>(t))),
                    lm::mix(a.color, b.color, static_cast<float>(t)),
                });

                return static_cast<int>(tmp_verts.size() - 1);
            },
            [&tmp_verts, &clipped_verts](std::span<const std::pair<lm::dvec2, int>> clipped)
            {
                if (clipped.size() < 3) return;

                clipped_verts.clear();
                clipped_verts.reserve(clipped.size());

                for (const auto& [pos, index] : clipped)
                {
                    clipped_verts.push_back(tmp_verts[(size_t)index]);
                }
            });

        input_verts = clipped_verts;
    }

    if (input_verts.size() < 3) return;

    const auto first_index = builder.vertices_count();

    for (const auto& p : input_verts)
    {
        builder.append_vertex(
            p.pos, p.normal, hrz::convert_rgba_color_to_bytes(p.color), info.feature_index);
    }

    // Generate fans
    for (size_t i = 1; i + 1 < input_verts.size(); ++i)
    {
        builder.append_index(first_index + 0);
        builder.append_index(first_index + (uint32_t)i);
        builder.append_index(first_index + (uint32_t)(i + 1));
    }
}

void generate_wall_roof_bevel(
    const VertexWithBevelInfo& bi0,
    const VertexWithBevelInfo& bi1,
    double wall_top_z_0,
    double wall_top_z_1,
    double roof_z_0,
    double roof_z_1,
    GeometryBuilder& builder,
    const FeatureInfo& info)
{
    if (!builder.vertices.is_valid()) return;

    const lm::vec3 n0(info.normal_matrix * bi0.normal1);
    const lm::vec3 n1(info.normal_matrix * bi1.normal0);
    const lm::vec3 nroof(info.normal_matrix.z);

    {
        VertexInterp v[4] = {
            {
                lm::dvec3((bi0.has_bevel ? bi0.bevel_pos1 : bi0.pos).xy, wall_top_z_0),
                n0,
                hrz::convert_bytes_to_rgba_color(info.wall_top_color_srgb),
            },
            {
                lm::dvec3((bi1.has_bevel ? bi1.bevel_pos0 : bi1.pos).xy, wall_top_z_1),
                n1,
                hrz::convert_bytes_to_rgba_color(info.wall_top_color_srgb),
            },
            {
                lm::dvec3(bi1.inset.xy, roof_z_1),
                nroof,
                hrz::convert_bytes_to_rgba_color(info.roof_color_srgb),
            },
            {
                lm::dvec3(bi0.inset.xy, roof_z_0),
                nroof,
                hrz::convert_bytes_to_rgba_color(info.roof_color_srgb),
            }};

        if (info.invert_walls_winding)
        {
            std::swap(v[0], v[3]);
            std::swap(v[1], v[2]);
        }

        generate_polygon(v, builder, info);
    }

    // There might be a little bit of vertex duplication here.
    // One way to fix that would be to pass indexed geometry to the generate_polygon function,
    // and multiple polygons at once.
    // Then somehow keep track of indices. Not easy!
    // But this is not a big deal, so ignore this for now.
    // And technically there is already duplication between the top of the wall and the bottom
    // of the bevel, and the top of the bevel and the roof.

    if (bi0.has_bevel)
    {
        VertexInterp v[3] = {
            {
                lm::dvec3(bi0.bevel_pos0.xy, wall_top_z_0),
                lm::vec3(info.normal_matrix * bi0.normal0),
                hrz::convert_bytes_to_rgba_color(info.wall_top_color_srgb),
            },
            {
                lm::dvec3(bi0.bevel_pos1.xy, wall_top_z_0),
                lm::vec3(info.normal_matrix * bi0.normal1),
                hrz::convert_bytes_to_rgba_color(info.wall_top_color_srgb),
            },
            {
                lm::dvec3(bi0.inset.xy, roof_z_0),
                nroof,
                hrz::convert_bytes_to_rgba_color(info.roof_color_srgb),
            }};

        if (info.invert_walls_winding)
        {
            std::swap(v[1], v[2]);
        }

        generate_polygon(v, builder, info);
    }
}

void generate_wall_geometry(
    const VertexWithBevelInfo& vbi0,
    const VertexWithBevelInfo& vbi1,
    double floor0,
    double floor1,
    double wall_top0,
    double wall_top1,
    GeometryBuilder& builder,
    const FeatureInfo& info)
{
    if (!vbi0.has_bevel)
    {
        generate_wall_geometry(
            vbi0.pos.xy, vbi1.has_bevel ? vbi1.bevel_pos0.xy : vbi1.pos.xy, floor0, floor1,
            wall_top0, wall_top1, vbi0.normal1, vbi1.normal0, builder, info);
    }
    else
    {
        generate_wall_geometry(
            vbi0.bevel_pos0.xy, vbi0.bevel_pos1.xy, floor0, floor0, wall_top0, wall_top0,
            vbi0.normal0, vbi0.normal1, builder, info);

        generate_wall_geometry(
            vbi0.bevel_pos1.xy, vbi1.has_bevel ? vbi1.bevel_pos0.xy : vbi1.pos.xy, floor0, floor1,
            wall_top0, wall_top1, vbi0.normal1, vbi1.normal0, builder, info);
    }
}

} // anonymous namespace

hrz::JobResult run(
    const hrz::vt::ExtrudedVectorData& input,
    hrz::vt::ExtrudedVectorGeometry& geometry,
    const JobContext& context)
{
    HRZ_SCOPED_SAMPLE("bake extruded geometry");

    const auto& style = input.style;

    const lm::ubvec4 default_upper_color_srgb =
        hrz::convert_rgba_color_to_bytes(input.default_upper_color);
    const lm::ubvec4 default_lower_color_srgb =
        hrz::convert_rgba_color_to_bytes(input.default_lower_color);
    const lm::ubvec4 default_roof_color_srgb =
        hrz::convert_rgba_color_to_bytes(input.default_roof_color);

    auto input_features = input.geometry.features.get_data();
    auto input_points = input.geometry.points.get_data();
    auto input_sizes = input.geometry.linestring_sizes.get_data();
    auto input_feature_ids = input.feature_ids.get_data();
    auto input_clamps = input.clamps.get_data();

    auto style_prps = input.style.prps.get_data();
    auto style_values = input.style.get_values_reader();

    // Compute the transformation to transform the normals
    const hrz::BSphere<double> tile_bsphere =
        hrz::vector_repr::compute_tile_bounding_sphere(input.geometry.bounds);
    const hrz::GeoPosition3 geo = hrz::ecef_to_geo3(tile_bsphere.center);
    const lm::dmat4 geo_location_xform = hrz::enu_to_ecef_transform_for_geo(geo);
    const lm::dmat3 normal_matrix{
        geo_location_xform.x.xyz, geo_location_xform.y.xyz, geo_location_xform.z.xyz};

    bool has_transparent_geometry = false;

    const bool use_z = input.clamping.use_z();
    const hrz::FeatureClampingGenerator clamps_gen(input_clamps.as_span(), input.clamping);

    GeometryBuilder builder(context.get_blob_allocator());

    TileInfo tile_info;
    tile_info.bevel_width = std::max(input.bevel_width, 0.0F);
    tile_info.clip_to_tile = input.clip_to_tile;
    tile_info.normal_matrix = normal_matrix;
    tile_info.tile_coords = input.coords;
    tile_info.wmerc_tile_bounds = hrz::mercator_tile_bbox_meters(input.coords);
    tile_info.geo_data_bounds = hrz::GeoBounds(
        hrz::web_mercator_to_geo2(input.geometry.bounds.min),
        hrz::web_mercator_to_geo2(input.geometry.bounds.max));

    uint32_t max_feature_index = 0;

    std::vector<VertexWithBevelInfo> vertices_bevel_info;
    std::vector<std::pair<size_t, size_t>> linestrings; // Start-size pairs

    for (const auto& instance : style.instances.get_data())
    {
        if (instance.repr_id != input.repr_id) continue;

        FeatureInfo info(tile_info);
        info.roof_color_srgb = default_roof_color_srgb;
        info.wall_top_color_srgb = default_upper_color_srgb;
        info.wall_bottom_color_srgb = default_lower_color_srgb;

        const auto& feature = input_features.at(instance.feature_index);
        info.feature_index = std::min(instance.feature_index, hrz::vt::MAX_FEATURE_INDEX);

        if (feature.type != hrz_proto::VectorGeometryType::POLYGON_GEOMETRY
            && feature.type != hrz_proto::VectorGeometryType::POLYLINE_GEOMETRY)
        {
            continue;
        }

        max_feature_index = std::max(max_feature_index, info.feature_index);

        const uint64_t prp_begin = instance.first_prp;
        const uint64_t prp_end = prp_begin + instance.prp_count;

        double altitude_offset = input.default_altitude_offset;
        double extrusion = input.default_extrusion;

        for (uint64_t j = prp_begin; j < prp_end; ++j)
        {
            if (style_prps[j] == input.extrusion_prp)
            {
                extrusion = (float)style_values.as_number(j);
            }
            if (style_prps[j] == input.upper_color_prp)
            {
                info.wall_top_color_srgb = style_values.as_color(j);
            }
            if (style_prps[j] == input.lower_color_prp)
            {
                info.wall_bottom_color_srgb = style_values.as_color(j);
            }
            if (style_prps[j] == input.roof_color_prp)
            {
                info.roof_color_srgb = style_values.as_color(j);
            }
            if (style_prps[j] == input.altitude_offset_prp)
            {
                altitude_offset = style_values.as_number(j);
            }
        }

        if (info.wall_top_color_srgb.a != 255 || info.wall_bottom_color_srgb.a != 255
            || info.roof_color_srgb.a != 255)
        {
            has_transparent_geometry = true;
        }

        auto feature_points =
            input_points.as_span().subspan(feature.first_point, feature.point_count);
        auto feature_linestring_sizes =
            input_sizes.as_span().subspan(feature.first_linestring_size, feature.linestring_count);

        const hrz::PointClampingGenerator point_clamp_gen =
            clamps_gen.for_feature(instance.feature_index, feature.first_point);

        linestrings.clear();
        vertices_bevel_info.clear();
        vertices_bevel_info.resize(feature_points.size());

        const bool is_polygon = feature.type == hrz_proto::VectorGeometryType::POLYGON_GEOMETRY;

        uint32_t current_linestring_start = 0;
        for (const uint32_t linestring_size : feature_linestring_sizes)
        {
            const std::span<const lm::dvec3> linestring_points =
                feature_points.subspan(current_linestring_start, linestring_size);
            const std::span<VertexWithBevelInfo> linestring_bevel_verts_info(
                vertices_bevel_info.data() + current_linestring_start, linestring_size);

            // The first linestring of a polygon determines the winding order.
            if (is_polygon && linestrings.empty())
            {
                info.invert_walls_winding = hrz::is_clockwise(linestring_points);
            }

            make_linestring_bevel_info(
                linestring_points, linestring_bevel_verts_info, info.bevel_width, is_polygon, use_z,
                info.invert_walls_winding && is_polygon);

            linestrings.emplace_back(current_linestring_start, linestring_size);
            current_linestring_start += linestring_size;
        }

        double min_clamp = std::numeric_limits<double>::max();
        double max_clamp = std::numeric_limits<double>::lowest();
        for (size_t i = 0; i < feature.point_count; ++i)
        {
            const double clamp = point_clamp_gen.get_clamp_for_point(i);
            min_clamp = std::min(min_clamp, clamp);
            max_clamp = std::max(max_clamp, clamp);
        }

        double min_z = std::numeric_limits<double>::max();
        for (const auto& p : vertices_bevel_info)
        {
            min_z = std::min(min_z, p.pos.z);
        }

        const double min_wall_top_altitude = min_clamp + altitude_offset + extrusion + min_z;
        info.max_angular_distance = max_segment_angular_length_for_altitude(
            min_wall_top_altitude, info.tile_coords, info.geo_data_bounds);

        if (std::isfinite(info.max_angular_distance))
        {
            // Disable bevels if we need tessellation. Why?
            //
            // Bevels just don't work with tessellation for very large polygons
            // because it creates non planar geometry, and tessellation for walls and
            // roofs use specialized techniques that only work because they are horizontal
            // or vertical.
            //
            // Making this work for bevels would be very complex as the tessellation would have
            // to match the other primitives exactly to not create cracks, which is not
            // possible with current specialized techniques.
            //
            // So this would need a more general solution for arbitrary meshes, which we don't have
            // at the moment, and I really don't feel like working on that given that bevels
            // should be fairly small most of the time, which would make them invisible at the
            // scale where tessellation is needed. And if somebody really needs huge tessellation
            // of very large polygons, well, they'll come complain and we'll negotiate.
            //
            // Note that currently the same issues would exist for polygons with extremely
            // elongated shapes (such a a rectangle with very high/low aspect ratio), but they are
            // rare and generally people don't use extruded polygons at this scale, so ignore that
            // for now.
            //
            // Bevels are still applied to the walls though, so that's something at least.
            //
            //     -slerouzic, 2025-10-09
            info.bevel_width = 0.0;
        }

        if (feature.type == hrz_proto::VectorGeometryType::POLYGON_GEOMETRY)
        {
            info.double_sided_walls = false;
            info.double_sided_roof = min_clamp == max_clamp && extrusion == 0;
            info.roof_bevel = info.bevel_width > kBevelMinWidth && !info.double_sided_roof
                && extrusion > info.bevel_width * 2.0;

            const double roof_altitude = max_clamp + altitude_offset + extrusion;
            const double wall_top_offset = info.roof_bevel ? -info.bevel_width * kInvSqrt2 : 0.0;

            generate_roof_geometry(vertices_bevel_info, linestrings, builder, roof_altitude, info);

            if (!info.double_sided_roof)
            {
                // Generate walls geometry for each linestring
                for (const auto [index_ring_start, linestring_size] : linestrings)
                {
                    auto bevel_info_span = std::span<VertexWithBevelInfo>(
                        vertices_bevel_info.data() + index_ring_start, linestring_size);

                    for (size_t p0 = linestring_size - 1, p1 = 0; p1 < linestring_size; p0 = p1++)
                    {
                        const auto& bi0 = bevel_info_span[p0];
                        const auto& bi1 = bevel_info_span[p1];

                        const double floor0 = altitude_offset
                            + point_clamp_gen.clamp_point(index_ring_start + p0, bi0.pos.z);
                        const double floor1 = altitude_offset
                            + point_clamp_gen.clamp_point(index_ring_start + p1, bi1.pos.z);

                        const double roof0 = roof_altitude + bi0.pos.z + wall_top_offset;
                        const double roof1 = roof_altitude + bi1.pos.z + wall_top_offset;

                        generate_wall_geometry(
                            bi0, bi1, floor0, floor1, roof0, roof1, builder, info);

                        if (info.roof_bevel)
                        {
                            generate_wall_roof_bevel(
                                bi0, bi1, roof0, roof1, roof_altitude + bi0.pos.z,
                                roof_altitude + bi1.pos.z, builder, info);
                        }
                    }
                }
            }
        }
        else if (feature.type == hrz_proto::VectorGeometryType::POLYLINE_GEOMETRY)
        {
            const size_t wall_count = vertices_bevel_info.size() - 1;
            info.double_sided_walls = true;

            for (size_t i = 0; i < wall_count; ++i)
            {
                const auto& bi0 = vertices_bevel_info[i];
                const auto& bi1 = vertices_bevel_info[i + 1];

                const double floor0 = altitude_offset + point_clamp_gen.clamp_point(i, bi0.pos.z);
                const double floor1 =
                    altitude_offset + point_clamp_gen.clamp_point(i + 1, bi1.pos.z);

                generate_wall_geometry(
                    bi0, bi1, floor0, floor1, floor0 + extrusion, floor1 + extrusion, builder,
                    info);
            }
        }
    }

    auto feature_id_array_size = input_feature_ids.size();
    if (feature_id_array_size % hrz::vt::DATA_TEXTURE_SIZE != 0)
    {
        feature_id_array_size = feature_id_array_size
            - (feature_id_array_size % hrz::vt::DATA_TEXTURE_SIZE) + hrz::vt::DATA_TEXTURE_SIZE;
    }
    hrz::BlobVector<hrz::vector_data::FeatureIdHash> feature_ids(
        context.get_blob_allocator(), feature_id_array_size);
    for (auto feature_id : input_feature_ids)
    {
        feature_ids.push_back(feature_id);
    }
    feature_ids.resize(feature_id_array_size);

    auto vertex_array_opt = builder.vertices.to_blob_array();
    auto index_array_opt = builder.indices.to_blob_array();
    auto positions_data_opt = builder.positions.data();
    auto feature_ids_data_opt = feature_ids.to_blob_array();
    if (!vertex_array_opt.has_value() || !index_array_opt.has_value()
        || !positions_data_opt.has_value() || !feature_ids_data_opt.has_value())
    {
        return hrz::JobResult::FAILURE;
    }
    auto positions_data = positions_data_opt.value();

    // Convert to Geocentric
    pl_transform_in_place_canonical(
        &hrz_proj::wmerc_to_ecef, (int)positions_data.size(), &positions_data.data()->x);

    // Compute relative coordinates
    {
        auto vertices_data = vertex_array_opt.value().get_mutable_data();
        hrz::vector_repr::compute_rel_coords(
            {positions_data}, tile_bsphere.center,
            {(lm::vec3*)vertices_data.data(), positions_data.size(), sizeof(Vertex)});
    }

    // Compute world-space bounding sphere
    hrz::BSphere<double> bsphere;
    bsphere = hrz::compute_bounding_sphere(std::span<const lm::dvec3>(positions_data));

    // Finalize
    geometry.vertex_data = std::move(vertex_array_opt.value());
    geometry.indices = std::move(index_array_opt.value());
    geometry.feature_ids = std::move(feature_ids_data_opt.value());
    geometry.max_feature_index = max_feature_index;
    geometry.center = tile_bsphere.center;
    geometry.bsphere_center = bsphere.center;
    geometry.bsphere_radius = bsphere.radius;
    geometry.has_transparency = has_transparent_geometry;

    geometry.vertex_data.register_blob_metadata(
        context.get_blob_allocator(), "contents"_ss, "extruded vector vertex data"_ss);
    geometry.indices.register_blob_metadata(
        context.get_blob_allocator(), "contents"_ss, "extruded vector indices"_ss);
    geometry.feature_ids.register_blob_metadata(
        context.get_blob_allocator(), "contents"_ss, "feature IDs"_ss);

    geometry.vertex_data.register_blob_owner(
        context.get_blob_allocator(), context.get_resource_owner());
    geometry.indices.register_blob_owner(
        context.get_blob_allocator(), context.get_resource_owner());
    geometry.feature_ids.register_blob_owner(
        context.get_blob_allocator(), context.get_resource_owner());

    return hrz::JobResult::SUCCESS;
}

} // namespace hrz_jobs::bake_extruded_vector_geometry
