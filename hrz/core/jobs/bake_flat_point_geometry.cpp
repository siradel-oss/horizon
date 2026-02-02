#include "hrz/common/blob_array.h"
#include "hrz/common/blob_vector.h"
#include "hrz/common/profiling.h"
#include "hrz/common/proj.h"
#include "hrz/common/triangulation.h" // IWYU pragma: keep
#include "hrz/common/vector_tiles/data_texture.h"
#include "hrz/common/vector_tiles/picking.h"
#include "hrz/core/jobs/jobs_declarations.h"
#include "hrz/core/jobs/vector_repr_common.h"
#include "hrz/core/jobs/vector_tiles_jobs_params.h"

namespace hrz_jobs::bake_flat_point_geometry
{
namespace
{
static constexpr size_t InitialVertexCapacity = 4096;

void generate_points_geometry(
    uint32_t feature_index,
    std::span<const lm::dvec3> feature_span,
    hrz::BlobVector<lm::dvec3>& positions,
    hrz::BlobVector<hrz_jobs::FlatPointGeometry::PointInstance>& point_data,
    const lm::ubvec4& rgba,
    float disc_radius)
{
    auto append_vertex = [&](const lm::dvec3& position)
    {
        positions.push_back(position);

        // Position will be written later.
        point_data.push_back({{}, rgba, disc_radius, feature_index});
    };

    for (unsigned int i = 0; i < feature_span.size(); ++i)
    {
        const lm::dvec3 p = feature_span[i];

        append_vertex(lm::dvec3(p.xy, 0));
    }
}

} // anonymous namespace

hrz_jobs::JobResult run(
    const hrz_jobs::FlatPointData& input,
    hrz_jobs::FlatPointGeometry& geometry,
    const JobContext& context)
{
    HRZ_SCOPED_SAMPLE("bake flat points geometry");

    auto input_features = input.geometry.features.get_data();
    auto input_points = input.geometry.points.get_data();
    auto input_linestring_sizes = input.geometry.linestring_sizes.get_data();
    auto input_feature_ids = input.feature_ids.get_data();

    auto style_prps = input.style.prps.get_data();
    auto style_values = input.style.get_values_reader();

    // Baked positions for later reprojection
    hrz::BlobVector<lm::dvec3> point_positions(context.get_blob_allocator(), InitialVertexCapacity);

    const auto& style = input.style;

    hrz::BlobVector<hrz_jobs::FlatPointGeometry::PointInstance> point_vertices(
        context.get_blob_allocator(), InitialVertexCapacity);

    uint32_t max_feature_index = 0;

    // Create triangulation
    for (const auto& instance : style.instances.get_data())
    {
        if (instance.repr_id != input.repr_id) continue;

        const auto& feature = input_features.at(instance.feature_index);
        if (feature.type != hrz_proto::VectorGeometryType::POINT_GEOMETRY) continue;

        auto feature_index = std::min(instance.feature_index, hrz::vt::MAX_FEATURE_INDEX);
        max_feature_index = std::max(max_feature_index, feature_index);

        const uint64_t prp_begin = instance.first_prp;
        const uint64_t prp_end = prp_begin + instance.prp_count;

        lm::ubvec4 fill_color_srgb = input.default_color_srgb;

        float disc_radius = input.default_radius;

        for (uint64_t j = prp_begin; j < prp_end; ++j)
        {
            if (style_prps[j] == input.color_prp)
            {
                fill_color_srgb = style_values.as_color(j);
            }
            else if (style_prps[j] == input.radius_prp)
            {
                disc_radius = (float)style_values.as_number(j);
            }
        }

        auto feature_points =
            input_points.as_span().subspan(feature.first_point, feature.point_count);

        generate_points_geometry(
            feature_index, feature_points, point_positions, point_vertices, fill_color_srgb,
            disc_radius);
    }

    const auto feature_id_array_size =
        hrz::vt::compute_data_texture_array_size(input_feature_ids.size());
    hrz::BlobVector<hrz::vector_data::FeatureIdHash> feature_ids(
        context.get_blob_allocator(), feature_id_array_size);
    for (auto feature_id : input_feature_ids)
    {
        feature_ids.push_back(feature_id);
    }
    feature_ids.resize(feature_id_array_size);

    auto point_positions_data_opt = point_positions.data();
    auto feature_ids_data_opt = feature_ids.to_blob_array();
    if (!point_positions_data_opt.has_value() || !feature_ids_data_opt.has_value())
    {
        return hrz_jobs::JobResult::FAILURE;
    }

    auto point_positions_data = point_positions_data_opt.value();

    lm::dbbox2 wmerc_bounds = lm::dbbox2::invalid();
    if (point_positions.size().value_or(0) > 0)
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

    auto point_vertices_data_opt = point_vertices.data();
    if (!point_vertices_data_opt.has_value())
    {
        return hrz_jobs::JobResult::FAILURE;
    }
    auto point_vertices_data = point_vertices_data_opt.value();

    // Compute relative coordinates
    hrz::vector_repr::compute_rel_coords(
        {point_positions_data}, bsphere.center,
        {(lm::vec3*)&point_vertices_data.data()->position, point_positions_data.size(),
         sizeof(hrz_jobs::FlatPointGeometry::PointInstance)});

    auto point_vertices_array_opt = point_vertices.to_blob_array();
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
