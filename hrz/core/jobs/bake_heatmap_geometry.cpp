#include "hrz/common/blob_array.h"
#include "hrz/common/blob_vector.h"
#include "hrz/common/profiling.h"
#include "hrz/common/proj.h"
#include "hrz/common/triangulation.h" // IWYU pragma: keep
#include "hrz/core/jobs/jobs_declarations.h"
#include "hrz/core/jobs/vector_repr_common.h"
#include "hrz/core/jobs/vector_tiles_jobs_params.h"

#include <earcut.hpp>

namespace
{

static constexpr size_t InitialVertexCapacity = 4096;

} // namespace

namespace hrz_jobs::bake_heatmap_geometry
{
namespace
{

void generate_points_geometry(
    std::span<const lm::dvec3> feature_span,
    hrz::BlobVector<lm::dvec3>& positions,
    hrz::BlobVector<hrz_jobs::HeatmapGeometry::PointInstance>& point_data,
    float value,
    float disc_radius)
{
    for (const auto& p : feature_span)
    {
        positions.push_back(lm::dvec3(p.xy, 0));

        // Position will be written later.
        point_data.push_back({{}, value, disc_radius});
    }
}

} // anonymous namespace

hrz_jobs::JobResult run(
    const hrz_jobs::HeatmapData& input,
    hrz_jobs::HeatmapGeometry& geometry,
    const JobContext& context)
{
    HRZ_SCOPED_SAMPLE("bake heatmap geometry");

    auto input_features = input.geometry.features.get_data();
    auto input_points = input.geometry.points.get_data();

    auto style_prps = input.style.prps.get_data();
    auto style_values = input.style.get_values_reader();

    // Baked positions for later reprojection
    hrz::BlobVector<lm::dvec3> point_positions(context.get_blob_allocator(), InitialVertexCapacity);

    const auto& style = input.style;

    hrz::BlobVector<hrz_jobs::HeatmapGeometry::PointInstance> point_vertices(
        context.get_blob_allocator(), InitialVertexCapacity);

    for (const auto& instance : style.instances.get_data())
    {
        if (instance.repr_id != input.repr_id) continue;

        const auto& feature = input_features.at(instance.feature_index);

        if (feature.type != hrz_proto::VectorGeometryType::POINT_GEOMETRY)
        {
            continue;
        }

        uint64_t prp_begin = instance.first_prp;
        uint64_t prp_end = prp_begin + instance.prp_count;

        float value = input.default_value;
        float disc_radius = input.default_disc_radius;

        for (uint64_t j = prp_begin; j < prp_end; ++j)
        {
            if (style_prps[j] == input.value_prp)
            {
                value = (float)style_values.as_number(j);
            }
            if (style_prps[j] == input.disc_radius_prp)
            {
                disc_radius = (float)style_values.as_number(j);
            }
        }

        auto feature_points =
            input_points.as_span().subspan(feature.first_point, feature.point_count);

        generate_points_geometry(
            feature_points, point_positions, point_vertices, value, disc_radius);
    }

    auto point_positions_data_opt = point_positions.data();
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

    auto point_vertices_data_opt = point_vertices.data();
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

    auto point_vertices_array_opt = point_vertices.to_blob_array();
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
