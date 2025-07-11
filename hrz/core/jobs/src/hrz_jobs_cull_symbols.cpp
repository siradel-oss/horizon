#include "hrz_jobs_declarations.h"

#include <hrz_common_blob_vector.h>
#include <hrz_common_horizon_culling.h>
#include <hrz_common_maths.h>
#include <hrz_common_profiling.h>
#include <hrz_common_vector_tiles.h>
#include <hrz_fnd_kdtree.h>
#include <hrz_fnd_log.h>

#define USE_KDTREE 1

namespace hrz_jobs::cull_symbols
{
namespace
{
struct TransformedAnchor
{
    lm::bbox2 screen_bbox;
    float depth;
    lm::dvec3 horizon_occlusion_point;
};
} // namespace

TransformedAnchor transform_anchor(
    const hrz::vt::SymbolCullingParams::ViewInfo& view,
    const hrz::vt::SymbolCullingParams::Group& group,
    const hrz::vt::BakedSymbols::AnchorCulling& anchor)
{
    lm::mat4 view_cc(view.view_cc);
    const auto& anchor_proto = group.anchor_protos[anchor.anchor_prototype_index];

    lm::vec3 anchor_pos_cc((anchor.in_tile_position + group.origin) - view.position);

    lm::vec4 anchor_pos_view = view_cc * lm::vec4(anchor_pos_cc, 1);

    float distance_to_anchor = std::max(0.0f, -anchor_pos_view.z);
    float relative_scale = hrz::clamp(
        anchor_proto.reference_distance / distance_to_anchor, anchor_proto.min_relative_scale,
        anchor_proto.max_relative_scale);

    float anchor_pixel_scale_factor = view.pixel_size_in_meters * distance_to_anchor;

    float global_pixel_scale_factor = hrz::clamp(
        view.pixel_size_in_meters * view.perceived_distance,
        anchor_pixel_scale_factor * anchor_proto.min_relative_scale,
        anchor_pixel_scale_factor * anchor_proto.max_relative_scale);

    float pos_offset_scale_factor = 1.0;
    switch ((anchor_proto.flags & hrz::vt::AnchorFlag_PosOffsetUnitMask)
            >> hrz::vt::AnchorFlag_PosOffsetUnitShift)
    {
        case hrz::vt::AnchorFlag_Unit_Pixels:
            pos_offset_scale_factor = anchor_pixel_scale_factor;
            break;
        case hrz::vt::AnchorFlag_Unit_PixelsRelativeToAnchorDistance:
            pos_offset_scale_factor = anchor_pixel_scale_factor * relative_scale;
            break;
        case hrz::vt::AnchorFlag_Unit_PixelsRelativeToCameraHeight:
            pos_offset_scale_factor = global_pixel_scale_factor;
            break;
    }
    anchor_pos_view += view_cc * lm::vec4(anchor.position_offset * pos_offset_scale_factor, 0.0);

    float element_scale_factor = 1.0;
    switch ((anchor_proto.flags & hrz::vt::AnchorFlag_ElementSizeUnitMask)
            >> hrz::vt::AnchorFlag_ElementSizeUnitShift)
    {
        case hrz::vt::AnchorFlag_Unit_Pixels:
            element_scale_factor = anchor_pixel_scale_factor;
            break;

        case hrz::vt::AnchorFlag_Unit_PixelsRelativeToAnchorDistance:
            element_scale_factor = anchor_pixel_scale_factor * relative_scale;
            break;

        case hrz::vt::AnchorFlag_Unit_PixelsRelativeToCameraHeight:
            element_scale_factor = global_pixel_scale_factor;
            break;
    }

    lm::vec3 canvas_x_axis = lm::vec3(1, 0, 0);
    lm::vec3 canvas_y_axis = lm::vec3(0, -1, 0);

    bool align_x_to_screen = anchor_proto.flags & hrz::vt::AnchorFlag_AlignXAxisToScreen;
    bool align_y_to_screen = anchor_proto.flags & hrz::vt::AnchorFlag_AlignYAxisToScreen;
    if (!align_x_to_screen || !align_y_to_screen)
    {
        lm::vec3 local_east_axis_view = (view_cc * lm::vec4(anchor.local_east_axis, 0)).xyz;
        lm::vec3 local_up_axis_view = (view_cc * lm::vec4(anchor.local_up_axis, 0)).xyz;

        if (align_x_to_screen)
        {
            canvas_x_axis = lm::vec3(1, 0, 0);
            canvas_y_axis = lm::cross(lm::cross(local_up_axis_view, canvas_x_axis), canvas_x_axis);
        }
        else if (align_y_to_screen)
        {
            canvas_x_axis = lm::normalize(lm::vec3(local_east_axis_view.xy, 0));
            canvas_y_axis = lm::vec3(canvas_x_axis.y, -canvas_x_axis.x, 0);
        }
        else
        {
            canvas_x_axis = local_east_axis_view;
            canvas_y_axis = -local_up_axis_view;
        }
    }

    lm::mat3 transform = lm::mat3(canvas_x_axis, canvas_y_axis, cross(canvas_y_axis, canvas_x_axis))
        * lm::mat3(hrz::euler_angles_xyz_to_mat4(anchor.rotation)) * element_scale_factor;

    if (anchor_proto.flags & hrz::vt::AnchorFlag_KeepUpright)
    {
        lm::mat4 proj(view.proj);

        // We project the X and Y axes of the symbol on the screen to detect if it
        // will appear upside down and flipped so we can correct that.
        lm::vec4 canvas_anchor_proj = proj * anchor_pos_view;
        lm::vec4 canvas_x_proj = proj * lm::vec4(anchor_pos_view.xyz + transform.x, 1);
        lm::vec4 canvas_y_proj = proj * lm::vec4(anchor_pos_view.xyz + transform.y, 1);
        lm::vec3 canvas_anchor_clip = canvas_anchor_proj.xyz / canvas_anchor_proj.w;
        lm::vec2 canvas_x_dir_clip = canvas_x_proj.xy / canvas_x_proj.w - canvas_anchor_clip.xy;
        lm::vec2 canvas_y_dir_clip = canvas_y_proj.xy / canvas_y_proj.w - canvas_anchor_clip.xy;

        // This checks the orientation of the Z axis of the basis with those two vectors as x and y.
        // Essentially We check cross(x, y) > 0.
        if (canvas_x_dir_clip.x * canvas_y_dir_clip.y > canvas_x_dir_clip.y * canvas_y_dir_clip.x)
        {
            // Flipped! Invert the X axis.
            transform.x = -transform.x;
        }
        else if (canvas_x_dir_clip.x < 0.0)
        {
            // Upside down, rotate by 180° on the symbol's Z axis.
            transform.x = -transform.x;
            transform.y = -transform.y;
        }
    }

    TransformedAnchor out;
    out.screen_bbox = lm::bbox2::invalid();
    out.depth = 2.0;

    for (int i = 0; i < 4; ++i)
    {
        auto corner = lm::corner(anchor.rect, i);
        auto corner_view = anchor_pos_view + lm::vec4(transform * lm::vec3(corner, 0), 0);
        auto corner_clip = lm::mat4(view.proj) * corner_view;
        if (corner_clip.z > corner_clip.w || corner_clip.z < -corner_clip.w)
        {
            out.screen_bbox = {};
            return out;
        }
        corner_clip.xyz /= corner_clip.w;
        out.screen_bbox = lm::expand(out.screen_bbox, corner_clip.xy);
    }

    lm::vec4 center_view =
        anchor_pos_view + lm::vec4(transform * lm::vec3(lm::center(anchor.rect), 0), 0);
    lm::vec4 center_clip = lm::mat4(view.proj) * center_view;
    out.depth = (float)(std::log(center_clip.w / HRZ_S_NEAR) / std::log(HRZ_S_FAR / HRZ_S_NEAR));
    out.horizon_occlusion_point = (lm::dvec3)(view.inv_view * center_view).xyz;

    return out;
}

bool cull_one_view(
    gsl::span<const hrz::vt::SymbolCullingParams::Group> groups,
    const hrz::vt::SymbolCullingParams::ViewInfo& view_info,
    hrz::vt::SymbolCullingResponse& response,
    const JobContext& context)
{
    static const lm::bbox2 screen_bbox{{-1, -1}, {1, 1}};

    hrz::HorizonCuller horizon_culler(view_info.position);

    enum
    {
        Instance_Handled = 1,
        Instance_CanOverlap = 2,
        Instance_HidesOthers = 4,
        Instance_Optional = 8,
    };

    struct Instance
    {
        lm::bbox2 rect;
        uint16_t z_index;
        float priority;
        uint32_t out_group_index;
        uint32_t index_in_group;
        uint32_t span;
        uint8_t flags;
    };

    std::vector<std::pair<uint32_t, float>> instance_depths;
    std::vector<std::pair<uint32_t, uint32_t>> spans;
    std::vector<Instance> instances;

    uint32_t total_bitset_bucket_count = 0;
    const uint32_t view_bit = 1u << view_info.view_index;

    // All bitsets for all groups are grouped in a single big uint32 BlobArray.
    // Groups are aligned by 32 instances so that later they can be memcpy-ed directly
    // into the bitset textures.
    for (const auto& group : groups)
    {
        if ((group.visible_in_views & view_bit) == 0) continue;
        total_bitset_bucket_count += (group.anchors.size() + 31) / 32;
    }

    hrz::BlobArray<uint32_t> all_bitsets_array;
    {
        hrz::BlobVector<uint32_t> all_bitsets_vec(
            context.get_blob_allocator(), total_bitset_bucket_count);
        all_bitsets_vec.resize(total_bitset_bucket_count);
        all_bitsets_vec.register_blob_owner(context.get_resource_owner());
        all_bitsets_vec.register_blob_metadata("content"_ss, "symbol visibility bitsets"_ss);

        auto all_bitsets_array_opt = all_bitsets_vec.to_blob_array();
        if (!all_bitsets_array_opt)
        {
            HRZ_LOG_ERROR(
                "Couldn't allocate bitset of size {} for symbols culling",
                total_bitset_bucket_count);
            return false;
        }
        all_bitsets_array = all_bitsets_array_opt.value();
    }

    response.view_bitsets[view_info.view_index] = all_bitsets_array;

    auto all_bitsets = all_bitsets_array.get_mutable_data();
    memset(all_bitsets.data(), 0, all_bitsets.size_bytes());
    total_bitset_bucket_count = 0;

    // First pass to gather all instances and prepare the visibility bitsets
    for (const auto& group : groups)
    {
        if ((group.visible_in_views & view_bit) == 0)
        {
            hrz::vt::SymbolCullingResponse::GroupId out_group;
            out_group.handle = group.handle;
            out_group.scene_view = view_info.view_index;
            response.groups_to_reset.push_back(std::move(out_group));
            continue;
        }

        {
            // We rely on the order of groups visited being the same as the for loop above so that
            // the spans in the bitset BlobArray are correct.
            size_t group_bitset_bucket_count = (group.anchors.size() + 31) / 32;
            uint32_t group_first_bitset_bucket = total_bitset_bucket_count;
            total_bitset_bucket_count += group_bitset_bucket_count;

            hrz::vt::SymbolCullingResponse::Group out_group;
            out_group.handle = group.handle;
            out_group.scene_view = view_info.view_index;
            out_group.first_bitset_bucket = group_first_bitset_bucket;
            out_group.bitset_bucket_count = group_bitset_bucket_count;
            out_group.ones_bbox = lm::ubbox2::invalid();
            response.groups_to_update.push_back(std::move(out_group));
        }

        uint32_t out_group_index = response.groups_to_update.size() - 1;

        auto anchors_data = group.anchors.get_cdata();
        auto anchors = anchors_data.as_span();

        auto anchor_spans_data = group.anchor_spans.get_cdata();
        auto anchor_spans = anchor_spans_data.as_span();

        for (const auto& span : anchor_spans)
        {
            uint32_t span_index = spans.size();
            uint32_t first_instance = instances.size();
            uint32_t instance_count = 0;

            for (uint32_t i = 0; i < span.anchor_count; ++i)
            {
                uint32_t index = span.first_anchor + i;
                auto transformed = transform_anchor(view_info, group, anchors[index]);

                if (transformed.depth < 1 && lm::intersect(screen_bbox, transformed.screen_bbox)
                    && (!horizon_culler.is_occluded(transformed.horizon_occlusion_point)))
                {
                    instance_depths.push_back(
                        std::make_pair(first_instance + instance_count, transformed.depth));

                    const auto& anchor_proto =
                        group.anchor_protos[anchors[index].anchor_prototype_index];

                    uint8_t flags = 0;
                    if (anchor_proto.flags & hrz::vt::AnchorFlag_CanOverlapOtherSymbols)
                        flags |= Instance_CanOverlap;
                    if (anchor_proto.flags & hrz::vt::AnchorFlag_IsOptional)
                        flags |= Instance_Optional;
                    if (anchor_proto.flags & hrz::vt::AnchorFlag_HidesOtherSymbols)
                        flags |= Instance_HidesOthers;

                    Instance instance{
                        transformed.screen_bbox,
                        group.z_index,
                        anchors[index].priority,
                        out_group_index,
                        index,
                        span_index,
                        flags};
                    instances.push_back(instance);

                    instance_count += 1;
                }
            }

            if (instance_count > 0)
            {
                spans.push_back(std::make_pair(first_instance, instance_count));
            }
        }
    }

    assert(instance_depths.size() == instances.size());

    int shown = 0;
    auto show = [&](const Instance& instance)
    {
        shown += 1;
        auto& out_group = response.groups_to_update[instance.out_group_index];

        uint32_t pixel = instance.index_in_group / 32u;
        uint32_t bit = instance.index_in_group % 32u;

        assert(pixel < out_group.bitset_bucket_count);
        all_bitsets[out_group.first_bitset_bucket + pixel] |= 1u << bit;

        static constexpr uint32_t BITSET_TEXTURE_WIDTH =
            hrz::vt::SymbolCullingResponse::BITSET_TEXTURE_WIDTH;

        lm::uvec2 position{pixel % BITSET_TEXTURE_WIDTH, pixel / BITSET_TEXTURE_WIDTH};
        out_group.ones_bbox = lm::expand(out_group.ones_bbox, position);
    };

    // Second pass does the actual culling and updates the bitsets
    if (instances.size() > 0)
    {
        std::sort(
            instance_depths.begin(), instance_depths.end(),
            [&](const std::pair<uint32_t, float>& a, const std::pair<uint32_t, float>& b)
            {
                const auto& instance_a = instances.at(a.first);
                const auto& instance_b = instances.at(b.first);

                if (instance_a.z_index != instance_b.z_index)
                {
                    return instance_a.z_index > instance_b.z_index;
                }
                else if (instance_a.priority != instance_b.priority)
                {
                    return instance_a.priority > instance_b.priority;
                }
                else if (a.second != b.second)
                {
                    return a.second < b.second;
                }
                else
                {
                    return (instance_a.rect.min.x != instance_b.rect.min.x)
                        ? instance_a.rect.min.x < instance_b.rect.min.x
                        : (instance_a.rect.min.y != instance_b.rect.min.y)
                        ? instance_a.rect.min.y < instance_b.rect.min.y
                        : (instance_a.rect.max.x != instance_b.rect.max.x)
                        ? instance_a.rect.max.x < instance_b.rect.max.x
                        : instance_a.rect.max.y < instance_b.rect.max.y;
                }
            });

#if USE_KDTREE
        hrz::Kdtree occluders;
#else
        std::vector<lm::bbox2> occluders;
#endif
        std::vector<bool> can_draw;

        for (const auto& instance_depth : instance_depths)
        {
            const auto& instance = instances[instance_depth.first];
            if (instance.flags & Instance_Handled) continue;

            std::pair<uint32_t, uint32_t> span = spans[instance.span];

            bool hide_all = false;
            bool draw_any = false;
            can_draw.clear();
            can_draw.resize(span.second, false);

            for (uint32_t i = 0; i < span.second; ++i)
            {
                auto& instance = instances[span.first + i];
                instance.flags |= Instance_Handled;

                if (hide_all) continue;

                bool optional = instance.flags & Instance_Optional;
                bool draw = true;
                if (lm::any(lm::size(instance.rect) <= view_info.small_size))
                {
                    optional = true;
                    draw = false;
                }
                else if (!(instance.flags & Instance_CanOverlap))
                {
#if USE_KDTREE
                    if (occluders.intersects(instance.rect))
                    {
                        draw = false;
                    }
#else
                    for (const auto& d : occluders)
                    {
                        if (lm::intersect(d, instance.rect))
                        {
                            draw = false;
                            break;
                        }
                    }
#endif
                }

                if (draw)
                {
                    can_draw[i] = true;
                    draw_any = true;
                }
                else if (!optional)
                {
                    hide_all = true;
                }
            }

            if (!hide_all && draw_any)
            {
                for (uint32_t i = 0; i < span.second; ++i)
                {
                    if (!can_draw[i]) continue;

                    const auto& instance = instances[span.first + i];
                    show(instance);

                    if (instance.flags & Instance_HidesOthers)
                    {
#if USE_KDTREE
                        occluders.insert(instance.rect);
#else
                        occluders.push_back(instance.rect);
#endif
                    }
                }
            }
        }
    }

    return true;
}

hrz::JobResult run(
    const hrz::vt::SymbolCullingParams& params,
    hrz::vt::SymbolCullingResponse& response,
    const JobContext& context)
{
    HRZ_SCOPED_SAMPLE("cull symbols");

    for (const auto& view : params.views)
    {
        if (!cull_one_view(params.groups, view, response, context))
        {
            return hrz::JobResult::FAILURE;
        }
    }

    response.frame = params.frame;

    return hrz::JobResult::SUCCESS;
}
} // namespace hrz_jobs::cull_symbols
