// SPDX-FileCopyrightText: Copyright 2023 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/common/blob_array.h"
#include "hrz/common/blob_vector.h"
#include "hrz/common/profiling.h"
#include "hrz/common/proj.h"
#include "hrz/common/triangulation.h" // IWYU pragma: keep
#include "hrz/common/vector_data/tile_geometry.h"
#include "hrz/core/jobs/jobs_declarations.h"
#include "hrz/core/jobs/vector_repr_common.h"
#include "hrz/core/jobs/vector_tiles_jobs_params.h"
#include "hrz/protocol/vector/geometry_type.pb.h"

#include <earcut.hpp>

#include <span>
#include <variant>

namespace
{

static constexpr size_t InitialVertexCapacity = 4096;

} // namespace

namespace hrz_jobs::bake_heatmap_geometry
{
namespace
{

struct InstanceProperties
{
    float value;
    float disc_radius;
};

InstanceProperties get_instance_properties(
    const hrz_jobs::HeatmapData& input,
    uint64_t prp_begin,
    uint64_t prp_end,
    std::span<const uint64_t> style_prps,
    const hrz::vector_data::PackedAttributeValuesReader& style_values)
{
    InstanceProperties props{};
    props.value = input.default_value;
    props.disc_radius = input.default_disc_radius;

    for (uint64_t j = prp_begin; j < prp_end; ++j)
    {
        if (style_prps[j] == input.value_prp)
        {
            props.value = (float)style_values.as_number(j);
        }
        if (style_prps[j] == input.disc_radius_prp)
        {
            props.disc_radius = (float)style_values.as_number(j);
        }
    }

    return props;
}

struct BakingContext
{
    // Baked positions for later reprojection
    hrz::BlobVector<lm::dvec3> positions;
    hrz::BlobVector<hrz_jobs::HeatmapGeometry::PointInstance> instances;
};

void generate_point_geometry(
    const lm::dvec3& point,
    const InstanceProperties& props,
    BakingContext& ctx)
{
    ctx.positions.push_back(lm::dvec3(point.xy, 0));

    // Position will be written later.
    ctx.instances.push_back({{}, props.value, props.disc_radius});
}

} // anonymous namespace

hrz_jobs::JobResult run(
    const hrz_jobs::HeatmapData& input,
    hrz_jobs::HeatmapGeometry& geometry,
    const JobContext& context)
{
    HRZ_SCOPED_SAMPLE("bake heatmap geometry");

    using hrz::vector_data::VectorTileGeometry;

    const auto input_geometry_reader = input.geometry.make_reader();

    const auto& style = input.style;
    const auto style_prps = style.prps.get_data();
    const auto style_values = style.get_values_reader();

    BakingContext ctx{
        .positions =
            hrz::BlobVector<lm::dvec3>(context.get_blob_allocator(), InitialVertexCapacity),
        .instances = hrz::BlobVector<hrz_jobs::HeatmapGeometry::PointInstance>(
            context.get_blob_allocator(), InitialVertexCapacity),
    };

    for (const auto& instance : style.instances.get_data())
    {
        if (instance.repr_id != input.repr_id) continue;

        const auto* feature = input_geometry_reader.get_feature(instance.feature_index);
        if (!feature) continue;
        if (!feature->flags.has_geometry(hrz_proto::VectorGeometryType::POINT_GEOMETRY)) continue;

        const auto instance_prps = get_instance_properties(
            input, instance.first_prp, instance.first_prp + instance.prp_count,
            style_prps.as_span(), style_values);

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

    auto point_positions_data_opt = ctx.positions.data();
    if (!point_positions_data_opt.has_value())
    {
        return hrz_jobs::JobResult::FAILURE;
    }
    auto point_positions_data = point_positions_data_opt.value();

    // Convert to Geocentric
    pl_transform_in_place_canonical(
        &hrz_proj::wmerc_to_ecef, point_positions_data.size(), &point_positions_data.data()->x);

    hrz::BSphere<double> bsphere =
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
        {(lm::vec3*)point_vertices_data.data(), point_positions_data.size(),
         sizeof(hrz_jobs::HeatmapGeometry::PointInstance)});

    auto point_vertices_array_opt = ctx.instances.to_blob_array();
    if (!point_vertices_array_opt.has_value())
    {
        return hrz_jobs::JobResult::FAILURE;
    }

    // Finalize
    geometry.point_data = std::move(point_vertices_array_opt.value());
    geometry.bsphere = bsphere;

    geometry.point_data.register_blob_metadata(
        context.get_blob_allocator(), "contents"_ss, "heatmap point instances"_ss);
    geometry.point_data.register_blob_owner(
        context.get_blob_allocator(), context.get_resource_owner());

    return hrz_jobs::JobResult::SUCCESS;
}

} // namespace hrz_jobs::bake_heatmap_geometry
