// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/common/blob_array.h"
#include "hrz/common/blob_vector.h"
#include "hrz/common/color.h"
#include "hrz/common/profiling.h"
#include "hrz/common/proj.h"
#include "hrz/common/vector_tiles/picking.h"
#include "hrz/common/vertex_utils.h"
#include "hrz/core/jobs/feature_clamping.h"
#include "hrz/core/jobs/jobs_declarations.h"
#include "hrz/core/jobs/vector_repr_common.h"
#include "hrz/core/jobs/vector_tiles_jobs_params.h"
#include "hrz/protocol/vector/geometry_type.pb.h"

using namespace hrz::vector_data;

namespace hrz_jobs::bake_cylinder_vector_geometry
{
namespace
{

static constexpr size_t InitialPointCapacity = 512;
static constexpr size_t InitialInstanceCapacity = 1024;
static constexpr size_t InitialBSpherePointCapacity = 2048;

struct InstanceProperties
{
    uint32_t feature_index;
    uint64_t feature_id;

    float radius;
    float altitude_offset;
    lm::ubvec4 fill_color_oklab;
    lm::ubvec4 secondary_color_oklab;
    float dash_period;
    float dash_primary_length;
    float animation_speed;
};

InstanceProperties get_instance_properties(
    const hrz_jobs::CylinderVectorData& input,
    uint64_t prp_begin,
    uint64_t prp_end,
    std::span<const uint64_t> style_prps,
    const PackedAttributeValuesReader& style_values)
{
    InstanceProperties props{};
    props.radius = input.default_radius;
    props.altitude_offset = input.default_altitude_offset;
    props.dash_period = input.dashes.default_period;
    props.dash_primary_length = input.dashes.default_primary_length;
    props.animation_speed = input.dashes.default_animation_speed;

    auto fill_color_srgb = input.default_color_srgb;
    auto secondary_color_srgb = input.dashes.default_secondary_color_srgb;

    for (uint64_t j = prp_begin; j < prp_end; ++j)
    {
        if (style_prps[j] == input.radius_prp)
        {
            props.radius = (float)style_values.as_number(j);
        }
        if (style_prps[j] == input.altitude_offset_prp)
        {
            props.altitude_offset = (float)style_values.as_number(j);
        }
        if (style_prps[j] == input.color_prp)
        {
            fill_color_srgb = style_values.as_color(j);
        }
        if (style_prps[j] == input.dashes.secondary_color_prp)
        {
            secondary_color_srgb = style_values.as_color(j);
        }
        if (style_prps[j] == input.dashes.period_prp)
        {
            props.dash_period = (float)style_values.as_number(j);
        }
        if (style_prps[j] == input.dashes.primary_length_prp)
        {
            props.dash_primary_length = (float)style_values.as_number(j);
        }
        if (style_prps[j] == input.dashes.animation_speed_prp)
        {
            props.animation_speed = (float)style_values.as_number(j);
        }
    }

    const lm::vec4 fill_color_oklab =
        hrz::srgb_to_oklab(hrz::convert_byte_color_to_rgba(fill_color_srgb));
    const lm::vec4 secondary_color_oklab =
        hrz::srgb_to_oklab(hrz::convert_byte_color_to_rgba(secondary_color_srgb));

    // This stores Oklab colours in 8-bit-per-channel vectors. It's not
    // great, and some precision is lost. (Usually only sRGB colours should
    // be reduced to 8 bits per channel.) However the colour interpolation
    // between these two colours in done with floats in the shader, so
    // the precision loss should be acceptable.
    props.fill_color_oklab = hrz::convert_rgba_color_to_bytes(fill_color_oklab);
    props.secondary_color_oklab = hrz::convert_rgba_color_to_bytes(secondary_color_oklab);

    return props;
}

struct BakingContext
{
    hrz::BlobVector<lm::dvec3> points;
    hrz::BlobVector<lm::vec3> bsphere_points;
    hrz::BlobVector<hrz_jobs::CylinderVectorGeometry::Instance> instances;
    lm::dvec3 tile_center;
    bool has_caps;
};

// Returns the progress at the end of the linestring, starting from start_progress,
// or std::nullopt if the geometry could not be generated.
std::optional<float> generate_geometry(
    std::span<const lm::dvec3> linestring_points,
    float start_progress,
    const InstanceProperties& props,
    BakingContext& ctx,
    const hrz::PointClampingGenerator& point_clamps_gen)
{
    ctx.points.clear();
    ctx.points.reserve(linestring_points.size());

    for (uint32_t p0 = 0; p0 < linestring_points.size(); ++p0)
    {
        lm::dvec3 pp0 = linestring_points[p0];
        pp0.z = point_clamps_gen.clamp_point(p0, pp0.z) + (double)props.altitude_offset;
        ctx.points.push_back(pp0);
    }

    auto points_data_opt = ctx.points.data();
    if (!points_data_opt.has_value())
    {
        return std::nullopt;
    }
    const auto points_data = points_data_opt.value();

    // Convert to Geocentric
    pl_transform_in_place_canonical(
        &hrz_proj::wmerc_to_ecef, points_data.size(), &points_data.data()->x);

    float progress = start_progress;
    const auto segment_count = (uint32_t)(linestring_points.size() - 1);

    for (uint32_t p1 = 0; p1 < segment_count; ++p1)
    {
        const lm::dvec3 pp0 = points_data[std::max<uint32_t>(1, p1) - 1] - ctx.tile_center;
        const lm::dvec3 pp1 = points_data[p1] - ctx.tile_center;
        const lm::dvec3 pp2 = points_data[p1 + 1] - ctx.tile_center;
        const lm::dvec3 pp3 =
            points_data[std::min<uint32_t>(p1 + 2, points_data.size() - 1)] - ctx.tile_center;

        const lm::vec3 n = lm::vec3(lm::normalize(pp2 - pp1));
        const lm::vec3 n0 = hrz::vector_repr::compute_joint_normal(pp2, pp1, pp0).first;
        const lm::vec3 n1 = hrz::vector_repr::compute_joint_normal(pp1, pp2, pp3).first;

        const auto n0_oct = hrz::octahedral_compress_normal(n0);
        const auto n1_oct = hrz::octahedral_compress_normal(n1);

        const lm::vec3 c0 = lm::vec3(pp1);
        const lm::vec3 c1 = lm::vec3(pp2);

        const float progress0 = progress;
        progress += lm::length(c1 - c0);

        // Line total length will be written once all linestrings have been iterated
        ctx.instances.push_back(
            {c0,
             n0_oct,
             c1,
             n1_oct,
             props.fill_color_oklab,
             {props.radius, props.radius},
             0.0F,
             progress0,
             progress,
             props.dash_period,
             props.dash_primary_length,
             props.animation_speed,
             props.secondary_color_oklab,
             props.feature_index,
             props.feature_id});

        // We add 0-length cylinders with one side that has 0 radius
        // to "close" the cylinders on both ends.
        if (ctx.has_caps && p1 == 0) // first point
        {
            const auto minus_n_oct = hrz::octahedral_compress_normal(-n);

            ctx.instances.push_back(
                {c0,
                 minus_n_oct,
                 c0,
                 minus_n_oct,
                 props.fill_color_oklab,
                 {props.radius, 0.0F},
                 0.0F,
                 progress0,
                 progress0,
                 props.dash_period,
                 props.dash_primary_length,
                 props.animation_speed,
                 props.secondary_color_oklab,
                 props.feature_index,
                 props.feature_id});
        }

        if (ctx.has_caps && p1 == segment_count - 1) // last point
        {
            const auto n_oct = hrz::octahedral_compress_normal(n);

            ctx.instances.push_back(
                {c1,
                 n_oct,
                 c1,
                 n_oct,
                 props.fill_color_oklab,
                 {props.radius, 0.0F},
                 0.0F,
                 progress,
                 progress,
                 props.dash_period,
                 props.dash_primary_length,
                 props.animation_speed,
                 props.secondary_color_oklab,
                 props.feature_index,
                 props.feature_id});
        }

        ctx.bsphere_points.push_back(c0);
        ctx.bsphere_points.push_back(c1);
    }

    return progress;
}

} // namespace

hrz_jobs::JobResult run(
    const hrz_jobs::CylinderVectorData& input,
    hrz_jobs::CylinderVectorGeometry& geometry,
    const JobContext& job_ctx)
{
    HRZ_SCOPED_SAMPLE("bake cylinder geometry");

    const auto input_geometry_reader = input.geometry.make_reader();
    const auto input_feature_ids = input.feature_ids.get_data();
    const auto input_clamps = input.clamps.get_data();

    const auto& style = input.style;
    const auto style_prps = style.prps.get_data();
    const auto style_values = style.get_values_reader();

    double tile_radius{};
    lm::dvec3 tile_center;
    hrz::vector_repr::compute_tile_radius_center(input.geometry.bounds, &tile_radius, &tile_center);

    bool tile_has_transparency = false;
    bool tile_is_animated = false;

    const hrz::FeatureClampingGenerator clamps_gen(input_clamps.as_span(), input.clamping);

    BakingContext ctx{
        .points = hrz::BlobVector<lm::dvec3>(job_ctx.get_blob_allocator(), InitialPointCapacity),
        .bsphere_points =
            hrz::BlobVector<lm::vec3>(job_ctx.get_blob_allocator(), InitialBSpherePointCapacity),
        .instances = hrz::BlobVector<hrz_jobs::CylinderVectorGeometry::Instance>(
            job_ctx.get_blob_allocator(), InitialInstanceCapacity),
        .tile_center = tile_center,
        .has_caps = input.dashes.mode == hrz_proto::DASH_DISABLED
    };

    for (const auto& instance : style.instances.get_data())
    {
        if (instance.repr_id != input.repr_id) continue;

        const auto* feature = input_geometry_reader.get_feature(instance.feature_index);
        if (!feature) continue;
        if (!feature->flags.has_geometry(hrz_proto::POLYLINE_GEOMETRY)) continue;

        const auto feature_index = std::min(instance.feature_index, hrz::vt::MAX_FEATURE_INDEX);
        const auto feature_id = input_feature_ids.at(instance.feature_index);

        auto instance_prps = get_instance_properties(
            input, instance.first_prp, instance.first_prp + instance.prp_count,
            style_prps.as_span(), style_values);

        instance_prps.feature_id = feature_id;
        instance_prps.feature_index = feature_index;

        switch (input.dashes.mode)
        {
            case hrz_proto::DASH_DISABLED:
                tile_has_transparency =
                    (instance_prps.fill_color_oklab.a != 255
                     && instance_prps.fill_color_oklab.a != 0);
                break;

            case hrz_proto::DASH_ENABLED_FILLED:
                tile_has_transparency = (instance_prps.fill_color_oklab.a != 255
                                         && instance_prps.fill_color_oklab.a != 0)
                    || (instance_prps.secondary_color_oklab.a != 255
                        && instance_prps.secondary_color_oklab.a != 0);
                break;

            case hrz_proto::DASH_ENABLED_GRADIENT:
                tile_has_transparency =
                    (instance_prps.fill_color_oklab.a != 255
                     || instance_prps.secondary_color_oklab.a != 255);
                break;

            default: assert(false && "Unhandled case");
        }

        if (instance_prps.animation_speed != 0.0)
        {
            tile_is_animated = true;
        }

        const auto first_instance_index = ctx.instances.size().value_or(0);
        float feature_progress = 0.0F;
        bool geometry_baking_success = true;

        input_geometry_reader.iterate_geometries_for_feature(
            instance.feature_index,
            [&](const VectorTileGeometry::Geometry& feature_geometry)
            {
                auto* polyline_geometry =
                    std::get_if<VectorTileGeometry::PolylineGeometry>(&feature_geometry);
                if (!polyline_geometry || polyline_geometry->points.size() < 2) return;

                const hrz::PointClampingGenerator point_clamps_gen = clamps_gen.for_feature(
                    instance.feature_index, polyline_geometry->first_point_index);

                auto end_progress_opt = generate_geometry(
                    polyline_geometry->points, feature_progress, instance_prps, ctx,
                    point_clamps_gen);
                if (!end_progress_opt.has_value())
                {
                    geometry_baking_success = false;
                    return;
                }

                feature_progress = end_progress_opt.value();
            });

        if (!geometry_baking_success)
        {
            return hrz_jobs::JobResult::FAILURE;
        }

        const auto end_instance_index = ctx.instances.size().value_or(0);
        if (end_instance_index > first_instance_index)
        {
            const auto instances_data = ctx.instances.data().value();
            for (auto i = first_instance_index; i < end_instance_index; ++i)
            {
                instances_data[i].line_total_length = feature_progress;
            }
        }
    }

    auto instances_array_opt = ctx.instances.to_blob_array();
    auto bsphere_points_data_opt = ctx.bsphere_points.data();
    if (!instances_array_opt.has_value() || !bsphere_points_data_opt.has_value())
    {
        return hrz_jobs::JobResult::FAILURE;
    }

    hrz::BSphere<float> bsphere =
        hrz::compute_bounding_sphere(std::span<const lm::vec3>(bsphere_points_data_opt.value()));

    bsphere.center += lm::vec3(tile_center); // The points were offset by "center".

    // Finalize
    geometry.instance_data = std::move(instances_array_opt.value());
    geometry.center = tile_center;
    geometry.bsphere_center = lm::dvec3(bsphere.center);
    geometry.bsphere_radius = bsphere.radius;
    geometry.has_transparency = tile_has_transparency;
    geometry.is_animated = tile_is_animated;

    geometry.instance_data.register_blob_metadata(
        job_ctx.get_blob_allocator(), "contents"_ss, "cylinder instances"_ss);
    geometry.instance_data.register_blob_owner(
        job_ctx.get_blob_allocator(), job_ctx.get_resource_owner());

    return hrz_jobs::JobResult::SUCCESS;
}

} // namespace hrz_jobs::bake_cylinder_vector_geometry
