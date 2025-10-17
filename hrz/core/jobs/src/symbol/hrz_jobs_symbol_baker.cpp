#include "symbol/hrz_jobs_symbol_baker.h"

#include "hrz_jobs_vector_repr_common.h"

#include <hrz_common_geo.h>
#include <hrz_common_maths.h>
#include <hrz_fnd_log.h>

namespace hrz_jobs::symbol
{
static constexpr size_t InitialAnchorCapacity = 256;

lm::bbox2 transform_rect_2d_no_rotation(lm::bbox2 rect, const lm::mat4& transform)
{
    lm::vec2 a = (transform * lm::vec4(rect.min, 0, 1)).xy;
    lm::vec2 b = (transform * lm::vec4(rect.max, 0, 1)).xy;
    return lm::merge(a, b);
}

lm::bbox2 transform_rect_2d_offset(lm::bbox2 rect, lm::vec2 offset)
{
    return lm::bbox2{
        rect.min + offset,
        rect.max + offset,
    };
}

lm::bbox2 transform_rect_2d(lm::bbox2 rect, const lm::mat4& transform)
{
    lm::vec2 pts[4] = {
        (transform * lm::vec4(lm::corner(rect, 0), 0, 1)).xy,
        (transform * lm::vec4(lm::corner(rect, 1), 0, 1)).xy,
        (transform * lm::vec4(lm::corner(rect, 2), 0, 1)).xy,
        (transform * lm::vec4(lm::corner(rect, 3), 0, 1)).xy,
    };
    return lm::merge(lm::merge(pts[0], pts[1]), lm::merge(pts[2], pts[3]));
}

lm::bbox2 transform_rect_3d(lm::bbox2 rect, const lm::mat4& transform)
{
    lm::vec4 pts[4] = {
        transform * lm::vec4(lm::corner(rect, 0), 0, 1),
        transform * lm::vec4(lm::corner(rect, 1), 0, 1),
        transform * lm::vec4(lm::corner(rect, 2), 0, 1),
        transform * lm::vec4(lm::corner(rect, 3), 0, 1),
    };
    for (auto& pt : pts)
    {
        pt.xy /= pt.w;
    }
    return lm::merge(lm::merge(pts[0].xy, pts[1].xy), lm::merge(pts[2].xy, pts[3].xy));
}

SymbolBaker::SymbolBaker(const hrz::vt::SymbolBakingData& params, const JobContext& context) :
    context(context),
    placeholder_visitor(this),
    anchor_visitor(this),
    stack_visitor(this),
    stack_expand_visitor(this),
    image_visitor(this),
    padding_visitor(this),
    sized_box_visitor(this),
    flex_visitor(this),
    flexible_visitor(this),
    constrained_box_visitor(this),
    rotated_box_visitor(this),
    decorated_shape_visitor(this),
    aspect_ratio_visitor(this),
    fitted_box_visitor(this),
    transform_visitor(this),
    text_visitor(this),
    optional_visitor(this),
    variant_visitor(this),
    leader_line_visitor(this),
    params(params),
    input_features(params.geometry.features.get_data()),
    input_points(params.geometry.points.get_data()),
    input_feature_ids(params.feature_ids.get_data()),
    input_clamps(params.clamps.get_data()),
    style(params.style),
    style_prps(style.prps.get_data()),
    style_values{style.values.get_data(), style.out_of_line_data.get_data()},
    clamps_gen(input_clamps.as_span(), params.clamping),
    anchor_count(0),
    anchor_gpu_data(context.get_blob_allocator(), InitialAnchorCapacity),
    anchor_culling_data(context.get_blob_allocator(), InitialAnchorCapacity),
    anchor_spans_data(context.get_blob_allocator(), params.style.instances.size()),
    anchor_positions(context.get_blob_allocator(), InitialAnchorCapacity),
    max_feature_index(0)
{
    element_visitors[hrz_proto::PLACEHOLDER_SYMBOL_ELEMENT] = &placeholder_visitor;
    element_visitors[hrz_proto::ANCHOR_SYMBOL_ELEMENT] = &anchor_visitor;
    element_visitors[hrz_proto::STACK_SYMBOL_ELEMENT] = &stack_visitor;
    element_visitors[hrz_proto::IMAGE_SYMBOL_ELEMENT] = &image_visitor;
    element_visitors[hrz_proto::PADDING_SYMBOL_ELEMENT] = &padding_visitor;
    element_visitors[hrz_proto::FLEX_SYMBOL_ELEMENT] = &flex_visitor;
    element_visitors[hrz_proto::SIZED_BOX_SYMBOL_ELEMENT] = &sized_box_visitor;
    element_visitors[hrz_proto::FLEXIBLE_SYMBOL_ELEMENT] = &flexible_visitor;
    element_visitors[hrz_proto::STACK_EXPAND_SYMBOL_ELEMENT] = &stack_expand_visitor;
    element_visitors[hrz_proto::ALIGN_SYMBOL_ELEMENT] = &fitted_box_visitor;
    element_visitors[hrz_proto::CONSTRAINED_BOX_SYMBOL_ELEMENT] = &constrained_box_visitor;
    element_visitors[hrz_proto::ROTATED_BOX_SYMBOL_ELEMENT] = &rotated_box_visitor;
    element_visitors[hrz_proto::DECORATED_SHAPE_SYMBOL_ELEMENT] = &decorated_shape_visitor;
    element_visitors[hrz_proto::ASPECT_RATIO_SYMBOL_ELEMENT] = &aspect_ratio_visitor;
    element_visitors[hrz_proto::FITTED_BOX_SYMBOL_ELEMENT] = &fitted_box_visitor;
    element_visitors[hrz_proto::TRANSFORM_SYMBOL_ELEMENT] = &transform_visitor;
    element_visitors[hrz_proto::TEXT_SYMBOL_ELEMENT] = &text_visitor;
    element_visitors[hrz_proto::OPTIONAL_SYMBOL_ELEMENT] = &optional_visitor;
    element_visitors[hrz_proto::VARIANT_SYMBOL_ELEMENT] = &variant_visitor;
    element_visitors[hrz_proto::LEADER_LINE_SYMBOL_ELEMENT] = &leader_line_visitor;
}

ElementGeometry SymbolBaker::visit_element(
    uint32_t element_index,
    const SizeConstraints& constraints)
{
    assert(element_index < params.elements.size());

    current_element_index = element_index;

    const auto& element = params.elements[element_index];
    auto geometry = get_element_visitor_for_type(element.type)->visit_element(element, constraints);
    element_instance_info[element_index].geometry = geometry;

    return geometry;
}

hrz::JobResult SymbolBaker::bake(hrz::vt::BakedSymbols& baked_symbols)
{
    hrz::vector_repr::compute_tile_radius_center(
        params.geometry.bounds, &tile_radius, &tile_center);

    if (params.elements.empty()) return hrz::JobResult::SUCCESS;

    for (auto visitor : element_visitors)
    {
        if (visitor.second->init() != hrz::JobResult::SUCCESS)
        {
            return hrz::JobResult::FAILURE;
        }
    }

    for (size_t i = 0; i < params.elements.size(); ++i)
    {
        const auto& element = params.elements[i];

        if (element.z_index.has_value())
        {
            if (get_element_visitor_for_type(element.type)->init_element_instances(element)
                != hrz::JobResult::SUCCESS)
            {
                return hrz::JobResult::FAILURE;
            }
        }
    }
    std::optional<lm::dbbox2> clipping_bbox = std::nullopt;
    if (params.clip_to_tile)
    {
        clipping_bbox = {hrz::mercator_tile_bbox_meters(params.tile_coords)};
    }

    for (const auto& instance : style.instances.get_data())
    {
        if (instance.repr_id != params.repr_id) continue;

        const auto& feature = input_features.at(instance.feature_index);

        feature_id = input_feature_ids.at(instance.feature_index);
        feature_index = std::min(instance.feature_index, hrz::vt::MAX_FEATURE_INDEX);
        max_feature_index = std::max(max_feature_index, feature_index);

        auto web_mercator_feature_position = input_points.as_span()[feature.first_point];
        if (feature.type != hrz_proto::VectorGeometryType::POINT_GEOMETRY)
        {
            web_mercator_feature_position.x = feature.anchor.x;
            web_mercator_feature_position.y = feature.anchor.y;
        }

        if (clipping_bbox.has_value()
            && !lm::contains(clipping_bbox.value(), web_mercator_feature_position.xy))
        {
            continue;
        }

        hrz::PointClampingGenerator point_clamps_gen =
            clamps_gen.for_feature(instance.feature_index, feature.first_point);
        web_mercator_feature_position.z =
            point_clamps_gen.clamp_point(0, web_mercator_feature_position.z);

        feature_position = hrz::web_mercator_to_ecef(
            web_mercator_feature_position.xy, web_mercator_feature_position.z);

        feature_prp_start = instance.first_prp;
        feature_prp_end = instance.first_prp + instance.prp_count;
        current_element_index = 0;
        element_instance_info.clear();
        element_instance_info.resize(params.elements.size());
        anchor_indices_to_baked_anchor_indices.clear();

        // Start with unconstrained dimensions.
        SizeConstraints root_element_constraints;
        root_element_constraints.min = {0, 0};
        root_element_constraints.max = {infinity(), infinity()};

        AnchorSpan anchor_span{};
        anchor_span.first_anchor = anchor_count;
        auto whole_symbol_size = visit_element(0, root_element_constraints).layout_size;
        anchor_span.anchor_count = anchor_count - anchor_span.first_anchor;
        if (anchor_span.anchor_count > 0)
        {
            anchor_spans_data.push_back(anchor_span);
        }

        // All the elements now have transforms, but they're local transforms.
        // We need to transform them to global transforms. (In the context of
        // symbol layouting. They're not world transforms!)
        // They each have the index of their parent transform, and the hierarchy
        // guarantees that the parent is before the child in the array. So we
        // can compute global transforms by going through the array in order.
        //
        // This is also the moment when we can translate components in order to
        // place them correctly wrt the anchors' alignment properties. It could
        // not be performed earlier as it needs the final size of the symbol.
        //
        // Finally, all the anchor indices are indices among anchors in the
        // representation. They must be translated to baked anchor instance
        // indices using the mapping that has been maintained each time an
        // anchor has been
        // pushed.
        for (size_t i = 0; i < params.elements.size(); ++i)
        {
            const auto& element = params.elements[i];
            auto& instance_info = element_instance_info[i];

            bool apply_parent_transform = true;
            bool is_anchor = element.type == hrz_proto::SymbolElementType::ANCHOR_SYMBOL_ELEMENT;

            if (is_anchor)
            {
                const auto& params = element.anchor();

                lm::vec2 alignment = params.default_element_alignment;
                load_vec2f_property(params.element_alignment_prp, &alignment);

                alignment = alignment * 0.5f + lm::vec2(0.5f);

                // Compute anchor alignment based on only its subtree or the whole symbol, based on
                // whether we reset the layout at the anchor or not. We also transform the visual
                // rectangle used for culling.
                if (params.reset_layout)
                {
                    instance_info.transform = lm::mat4::identity();

                    const auto& child_geometry = element_instance_info[params.child_index].geometry;
                    lm::vec2 offset = -child_geometry.layout_size * alignment;

                    instance_info.geometry.layout_size = child_geometry.layout_size;

                    instance_info.geometry.visual_rect =
                        transform_rect_2d_offset(child_geometry.visual_rect, offset);

                    element_instance_info[params.child_index].transform =
                        lm::translation(lm::vec3(offset, 0.0f));

                    apply_parent_transform = false;
                }
                else
                {
                    lm::vec2 offset = -whole_symbol_size * alignment;

                    instance_info.geometry.visual_rect =
                        transform_rect_2d_offset(instance_info.geometry.visual_rect, offset);

                    element_instance_info[params.child_index].transform =
                        lm::translation(lm::vec3(offset, 0.0f));
                }
            }

            if (apply_parent_transform && element.parent_index.has_value())
            {
                instance_info.transform =
                    element_instance_info[element.parent_index.value()].transform
                    * instance_info.transform;
            }

            if (is_anchor)
            {
                // Don't forget to apply parent transforms to the visual rectangle, useful for
                // anchors that don't reset layout!
                instance_info.geometry.visual_rect =
                    transform_rect_3d(instance_info.geometry.visual_rect, instance_info.transform);

                // Here we write the visual rectangle to the culling data if the anchor is valid.
                if (anchor_indices_to_baked_anchor_indices.size() > element.anchor_index.value())
                {
                    auto anchor_index =
                        anchor_indices_to_baked_anchor_indices[element.anchor_index.value()];
                    auto data = anchor_culling_data.data();
                    if (anchor_index && data)
                    {
                        data.value()[anchor_index.value()].rect =
                            instance_info.geometry.visual_rect;
                    }
                }
            }

            // Visual elements have created visual instances. They have a transform
            // as one of their properties. Every type of element has a different
            // layout for their instances (the blob contents are directly uploaded
            // to the GPU as a VBO), so each type must be handled separately.
            if (element.z_index.has_value() && instance_info.element_instance_index.has_value())
            {
                auto z_index = element.z_index.value();
                auto element_instance_index = instance_info.element_instance_index.value();

                get_element_visitor_for_type(element.type)
                    ->finalize_element_instance(
                        z_index, element_instance_index, instance_info.transform);
            }
        }
    }

    auto anchor_gpu_data_size_opt = anchor_gpu_data.size();
    if (!anchor_gpu_data_size_opt.has_value())
    {
        return hrz::JobResult::FAILURE;
    }

    auto anchor_gpu_data_texture_size =
        hrz::vt::compute_data_texture_size(anchor_gpu_data_size_opt.value());
    auto anchor_gpu_data_texture_entries =
        anchor_gpu_data_texture_size.x * anchor_gpu_data_texture_size.y;

    // Anchor data will be stored in a data texture, and we don't want GL to read random pixels
    // when initializing it.
    assert(anchor_gpu_data_texture_entries >= anchor_gpu_data_size_opt.value());
    anchor_gpu_data.resize(anchor_gpu_data_texture_entries);

    auto anchor_gpu_data_array_opt = anchor_gpu_data.to_blob_array();
    auto anchor_culling_data_array_opt = anchor_culling_data.to_blob_array();
    auto anchor_position_data_opt = anchor_positions.data();
    auto anchor_spans_data_array_opt = anchor_spans_data.to_blob_array();
    if (!anchor_gpu_data_array_opt.has_value() || !anchor_position_data_opt.has_value()
        || !anchor_culling_data_array_opt.has_value() || !anchor_spans_data_array_opt.has_value())
    {
        return hrz::JobResult::FAILURE;
    }

    hrz::BSphere<double> bsphere =
        hrz::compute_bounding_sphere(std::span<const lm::dvec3>(anchor_position_data_opt.value()));

    baked_symbols.anchor_gpu_data = std::move(anchor_gpu_data_array_opt.value());
    baked_symbols.anchor_culling_data = std::move(anchor_culling_data_array_opt.value());
    baked_symbols.anchor_spans = std::move(anchor_spans_data_array_opt.value());
    baked_symbols.max_feature_index = max_feature_index;
    baked_symbols.tile_center = tile_center;
    baked_symbols.bsphere_center = bsphere.center;
    baked_symbols.bsphere_radius = bsphere.radius;

    baked_symbols.anchor_gpu_data.register_blob_metadata(
        context.get_blob_allocator(), "contents"_ss, "anchors gpu data"_ss);

    baked_symbols.anchor_gpu_data.register_blob_owner(
        context.get_blob_allocator(), context.get_resource_owner());

    baked_symbols.anchor_culling_data.register_blob_metadata(
        context.get_blob_allocator(), "contents"_ss, "anchors culling data"_ss);

    baked_symbols.anchor_culling_data.register_blob_owner(
        context.get_blob_allocator(), context.get_resource_owner());

    baked_symbols.anchor_spans.register_blob_metadata(
        context.get_blob_allocator(), "contents"_ss, "anchors spans"_ss);

    baked_symbols.anchor_spans.register_blob_owner(
        context.get_blob_allocator(), context.get_resource_owner());

    // Every visual element type has its own GPU data format, so each case must
    // be handled specifically.
    for (size_t i = 0; i < params.elements.size(); ++i)
    {
        const auto& element = params.elements[i];

        if (element.z_index.has_value())
        {
            auto instances_opt = get_element_visitor_for_type(element.type)
                                     ->get_element_instances_at_z_index(element.z_index.value());

            if (!instances_opt.has_value())
            {
                HRZ_LOG_ERROR(
                    "Could not get instances at z-index {} of type {}", element.z_index.value(),
                    hrz_proto::SymbolElementType_Name(element.type));
                return hrz::JobResult::FAILURE;
            }

            baked_symbols.instances.push_back(std::move(instances_opt.value()));
        }
    }

    for (auto visitor : element_visitors)
    {
        visitor.second->deinit();
    }

    return hrz::JobResult::SUCCESS;
}
} // namespace hrz_jobs::symbol
