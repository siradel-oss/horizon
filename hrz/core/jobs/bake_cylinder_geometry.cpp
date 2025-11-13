#include "hrz/common/blob_array.h"
#include "hrz/common/blob_vector.h"
#include "hrz/common/color.h"
#include "hrz/common/profiling.h"
#include "hrz/common/proj.h"
#include "hrz/common/vector_data.h"
#include "hrz/common/vector_tiles.h"
#include "hrz/common/vertex_utils.h"
#include "hrz/core/jobs/feature_clamping.h"
#include "hrz/core/jobs/jobs_declarations.h"
#include "hrz/core/jobs/vector_repr_common.h"
#include "hrz/fnd/log.h"
#include "hrz/fnd/string_utils.h"

using namespace hrz::vector_data;

namespace hrz_jobs::bake_cylinder_vector_geometry
{
namespace
{
static constexpr size_t InitialPointCapacity = 512;
static constexpr size_t InitialInstanceCapacity = 1024;
static constexpr size_t InitialBSpherePointCapacity = 2048;
} // namespace

hrz::JobResult run(
    const hrz::vt::CylinderVectorData& input,
    hrz::vt::CylinderVectorGeometry& geometry,
    const JobContext& context)
{
    HRZ_SCOPED_SAMPLE("bake cylinder geometry");

    auto input_features = input.geometry.features.get_data();
    auto input_points = input.geometry.points.get_data();
    auto input_linestring_sizes = input.geometry.linestring_sizes.get_data();
    auto input_feature_ids = input.feature_ids.get_data();
    auto input_clamps = input.clamps.get_data();

    auto style_prps = input.style.prps.get_data();
    auto style_values = input.style.get_values_reader();

    hrz::BlobVector<hrz::vt::CylinderVectorGeometry::Instance> instances(
        context.get_blob_allocator(), InitialInstanceCapacity);

    // Declared here to recycle memory.
    hrz::BlobVector<lm::dvec3> points(context.get_blob_allocator(), InitialPointCapacity);

    const auto& style = input.style;

    double radius{};
    lm::dvec3 center;
    hrz::vector_repr::compute_tile_radius_center(input.geometry.bounds, &radius, &center);

    hrz::BlobVector<lm::vec3> bsphere_point_buffer(
        context.get_blob_allocator(), InitialBSpherePointCapacity);
    bool has_transparency = false;
    bool is_animated = false;
    bool has_caps = input.dash_mode == hrz_proto::DASH_DISABLED;

    hrz::FeatureClampingGenerator clamps_gen(input_clamps.as_span(), input.clamping);

    for (const auto& instance : style.instances.get_data())
    {
        if (instance.repr_id != input.repr_id) continue;

        const auto& feature = input_features.at(instance.feature_index);
        auto feature_index = std::min(instance.feature_index, hrz::vt::MAX_FEATURE_INDEX);
        auto feature_id = input_feature_ids.at(instance.feature_index);

        if (feature.type != hrz_proto::VectorGeometryType::POLYLINE_GEOMETRY)
        {
            continue;
        }

        uint64_t prp_begin = instance.first_prp;
        uint64_t prp_end = prp_begin + instance.prp_count;

        double altitude_offset = input.default_altitude_offset;
        float radius = input.default_radius;
        lm::ubvec4 fill_color_srgb = input.default_color_srgb;
        lm::ubvec4 empty_color_srgb = input.default_empty_color_srgb;
        float dash_period = input.default_dash_period;
        float dash_length = input.default_dash_length;
        float animation_speed = input.default_animation_speed;

        for (uint64_t j = prp_begin; j < prp_end; ++j)
        {
            if (style_prps[j] == input.radius_prp)
            {
                radius = (float)style_values.as_number(j);
            }
            if (style_prps[j] == input.altitude_offset_prp)
            {
                altitude_offset = (float)style_values.as_number(j);
            }
            if (style_prps[j] == input.color_prp)
            {
                fill_color_srgb = style_values.as_color(j);
            }
            if (style_prps[j] == input.empty_color_prp)
            {
                empty_color_srgb = style_values.as_color(j);
            }
            if (style_prps[j] == input.dash_period_prp)
            {
                dash_period = (float)style_values.as_number(j);
            }
            if (style_prps[j] == input.dash_length_prp)
            {
                dash_length = (float)style_values.as_number(j);
            }
            if (style_prps[j] == input.animation_speed_prp)
            {
                animation_speed = (float)style_values.as_number(j);
            }
        }

        lm::vec4 fill_color_oklab =
            hrz::srgb_to_oklab(hrz::convert_byte_color_to_rgba(fill_color_srgb));
        lm::vec4 empty_color_oklab =
            hrz::srgb_to_oklab(hrz::convert_byte_color_to_rgba(empty_color_srgb));

        // This stores Oklab colours in 8-bit-per-channel vectors. It's not
        // great, and some precision is lost. (Usually only sRGB colours should
        // be reduced to 8 bits per channel.) However the colour interpolation
        // between these two colours in done with floats in the shader, so
        // the precision loss should be acceptable.
        lm::ubvec4 fill_color_oklab_uint8 = hrz::convert_rgba_color_to_bytes(fill_color_oklab);
        lm::ubvec4 empty_color_oklab_uint8 = hrz::convert_rgba_color_to_bytes(empty_color_oklab);

        switch (input.dash_mode)
        {
            case hrz_proto::DASH_DISABLED:
                has_transparency =
                    (fill_color_oklab_uint8.a != 255 && fill_color_oklab_uint8.a != 0);
                break;

            case hrz_proto::DASH_ENABLED_FILLED:
                has_transparency =
                    (fill_color_oklab_uint8.a != 255 && fill_color_oklab_uint8.a != 0)
                    || (empty_color_oklab_uint8.a != 255 && empty_color_oklab_uint8.a != 0);
                break;

            case hrz_proto::DASH_ENABLED_GRADIENT:
                has_transparency =
                    (fill_color_oklab_uint8.a != 255 || empty_color_oklab_uint8.a != 255);
                break;

            default: assert(false && "Unhandled case");
        }

        if (animation_speed != 0.0)
        {
            is_animated = true;
        }

        if (feature.point_count < 2) continue;

        uint32_t first_point = feature.first_point;
        uint32_t point_count = feature.point_count;

        std::span<const lm::dvec3> feature_span(
            ((const lm::dvec3*)input_points.data()) + first_point, point_count);

        points.clear();
        points.reserve(point_count);

        hrz::PointClampingGenerator point_clamps_gen =
            clamps_gen.for_feature(instance.feature_index, first_point);

        for (uint32_t p0 = 0; p0 < point_count; ++p0)
        {
            lm::dvec3 pp0 = feature_span[p0];
            pp0.z = point_clamps_gen.clamp_point(p0, pp0.z) + altitude_offset;
            points.push_back(pp0);
        }

        auto points_data_opt = points.data();
        if (!points_data_opt.has_value())
        {
            return hrz::JobResult::FAILURE;
        }
        auto points_data = points_data_opt.value();

        // Convert to Geocentric
        pl_transform_in_place_canonical(
            &hrz_proj::wmerc_to_ecef, points_data.size(), &points_data.data()->x);

        const uint32_t linestring_first_instance = instances.size().value_or(0);

        uint32_t linestring_first_point = 0;
        float progress = 0.0f;

        for (uint32_t i = 0; i < feature.linestring_count; ++i)
        {
            uint32_t linestring_index = i + feature.first_linestring_size;
            uint32_t linestring_size = input_linestring_sizes.at(linestring_index);

            std::span<const lm::dvec3> linestring_points =
                points_data.subspan(linestring_first_point, linestring_size);

            uint32_t segment_count = linestring_size - 1;

            for (int32_t p1 = 0; p1 < (int32_t)segment_count; ++p1)
            {
                lm::dvec3 pp0 = linestring_points[std::max(0, p1 - 1)] - center;
                lm::dvec3 pp1 = linestring_points[p1] - center;
                lm::dvec3 pp2 = linestring_points[p1 + 1] - center;
                lm::dvec3 pp3 =
                    linestring_points[std::min(p1 + 2, (int32_t)(linestring_points.size() - 1))]
                    - center;

                lm::vec3 n = lm::vec3(lm::normalize(pp2 - pp1));
                lm::vec3 n0 = hrz::vector_repr::compute_joint_normal(pp2, pp1, pp0).first;
                lm::vec3 n1 = hrz::vector_repr::compute_joint_normal(pp1, pp2, pp3).first;

                auto n0_oct = hrz::octahedral_compress_normal(n0);
                auto n1_oct = hrz::octahedral_compress_normal(n1);

                lm::vec3 c0 = lm::vec3(pp1);
                lm::vec3 c1 = lm::vec3(pp2);

                float progress0 = progress;
                progress += lm::length(c1 - c0);

                // Line total length will be written once all linestrings have been iterated
                instances.push_back(
                    {c0,
                     n0_oct,
                     c1,
                     n1_oct,
                     fill_color_oklab_uint8,
                     {radius, radius},
                     0.0f,
                     progress0,
                     progress,
                     dash_period,
                     dash_length,
                     animation_speed,
                     empty_color_oklab_uint8,
                     feature_index,
                     feature_id});

                // We add 0-length cylinders with one side that has 0 radius
                // to "close" the cylinders on both ends.
                if (has_caps && p1 == 0) // first point
                {
                    auto minus_n_oct = hrz::octahedral_compress_normal(-n);

                    instances.push_back(
                        {c0,
                         minus_n_oct,
                         c0,
                         minus_n_oct,
                         fill_color_oklab_uint8,
                         {radius, 0.0f},
                         0.0f,
                         progress0,
                         progress0,
                         dash_period,
                         dash_length,
                         animation_speed,
                         empty_color_oklab_uint8,
                         feature_index,
                         feature_id});
                }

                if (has_caps && p1 == (int32_t)segment_count - 1) // last point
                {
                    auto n_oct = hrz::octahedral_compress_normal(n);

                    instances.push_back(
                        {c1,
                         n_oct,
                         c1,
                         n_oct,
                         fill_color_oklab_uint8,
                         {radius, 0.0f},
                         0.0f,
                         progress,
                         progress,
                         dash_period,
                         dash_length,
                         animation_speed,
                         empty_color_oklab_uint8,
                         feature_index,
                         feature_id});
                }

                bsphere_point_buffer.push_back(c0);
                bsphere_point_buffer.push_back(c1);
            }

            linestring_first_point += linestring_size;
        }

        // All linestrings of the current feature have been iterated, so we know the total length
        for (uint32_t i = linestring_first_instance; i < instances.size().value_or(0); ++i)
        {
            instances.data().value()[i].line_total_length = progress;
        }
    }

    auto instances_array_opt = instances.to_blob_array();
    auto bsphere_point_buffer_data_opt = bsphere_point_buffer.data();
    if (!instances_array_opt.has_value() || !bsphere_point_buffer_data_opt.has_value())
    {
        return hrz::JobResult::FAILURE;
    }

    hrz::BSphere<float> bsphere = hrz::compute_bounding_sphere(
        std::span<const lm::vec3>(bsphere_point_buffer_data_opt.value()));

    bsphere.center += lm::vec3(center); // The points were offset by "center".

    // Finalize
    geometry.instance_data = std::move(instances_array_opt.value());
    geometry.center = center;
    geometry.bsphere_center = lm::dvec3(bsphere.center);
    geometry.bsphere_radius = bsphere.radius;
    geometry.has_transparency = has_transparency;
    geometry.is_animated = is_animated;

    geometry.instance_data.register_blob_metadata(
        context.get_blob_allocator(), "contents"_ss, "cylinder instances"_ss);
    geometry.instance_data.register_blob_owner(
        context.get_blob_allocator(), context.get_resource_owner());

    return hrz::JobResult::SUCCESS;
}

} // namespace hrz_jobs::bake_cylinder_vector_geometry
