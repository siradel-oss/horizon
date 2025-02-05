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

namespace hrz_jobs::bake_extruded_vector_geometry
{
namespace
{
static constexpr double MAX_SEGMENT_ANGULAR_LENGTH = lm::radians(4.0); // in radians

static constexpr size_t InitialVertexCapacity = 512;
static constexpr size_t InitialPositionCapacity = 2048;
static constexpr size_t InitialIndexCapacity = 2048;

using Vertex = hrz::vt::ExtrudedVectorGeometry::Vertex;
using VertexBuffer = hrz::BlobVector<Vertex>;

// The longer the angular length, and the closer to the ground,
// the more a segment must be subdivided so that it does not
// intersect the planet.
double max_segment_angular_length_for_altitude(
    double altitude,
    const hrz::TileCoords& tile_coords,
    const hrz::GeoBounds& geo_data_bounds)
{
    if (tile_coords.lod >= 10) return std::numeric_limits<double>::infinity();

    double max_angular_distance = MAX_SEGMENT_ANGULAR_LENGTH;
    if (altitude > 0.0)
    {
        max_angular_distance = std::max(
            2.0 * std::acos(hrz::EARTH_RADIUS / (hrz::EARTH_RADIUS + altitude)),
            std::abs(geo_data_bounds.east - geo_data_bounds.west) / 32.0);
    }

    return max_angular_distance;
}

void append_vertex(
    VertexBuffer& vertices,
    hrz::BlobVector<lm::dvec3>& positions,
    lm::dvec3 position,
    lm::vec3 normal,
    lm::ubvec4 color,
    uint32_t feature_index)
{
    vertices.push_back({{}, hrz::octahedral_compress_normal(normal), color, feature_index});
    positions.push_back(position);
}

void generate_roof_geometry(
    gsl::span<const lm::dvec3> feature_span,
    gsl::span<const gsl::span<const lm::dvec3>> linestrings,
    hrz::BlobVector<lm::dvec3>& positions,
    VertexBuffer& vertices,
    hrz::BlobVector<uint32_t>& indices,
    double altitude,
    lm::ubvec4 rgba,
    const lm::dmat3& normal_matrix,
    uint32_t feature_index,
    bool use_z,
    bool double_sided,
    const hrz::TileCoords& tile_coords,
    const lm::dbbox2& wmerc_tile_bounds,
    const hrz::GeoBounds& geo_data_bounds,
    bool clip_to_tile)
{
    if (!vertices.is_valid()) return;

    auto max_angular_distance =
        max_segment_angular_length_for_altitude(altitude, tile_coords, geo_data_bounds);

    uint32_t first_vertex_index = vertices.size().value_or(0);

    std::vector<lm::dvec3> vertex_positions;
    vertex_positions.reserve(feature_span.size());

    auto generate_vertex = [&](const lm::dvec3& pt) -> uint32_t
    {
        lm::dvec3 position;
        if (use_z)
        {
            position = pt + lm::dvec3(0, 0, altitude);
        }
        else
        {
            position = lm::dvec3(pt.xy, altitude);
        }

        lm::dvec3 normal = normal_matrix.z;

        auto index = vertex_positions.size();

        vertex_positions.push_back(pt);

        append_vertex(vertices, positions, position, lm::vec3(normal), rgba, feature_index);

        if (double_sided)
        {
            append_vertex(vertices, positions, position, lm::vec3(-normal), rgba, feature_index);
        }

        return index;
    };

    auto triangulation_indices = mapbox::earcut<uint32_t>(linestrings);

    if (clip_to_tile)
    {
        std::vector<lm::dvec3> clipped_points;
        std::vector<uint32_t> clipped_indices;
        clipped_indices.reserve(triangulation_indices.size());

        auto feature_span_view = hrz::ArrayView<const lm::dvec2>(
            (const lm::dvec2*)feature_span.data(), feature_span.size(), sizeof(lm::dvec3));

        // Some of the original feature points are not used because of clipping.
        hrz::flat_hash_map<uint32_t, uint32_t> point_indices_to_vertex_indices;

        for (size_t i = 0; i < triangulation_indices.size(); i += 3)
        {
            const uint32_t i0 = triangulation_indices[i + 0];
            const uint32_t i1 = triangulation_indices[i + 1];
            const uint32_t i2 = triangulation_indices[i + 2];

            hrz::clip_triangle<double, uint32_t>(
                feature_span_view, i0, i1, i2, feature_span.size() + clipped_points.size(),
                wmerc_tile_bounds,
                [&](uint32_t i0, uint32_t i1, uint32_t i2)
                {
                    uint32_t i[3] = {i0, i1, i2};
                    for (size_t j = 0; j < 3; ++j)
                    {
                        uint32_t vertex_index = 0;
                        auto it = point_indices_to_vertex_indices.find(i[j]);
                        if (it == point_indices_to_vertex_indices.end())
                        {
                            vertex_index = generate_vertex(
                                i[j] >= feature_span.size()
                                    ? clipped_points[i[j] - feature_span.size()]
                                    : feature_span[i[j]]);
                            point_indices_to_vertex_indices.insert({i[j], vertex_index});
                        }
                        else
                        {
                            vertex_index = it->second;
                        }
                        clipped_indices.push_back(vertex_index);
                    }
                },
                [&](const lm::dvec2& p) { clipped_points.emplace_back(p); });
        }

        triangulation_indices = std::move(clipped_indices);
    }
    else
    {
        for (uint32_t i = 0; i < feature_span.size(); ++i)
        {
            generate_vertex(feature_span[i]);
        }
    }

    if (std::isfinite(max_angular_distance))
    {
        hrz::flat_hash_map<lm::uvec2, uint32_t> midpoints_indices;

        for (size_t i = 0; i < triangulation_indices.size();)
        {
            uint32_t i0 = triangulation_indices[i + 0];
            uint32_t i1 = triangulation_indices[i + 1];
            uint32_t i2 = triangulation_indices[i + 2];

            lm::dvec3 p0 = vertex_positions[i0];
            lm::dvec3 p1 = vertex_positions[i1];
            lm::dvec3 p2 = vertex_positions[i2];

            auto geo0 = hrz::web_mercator_to_geo2(p0.xy);
            auto geo1 = hrz::web_mercator_to_geo2(p1.xy);
            auto geo2 = hrz::web_mercator_to_geo2(p2.xy);

            double edge_lengths[] = {
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

            if (edge_lengths[longest_edge] > max_angular_distance)
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
                assert(edge_lengths[0] <= max_angular_distance);
                assert(edge_lengths[1] <= max_angular_distance);
                assert(edge_lengths[2] <= max_angular_distance);

                i += 3;
            }
        }
    }

    if (double_sided)
    {
        size_t index_count = triangulation_indices.size();
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

    for (size_t i = 0; i < triangulation_indices.size(); ++i)
    {
        indices.push_back(triangulation_indices[i] + first_vertex_index);
    }
}

void generate_wall_geometry(
    lm::dvec2 pp0,
    lm::dvec2 pp1,
    double floor0,
    double floor1,
    double roof0,
    double roof1,
    hrz::BlobVector<lm::dvec3>& positions,
    VertexBuffer& vertices,
    hrz::BlobVector<uint32_t>& indices,
    lm::ubvec4 upper_rgba,
    lm::ubvec4 lower_rgba,
    const lm::dmat3& normal_matrix,
    uint32_t feature_index,
    const hrz::TileCoords& tile_coords,
    const hrz::GeoBounds& geo_data_bounds)
{
    auto vertices_size = vertices.size();
    if (!vertices_size.has_value()) return;

    double altitude = std::max(roof0, roof1);
    auto max_angular_distance =
        max_segment_angular_length_for_altitude(altitude, tile_coords, geo_data_bounds);

    bool append_as_is = false;

    if (std::isfinite(max_angular_distance))
    {
        auto geo0 = hrz::web_mercator_to_geo2(pp0);
        auto geo1 = hrz::web_mercator_to_geo2(pp1);

        double segment_length =
            lm::length(lm::dvec2{geo1.lon, geo1.lat} - lm::dvec2{geo0.lon, geo0.lat});

        if (segment_length > max_angular_distance)
        {
            lm::dvec2 midpoint = lm::mix(pp0, pp1, 0.5);
            double midpoint_floor = hrz::lerp(floor0, floor1, 0.5);
            double midpoint_roof = hrz::lerp(roof0, roof1, 0.5);

            generate_wall_geometry(
                pp0, midpoint, floor0, midpoint_floor, roof0, midpoint_roof, positions, vertices,
                indices, upper_rgba, lower_rgba, normal_matrix, feature_index, tile_coords,
                geo_data_bounds);
            generate_wall_geometry(
                midpoint, pp1, midpoint_floor, floor1, midpoint_roof, roof1, positions, vertices,
                indices, upper_rgba, lower_rgba, normal_matrix, feature_index, tile_coords,
                geo_data_bounds);
        }
        else
        {
            append_as_is = true;
        }
    }
    else
    {
        append_as_is = true;
    }

    if (append_as_is)
    {
        uint32_t first_wall_index = vertices_size.value();

        auto normal =
            lm::vec3(normal_matrix * lm::normalize(lm::dvec3(pp0.y - pp1.y, pp1.x - pp0.x, 0)));

        append_vertex(
            vertices, positions, lm::dvec3(pp0, floor0), normal, lower_rgba, feature_index);
        append_vertex(
            vertices, positions, lm::dvec3(pp0, roof0), normal, upper_rgba, feature_index);
        append_vertex(
            vertices, positions, lm::dvec3(pp1, floor1), normal, lower_rgba, feature_index);
        append_vertex(
            vertices, positions, lm::dvec3(pp1, roof1), normal, upper_rgba, feature_index);

        indices.push_back(first_wall_index + 0);
        indices.push_back(first_wall_index + 1);
        indices.push_back(first_wall_index + 2);
        indices.push_back(first_wall_index + 1);
        indices.push_back(first_wall_index + 3);
        indices.push_back(first_wall_index + 2);
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

    auto default_upper_rgba = hrz::convert_rgba_color_to_bytes(input.default_upper_color);
    auto default_lower_rgba = hrz::convert_rgba_color_to_bytes(input.default_lower_color);
    auto default_roof_rgba = hrz::convert_rgba_color_to_bytes(input.default_roof_color);

    auto input_features = input.geometry.features.get_data();
    auto input_points = input.geometry.points.get_data();
    auto input_sizes = input.geometry.linestring_sizes.get_data();
    auto input_feature_ids = input.feature_ids.get_data();
    auto input_clamps = input.clamps.get_data();

    auto style_prps = input.style.prps.get_data();
    auto style_values = input.style.get_values_reader();

    // Compute the transformation to transform the normals
    double radius{};
    lm::dvec3 center;
    hrz::vector_repr::compute_tile_radius_center(input.geometry.bounds, &radius, &center);
    hrz::GeoPosition3 geo = hrz::ecef_to_geo3(center);
    lm::dmat4 geo_location_xform = hrz::enu_to_ecef_transform_for_geo(geo);
    lm::dmat3 normal_matrix{
        geo_location_xform.x.xyz, geo_location_xform.y.xyz, geo_location_xform.z.xyz};

    bool has_transparent_geometry = false;

    bool use_z = input.clamping.use_z();
    hrz::FeatureClampingGenerator clamps_gen(input_clamps.as_span(), input.clamping);

    lm::dbbox2 wmerc_tile_bounds = hrz::mercator_tile_bbox_meters(input.coords);
    auto sw_geo = hrz::web_mercator_to_geo2(input.geometry.bounds.min);
    auto ne_geo = hrz::web_mercator_to_geo2(input.geometry.bounds.max);
    hrz::GeoBounds geo_data_bounds = {sw_geo.lon, ne_geo.lon, sw_geo.lat, ne_geo.lat};

    VertexBuffer vertices(context.get_blob_allocator(), InitialVertexCapacity);
    hrz::BlobVector<uint32_t> indices(context.get_blob_allocator(), InitialIndexCapacity);

    // Baked positions for later reprojection
    hrz::BlobVector<lm::dvec3> positions(context.get_blob_allocator(), InitialPositionCapacity);

    uint32_t max_feature_index = 0;

    // Create triangulation
    for (const auto& instance : style.instances.get_data())
    {
        if (instance.repr_id != input.repr_id) continue;

        const auto& feature = input_features.at(instance.feature_index);
        auto feature_index = std::min(instance.feature_index, hrz::vt::MAX_FEATURE_INDEX);

        if (feature.type != hrz_proto::VectorGeometryType::POLYGON_GEOMETRY
            && feature.type != hrz_proto::VectorGeometryType::POLYLINE_GEOMETRY)
        {
            continue;
        }

        max_feature_index = std::max(max_feature_index, feature_index);

        uint64_t prp_begin = instance.first_prp;
        uint64_t prp_end = prp_begin + instance.prp_count;

        double altitude_offset = input.default_altitude_offset;
        double extrusion = input.default_extrusion;
        lm::ubvec4 upper_rgba = default_upper_rgba;
        lm::ubvec4 lower_rgba = default_lower_rgba;
        lm::ubvec4 roof_rgba = default_roof_rgba;

        for (uint64_t j = prp_begin; j < prp_end; ++j)
        {
            if (style_prps[j] == input.extrusion_prp)
            {
                extrusion = (float)style_values.as_number(j);
            }
            if (style_prps[j] == input.upper_color_prp)
            {
                upper_rgba = style_values.as_color(j);
            }
            if (style_prps[j] == input.lower_color_prp)
            {
                lower_rgba = style_values.as_color(j);
            }
            if (style_prps[j] == input.roof_color_prp)
            {
                roof_rgba = style_values.as_color(j);
            }
            if (style_prps[j] == input.altitude_offset_prp)
            {
                altitude_offset = style_values.as_number(j);
            }
        }

        if (upper_rgba.a != 255 || lower_rgba.a != 255 || roof_rgba.a != 255)
        {
            has_transparent_geometry = true;
        }

        auto feature_points =
            input_points.as_span().subspan(feature.first_point, feature.point_count);
        auto feature_linestring_sizes =
            input_sizes.as_span().subspan(feature.first_linestring_size, feature.linestring_count);

        hrz::PointClampingGenerator point_clamp_gen =
            clamps_gen.for_feature(instance.feature_index, feature.first_point);

        if (feature.type == hrz_proto::VectorGeometryType::POLYGON_GEOMETRY)
        {
            std::vector<gsl::span<const lm::dvec3>> linestrings;

            uint32_t current_linestring_start = 0;
            for (uint32_t linestring_size : feature_linestring_sizes)
            {
                gsl::span<const lm::dvec3> ring_points =
                    feature_points.subspan(current_linestring_start, linestring_size);
                current_linestring_start += linestring_size;
                linestrings.push_back(ring_points);
            }

            double min_clamp = std::numeric_limits<double>::max();
            double max_clamp = std::numeric_limits<double>::lowest();
            for (size_t i = 0; i < feature.point_count; ++i)
            {
                double clamp = point_clamp_gen.get_clamp_for_point(i);
                min_clamp = std::min(min_clamp, clamp);
                max_clamp = std::max(max_clamp, clamp);
            }

            double roof_altitude = max_clamp + altitude_offset + extrusion;
            bool is_simple_plane = min_clamp == max_clamp && extrusion == 0;

            generate_roof_geometry(
                feature_points, linestrings, positions, vertices, indices, roof_altitude, roof_rgba,
                normal_matrix, feature_index, input.clamping.use_z(), is_simple_plane, input.coords,
                wmerc_tile_bounds, geo_data_bounds, input.clip_to_tile);

            if (!is_simple_plane)
            {
                bool is_clockwise = hrz::is_clockwise(linestrings[0]);

                // Generate walls geometry for each linestring
                for (auto linestring_span : linestrings)
                {
                    uint32_t linestring_size = linestring_span.size();
                    size_t index_ring_start =
                        std::distance(feature_points.data(), linestring_span.data());

                    uint32_t p0 = linestring_size - 1;
                    for (uint32_t p1 = 0; p1 < linestring_size; ++p1)
                    {
                        lm::dvec3 pp0 = linestring_span[p0];
                        lm::dvec3 pp1 = linestring_span[p1];
                        bool in_bounds = true;

                        if (input.clip_to_tile)
                        {
                            in_bounds = lm::contains(wmerc_tile_bounds, pp0.xy)
                                || lm::contains(wmerc_tile_bounds, pp1.xy);
                            hrz::clip_segment<double>(
                                pp0.xy, pp1.xy, pp0.z, pp1.z, wmerc_tile_bounds,
                                [&](const lm::dvec2& a, const lm::dvec2& b, double az, double bz)
                                {
                                    pp0 = lm::dvec3(a, az);
                                    pp1 = lm::dvec3(b, bz);
                                    in_bounds = true;
                                });
                        }

                        if (in_bounds)
                        {
                            double floor0 = altitude_offset
                                + point_clamp_gen.clamp_point(index_ring_start + p0, pp0.z);
                            double floor1 = altitude_offset
                                + point_clamp_gen.clamp_point(index_ring_start + p1, pp1.z);

                            double roof0 = roof_altitude + (use_z ? pp0.z : 0);
                            double roof1 = roof_altitude + (use_z ? pp1.z : 0);

                            if (!is_clockwise)
                            {
                                std::swap(pp0, pp1);
                                std::swap(floor0, floor1);
                                std::swap(roof0, roof1);
                            }

                            generate_wall_geometry(
                                pp0.xy, pp1.xy, floor0, floor1, roof0, roof1, positions, vertices,
                                indices, upper_rgba, lower_rgba, normal_matrix, feature_index,
                                input.coords, geo_data_bounds);
                        }

                        p0 = p1;
                    }
                }
            }
        }
        else if (feature.type == hrz_proto::VectorGeometryType::POLYLINE_GEOMETRY)
        {
            uint32_t wall_count = feature_points.size() - 1;

            for (uint32_t p0 = 0; p0 < wall_count; ++p0)
            {
                lm::dvec3 pp0 = feature_points[p0];
                lm::dvec3 pp1 = feature_points[p0 + 1];

                double alt0 = altitude_offset + point_clamp_gen.clamp_point(p0, pp0.z);
                double alt1 = altitude_offset + point_clamp_gen.clamp_point(p0 + 1, pp1.z);

                // Generate double-sided walls.
                generate_wall_geometry(
                    pp0.xy, pp1.xy, alt0, alt1, alt0 + extrusion, alt1 + extrusion, positions,
                    vertices, indices, upper_rgba, lower_rgba, normal_matrix, feature_index,
                    input.coords, geo_data_bounds);

                generate_wall_geometry(
                    pp1.xy, pp0.xy, alt1, alt0, alt1 + extrusion, alt0 + extrusion, positions,
                    vertices, indices, upper_rgba, lower_rgba, normal_matrix, feature_index,
                    input.coords, geo_data_bounds);
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

    auto vertex_array_opt = vertices.to_blob_array();
    auto index_array_opt = indices.to_blob_array();
    auto positions_data_opt = positions.data();
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
        auto vertices_data = vertex_array_opt.value().get_data();
        hrz::vector_repr::compute_rel_coords(
            {positions_data}, center,
            {(lm::vec3*)vertices_data.data(), positions_data.size(), sizeof(Vertex)});
    }

    // Compute world-space bounding sphere
    hrz::BSphere<double> bsphere;
    bsphere = hrz::compute_bounding_sphere(gsl::span<const lm::dvec3>(positions_data));

    // Finalize
    geometry.vertex_data = std::move(vertex_array_opt.value());
    geometry.indices = std::move(index_array_opt.value());
    geometry.feature_ids = std::move(feature_ids_data_opt.value());
    geometry.max_feature_index = max_feature_index;
    geometry.center = center;
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
