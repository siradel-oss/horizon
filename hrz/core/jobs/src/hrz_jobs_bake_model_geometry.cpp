#include "hrz_jobs_declarations.h"
#include "hrz_jobs_feature_clamping.h"
#include "hrz_jobs_vector_repr_common.h"

#include <hrz_common_profiling.h>
#include <hrz_common_proto_maths.h>
#include <hrz_common_vector_data.h>
#include <hrz_common_vector_tiles.h>
#include <hrz_common_vertex_utils.h>

namespace hrz_jobs::bake_3d_model_geometry
{
hrz::JobResult run(
    const hrz::vt::ModelData& input,
    hrz::vt::ModelGeometry& geometry,
    const JobContext& context)
{
    HRZ_SCOPED_SAMPLE("bake model geometry");

    auto input_features = input.geometry.features.get_data();
    auto input_points = input.geometry.points.get_data();
    auto input_feature_ids = input.feature_ids.get_data();
    auto input_clamps = input.clamps.get_data();

    auto style_prps = input.style.prps.get_data();
    auto style_values = input.style.get_values_reader();

    const auto& style = input.style;

    std::optional<lm::dbbox2> clipping_bbox = std::nullopt;
    if (input.clip_to_tile)
    {
        clipping_bbox = {hrz::mercator_tile_bbox_meters(input.tile_coords)};
    }

    double tile_radius{};
    lm::dvec3 tile_center;
    hrz::vector_repr::compute_tile_radius_center(input.geometry.bounds, &tile_radius, &tile_center);

    hrz::FeatureClampingGenerator clamps_gen(input_clamps.as_span(), input.clamping);

    lm::vec3 scale_sum = {0, 0, 0};
    auto geometry_bounds = lm::dbbox3::invalid();

    bool is_scale_unique = true;
    bool is_color_unique = true;

    std::optional<lm::ubvec4> first_instance_color = std::nullopt;
    std::optional<lm::vec3> first_instance_scale = std::nullopt;

    for (const auto& instance : style.instances.get_data())
    {
        if (instance.repr_id != input.repr_id) continue;

        const auto& feature = input_features.at(instance.feature_index);
        auto feature_index = std::min(instance.feature_index, hrz::vt::MAX_FEATURE_INDEX);

        lm::dvec3 p = ((const lm::dvec3*)(input_points.data()))[feature.first_point];

        if (feature.type != hrz_proto::VectorGeometryType::POINT_GEOMETRY)
        {
            p.x = feature.anchor.x;
            p.y = feature.anchor.y;
        }

        if (clipping_bbox.has_value() && !lm::contains(clipping_bbox.value(), p.xy))
        {
            continue;
        }

        lm::ubvec4 color_srgb = input.default_color_srgb;
        lm::vec3 scale = input.default_scale;
        lm::dvec3 rotation = lm::dvec3(input.default_rotation);
        lm::dvec3 world_offset = lm::dvec3(input.default_world_offset);

        uint64_t prp_begin = instance.first_prp;
        uint64_t prp_end = prp_begin + instance.prp_count;

        for (uint64_t j = prp_begin; j < prp_end; ++j)
        {
            if (style_prps[j] == input.color_prp)
            {
                color_srgb = style_values.as_color(j);
            }
            if (style_prps[j] == input.scale_x_prp)
            {
                scale.x = (float)style_values.as_number(j);
            }
            if (style_prps[j] == input.scale_y_prp)
            {
                scale.y = (float)style_values.as_number(j);
            }
            if (style_prps[j] == input.scale_z_prp)
            {
                scale.z = (float)style_values.as_number(j);
            }
            if (style_prps[j] == input.world_offset_x_prp)
            {
                world_offset.x = style_values.as_number(j);
            }
            if (style_prps[j] == input.world_offset_y_prp)
            {
                world_offset.y = style_values.as_number(j);
            }
            if (style_prps[j] == input.world_offset_z_prp)
            {
                world_offset.z = style_values.as_number(j);
            }
            if (style_prps[j] == input.rotation_x_prp)
            {
                rotation.x = style_values.as_number(j);
            }
            if (style_prps[j] == input.rotation_y_prp)
            {
                rotation.y = style_values.as_number(j);
            }
            if (style_prps[j] == input.rotation_z_prp)
            {
                rotation.z = style_values.as_number(j);
            }
        }

        if (is_color_unique && first_instance_color.has_value()
            && first_instance_color.value() != color_srgb)
        {
            is_color_unique = false;
        }

        if (is_scale_unique && first_instance_scale.has_value()
            && first_instance_scale.value() != scale)
        {
            is_scale_unique = false;
        }

        hrz::PointClampingGenerator point_clamps_gen =
            clamps_gen.for_feature(instance.feature_index, feature.first_point);
        p.z = point_clamps_gen.clamp_point(0, p.z);

        p += world_offset;
        p = hrz::web_mercator_to_ecef(p.xy, p.z);

        geometry_bounds = lm::expand(geometry_bounds, p);
        scale_sum += lm::abs(scale);

        lm::dmat4 local_rotation_matrix = hrz::euler_rotation(rotation, input.rotation_order);

        hrz::GeoPosition3 geo = hrz::ecef_to_geo3(p);
        lm::dmat4 geo_location_xform = hrz::enu_to_ecef_transform_for_geo(geo);
        lm::mat4 global_rotation =
            lm::mat4(geo_location_xform * local_rotation_matrix * input.frame);
        lm::vec3 position = lm::vec3(p - tile_center);

        uint32_t normal_right = hrz::octahedral_compress_normal(global_rotation.x.xyz);
        uint32_t normal_up = hrz::octahedral_compress_normal(global_rotation.y.xyz);

        geometry.positions.push_back(position);
        geometry.normals.emplace_back(
            normal_right & 0xffff, (normal_right >> 16) & 0xffff, normal_up & 0xffff,
            (normal_up >> 16) & 0xffff);
        geometry.scales.emplace_back(scale);
        geometry.colors.push_back(color_srgb);
        geometry.object_ids.push_back(feature_index);
        geometry.feature_ids.push_back(input_feature_ids.at(instance.feature_index));

        // We don't need to multiply with the `frame` matrix because when the model was baked
        // into an atlas the `frame` matrix was already applied there. Thus, the image rendered
        // on a quad is already expected to be correctly positioned relative to the ground.
        lm::dmat4 impostor_transform = lm::inverse(geo_location_xform * local_rotation_matrix);
        uint32_t impostor_right =
            hrz::octahedral_compress_normal(lm::vec3(impostor_transform.x.xyz));
        uint32_t impostor_up = hrz::octahedral_compress_normal(lm::vec3(impostor_transform.y.xyz));

        geometry.impostor_orientations.emplace_back(impostor_right, impostor_up);
        geometry.impostor_positions.push_back(position);
        geometry.impostor_scales.push_back((lm::vec3)(input.frame * lm::dvec4(scale, 1.0)).xyz);

        if (!first_instance_color.has_value())
        {
            first_instance_color = {color_srgb};
        }

        if (!first_instance_scale.has_value())
        {
            first_instance_scale = {scale};
        }
    }

    if (is_scale_unique)
    {
        geometry.scales.resize(1);
        geometry.scales.shrink_to_fit();

        geometry.impostor_scales.resize(1);
        geometry.impostor_scales.shrink_to_fit();
    }

    if (is_color_unique)
    {
        geometry.colors.resize(1);
        geometry.colors.shrink_to_fit();
    }

    double scale_avg = 1.0;
    if (geometry.positions.size() > 0)
    {
        scale_avg =
            (scale_sum.x + scale_sum.y + scale_sum.z) / 3.0 / (float)geometry.positions.size();
    }

    geometry.average_scale = scale_avg;
    geometry.origin = tile_center;
    geometry.bsphere = {lm::center(geometry_bounds), lm::radius(geometry_bounds)};

    return hrz::JobResult::SUCCESS;
}

} // namespace hrz_jobs::bake_3d_model_geometry
