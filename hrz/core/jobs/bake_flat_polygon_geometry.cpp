#include "hrz/common/blob_allocator.h"
#include "hrz/common/blob_array.h"
#include "hrz/common/blob_vector.h"
#include "hrz/common/color.h"
#include "hrz/common/profiling.h"
#include "hrz/common/proj.h"
#include "hrz/common/triangulation.h" // IWYU pragma: keep
#include "hrz/common/vector_tiles/data_texture.h"
#include "hrz/common/vector_tiles/picking.h"
#include "hrz/common/vertex_utils.h"
#include "hrz/core/jobs/clipping.h"
#include "hrz/core/jobs/jobs_declarations.h"
#include "hrz/core/jobs/vector_repr_common.h"
#include "hrz/core/jobs/vector_tiles_jobs_params.h"
#include "hrz/fnd/flat_hash_map.h"
#include "hrz/fnd/inlined_vector.h"
#include "hrz/fnd/maths.h"
#include "hrz/fnd/static_vector.h"

#include <CDT.h>
#include <delabella.h>
#include <earcut.hpp>

#include <limits>

namespace hrz_jobs::bake_flat_polygon_geometry
{
namespace
{
static constexpr size_t InitialVertexCapacity = 4096;

double lm_dvec2_get_x(const lm::dvec2& v)
{
    return v.x;
}

double lm_dvec2_get_y(const lm::dvec2& v)
{
    return v.y;
}

double lm_ivec2_get_x(const lm::ivec2& v)
{
    return v.x;
}

double lm_ivec2_get_y(const lm::ivec2& v)
{
    return v.y;
}

struct Triangle
{
    uint32_t i0;
    lm::dvec2 p0;
    uint32_t i1;
    lm::dvec2 p1;
    uint32_t i2;
    lm::dvec2 p2;
};

struct DelabellaTriangulationIterator
{
    DelabellaTriangulationIterator(
        IDelaBella2<double, int32_t>* triangulation_,
        std::span<const lm::dvec2> vertices) :
        triangulation(triangulation_),
        vertices(vertices),
        current_triangle(triangulation->GetFirstDelaunaySimplex())
    {
    }

    std::optional<Triangle> get_next_triangle()
    {
        if (current_triangle == nullptr) return std::nullopt;

        while (!current_triangle->IsInterior(0)) // The parameter is unused.
        {
            current_triangle = current_triangle->next;

            if (current_triangle == nullptr) return std::nullopt;
        }

        uint32_t i0 = current_triangle->v[0]->i;
        uint32_t i1 = current_triangle->v[1]->i;
        uint32_t i2 = current_triangle->v[2]->i;

        auto result =
            std::optional<Triangle>{{i0, vertices[i0], i1, vertices[i1], i2, vertices[i2]}};

        current_triangle = current_triangle->next;

        return result;
    }

    IDelaBella2<double, int32_t>* triangulation;
    std::span<const lm::dvec2> vertices;
    const IDelaBella2<double, int32_t>::Simplex* current_triangle;
};

struct CdtTriangulationIterator
{
    explicit CdtTriangulationIterator(CDT::Triangulation<double>* triangulation_) :
        triangulation(triangulation_), it(triangulation->triangles.begin())
    {
    }

    std::optional<Triangle> get_next_triangle()
    {
        if (it == triangulation->triangles.end()) return std::nullopt;

        auto to_lm = [&](const CDT::V2d<double>& v) { return lm::dvec2(v.x, v.y); };

        auto result = std::optional<Triangle>{
            {it->vertices[0], to_lm(triangulation->vertices[it->vertices[0]]), it->vertices[1],
             to_lm(triangulation->vertices[it->vertices[1]]), it->vertices[2],
             to_lm(triangulation->vertices[it->vertices[2]])}};

        it++;

        return result;
    }

