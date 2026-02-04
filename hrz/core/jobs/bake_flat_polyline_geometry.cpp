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
#include "hrz/fnd/maths.h"

namespace hrz_jobs::bake_flat_polyline_geometry
{
namespace
{
constexpr size_t InitialVertexCapacity = 4096;

void transform_wmerc_to_geo(int n, const lm::dvec3* in, hrz::GeoPosition3* out)
{
    pl_transform(
        &hrz_proj::wmerc_to_lonlat_rad, n, &in->x, &in->y, &in->z, sizeof(lm::dvec3), &out->lon,
        &out->lat, &out->alt, sizeof(hrz::GeoPosition3));
}

// Put the geographic position on the same side of the prime meridian as the
// Web Mercator position.
[[nodiscard]]
hrz::GeoPosition2 reconcile_wmerc_and_geo(const lm::dvec2& wmerc, hrz::GeoPosition2 geo)
{
    if (wmerc.x > 0 && geo.lon < 0)
    {
        geo.lon += lm::PI * 2;
    }
    else if (wmerc.x < 0 && geo.lon > 0)
    {
        geo.lon -= lm::PI * 2;
    }
    return geo;
}

struct SegmentSubdivisionContext
{
    uint32_t feature_index;
    hrz::BlobVector<lm::dvec3>& positions;
    hrz::BlobVector<hrz_jobs::FlatPolylineGeometry::PolylineInstance>& polyline_data;
    lm::ubvec4 rgba;
    float line_width;
    float dash_length;
    float dash_period;
    float animation_speed;
    lm::ubvec4 background_rgba;
    double max_segment_angular_length;
};

struct SegmentSubdivisionResult
{
    double segment_length; // in metres, along rhumb lines
    uint32_t segment_count;
};

SegmentSubdivisionResult append_segment(
    const lm::dvec3& position0,
    const lm::dvec3& position1,
    hrz::GeoPosition2 geo0,
    hrz::GeoPosition2 geo1,
    double progress0,
    SegmentSubdivisionContext& ctx)
{
    auto append_vertex = [&](const lm::dvec3& position0, const lm::dvec3& position1,
                             double progress0, double progress1)
    {
        ctx.positions.push_back(position0);
        ctx.positions.push_back(position1);

        // Positions, normals and total length will be written later.
        ctx.polyline_data.push_back(
            {{},
             {},
             0,
             0,
             ctx.rgba,
             ctx.line_width,
             0,
             (float)progress0,
             (float)progress1,
             ctx.dash_period,
             ctx.dash_length,
             ctx.animation_speed,
             ctx.background_rgba,
             ctx.feature_index});
    };

    SegmentSubdivisionResult res{};

    // Polyline segments should follow rhumb lines. But because flat overlays are
    // not drawn in Web Mercator (they are drawn in a 3D space), long segments do
    // not follow the intended path.
    // To counter this, long segments are sub-divided into sub-segments. The more a
    // segment is curved, the more it must be sub-divided. (Think of a long line
    // going in a North-South direction, compared to a short line going in an West-
    // East direction, but close to the pole. Both have large curvatures. As opposed
    // to another W-E line of the same length, but close to the Equator.)
    // The amount of curvature is nicely given by the distance in lat-lon (while
    // considering this space as a cartesian space).
    // A maximum angular distance is defined, if the segment exceeds it, it gets
    // split into two. (Splitting the segment into n sub-segments in Web Mercator
    // space wouldn't be correct, as the density of the sub-segments wouldn't fol-
    // low the planet's curvature. Because curvature measurement is done in lat-lon
    // but sub-dividing is done in Web Mercator, the process has to be recursive.)
    // This subdivision makes segments follow rhumb lines (i.e., lines of constant
    // bearing).
    // Progress cannot be computed using the Web Mercator coordinates, as their
    // distance metric does not map to distances on the globe, and we want it to
    // map to real world distances. The relationship between distances in the two
    // coordinate systems is not even linear, the progress cannot be interpolated.
    // It is computed piecemeal from lat-lon positions instead.

    // Sometimes for positions along the antimeridian, the Web Mercator position can
    // be on one side of the prime meridian, and the geographic position on the other
    // side. This can lead to infinite loops when subdividing the segments, so it has
    // to be fixed.
    geo0 = reconcile_wmerc_and_geo(position0.xy, geo0);
    geo1 = reconcile_wmerc_and_geo(position1.xy, geo1);

    const double angular_length =
        lm::length(lm::dvec2(geo1.lon, geo1.lat) - lm::dvec2(geo0.lon, geo0.lat));

    if (angular_length > ctx.max_segment_angular_length)
    {
        const lm::dvec3 midpoint_position = lm::mix(position0, position1, 0.5);
        const hrz::GeoPosition2 midpoint_geo = hrz::web_mercator_to_geo2(midpoint_position.xy);

        auto r0 = append_segment(position0, midpoint_position, geo0, midpoint_geo, progress0, ctx);
        res.segment_length += r0.segment_length;
        res.segment_count += r0.segment_count;

        auto r1 = append_segment(
            midpoint_position, position1, midpoint_geo, geo1, progress0 + r0.segment_length, ctx);
        res.segment_length += r1.segment_length;
        res.segment_count += r1.segment_count;
    }
    else
    {
        const double distance = hrz::rhumb_line_distance(geo0, geo1, false);

        append_vertex(
            lm::dvec3{position0.xy, 0}, lm::dvec3{position1.xy, 0}, progress0,
            progress0 + distance);

        res.segment_length += distance;
        res.segment_count += 1;
    }

    return res;
}

void generate_polylines_geometry(
    uint32_t feature_index,
    std::span<const lm::dvec3> feature_span,
    std::span<const hrz::GeoPosition3> feature_geo_span,
    std::span<const uint32_t> linestring_sizes,
    hrz::BlobVector<lm::dvec3>& positions,
    hrz::BlobVector<uint32_t>& polylines_segment_counts,
    hrz::BlobVector<hrz_jobs::FlatPolylineGeometry::PolylineInstance>& polyline_data,
    lm::ubvec4 rgba,
    float line_width,
    float dash_length,
    float dash_period,
    float animation_speed,
    lm::ubvec4 background_rgba,
    const hrz::TileCoords& tile_coords,
    const lm::dbbox2& tile_bounds,
    bool should_clip,
    bool should_loop)
{
    if (!polyline_data.is_valid()) return;

    uint32_t segment_count = 0;

    const size_t first_instance_index = polyline_data.size().value();

    size_t p0 = 0;
    size_t p1 = 1;
    double progress = 0;

    size_t linestring_first_point = 0;
    size_t linestring_end = 0;

    SegmentSubdivisionContext ctx{
        feature_index,   positions,
        polyline_data,   rgba,
        line_width,      dash_length,
        dash_period,     animation_speed,
        background_rgba, hrz::vector_repr::max_segment_angular_length_for_lod(tile_coords.lod)};

    for (const auto& linestring_size : linestring_sizes)
    {
        if (linestring_size <= 1)
        {
            linestring_first_point += linestring_size;
            continue;
        }

        linestring_end = linestring_first_point + linestring_size;

        // Create a loop for polygon outlines
        p0 = (should_loop) ? linestring_end - 1 : linestring_first_point;
        p1 = (should_loop) ? linestring_first_point : linestring_first_point + 1;

        if (should_clip)
        {
            while (p1 < linestring_end)
            {
                const lm::dvec3 pp0 = feature_span[p0];
                const lm::dvec3 pp1 = feature_span[p1];

                const hrz::GeoPosition3 geo0 = feature_geo_span[p0];
                const hrz::GeoPosition3 geo1 = feature_geo_span[p1];

                const double unclamped_distance =
                    hrz::rhumb_line_distance(geo0.latlon(), geo1.latlon(), false);

                hrz::clip_segment<double>(
                    pp0.xy, pp1.xy, tile_bounds,
                    [&](const lm::dvec2& a, const lm::dvec2& b, double ta, double tb)
                    {
                        lm::dvec3 clipped[2] = {
                            lm::dvec3(a, hrz::lerp(pp0.z, pp1.z, ta)),
                            lm::dvec3(b, hrz::lerp(pp0.z, pp1.z, tb))};

                        hrz::GeoPosition3 clipped_geo[2];
                        transform_wmerc_to_geo(2, clipped, clipped_geo);

                        // Skip the distance between the actual start point and the clipped
                        // start point, so that the progress of polylines split between tiles
                        // visually matches.
                        const double clipped_progress = progress
                            + hrz::rhumb_line_distance(geo0.latlon(), clipped_geo[0].latlon(),
                                                       false);

                        auto res = append_segment(
                            clipped[0], clipped[1], clipped_geo[0].latlon(),
                            clipped_geo[1].latlon(), clipped_progress, ctx);
                        segment_count += res.segment_count;
                    });

                p0 = p1;
                p1 += 1;
                progress += unclamped_distance;
            }
        }
        else
        {
            while (p1 < linestring_end)
            {
                const lm::dvec3 pp0 = feature_span[p0];
                const lm::dvec3 pp1 = feature_span[p1];
                const hrz::GeoPosition3 geo0 = feature_geo_span[p0];
                const hrz::GeoPosition3 geo1 = feature_geo_span[p1];

                auto res = append_segment(pp0, pp1, geo0.latlon(), geo1.latlon(), progress, ctx);

                p0 = p1;
                p1 += 1;
                progress += res.segment_length;
                segment_count += res.segment_count;
            }
        }

        linestring_first_point += linestring_size;
    }

    if (!polyline_data.is_valid()) return;

    // Write the total length of the polyline to the created instances
    auto polyline_instances_data_opt = polyline_data.data();
    if (!polyline_instances_data_opt.has_value()) return;
    const auto& polyline_instances_data = polyline_instances_data_opt.value();

    for (size_t index = first_instance_index; index < polyline_instances_data.size(); index++)
    {
        auto& instance = polyline_instances_data[index];
        assert(instance.feature_index == feature_index);

        instance.line_total_length = (float)progress;
    }

    polylines_segment_counts.push_back(segment_count);
}

} // anonymous namespace

hrz_jobs::JobResult run(
    const hrz_jobs::FlatPolylineData& input,
    hrz_jobs::FlatPolylineGeometry& geometry,
    const JobContext& context)
{
    HRZ_SCOPED_SAMPLE("bake flat polylines geometry");

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

    // Input points as lat/lon positions for computing exact geodesic distances
    auto points_geo_array = hrz::BlobArray<hrz::GeoPosition3>::make_blob_array(
        hrz::unsafe("The blob is a root blob"), context.get_blob_allocator(),
        points_geo_blob_opt.value());
    auto points_geo_data = points_geo_array.get_mutable_data();

    transform_wmerc_to_geo(input_points.size(), input_points.data(), points_geo_data.data());
    const std::span<const hrz::GeoPosition3> points_geo = points_geo_data.as_span();

    // Baked positions for later reprojection
    hrz::BlobVector<lm::dvec3> polyline_positions(
        context.get_blob_allocator(), InitialVertexCapacity);
    hrz::BlobVector<uint32_t> polyline_segment_counts(
        context.get_blob_allocator(), InitialVertexCapacity);

    const auto& style = input.style;

    const lm::dbbox2 tile_bounds = hrz::mercator_tile_bbox_meters(input.coords);

    hrz::BlobVector<hrz_jobs::FlatPolylineGeometry::PolylineInstance> polyline_vertices(
        context.get_blob_allocator(), InitialVertexCapacity);

    uint32_t max_feature_index = 0;

    // Create triangulation
    for (const auto& instance : style.instances.get_data())
    {
        if (instance.repr_id != input.repr_id) continue;

        const auto& feature = input_features.at(instance.feature_index);
        if (feature.type != hrz_proto::VectorGeometryType::POLYGON_GEOMETRY
            && feature.type != hrz_proto::VectorGeometryType::POLYLINE_GEOMETRY)
        {
            continue;
        }

        auto feature_index = std::min(instance.feature_index, hrz::vt::MAX_FEATURE_INDEX);
        max_feature_index = std::max(max_feature_index, feature_index);

        const uint64_t prp_begin = instance.first_prp;
        const uint64_t prp_end = prp_begin + instance.prp_count;

        lm::ubvec4 fill_color_srgb = input.default_color_srgb;
        lm::ubvec4 secondary_color_srgb = input.dashes.default_secondary_color_srgb;

        float line_width = input.default_line_width;
        float dash_primary_length = input.dashes.default_primary_length;
        float dash_period = input.dashes.default_period;
        float animation_speed = input.dashes.default_animation_speed;

        for (uint64_t j = prp_begin; j < prp_end; ++j)
        {
            if (style_prps[j] == input.line_width_prp)
            {
                line_width = (float)style_values.as_number(j);
            }
            else if (style_prps[j] == input.color_prp)
            {
                fill_color_srgb = style_values.as_color(j);
            }
            else if (style_prps[j] == input.dashes.primary_length_prp)
            {
                dash_primary_length = (float)style_values.as_number(j);
            }
            else if (style_prps[j] == input.dashes.period_prp)
            {
                dash_period = (float)style_values.as_number(j);
            }
            else if (style_prps[j] == input.dashes.animation_speed_prp)
            {
                animation_speed = (float)style_values.as_number(j);
            }
            else if (style_prps[j] == input.dashes.secondary_color_prp)
            {
                secondary_color_srgb = style_values.as_color(j);
            }
        }

        // This stores Oklab colours in 8-bit-per-channel vectors. It's not
        // great, and some precision is lost. (Usually only sRGB colours should
        // be reduced to 8 bits per channel.) However the colour interpolation
        // between these two colours in done with floats in the shader, so
        // the precision loss should be acceptable.
        const lm::ubvec4 fill_color_oklab = hrz::convert_rgba_color_to_bytes(
            hrz::srgb_to_oklab(hrz::convert_byte_color_to_rgba(fill_color_srgb)));
        const lm::ubvec4 secondary_color_oklab = hrz::convert_rgba_color_to_bytes(
            hrz::srgb_to_oklab(hrz::convert_byte_color_to_rgba(secondary_color_srgb)));

        if (animation_speed != 0)
        {
            geometry.is_animated = true;
        }

        auto feature_points =
            input_points.as_span().subspan(feature.first_point, feature.point_count);
        auto feature_points_geo = std::span<const hrz::GeoPosition3>(points_geo)
                                      .subspan(feature.first_point, feature.point_count);

        if (feature.type == hrz_proto::VectorGeometryType::POLYGON_GEOMETRY)
        {
            auto feature_linestring_sizes = input_linestring_sizes.as_span().subspan(
                feature.first_linestring_size, feature.linestring_count);

            generate_polylines_geometry(
                feature_index, feature_points, feature_points_geo, feature_linestring_sizes,
                polyline_positions, polyline_segment_counts, polyline_vertices, fill_color_oklab,
                line_width, dash_primary_length, dash_period, animation_speed,
                secondary_color_oklab, input.coords, tile_bounds, input.clip_to_tile, true);
        }
        else if (feature.type == hrz_proto::VectorGeometryType::POLYLINE_GEOMETRY)
        {
            auto feature_linestring_sizes = input_linestring_sizes.as_span().subspan(
                feature.first_linestring_size, feature.linestring_count);

            if (feature_points.size() >= 2)
            {
                generate_polylines_geometry(
                    feature_index, feature_points, feature_points_geo, feature_linestring_sizes,
                    polyline_positions, polyline_segment_counts, polyline_vertices,
                    fill_color_oklab, line_width, dash_primary_length, dash_period, animation_speed,
                    secondary_color_oklab, input.coords, tile_bounds, input.clip_to_tile, false);
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

    auto polyline_positions_data_opt = polyline_positions.data();
    auto polyline_segment_counts_data_opt = polyline_segment_counts.data();
    auto feature_ids_data_opt = feature_ids.to_blob_array();
    if (!polyline_positions_data_opt.has_value() || !polyline_segment_counts_data_opt.has_value()
        || !feature_ids_data_opt.has_value())
    {
        return hrz_jobs::JobResult::FAILURE;
    }
    auto polyline_positions_data = polyline_positions_data_opt.value();
    auto polyline_segment_counts_data = polyline_segment_counts_data_opt.value();

    lm::dbbox2 wmerc_bounds = lm::dbbox2::invalid();
    if (polyline_positions.size().value_or(0) > 0)
    {
        for (const lm::dvec3& p : polyline_positions_data)
        {
            wmerc_bounds = lm::expand(wmerc_bounds, p.xy);
        }
    }

    // Convert to Geocentric
    pl_transform_in_place_canonical(
        &hrz_proj::wmerc_to_ecef, polyline_positions_data.size(),
        &polyline_positions_data.data()->x);

    const hrz::BSphere<double> bsphere =
        hrz::compute_bounding_sphere(std::span<const lm::dvec3>(polyline_positions_data));

    auto polyline_vertices_data_opt = polyline_vertices.data();
    if (!polyline_vertices_data_opt.has_value())
    {
        return hrz_jobs::JobResult::FAILURE;
    }
    auto polyline_vertices_data = polyline_vertices_data_opt.value();

    // Generate the joint normals used for flat polylines without round tips.
    if (!polyline_positions_data.empty())
    {
        assert(polyline_positions_data.size() % 2 == 0);

        size_t current_point = 0;
        size_t previous_segment_count = 0;
        for (const uint32_t segment_count : polyline_segment_counts_data)
        {
            if (segment_count == 0) continue;

            const std::span<const lm::dvec3> positions(
                (const lm::dvec3*)polyline_positions_data.data() + previous_segment_count * 2,
                segment_count * 2);

            lm::dvec3 first_prev = positions[(segment_count - 1) * 2 + 0];
            lm::dvec3 second_prev = positions[(segment_count - 1) * 2 + 1];

            for (size_t i = 0; i < segment_count; ++i)
            {
                const lm::dvec3 first = positions[i * 2 + 0];
                const lm::dvec3 second = positions[i * 2 + 1];

                // 0-length normals are used to indicate that a joint can't be nicely joined
                // or that this is the beginning or start of a polyline.
                lm::vec3 n0{};
                lm::vec3 n1{};

                if (lm::length2(second_prev - first) < 0.1) // Close enough to be continuous
                {
                    if (auto [normal, is_nice_joint] =
                            hrz::vector_repr::compute_joint_normal(second, first, first_prev);
                        is_nice_joint)
                    {
                        n0 = normal;
                    }
                }

                lm::dvec3 first_next;
                lm::dvec3 second_next;
                if (i < segment_count - 1) // Not the last segment, we can continue
                {
                    first_next = positions[(i + 1) * 2 + 0];
                    second_next = positions[(i + 1) * 2 + 1];
                }
                else // Loop around!
                {
                    first_next = positions[0];
                    second_next = positions[1];
                }

                if (lm::length2(second - first_next) < 0.1) // Close enough to be continuous
                {
                    if (auto [normal, is_nice_joint] =
                            hrz::vector_repr::compute_joint_normal(first, second, second_next);
                        is_nice_joint)
                    {
                        n1 = normal;
                    }
                }

                const uint32_t n0_oct = hrz::octahedral_compress_normal(n0);
                const uint32_t n1_oct = hrz::octahedral_compress_normal(n1);

                polyline_vertices_data[current_point].normal0 = n0_oct;
                polyline_vertices_data[current_point].normal1 = n1_oct;

                first_prev = first;
                second_prev = second;

                current_point += 1;
            }

            previous_segment_count += (size_t)segment_count;
        }
    }

    // Compute relative coordinates
    hrz::vector_repr::compute_rel_coords(
        {polyline_positions_data.data() + 0, polyline_positions_data.size() / 2,
         2 * sizeof(lm::dvec3)},
        bsphere.center,
        {&polyline_vertices_data.data()->position0, polyline_positions_data.size() / 2,
         sizeof(hrz_jobs::FlatPolylineGeometry::PolylineInstance)});
    hrz::vector_repr::compute_rel_coords(
        {polyline_positions_data.data() + 1, polyline_positions_data.size() / 2,
         2 * sizeof(lm::dvec3)},
        bsphere.center,
        {&polyline_vertices_data.data()->position1, polyline_positions_data.size() / 2,
         sizeof(hrz_jobs::FlatPolylineGeometry::PolylineInstance)});

    auto polyline_vertices_array_opt = polyline_vertices.to_blob_array();
    if (!polyline_vertices_array_opt.has_value())
    {
        return hrz_jobs::JobResult::FAILURE;
    }

    // Finalize
    geometry.polyline_data = std::move(polyline_vertices_array_opt.value());
    geometry.feature_ids = std::move(feature_ids_data_opt.value());
    geometry.max_feature_index = max_feature_index;
    geometry.wmerc_bounds = wmerc_bounds;
    geometry.sea_bsphere_center = bsphere.center;
    geometry.sea_bsphere_radius = bsphere.radius;

    geometry.polyline_data.register_blob_metadata(
        context.get_blob_allocator(), "contents"_ss, "flat polyline instances"_ss);
    geometry.feature_ids.register_blob_metadata(
        context.get_blob_allocator(), "contents"_ss, "flat vector feature IDs"_ss);

    geometry.polyline_data.register_blob_owner(
        context.get_blob_allocator(), context.get_resource_owner());
    geometry.feature_ids.register_blob_owner(
        context.get_blob_allocator(), context.get_resource_owner());

    return hrz_jobs::JobResult::SUCCESS;
}

} // namespace hrz_jobs::bake_flat_polyline_geometry
