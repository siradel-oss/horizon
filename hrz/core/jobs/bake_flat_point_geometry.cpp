// SPDX-FileCopyrightText: Copyright 2026 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/common/blob_array.h"
#include "hrz/common/blob_vector.h"
#include "hrz/common/profiling.h"
#include "hrz/common/proj.h"
#include "hrz/common/triangulation.h" // IWYU pragma: keep
#include "hrz/common/vector_data/tile_geometry.h"
#include "hrz/common/vector_tiles/data_texture.h"
#include "hrz/common/vector_tiles/picking.h"
#include "hrz/core/jobs/jobs_declarations.h"
#include "hrz/core/jobs/vector_repr_common.h"
#include "hrz/core/jobs/vector_tiles_jobs_params.h"
#include "hrz/protocol/vector/geometry_type.pb.h"

#include <span>
#include <variant>

namespace hrz_jobs::bake_flat_point_geometry
{
namespace
{

constexpr size_t InitialVertexCapacity = 4096;

struct InstanceProperties
{
    uint32_t feature_index;

    lm::ubvec4 fill_color_srgb;
    float disc_radius;
    lm::ubvec4 outline_color_srgb;
    float outline_width;
};

InstanceProperties get_instance_properties(
    const hrz_jobs::FlatPointData& input,
    uint64_t prp_begin,
    uint64_t prp_end,
    std::span<const uint64_t> style_prps,
    const hrz::vector_data::PackedAttributeValuesReader& style_values)
{
    InstanceProperties props{};
    props.fill_color_srgb = input.default_color_srgb;
    props.disc_radius = input.default_radius;
    props.outline_color_srgb = input.default_outline_color_srgb;
    props.outline_width = input.default_outline_width;

    for (uint64_t j = prp_begin; j < prp_end; ++j)
    {
        if (style_prps[j] == input.color_prp)
        {
            props.fill_color_srgb = style_values.as_color(j);
        }
        else if (style_prps[j] == input.radius_prp)
        {
            props.disc_radius = (float)style_values.as_number(j);
        }
        else if (style_prps[j] == input.outline_color_prp)
        {
            props.outline_color_srgb = style_values.as_color(j);
        }
        else if (style_prps[j] == input.outline_width_prp)
        {
            props.outline_width = (float)style_values.as_number(j);
        }
    }

    return props;
}

struct BakingContext
{
    // Baked positions for later reprojection
    hrz::BlobVector<lm::dvec3> positions;
    hrz::BlobVector<hrz_jobs::FlatPointGeometry::PointInstance> instances;
};

void generate_point_geometry(
    const lm::dvec3& point,
    const InstanceProperties& props,
    BakingContext& ctx)
{
    ctx.positions.push_back(lm::dvec3(point.xy, 0));

    // Position will be written later.
    ctx.instances.push_back(
        {{},
         props.fill_color_srgb,
         props.disc_radius,
         props.outline_color_srgb,
         props.outline_width,
         props.feature_index});
}

} // anonymous namespace

hrz_jobs::JobResult run(
    const hrz_jobs::FlatPointData& input,
    hrz_jobs::FlatPointGeometry& geometry,
    const JobContext& context)
{
    HRZ_SCOPED_SAMPLE("bake flat points geometry");

    using hrz::vector_data::VectorTileGeometry;

    const auto input_geometry_reader = input.geometry.make_reader();
    const auto input_feature_ids = input.feature_ids.get_data();

    const auto& style = input.style;
    const auto style_prps = style.prps.get_data();
    const auto style_values = style.get_values_reader();

    BakingContext ctx{
        .positions =
            hrz::BlobVector<lm::dvec3>(context.get_blob_allocator(), InitialVertexCapacity),
        .instances = hrz::BlobVector<hrz_jobs::FlatPointGeometry::PointInstance>(
            context.get_blob_allocator(), InitialVertexCapacity),
    };

    uint32_t max_feature_index = 0;

    for (const auto& instance : style.instances.get_data())
    {
        if (instance.repr_id != input.repr_id) continue;

        const auto* feature = input_geometry_reader.get_feature(instance.feature_index);
        if (!feature) continue;
        if (!feature->flags.has_geometry(hrz_proto::VectorGeometryType::POINT_GEOMETRY)) continue;

        auto instance_prps = get_instance_properties(
            input, instance.first_prp, instance.first_prp + instance.prp_count,
            style_prps.as_span(), style_values);
        instance_prps.feature_index = std::min(instance.feature_index, hrz::vt::MAX_FEATURE_INDEX);

        max_feature_index = std::max(max_feature_index, instance_prps.feature_index);

        input_geometry_reader.iterate_geometries_for_feature(
            instance.feature_index,
            [&](const VectorTileGeometry::Geometry& feature_geometry)
            {
                if (const auto* point =
                        std::get_if<VectorTileGeometry::PointGeometry>(&feature_geometry))
                {
                    generate_point_geometry(point->point, instance_prps, ctx);
                }
            });
    }

    const auto feature_id_array_size =
        hrz::vt::compute_data_texture_array_size(input_feature_ids.size());
    hrz::BlobVector<hrz::vector_data::FeatureIdHash> feature_ids(
        context.get_blob_allocator(), feature_id_array_size);
    for (auto feature_id : input_feature_ids.as_span())
    {
        feature_ids.push_back(feature_id);
    }
    feature_ids.resize(feature_id_array_size);

    auto point_positions_data_opt = ctx.positions.data();
    auto feature_ids_data_opt = feature_ids.to_blob_array();
    if (!point_positions_data_opt.has_value() || !feature_ids_data_opt.has_value())
    {
        return hrz_jobs::JobResult::FAILURE;
    }

    auto point_positions_data = point_positions_data_opt.value();

    lm::dbbox2 wmerc_bounds = lm::dbbox2::invalid();
    if (ctx.positions.size().value_or(0) > 0)
    {
        for (const lm::dvec3& p : point_positions_data)
        {
            wmerc_bounds = lm::expand(wmerc_bounds, p.xy);
        }
    }

    // Convert to Geocentric to compute bsphere
    pl_transform_in_place_canonical(
        &hrz_proj::wmerc_to_ecef, point_positions_data.size(), &point_positions_data.data()->x);

    const hrz::BSphere<double> bsphere =
        hrz::compute_bounding_sphere(std::span<const lm::dvec3>(point_positions_data));

    auto point_vertices_data_opt = ctx.instances.data();
    if (!point_vertices_data_opt.has_value())
    {
        return hrz_jobs::JobResult::FAILURE;
    }
    auto point_vertices_data = point_vertices_data_opt.value();

    // Compute relative coordinates
    hrz::vector_repr::compute_rel_coords(
        {point_positions_data}, bsphere.center,
        {&point_vertices_data.data()->position, point_positions_data.size(),
         sizeof(hrz_jobs::FlatPointGeometry::PointInstance)});

    auto point_vertices_array_opt = ctx.instances.to_blob_array();
    if (!point_vertices_array_opt.has_value())
    {
        return hrz_jobs::JobResult::FAILURE;
    }

    // Finalize
    geometry.point_data = std::move(point_vertices_array_opt.value());
    geometry.feature_ids = std::move(feature_ids_data_opt.value());
    geometry.max_feature_index = max_feature_index;
    geometry.wmerc_bounds = wmerc_bounds;
    geometry.sea_bsphere_center = bsphere.center;
    geometry.sea_bsphere_radius = bsphere.radius;

    geometry.point_data.register_blob_metadata(
        context.get_blob_allocator(), "contents"_ss, "flat point instances"_ss);
    geometry.feature_ids.register_blob_metadata(
        context.get_blob_allocator(), "contents"_ss, "flat vector feature IDs"_ss);

    geometry.point_data.register_blob_owner(
        context.get_blob_allocator(), context.get_resource_owner());
    geometry.feature_ids.register_blob_owner(
        context.get_blob_allocator(), context.get_resource_owner());

    return hrz_jobs::JobResult::SUCCESS;
}

} // namespace hrz_jobs::bake_flat_point_geometry
