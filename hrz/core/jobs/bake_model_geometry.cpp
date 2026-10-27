// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/common/geo.h"
#include "hrz/common/profiling.h"
#include "hrz/common/proto_maths.h"
#include "hrz/common/vector_data/tile_geometry.h"
#include "hrz/common/vector_tiles/picking.h"
#include "hrz/common/vertex_utils.h"
#include "hrz/core/jobs/feature_clamping.h"
#include "hrz/core/jobs/jobs_declarations.h"
#include "hrz/core/jobs/vector_repr_common.h"
#include "hrz/core/jobs/vector_tiles_jobs_params.h"
#include "hrz/protocol/vector/geometry_type.pb.h"

#include <optional>
#include <span>
#include <variant>

namespace hrz_jobs::bake_3d_model_geometry
{
namespace
{

struct InstanceProperties
{
    lm::ubvec4 color_srgb;
    lm::vec3 scale;
    lm::dvec3 rotation;
    lm::dvec3 world_offset;
};

InstanceProperties get_instance_properties(
    const hrz_jobs::ModelData& input,
    uint64_t prp_begin,
    uint64_t prp_end,
    std::span<const uint64_t> style_prps,
    const hrz::vector_data::PackedAttributeValuesReader& style_values)
{
    InstanceProperties props{};
    props.color_srgb = input.default_color_srgb;
    props.scale = input.default_scale;
    props.rotation = lm::dvec3(input.default_rotation);
    props.world_offset = lm::dvec3(input.default_world_offset);

    for (uint64_t j = prp_begin; j < prp_end; ++j)
    {
        if (style_prps[j] == input.color_prp)
        {
            props.color_srgb = style_values.as_color(j);
        }
        if (style_prps[j] == input.scale_x_prp)
        {
            props.scale.x = (float)style_values.as_number(j);
        }
        if (style_prps[j] == input.scale_y_prp)
        {
            props.scale.y = (float)style_values.as_number(j);
        }
        if (style_prps[j] == input.scale_z_prp)
        {
            props.scale.z = (float)style_values.as_number(j);
        }
        if (style_prps[j] == input.world_offset_x_prp)
        {
            props.world_offset.x = style_values.as_number(j);
        }
        if (style_prps[j] == input.world_offset_y_prp)
        {
            props.world_offset.y = style_values.as_number(j);
        }
        if (style_prps[j] == input.world_offset_z_prp)
        {
            props.world_offset.z = style_values.as_number(j);
        }
        if (style_prps[j] == input.rotation_x_prp)
        {
            props.rotation.x = style_values.as_number(j);
        }
        if (style_prps[j] == input.rotation_y_prp)
        {
            props.rotation.y = style_values.as_number(j);
        }
        if (style_prps[j] == input.rotation_z_prp)
        {
            props.rotation.z = style_values.as_number(j);
        }
    }

    return props;
}

} // anonymous namespace

hrz_jobs::JobResult run(
    const hrz_jobs::ModelData& input,
    hrz_jobs::ModelGeometry& geometry,
    const JobContext& context)
{
    HRZ_SCOPED_SAMPLE("bake model geometry");

    using hrz::vector_data::VectorTileGeometry;

    const auto input_geometry_reader = input.geometry.make_reader();
    const auto input_feature_ids = input.feature_ids.get_data();
    const auto input_clamps = input.clamps.get_data();

    const auto& style = input.style;
    const auto style_prps = style.prps.get_data();
    const auto style_values = style.get_values_reader();

    std::optional<lm::dbbox2> clipping_bbox = std::nullopt;
    if (input.clip_to_tile)
    {
        clipping_bbox = {hrz::mercator_tile_bbox_meters(input.tile_coords)};
    }

    double tile_radius{};
    lm::dvec3 tile_center;
    hrz::vector_repr::compute_tile_radius_center(input.geometry.bounds, &tile_radius, &tile_center);

    const hrz::FeatureClampingGenerator clamps_gen(input_clamps.as_span(), input.clamping);

    lm::vec3 scale_sum = {0, 0, 0};
    auto geometry_bounds = lm::dbbox3::invalid();

    bool is_scale_unique = true;
    bool is_color_unique = true;

    std::optional<lm::ubvec4> first_instance_color = std::nullopt;
    std::optional<lm::vec3> first_instance_scale = std::nullopt;

    for (const auto& instance : style.instances.get_data())
    {
        if (instance.repr_id != input.repr_id) continue;

        const auto* feature = input_geometry_reader.get_feature(instance.feature_index);
        if (!feature) continue;
        if (!feature->flags.has_geometry(hrz_proto::VectorGeometryType::POINT_GEOMETRY)) continue;

        const auto feature_index = std::min(instance.feature_index, hrz::vt::MAX_FEATURE_INDEX);

        const auto instance_prps = get_instance_properties(
            input, instance.first_prp, instance.first_prp + instance.prp_count,
            style_prps.as_span(), style_values);

        const lm::ubvec4 color_srgb = instance_prps.color_srgb;
        const lm::vec3 scale = instance_prps.scale;
        const lm::dmat4 local_rotation_matrix =
            hrz::euler_rotation(instance_prps.rotation, input.rotation_order);

        auto bake_model = [&](lm::dvec3 p, uint32_t point_index)
        {
            if (clipping_bbox.has_value() && !lm::contains(clipping_bbox.value(), p.xy))
            {
                return;
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

            const hrz::PointClampingGenerator point_clamps_gen =
                clamps_gen.for_feature(instance.feature_index, point_index);
            p.z = point_clamps_gen.clamp_point(0, p.z);

            p += instance_prps.world_offset;
            p = hrz::web_mercator_to_ecef(p.xy, p.z);

            geometry_bounds = lm::expand(geometry_bounds, p);
            scale_sum += lm::abs(scale);

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
            uint32_t impostor_up =
                hrz::octahedral_compress_normal(lm::vec3(impostor_transform.y.xyz));

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
        };

        input_geometry_reader.iterate_geometries_for_feature(
            instance.feature_index,
            [&](const VectorTileGeometry::Geometry& feature_geometry)
            {
                if (const auto* point =
                        std::get_if<VectorTileGeometry::PointGeometry>(&feature_geometry))
                {
                    bake_model(point->point, point->point_index);
                }
            });
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

    return hrz_jobs::JobResult::SUCCESS;
}

} // namespace hrz_jobs::bake_3d_model_geometry