    CDT::Triangulation<double>* triangulation;
    CDT::TriangleVec::iterator it;
};

void append_polygon_edge(
    uint32_t index0,
    uint32_t index1,
    hrz::BlobVector<lm::dvec2>& positions,
    hrz::BlobVector<lm::ivec2>& edges,
    double max_segment_angular_length)
{
    auto position0 = positions.get_value(index0).value_or(lm::dvec2{0, 0});
    auto position1 = positions.get_value(index1).value_or(lm::dvec2{0, 0});

    auto geo0 = hrz::web_mercator_to_geo2({position0.x, position0.y});
    auto geo1 = hrz::web_mercator_to_geo2({position1.x, position1.y});

    auto edge_length = lm::length(lm::dvec2(geo1.lon, geo1.lat) - lm::dvec2(geo0.lon, geo0.lat));

    if (edge_length > max_segment_angular_length)
    {
        auto midpoint =
            lm::mix(lm::dvec2{position0.x, position0.y}, lm::dvec2{position1.x, position1.y}, 0.5);

        const uint32_t midpoint_index = positions.size().value_or(0);
        positions.push_back({midpoint.x, midpoint.y});

        append_polygon_edge(index0, midpoint_index, positions, edges, max_segment_angular_length);
        append_polygon_edge(midpoint_index, index1, positions, edges, max_segment_angular_length);
    }
    else
    {
        edges.push_back({(int32_t)index0, (int32_t)index1});
    }
}

template<typename TVertex>
void append_polygon_vertex(
    hrz::BlobVector<TVertex>& polygon_data,
    lm::ubvec4 rgba,
    uint32_t pattern_style_index,
    uint32_t feature_index);

template<>
void append_polygon_vertex<hrz_jobs::FlatPolygonGeometry::SolidColorPolygonVertex>(
    hrz::BlobVector<hrz_jobs::FlatPolygonGeometry::SolidColorPolygonVertex>& polygon_data,
    lm::ubvec4 rgba,
    uint32_t /* pattern_style_index */,
    uint32_t feature_index)
{
    // Position will be written later.
    polygon_data.push_back({{}, rgba, feature_index});
}

template<>
void append_polygon_vertex<hrz_jobs::FlatPolygonGeometry::PatternPolygonVertex>(
    hrz::BlobVector<hrz_jobs::FlatPolygonGeometry::PatternPolygonVertex>& polygon_data,
    lm::ubvec4 /* rgba */,
    uint32_t pattern_style_index,
    uint32_t feature_index)
{
    // Position, UV, and in-tile latitude will be written later.
    // Background colour is part of the pattern style, and is written in the
    // style texture.
    polygon_data.push_back({{}, {}, 0.0, pattern_style_index, feature_index});
}

template<typename TVertex>
void generate_polygon_geometry(
    uint32_t feature_index,
    std::span<const lm::dvec3> feature_span,
    std::span<const uint32_t> linestring_sizes,
    hrz::BlobVector<lm::dvec3>& positions,
    hrz::BlobVector<TVertex>& polygon_data,
    hrz::BlobVector<uint32_t>& indices,
    lm::ubvec4 rgba,
    uint32_t pattern_style_index,
    const hrz::TileCoords& tile_coords,
    const lm::dbbox2& tile_bounds,
    bool should_clip,
    hrz::BlobAllocator* blob_allocator)
{
    hrz::flat_hash_map<uint32_t, uint32_t> triangulation_to_mesh_indices;

    auto append_vertex = [&](std::optional<uint32_t> triangulation_index, const lm::dvec3& position)
    {
        if (triangulation_index.has_value())
        {
            auto it = triangulation_to_mesh_indices.find(triangulation_index.value());
            if (it != triangulation_to_mesh_indices.end())
            {
                return it->second;
            }
        }

        const uint32_t mesh_index = positions.size().value_or(0);
        positions.push_back(position);
        append_polygon_vertex<TVertex>(polygon_data, rgba, pattern_style_index, feature_index);

        if (triangulation_index.has_value())
        {
            triangulation_to_mesh_indices.insert({triangulation_index.value(), mesh_index});
        }

        return mesh_index;
    };

    auto append_triangle = [&](std::optional<uint32_t> index0, const lm::dvec3& position0,
                               std::optional<uint32_t> index1, const lm::dvec3& position1,
                               std::optional<uint32_t> index2, const lm::dvec3& position2)
    {
        indices.push_back(append_vertex(index0, lm::dvec3(position0.xy, 0)));
        indices.push_back(append_vertex(index1, lm::dvec3(position1.xy, 0)));
        indices.push_back(append_vertex(index2, lm::dvec3(position2.xy, 0)));
    };

    // Triangulate the polygon and generate its geometry.
    //
    // Low-zoom tiles are wrapped around the planet after they are triangulated.
    // This causes two problems:
    //   * Thin triangles (which are common after triangulating a polygon) can be
    //     flipped.
    //   * Linear interpolation of UVs gives wrong results after the mesh is
    //     deformed.
    // The first problem requires smaller, squarer triangles. The second problem
    // could be resolved by doing bilinear interpolation of UVs in the shader,
    // but it would involve bringing more data (to define quads), do more work in
    // the fragment shaders, and have two paths depending on the tile level.
    // Instead low-zoom tiles are triangulated with CDT, and high-zoom tiles with
    // Earcut. CDT is slower than Earcut, but enables generating much more regular
    // meshes by inserting additional points. Such points are added so that tiny,
    // regular polygons, that can follow the curvature of the planet, are generated.

    if (tile_coords.lod <= hrz::vector_repr::MAX_LOD_FOR_SUBDIVISION)
    {
        // At this level, clamping is not taken into account because its effects are
        // negligible and it doesn't mesh well with how CDT works. (Pun unintended)

        auto max_segment_angular_length =
            hrz::vector_repr::max_segment_angular_length_for_lod(tile_coords.lod);

        lm::dbbox2 polygon_bbox = lm::dbbox2::invalid();

        hrz::BlobVector<lm::dvec2> vertices(blob_allocator, feature_span.size() * 2);
        for (const auto& vertex : feature_span)
        {
            vertices.push_back({vertex.x, vertex.y});
            polygon_bbox = lm::expand(polygon_bbox, vertex.xy);
        }

        hrz::BlobVector<lm::ivec2> edges(blob_allocator, feature_span.size());

        auto append_edge = [&](uint32_t index0, uint32_t index1)
        { append_polygon_edge(index0, index1, vertices, edges, max_segment_angular_length); };

        {
            uint32_t edge_count = 0;
            for (const uint32_t linestring_size : linestring_sizes)
            {
                if (linestring_size >= 2)
                {
                    edge_count += linestring_size;
                }
            }

            edges.reserve(edge_count);

            uint32_t linestring_start = 0;
            for (const uint32_t linestring_size : linestring_sizes)
            {
                for (uint32_t i = 0; i < linestring_size - 1; ++i)
                {
                    auto index = linestring_start + i;
                    append_edge(index, index + 1);
                }
                if (linestring_size >= 2)
                {
                    append_edge(linestring_start + linestring_size - 1, linestring_start);
                }

                linestring_start += linestring_size;
            }
        }

        // Add additional points to make the resulting mesh more regular, which make
        // it behave more nicely under deformation. (When it is wrapped on the planet's
        // surface.

        auto point_grid_bbox = lm::intersection(tile_bounds, polygon_bbox);
        if (lm::is_valid(point_grid_bbox))
        {
            auto lon_angle_to_distance = [&](double lon_angle, double lat)
            {
                // This does not take the planet's eccentricity into account,
                // but we don't need to be super precise anyway.
                return (lon_angle / (lm::PI * 2.0)) * hrz::EARTH_CIRCUMFERENCE * std::cos(lat);
            };

            auto distance_to_lat_angle = [&](double distance)
            {
                // Same thing for the eccentricity.
                return (distance / hrz::EARTH_CIRCUMFERENCE) * lm::PI * 2.0;
            };

            auto tile_geo_min = hrz::web_mercator_to_geo2(tile_bounds.min);
            auto tile_geo_max = hrz::web_mercator_to_geo2(tile_bounds.max);

            auto tile_lon_subdivisions = (size_t)std::ceil(
                (tile_geo_max.lon - tile_geo_min.lon) / max_segment_angular_length);
            auto lon_increment = (tile_geo_max.lon - tile_geo_min.lon) / tile_lon_subdivisions;

            auto geo_min = hrz::web_mercator_to_geo2(point_grid_bbox.min);
            auto geo_max = hrz::web_mercator_to_geo2(point_grid_bbox.max);

            auto lon_subdivisions =
                (size_t)std::ceil((geo_max.lon - geo_min.lon) / max_segment_angular_length);

            if (lon_subdivisions > 1)
            {
                double current_lat = geo_min.lat;
                while (true)
                {
                    for (size_t i = 0; i <= lon_subdivisions; ++i)
                    {
                        auto geo = hrz::GeoPosition2(current_lat, geo_min.lon + lon_increment * i);

                        auto wmerc = hrz::geo_to_web_mercator(geo);

                        vertices.push_back({wmerc.x, wmerc.y});
                    }

                    double lon_increment_length = lon_angle_to_distance(lon_increment, current_lat);
                    double lat_increment_angle = std::min(
                        distance_to_lat_angle(lon_increment_length), max_segment_angular_length);

                    if (current_lat >= geo_max.lat) break;

                    current_lat = std::min(current_lat + lat_increment_angle, geo_max.lat);
                }
            }
        }

        auto vertices_data_opt = vertices.data();
        if (!vertices_data_opt.has_value()) return;
        auto& vertices_data = vertices_data_opt.value();

        auto edges_data_opt = edges.data();
        if (!edges_data_opt.has_value()) return;
        auto& edges_data = edges_data_opt.value();

        auto generate_mesh = [&](auto& triangulation)
            requires requires { triangulation.get_next_triangle(); }
        {
            if (should_clip)
            {
                auto triangle = triangulation.get_next_triangle();
                while (triangle.has_value())
                {
                    uint32_t i0 = triangle->i0;
                    uint32_t i1 = triangle->i1;
                    uint32_t i2 = triangle->i2;

                    lm::dvec3 p0 = lm::dvec3(triangle->p0, 0.0);
                    lm::dvec3 p1 = lm::dvec3(triangle->p1, 0.0);
                    lm::dvec3 p2 = lm::dvec3(triangle->p2, 0.0);

                    hrz::clip_triangle<double>(
                        p0.xy, p1.xy, p2.xy, p0.z, p1.z, p2.z, tile_bounds,
                        [&](const lm::dvec2& a, const lm::dvec2& b, const lm::dvec2& c, double az,
                            double bz, double cz)
                        {
                            auto get_index = [&](const lm::dvec2& v,
                                                 double z) -> std::optional<uint32_t>
                            {
                                if (v == p0.xy && z == p0.z) return {i0};
                                if (v == p1.xy && z == p1.z) return {i1};
                                if (v == p2.xy && z == p2.z) return {i2};
                                return std::nullopt;
                            };

                            append_triangle(
                                get_index(a, az), lm::dvec3(a, az), get_index(b, bz),
                                lm::dvec3(b, bz), get_index(c, cz), lm::dvec3(c, cz));
                        });

                    triangle = triangulation.get_next_triangle();
                }
            }
            else
            {
                auto triangle = triangulation.get_next_triangle();
                while (triangle.has_value())
                {
                    const uint32_t i0 = triangle->i0;
                    const uint32_t i1 = triangle->i1;
                    const uint32_t i2 = triangle->i2;

                    const lm::dvec3 p0 = lm::dvec3(triangle->p0, 0.0);
                    const lm::dvec3 p1 = lm::dvec3(triangle->p1, 0.0);
                    const lm::dvec3 p2 = lm::dvec3(triangle->p2, 0.0);

                    append_triangle({i0}, p0, {i1}, p1, {i2}, p2);

                    triangle = triangulation.get_next_triangle();
                }
            }
        };

        auto db_triangulation = IDelaBella2<double, int32_t>::Create();

        auto triangulation_res = db_triangulation->Triangulate(
            vertices_data.size(),
            (const double*)((const char*)vertices_data.data() + offsetof(lm::dvec2, x)),
            (const double*)((const char*)vertices_data.data() + offsetof(lm::dvec2, y)),
            sizeof(lm::dvec2));

        if (triangulation_res <= 0)
        {
            db_triangulation->Destroy();
            return;
        }

        db_triangulation->ConstrainEdges(
            edges_data.size(),
            (const int32_t*)((const char*)edges_data.data() + offsetof(lm::uvec2, x)),
            (const int32_t*)((const char*)edges_data.data() + offsetof(lm::uvec2, y)),
            sizeof(lm::uvec2));

        auto interior_triangles = db_triangulation->FloodFill(false);

        if (interior_triangles > 0)
        {
            DelabellaTriangulationIterator triangulation(db_triangulation, vertices_data);
            generate_mesh(triangulation);
            db_triangulation->Destroy();
        }
        else
        {
            // Bad luck! Delabella couldn't determine which triangles are on the
            // interior of the polygon, probably because of self-intersections.
            // Let's try CDT instead. It is slower, but can handle these cases.

            db_triangulation->Destroy();

            auto duplicates = CDT::FindDuplicates<double>(
                vertices_data.begin(), vertices_data.end(), lm_dvec2_get_x, lm_dvec2_get_y);
            if (!duplicates.duplicates.empty())
            {
                // Before:
                //     o: non duplicate, D: duplicate
                //     ooooooooDoooooDooooDoooDoo
                //             8    14   19  23       Indices of the duplicates
                // After:
                //     oooooooooooooooooooooo....

                // Each slice between two duplicates is shifted left by the number
                // of duplicates before it.
                for (size_t i = 1; i < duplicates.duplicates.size(); ++i)
                {
                    const size_t previous_duplicate_index = duplicates.duplicates[i - 1];
                    const size_t duplicate_index = duplicates.duplicates[i];

                    for (size_t j = previous_duplicate_index + 1; j < duplicate_index; ++j)
                    {
                        vertices_data[j - i] = vertices_data[j];
                    }
                }

                // The last slice is shifted by the total number of duplicates.
                for (size_t j = duplicates.duplicates.back() + 1; j < vertices_data.size(); ++j)
                {
                    vertices_data[j - duplicates.duplicates.size()] = vertices_data[j];
                }

                // Finally the size of the span is adjusted.
                vertices_data =
                    vertices_data.subspan(0, vertices_data.size() - duplicates.duplicates.size());

                // Remap the edges
                for (auto& edge : edges_data)
                {
                    edge.x = duplicates.mapping[edge.x];
                    edge.y = duplicates.mapping[edge.y];
                }
            }

            CDT::Triangulation<double> cdt_triangulation(
                CDT::VertexInsertionOrder::Enum::Auto,
                CDT::IntersectingConstraintEdges::Enum::TryResolve, 1.0);
            cdt_triangulation.insertVertices(
                vertices_data.begin(), vertices_data.end(), lm_dvec2_get_x, lm_dvec2_get_y);
            cdt_triangulation.insertEdges(
                edges_data.begin(), edges_data.end(), lm_ivec2_get_x, lm_ivec2_get_y);
            cdt_triangulation.eraseOuterTrianglesAndHoles();

            CdtTriangulationIterator triangulation(&cdt_triangulation);
            generate_mesh(triangulation);
        }
    }
    else
    {
        // The LOD of the tile is high enough that it can be reasonably approximated to
        // a flat surface. A less pretty but much faster triangulation can be used,
        // using Earcut.

        hrz::InlinedVector<std::span<const lm::dvec3>, 32> rings;
        rings.reserve(linestring_sizes.size());

        uint32_t linestring_start = 0;
        for (const uint32_t linestring_size : linestring_sizes)
        {
            const std::span<const lm::dvec3> ring_span =
                feature_span.subspan(linestring_start, linestring_size);

            rings.push_back(ring_span);
            linestring_start += linestring_size;
        }

        auto indices = mapbox::earcut<uint32_t>(rings);
        assert(indices.size() % 3 == 0);

        if (should_clip)
        {
            for (size_t i = 0; i < indices.size(); i += 3)
            {
                const lm::dvec3 p0 = feature_span[indices[i + 0]];
                const lm::dvec3 p1 = feature_span[indices[i + 1]];
                const lm::dvec3 p2 = feature_span[indices[i + 2]];

                hrz::clip_triangle<double>(
                    p0.xy, p1.xy, p2.xy, p0.z, p1.z, p2.z, tile_bounds,
                    [&](const lm::dvec2& a, const lm::dvec2& b, const lm::dvec2& c, double az,
                        double bz, double cz)
                    {
                        auto get_index = [&](const lm::dvec2& v,
                                             double z) -> std::optional<uint32_t>
                        {
                            if (v == p0.xy && z == p0.z) return {indices[i + 0]};
                            if (v == p1.xy && z == p1.z) return {indices[i + 1]};
                            if (v == p2.xy && z == p2.z) return {indices[i + 2]};
                            return std::nullopt;
                        };

                        auto position0 = lm::dvec3(a, az);
                        auto position1 = lm::dvec3(b, bz);
                        auto position2 = lm::dvec3(c, cz);
                        append_triangle(
                            get_index(a, az), position0, get_index(b, bz), position1,
                            get_index(c, cz), position2);
                    });
            }
        }
        else
        {
            for (size_t i = 0; i < indices.size(); i += 3)
            {
                const auto index0 = indices[i + 0];
                const auto index1 = indices[i + 1];
                const auto index2 = indices[i + 2];

                append_triangle(
                    index0, feature_span[index0], index1, feature_span[index1], index2,
                    feature_span[index2]);
            }
        }
    }
}

} // anonymous namespace

hrz_jobs::JobResult run(
    const hrz_jobs::FlatPolygonData& input,
    hrz_jobs::FlatPolygonGeometry& geometry,
    const JobContext& context)
{
    HRZ_SCOPED_SAMPLE("bake flat geometry");

    auto input_features = input.geometry.features.get_data();
    auto input_points = input.geometry.points.get_data();
    auto input_linestring_sizes = input.geometry.linestring_sizes.get_data();
    auto input_feature_ids = input.feature_ids.get_data();

    auto style_prps = input.style.prps.get_data();
    auto style_values = input.style.get_values_reader();

    auto points_geo_blob_opt = hrz::blobs::allocate_blob_sync(
        context.get_blob_allocator(), input_points.size() * sizeof(hrz::GeoPosition3));
    if (!points_geo_blob_opt.has_value())
    {
        return hrz_jobs::JobResult::FAILURE;
    }

    // Baked positions for later reprojection
    hrz::BlobVector<lm::dvec3> polygon_positions(
        context.get_blob_allocator(), InitialVertexCapacity);

    const auto& style = input.style;

    const lm::dbbox2 tile_bounds = hrz::mercator_tile_bbox_meters(input.coords);
    const lm::dvec2 tile_size = tile_bounds.max - tile_bounds.min;

    hrz::BlobVector<hrz_jobs::FlatPolygonGeometry::SolidColorPolygonVertex>
        solid_color_polygon_vertices(
            context.get_blob_allocator(), input.has_polygon_pattern ? 0 : InitialVertexCapacity);
    hrz::BlobVector<hrz_jobs::FlatPolygonGeometry::PatternPolygonVertex> pattern_polygon_vertices(
        context.get_blob_allocator(), input.has_polygon_pattern ? InitialVertexCapacity : 0);
    hrz::BlobVector<uint32_t> pattern_polygon_indices(
        context.get_blob_allocator(), InitialVertexCapacity);

    hrz::flat_hash_map<hrz_jobs::FlatPolygonGeometry::PolygonPatternStyle, uint32_t>
        polygon_pattern_styles;

    uint32_t max_feature_index = 0;

    // Create triangulation
    for (const auto& instance : style.instances.get_data())
    {
        if (instance.repr_id != input.repr_id) continue;

        const auto& feature = input_features.at(instance.feature_index);
        if (feature.type != hrz_proto::VectorGeometryType::POLYGON_GEOMETRY) continue;

        auto feature_index = std::min(instance.feature_index, hrz::vt::MAX_FEATURE_INDEX);
        max_feature_index = std::max(max_feature_index, feature_index);

        const uint64_t prp_begin = instance.first_prp;
        const uint64_t prp_end = prp_begin + instance.prp_count;

        lm::ubvec4 fill_color_srgb = input.default_color_srgb;

        size_t polygon_pattern_sprite_index = input.default_polygon_pattern_sprite_index;
        std::string_view polygon_pattern_sprite_name = input.default_polygon_pattern_sprite_name;
        lm::vec2 polygon_pattern_size = input.default_polygon_pattern_size;
        float polygon_pattern_rotation = input.default_polygon_pattern_rotation;
        lm::ubvec4 polygon_pattern_color_srgb = input.default_polygon_pattern_color_srgb;
        float polygon_pattern_blend_strength = input.default_polygon_pattern_color_blend_strength;

        for (uint64_t j = prp_begin; j < prp_end; ++j)
        {
            if (style_prps[j] == input.color_prp)
            {
                fill_color_srgb = style_values.as_color(j);
            }
            else if (style_prps[j] == input.polygon_pattern_sprite_index_prp)
            {
                polygon_pattern_sprite_index = (size_t)style_values.as_uint64(j);
            }
            else if (style_prps[j] == input.polygon_pattern_sprite_name_prp)
            {
                polygon_pattern_sprite_name = style_values.as_string(j);
            }
            else if (style_prps[j] == input.polygon_pattern_size_prp.x)
            {
                polygon_pattern_size.x = (float)style_values.as_number(j);
            }
            else if (style_prps[j] == input.polygon_pattern_size_prp.y)
            {
                polygon_pattern_size.y = (float)style_values.as_number(j);
            }
            else if (style_prps[j] == input.polygon_pattern_rotation_prp)
            {
                polygon_pattern_rotation = (float)style_values.as_number(j);
            }
            else if (style_prps[j] == input.polygon_pattern_color_prp)
            {
                polygon_pattern_color_srgb = style_values.as_color(j);
            }
            else if (style_prps[j] == input.polygon_pattern_color_blend_strength_prp)
            {
                polygon_pattern_blend_strength =
                    hrz::clamp((float)style_values.as_number(j), 0.0F, 1.0F);
            }
        }

        auto feature_points =
            input_points.as_span().subspan(feature.first_point, feature.point_count);

        auto feature_linestring_sizes = input_linestring_sizes.as_span().subspan(
            feature.first_linestring_size, feature.linestring_count);

        if (input.has_polygon_pattern)
        {
            auto sprite_name_it =
                input.pattern_sprite_name_to_index.find(polygon_pattern_sprite_name);
            if (sprite_name_it != input.pattern_sprite_name_to_index.end())
            {
                polygon_pattern_sprite_index = sprite_name_it->second;
            }
        }

        if (input.has_polygon_pattern
            && polygon_pattern_sprite_index < input.pattern_sprites.size())
        {
            // The pattern style configuration can be different for every polygon. It
            // includes the sprite index, size, rotation, colour, etc. Including all
            // this data in every vertex structure would create a lot of duplication, so
            // instead vertices only refer to their style through an index, and the
            // styles themselves are stored in a texture.
            // In practice most of the time there won't be a different style for each
            // polygon, but only a few styles will be used. To decrease the size of the
            // style texture, styles are first stored in a map and deduplicated.

            auto& pattern_sprite = input.pattern_sprites[polygon_pattern_sprite_index];

            auto pattern_size = polygon_pattern_size;
            if (input.polygon_pattern_size_unit
                    == hrz_proto::POLYGON_PATTERN_SIZE_RELATIVE_TO_SPRITE_IN_METERS
                || input.polygon_pattern_size_unit
                    == hrz_proto::POLYGON_PATTERN_SIZE_RELATIVE_TO_SPRITE_IN_PIXELS)
            {
                pattern_size *= lm::vec2(pattern_sprite.atlas_size);
            }

            // Combine pattern size and rotation in a linear transformation matrix.
            // In-tile UVs range from 0 to 1 so coordinates must be scaled by the
            // size of the tile.
            auto transform = lm::inverse(
                lm::rotation(lm::dvec3(0.0, 0.0, 1.0), (double)polygon_pattern_rotation)
                * lm::scaling(lm::dvec3(pattern_size, 1.0))
                * lm::scaling(lm::dvec3(1.0 / tile_size.x, 1.0 / tile_size.y, 1.0)));

            hrz_jobs::FlatPolygonGeometry::PolygonPatternStyle pattern_style;
            pattern_style.sprite_size =
                lm::vec2(pattern_sprite.atlas_size) / input.pattern_texture_size;
            pattern_style.sprite_offset =
                lm::vec2(pattern_sprite.atlas_offset) / input.pattern_texture_size;
            pattern_style.polygon_pattern_transform = lm::mat2(
                lm::vec2(lm::dvec2(transform.x.x, transform.x.y)),
                lm::vec2(lm::dvec2(transform.y.x, transform.y.y)));
            pattern_style.background_color_srgb = fill_color_srgb;
            pattern_style.pattern_color_srgb = polygon_pattern_color_srgb;
            pattern_style.pattern_color_blend_strength = polygon_pattern_blend_strength;

            uint32_t pattern_style_index = 0;
            auto style_it = polygon_pattern_styles.find(pattern_style);
            if (style_it != polygon_pattern_styles.end())
            {
                pattern_style_index = style_it->second;
            }
            else
            {
                pattern_style_index = (uint32_t)polygon_pattern_styles.size();
                polygon_pattern_styles.insert({pattern_style, pattern_style_index});
            }

            generate_polygon_geometry<hrz_jobs::FlatPolygonGeometry::PatternPolygonVertex>(
                feature_index, feature_points, feature_linestring_sizes, polygon_positions,
                pattern_polygon_vertices, pattern_polygon_indices, fill_color_srgb,
                pattern_style_index, input.coords, tile_bounds, input.clip_to_tile,
                context.get_blob_allocator());
        }
        else
        {
            generate_polygon_geometry<hrz_jobs::FlatPolygonGeometry::SolidColorPolygonVertex>(
                feature_index, feature_points, feature_linestring_sizes, polygon_positions,
                solid_color_polygon_vertices, pattern_polygon_indices, fill_color_srgb, 0,
                input.coords, tile_bounds, input.clip_to_tile, context.get_blob_allocator());
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

    auto polygon_positions_data_opt = polygon_positions.data();
    auto feature_ids_data_opt = feature_ids.to_blob_array();
    auto pattern_polygon_vertices_data_opt = pattern_polygon_vertices.data();
    if (!polygon_positions_data_opt.has_value() || !feature_ids_data_opt.has_value()
        || !pattern_polygon_vertices_data_opt.has_value())
    {
        return hrz_jobs::JobResult::FAILURE;
    }
    auto polygon_positions_data = polygon_positions_data_opt.value();
    auto pattern_polygon_vertices_data = pattern_polygon_vertices_data_opt.value();

    lm::dbbox2 wmerc_bounds = lm::dbbox2::invalid();
    if (polygon_positions.size().value_or(0) > 0)
    {
        for (const lm::dvec3& p : polygon_positions_data)
        {
            wmerc_bounds = lm::expand(wmerc_bounds, p.xy);
        }
    }

    if (input.has_polygon_pattern)
    {
        auto tile_origin = tile_bounds.min;

        auto origin_geo = hrz::web_mercator_to_geo2(tile_origin);
        geometry.origin_lat = origin_geo.lat;

        auto nw_geo = hrz::web_mercator_to_geo2({tile_bounds.min.x, tile_bounds.max.y});
        geometry.lat_span = nw_geo.lat - origin_geo.lat;

        geometry.origin_uv = tile_origin / tile_size;

        assert(pattern_polygon_vertices_data.size() == polygon_positions_data.size());
        for (size_t i = 0; i < pattern_polygon_vertices_data.size(); ++i)
        {
            // For every vertex, write UV (in [0,1], used to place patterns) and relative
            // latitude (in [0,1] too, used to scale patterns).
            // uv.y and in_tile_lat are different because the former is in Web Mercator
            // and the latter in WGS84 latitude.
            // Using Web Mercator allows drawing a consistent grid of pattern instances,
            // thanks to the conformal projection. (The instances are smaller near the
            // poles though.)

            const auto& position = polygon_positions_data[i].xy;

            pattern_polygon_vertices_data[i].uv = lm::vec2((position - tile_origin) / tile_size);

            auto lat = hrz::web_mercator_to_geo2(position).lat;
            pattern_polygon_vertices_data[i].in_tile_lat =
                (lat - geometry.origin_lat) / geometry.lat_span;
        }
    }
    else
    {
        geometry.origin_uv = {0.0, 0.0};
        geometry.origin_lat = 0.0;
        geometry.lat_span = 0.0;
    }

    // Convert to Geocentric
    pl_transform_in_place_canonical(
        &hrz_proj::wmerc_to_ecef, polygon_positions_data.size(), &polygon_positions_data.data()->x);

    const hrz::BSphere<double> bsphere =
        hrz::compute_bounding_sphere(std::span<const lm::dvec3>(polygon_positions_data));

    auto solid_color_polygon_vertices_data_opt = solid_color_polygon_vertices.data();
    if (!solid_color_polygon_vertices_data_opt.has_value())
    {
        return hrz_jobs::JobResult::FAILURE;
    }
    auto solid_color_polygon_vertices_data = solid_color_polygon_vertices_data_opt.value();

    // Compute relative coordinates
    if (input.has_polygon_pattern)
    {
        hrz::vector_repr::compute_rel_coords(
            {polygon_positions_data}, bsphere.center,
            {(lm::vec3*)&pattern_polygon_vertices_data.data()->position,
             polygon_positions_data.size(),
             sizeof(hrz_jobs::FlatPolygonGeometry::PatternPolygonVertex)});
    }
    else
    {
        hrz::vector_repr::compute_rel_coords(
            {polygon_positions_data}, bsphere.center,
            {(lm::vec3*)&solid_color_polygon_vertices_data.data()->position,
             polygon_positions_data.size(),
             sizeof(hrz_jobs::FlatPolygonGeometry::SolidColorPolygonVertex)});
    }

    auto solid_color_polygon_vertices_array_opt = solid_color_polygon_vertices.to_blob_array();
    auto pattern_polygon_vertices_array_opt = pattern_polygon_vertices.to_blob_array();
    auto pattern_polygon_indices_array_opt = pattern_polygon_indices.to_blob_array();
    if (!solid_color_polygon_vertices_array_opt.has_value()
        || !pattern_polygon_vertices_array_opt.has_value()
        || !pattern_polygon_indices_array_opt.has_value())
    {
        return hrz_jobs::JobResult::FAILURE;
    }

    // Finalize
    if (input.has_polygon_pattern)
    {
        pattern_polygon_vertices_array_opt.value().register_blob_metadata(
            context.get_blob_allocator(), "contents"_ss, "flat polygon vertices"_ss);
        pattern_polygon_vertices_array_opt.value().register_blob_owner(
            context.get_blob_allocator(), context.get_resource_owner());
        geometry.polygon_data = {std::move(pattern_polygon_vertices_array_opt.value())};

        hrz::BlobVector<hrz_jobs::FlatPolygonGeometry::PolygonPatternStyle>
            polygon_pattern_styles_vector(
                context.get_blob_allocator(), polygon_pattern_styles.size());
        polygon_pattern_styles_vector.resize(polygon_pattern_styles.size());
        auto polygon_pattern_styles_span = polygon_pattern_styles_vector.data();
        if (!polygon_pattern_styles_span.has_value())
        {
            return hrz_jobs::JobResult::FAILURE;
        }

        for (const auto& pattern_style : polygon_pattern_styles)
        {
            polygon_pattern_styles_span.value()[pattern_style.second] = pattern_style.first;
        }

        if (polygon_pattern_styles.size() > hrz::vt::DATA_TEXTURE_SIZE
            && polygon_pattern_styles.size() % hrz::vt::DATA_TEXTURE_SIZE != 0)
        {
            polygon_pattern_styles_vector.resize(
                polygon_pattern_styles.size() + hrz::vt::DATA_TEXTURE_SIZE
                - (polygon_pattern_styles.size() % hrz::vt::DATA_TEXTURE_SIZE));
        }

        auto polygon_pattern_styles_array_opt = polygon_pattern_styles_vector.to_blob_array();
        if (!polygon_pattern_styles_array_opt.has_value())
        {
            return hrz_jobs::JobResult::FAILURE;
        }

        polygon_pattern_styles_array_opt.value().register_blob_metadata(
            context.get_blob_allocator(), "contents"_ss, "flat polygon pattern styles"_ss);
        polygon_pattern_styles_array_opt.value().register_blob_owner(
            context.get_blob_allocator(), context.get_resource_owner());

        geometry.polygon_pattern_style_data = std::move(polygon_pattern_styles_array_opt.value());
    }
    else
    {
        solid_color_polygon_vertices_array_opt.value().register_blob_metadata(
            context.get_blob_allocator(), "contents"_ss, "flat polygon vertices"_ss);
        solid_color_polygon_vertices_array_opt.value().register_blob_owner(
            context.get_blob_allocator(), context.get_resource_owner());
        geometry.polygon_data = {std::move(solid_color_polygon_vertices_array_opt.value())};
    }

    pattern_polygon_indices_array_opt.value().register_blob_metadata(
        context.get_blob_allocator(), "contents"_ss, "flat polygon indices"_ss);
    pattern_polygon_indices_array_opt.value().register_blob_owner(
        context.get_blob_allocator(), context.get_resource_owner());
    geometry.polygon_indices = std::move(pattern_polygon_indices_array_opt.value());

    geometry.feature_ids = std::move(feature_ids_data_opt.value());
    geometry.max_feature_index = max_feature_index;
    geometry.wmerc_bounds = wmerc_bounds;
    geometry.sea_bsphere_center = bsphere.center;
    geometry.sea_bsphere_radius = bsphere.radius;

    geometry.feature_ids.register_blob_metadata(
        context.get_blob_allocator(), "contents"_ss, "flat vector feature IDs"_ss);

    geometry.feature_ids.register_blob_owner(
        context.get_blob_allocator(), context.get_resource_owner());

    return hrz_jobs::JobResult::SUCCESS;
}

} // namespace hrz_jobs::bake_flat_polygon_geometry
