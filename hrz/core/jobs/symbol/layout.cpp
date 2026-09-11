// SPDX-FileCopyrightText: Copyright 2023 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/common/proto_maths.h"
#include "hrz/core/jobs/symbol/baker.h"
#include "hrz/fnd/inlined_vector.h"

namespace hrz_jobs::symbol
{

Size constrain_size_preserve_aspect_ratio(
    const SizeConstraints& constraints,
    Size size,
    float aspect_ratio)
{
    if (aspect_ratio <= 0) aspect_ratio = size.x / size.y;

    if (size.x > constraints.max.x)
    {
        size.x = constraints.max.x;
        size.y = size.x / aspect_ratio;
    }

    if (size.y > constraints.max.y)
    {
        size.y = constraints.max.y;
        size.x = size.y * aspect_ratio;
    }

    if (size.x < constraints.min.x)
    {
        size.x = constraints.min.x;
        size.y = size.x / aspect_ratio;
    }

    if (size.y < constraints.min.y)
    {
        size.y = constraints.min.y;
        size.x = size.y * aspect_ratio;
    }

    return lm::clamp(size, constraints.min, constraints.max);
}

Size constrain_box_fit(
    Size container_size,
    Size content_size,
    hrz_proto::BoxFit mode,
    hrz_proto::BoxFitAxes axes)
{
    auto fitted_size = content_size;

    if (axes == hrz_proto::BOX_FIT_AXES_WIDTH || axes == hrz_proto::BOX_FIT_AXES_BOTH)
    {
        fitted_size.x = container_size.x;
    }

    if (axes == hrz_proto::BOX_FIT_AXES_HEIGHT || axes == hrz_proto::BOX_FIT_AXES_BOTH)
    {
        fitted_size.y = container_size.y;
    }

    if (axes != hrz_proto::BOX_FIT_AXES_NONE && mode != hrz_proto::BOX_FIT_FILL)
    {
        lm::vec2 ratio = fitted_size / content_size;

        // Erase dimensions we don't care about
        if (axes == hrz_proto::BOX_FIT_AXES_WIDTH)
            ratio.y = ratio.x;
        else if (axes == hrz_proto::BOX_FIT_AXES_HEIGHT)
            ratio.x = ratio.y;

        switch (mode)
        {
            case hrz_proto::BOX_FIT_CONTAIN: ratio = lm::vec2(std::min(ratio.x, ratio.y)); break;
            case hrz_proto::BOX_FIT_COVER: ratio = lm::vec2(std::max(ratio.x, ratio.y)); break;
            case hrz_proto::BOX_FIT_SCALE_DOWN:
                ratio = lm::vec2(std::min(std::min(ratio.x, ratio.y), 1.0F));
                break;
            default: break;
        }

        fitted_size = content_size * ratio;
    }

    return fitted_size;
}

ElementGeometry SymbolBaker::StackVisitor::visit_element(
    const hrz_jobs::SymbolBakingData::Element& element,
    const SizeConstraints& constraints)
{
    assert(element.type == hrz_proto::SymbolElement::ElementTypeCase::kStack);
    const auto& params = element.stack();

    auto alignment = params.default_alignment;
    load_vec2f_property(params.alignment_prp, &alignment);
    alignment = alignment * 0.5F + lm::vec2(0.5F);

    Size max_child_size = {0, 0};

    hrz::InlinedVector<uint32_t, 8> expands;
    hrz::InlinedVector<uint32_t, 8> non_expands;

    for (auto child_index : params.child_indices)
    {
        if (get_child_type(child_index) == hrz_proto::SymbolElement::ElementTypeCase::kStackExpand)
        {
            expands.push_back(child_index);
        }
        else
        {
            non_expands.push_back(child_index);
        }
    }
    assert(expands.size() + non_expands.size() == params.child_indices.size());

    // First layout normal children, accumulating their sizes.

    for (auto child_index : non_expands)
    {
        auto child_size = visit_child(child_index, constraints);

        max_child_size.x = std::max(child_size.layout_size.x, max_child_size.x);
        max_child_size.y = std::max(child_size.layout_size.y, max_child_size.y);
    }

    lm::bbox2 visual_rect = lm::bbox2::invalid();

    for (auto child_index : non_expands)
    {
        auto child_size = get_child_size(child_index);
        auto child_rect = get_child_visual_rect(child_index);
        auto offset = (max_child_size - child_size) * alignment;
        set_child_local_transform(child_index, lm::translation(lm::vec3(offset, 0.0F)));
        visual_rect = lm::merge(visual_rect, transform_rect_2d_offset(child_rect, offset));
    }

    // Then give this size to Expand children
    SizeConstraints expand_constraints{max_child_size, max_child_size};

    for (auto child_index : expands)
    {
        // Since the constraint is tight, there is no need for further layouting.
        auto child_geometry = visit_child(child_index, expand_constraints);
        set_child_local_transform(child_index, lm::mat4::identity());
        visual_rect = lm::merge(visual_rect, child_geometry.visual_rect);
    }

    return ElementGeometry{max_child_size, visual_rect};
}

ElementGeometry SymbolBaker::StackExpandVisitor::visit_element(
    const hrz_jobs::SymbolBakingData::Element& element,
    const SizeConstraints& constraints)
{
    assert(element.type == hrz_proto::SymbolElement::ElementTypeCase::kStackExpand);
    const auto& params = element.stack_expand();

    auto child_geometry = visit_child(params.child_index, constraints);
    set_child_local_transform(params.child_index, lm::mat4::identity());

    return child_geometry;
}

ElementGeometry SymbolBaker::ConstrainedBoxVisitor::visit_element(
    const hrz_jobs::SymbolBakingData::Element& element,
    const SizeConstraints& constraints)
{
    assert(element.type == hrz_proto::SymbolElement::ElementTypeCase::kConstrainedBox);
    const auto& params = element.constrained_box();

    Size min_size = params.default_min_size;
    load_vec2f_property(params.min_size_prp, &min_size);

    Size max_size = params.default_max_size;
    load_vec2f_property(params.max_size_prp, &max_size);

    SizeConstraints constrained_constraints{
        lm::clamp(min_size, constraints.min, constraints.max),
        lm::clamp(max_size, constraints.min, constraints.max),
    };

    auto child_geometry = visit_child(params.child_index, constrained_constraints);
    set_child_local_transform(params.child_index, lm::mat4::identity());

    return child_geometry;
}

ElementGeometry SymbolBaker::RotatedBoxVisitor::visit_element(
    const hrz_jobs::SymbolBakingData::Element& element,
    const SizeConstraints& constraints)
{
    assert(element.type == hrz_proto::SymbolElement::ElementTypeCase::kRotatedBox);
    const auto& params = element.rotated_box();

    int64_t quarter_turns = params.default_quarter_turns;
    load_int_property(params.quarter_turns_prp, &quarter_turns);

    // Normalize between 0 and 3
    quarter_turns = ((quarter_turns % 4) + 4) % 4;

    SizeConstraints rotated_constraints = constraints;
    if (quarter_turns % 2 == 1)
    {
        std::swap(rotated_constraints.min.x, rotated_constraints.min.y);
        std::swap(rotated_constraints.max.x, rotated_constraints.max.y);
    }

    ElementGeometry child_geometry = visit_child(params.child_index, rotated_constraints);
    Size child_size = child_geometry.layout_size;

    lm::mat4 transform = lm::mat4::identity();
    switch (quarter_turns)
    {
        case 1:
        {
            transform = lm::translation(lm::vec3{child_size.y, 0, 0})
                * lm::mat4{{0, 1, 0, 0}, {-1, 0, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
            break;
        }
        case 2:
        {
            transform = lm::translation(lm::vec3{child_size.x, child_size.y, 0})
                * lm::mat4{{-1, 0, 0, 0}, {0, -1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
            break;
        }
        case 3:
        {
            transform = lm::translation(lm::vec3{0, child_size.x, 0})
                * lm::mat4{{0, -1, 0, 0}, {1, 0, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
            break;
        }
        default: break;
    }

    lm::bbox2 visual_rect = transform_rect_2d(child_geometry.visual_rect, transform);
    set_child_local_transform(params.child_index, transform);

    if (quarter_turns % 2 == 1)
    {
        return ElementGeometry{{child_size.y, child_size.x}, visual_rect};
    }
    else
    {
        return ElementGeometry{child_size, visual_rect};
    }
}

ElementGeometry SymbolBaker::PaddingVisitor::visit_element(
    const hrz_jobs::SymbolBakingData::Element& element,
    const SizeConstraints& constraints)
{
    assert(element.type == hrz_proto::SymbolElement::ElementTypeCase::kPadding);
    const auto& params = element.padding();

    float top_padding = params.default_top_padding;
    float bottom_padding = params.default_bottom_padding;
    float left_padding = params.default_left_padding;
    float right_padding = params.default_right_padding;

    load_float_property(params.top_padding_prp, &top_padding);
    load_float_property(params.bottom_padding_prp, &bottom_padding);
    load_float_property(params.left_padding_prp, &left_padding);
    load_float_property(params.right_padding_prp, &right_padding);

    float h_padding = std::min(left_padding + right_padding, constraints.max.x);
    float v_padding = std::min(top_padding + bottom_padding, constraints.max.y);

    SizeConstraints padding_constraints = constraints;
    padding_constraints.min.x = std::max(0.0F, constraints.min.x - h_padding);
    padding_constraints.max.x = constraints.max.x - h_padding;
    padding_constraints.min.y = std::max(0.0F, constraints.min.y - v_padding);
    padding_constraints.max.y = constraints.max.y - v_padding;

    ElementGeometry child_geometry = visit_child(params.child_index, padding_constraints);
    set_child_local_transform(
        params.child_index, lm::translation(lm::vec3(left_padding, top_padding, 0.0F)));

    lm::bbox2 visual_rect = child_geometry.visual_rect;
    // Translate by left/top then expand by -left/-top -> do nothing to min point
    visual_rect.max += lm::vec2(h_padding, v_padding);

    return ElementGeometry{
        child_geometry.layout_size + lm::vec2(h_padding, v_padding), visual_rect
    };
}

ElementGeometry SymbolBaker::SizedBoxVisitor::visit_element(
    const hrz_jobs::SymbolBakingData::Element& element,
    const SizeConstraints& constraints)
{
    assert(element.type == hrz_proto::SymbolElement::ElementTypeCase::kSizedBox);
    const auto& params = element.sized_box();

    lm::vec2 target_size = params.default_size;
    load_vec2f_property(params.size_prp, &target_size);

    Size sized_size = lm::clamp(target_size, constraints.min, constraints.max);

    ElementGeometry child_geometry = visit_child(params.child_index, {{sized_size}, {sized_size}});
    set_child_local_transform(params.child_index, lm::mat4::identity());

    return child_geometry;
}

ElementGeometry SymbolBaker::FlexVisitor::visit_element(
    const hrz_jobs::SymbolBakingData::Element& element,
    const SizeConstraints& constraints)
{
    assert(element.type == hrz_proto::SymbolElement::ElementTypeCase::kFlex);
    const auto& params = element.flex();

    // We will do all the math with main axis as x and cross as y.
    // These functions do the necessary conversions.

    auto size_to_flex_space = [&](const Size& v) -> Size
    { return params.main_axis == hrz_proto::FLEX_AXIS_HORIZONTAL ? v : lm::vec2{v.y, v.x}; };

    auto constraints_to_flex_space = [&](const SizeConstraints& c) -> SizeConstraints
    {
        return params.main_axis == hrz_proto::FLEX_AXIS_HORIZONTAL
            ? c
            : SizeConstraints{size_to_flex_space(c.min), size_to_flex_space(c.max)};
    };

    // It so happens that it's the same transform, but we use a different name so that the code is
    // clearer.
    auto size_to_symbol_space = size_to_flex_space;
    auto position_to_symbol_space = size_to_flex_space; // Size is lm::vec2, easy
    auto constraints_to_symbol_space = constraints_to_flex_space;

    SizeConstraints flex_constraints = constraints_to_flex_space(constraints);

    // This is the template for the constraints of the children. Only the max x size will be
    // modified.
    SizeConstraints child_constraints_tpl = {{0.0F, 0.0F}, {flex_constraints.max}};
    if (params.cross_axis_alignment == hrz_proto::FLEX_CROSS_AXIS_STRETCH)
    {
        child_constraints_tpl.min.y = flex_constraints.max.y;
    }

    // We accumulate the size of the children on the main axis, and max on the cross axis, as well
    // as the flex factor of flexible children
    Size children_size{0.0F, flex_constraints.min.y};
    float flexible_total = 0;

    auto is_flexible = [&](uint32_t index) -> bool
    {
        const auto& elmt = get_child(index);
        return elmt.type == hrz_proto::SymbolElement::ElementTypeCase::kFlexible
            && elmt.flexible().factor > 0;
    };

    auto layout_child = [&](uint32_t child_index, const SizeConstraints& constraints)
    {
        auto child_size = size_to_flex_space(
            visit_child(child_index, constraints_to_symbol_space(constraints)).layout_size);

        children_size.x += child_size.x;
        children_size.y = std::max(children_size.y, child_size.y);
    };

    // First layout non-flexible children, and accumulate the flex factors of flexible ones.
    for (auto child_index : params.child_indices)
    {
        if (is_flexible(child_index))
        {
            flexible_total += get_child(child_index).flexible().factor;
        }
        else
        {
            SizeConstraints child_constraints = child_constraints_tpl;
            child_constraints.max.x = std::max(0.0F, flex_constraints.max.x - children_size.x);
            layout_child(child_index, child_constraints);
        }
    }

    // Then visit the flexible children, giving them some proportion of remaining space.
    float free_space_for_flexible_elements =
        std::max(flex_constraints.max.x - children_size.x, 0.0F);
    for (auto child_index : params.child_indices)
    {
        if (is_flexible(child_index))
        {
            const auto& child = get_child(child_index).flexible();

            SizeConstraints child_constraints = child_constraints_tpl;
            child_constraints.max.x =
                free_space_for_flexible_elements * child.factor / flexible_total;
            if (child.fit == hrz_proto::FLEX_FIT_TIGHT)
            {
                child_constraints.min.x = max_finite(child_constraints.max.x, 0);
            }

            layout_child(child_index, child_constraints);
        }
    }

    // If we don't fill the minimum size on the main axis, we have some space to redistribute around
    // the elements based on the main axis alignment.
    float space_to_distribute = std::max(flex_constraints.min.x - children_size.x, 0.0F);

    // Position on the main axis that we'll accumulate
    float main_position = 0.0F;
    float main_space_between_children = 0.0F;
    const size_t children_count = params.child_indices.size();

    switch (params.main_axis_alignment)
    {
        case hrz_proto::FLEX_MAIN_AXIS_START: break;
        case hrz_proto::FLEX_MAIN_AXIS_END: main_position = space_to_distribute; break;
        case hrz_proto::FLEX_MAIN_AXIS_CENTER: main_position = space_to_distribute / 2; break;
        case hrz_proto::FLEX_MAIN_AXIS_SPACE_BETWEEN:
            if (children_count > 1)
            {
                main_space_between_children = space_to_distribute / (children_count - 1);
            }
            break;
        case hrz_proto::FLEX_MAIN_AXIS_SPACE_AROUND:
            if (children_count > 0)
            {
                main_space_between_children = space_to_distribute / children_count;
                main_position = main_space_between_children / 2;
            }
            break;
        case hrz_proto::FLEX_MAIN_AXIS_SPACE_EVENLY:
            main_space_between_children = space_to_distribute / (children_count + 1);
            main_position = main_space_between_children;
            break;
        default: assert(!"Unhandled case");
    }

    float cross_alignment = 0.0F;
    switch (params.cross_axis_alignment)
    {
        case hrz_proto::FLEX_CROSS_AXIS_START:
        case hrz_proto::FLEX_CROSS_AXIS_STRETCH: break;
        case hrz_proto::FLEX_CROSS_AXIS_END: cross_alignment = 1.0F; break;
        case hrz_proto::FLEX_CROSS_AXIS_CENTER: cross_alignment = 0.5F; break;
        default: assert(!"Unhandled case");
    }

    lm::bbox2 visual_rect = lm::bbox2::invalid();

    for (auto child_index : params.child_indices)
    {
        Size child_size = size_to_flex_space(get_child_size(child_index));
        float cross_position = (children_size.y - child_size.y) * cross_alignment;

        lm::vec2 offset = position_to_symbol_space({main_position, cross_position});
        set_child_local_transform(child_index, lm::translation(lm::vec3(offset, 0.0F)));
        visual_rect = lm::merge(
            visual_rect, transform_rect_2d_offset(get_child_visual_rect(child_index), offset));

        main_position += child_size.x + main_space_between_children;
    }

    Size size = lm::clamp(children_size, flex_constraints.min, flex_constraints.max);
    return ElementGeometry{size_to_symbol_space(size), visual_rect};
}

ElementGeometry SymbolBaker::FlexibleVisitor::visit_element(
    const hrz_jobs::SymbolBakingData::Element& element,
    const SizeConstraints& constraints)
{
    assert(element.type == hrz_proto::SymbolElement::ElementTypeCase::kFlexible);
    const auto& params = element.flexible();

    auto child_geometry = visit_child(params.child_index, constraints);
    set_child_local_transform(params.child_index, lm::mat4::identity());

    return child_geometry;
}

ElementGeometry SymbolBaker::AspectRatioVisitor::visit_element(
    const hrz_jobs::SymbolBakingData::Element& element,
    const SizeConstraints& constraints)
{
    assert(element.type == hrz_proto::SymbolElement::ElementTypeCase::kAspectRatio);
    const auto& params = element.aspect_ratio();

    float aspect_ratio = params.default_aspect_ratio;
    load_float_property(params.aspect_ratio_prp, &aspect_ratio);

    if (aspect_ratio <= 0.0F) aspect_ratio = 1.0F;

    float width, height;
    if (!std::isinf(constraints.max.x))
    {
        width = constraints.max.x;
        height = width / aspect_ratio;
    }
    else
    {
        height = constraints.max.y;
        width = height * aspect_ratio;
    }

    Size size = constrain_size_preserve_aspect_ratio(constraints, {width, height}, aspect_ratio);

    auto child_geometry = visit_child(params.child_index, {size, size});
    set_child_local_transform(params.child_index, lm::mat4::identity());

    return child_geometry;
}

ElementGeometry SymbolBaker::FittedBoxVisitor::visit_element(
    const hrz_jobs::SymbolBakingData::Element& element,
    const SizeConstraints& constraints)
{
    assert(
        element.type == hrz_proto::SymbolElement::ElementTypeCase::kFittedBox
        || element.type == hrz_proto::SymbolElement::ElementTypeCase::kAlign);
    const auto& params = element.fitted_box();

    auto alignment = params.default_alignment;
    load_vec2f_property(params.alignment_prp, &alignment);
    alignment = alignment * 0.5F + lm::vec2(0.5F);

    ElementGeometry child_geometry = visit_child(params.child_index, {{0, 0}, constraints.max});
    Size this_size = lm::clamp(child_geometry.layout_size, constraints.min, constraints.max);
    Size fitted_child_size = constrain_box_fit(
        this_size, child_geometry.layout_size, params.fit, hrz_proto::BOX_FIT_AXES_BOTH);

    lm::vec2 scale = fitted_child_size / child_geometry.layout_size;
    lm::vec2 offset = (this_size - fitted_child_size) * alignment;

    lm::mat4 transform = lm::translation(lm::vec3(offset, 0)) * lm::scaling(lm::vec3(scale, 0));
    set_child_local_transform(params.child_index, transform);

    lm::bbox2 visual_rect = transform_rect_2d_no_rotation(child_geometry.visual_rect, transform);

    return ElementGeometry{this_size, visual_rect};
}

ElementGeometry SymbolBaker::TransformVisitor::visit_element(
    const hrz_jobs::SymbolBakingData::Element& element,
    const SizeConstraints& constraints)
{
    using Transform = typename hrz_jobs::SymbolBakingData::Transform;

    assert(element.type == hrz_proto::SymbolElement::ElementTypeCase::kTransform);
    const auto& params = element.transform();

    auto origin = params.default_origin;
    load_vec2f_property(params.origin_prp, &origin);

    auto alignment = params.default_alignment;
    load_vec2f_property(params.alignment_prp, &alignment);
    alignment = alignment * 0.5F + lm::vec2(0.5F);

    ElementGeometry child_geometry = visit_child(params.child_index, constraints);

    origin += alignment * child_geometry.layout_size;

    lm::mat4 transform = lm::translation(lm::vec3(-origin, 0.0F));

    for (const auto& component : params.components)
    {
        transform = std::visit(
            hrz::overload{
                [&transform, this](const Transform::Translation& comp)
                {
                    lm::vec3 t = comp.default_translation;
                    load_vec3f_property(comp.translation_prp, &t);
                    return lm::translation(t) * transform;
                },
                [&transform, this](const Transform::Scaling& comp)
                {
                    lm::vec3 s = comp.default_scaling;
                    load_vec3f_property(comp.scaling_prp, &s);
                    return lm::scaling(s) * transform;
                },
                [&transform, this](const Transform::Rotation& comp)
                {
                    lm::vec3 r = comp.default_rotation;
                    load_vec3f_property(comp.rotation_prp, &r);
                    return hrz::euler_rotation(r, comp.order) * transform;
                },
                [&transform](const Transform::Generic& comp) { return comp.matrix * transform; }
            },
            component);
    }

    transform = lm::translation(lm::vec3(origin, 0.0F)) * transform;

    set_child_local_transform(params.child_index, transform);
    lm::bbox2 visual_rect = transform_rect_3d(child_geometry.visual_rect, transform);

    return ElementGeometry{child_geometry.layout_size, visual_rect};
}

ElementGeometry SymbolBaker::OptionalVisitor::visit_element(
    const hrz_jobs::SymbolBakingData::Element& element,
    const SizeConstraints& constraints)
{
    assert(element.type == hrz_proto::SymbolElement::ElementTypeCase::kOptional);
    const auto& params = element.optional();

    bool value = params.default_display_child;
    load_bool_property(params.display_child_prp, &value);

    if (value)
    {
        auto child_geometry = visit_child(params.child_index, constraints);
        set_child_local_transform(params.child_index, lm::mat4::identity());
        return child_geometry;
    }
    else
    {
        return ElementGeometry{constraints.min, lm::bbox2{{}, constraints.min}};
    }
}

ElementGeometry SymbolBaker::VariantVisitor::visit_element(
    const hrz_jobs::SymbolBakingData::Element& element,
    const SizeConstraints& constraints)
{
    assert(element.type == hrz_proto::SymbolElement::ElementTypeCase::kVariant);
    const auto& params = element.variant();

    int64_t displayed_child_index = params.default_displayed_child_index;
    load_int_property(params.displayed_child_index_prp, &displayed_child_index);

    if (displayed_child_index >= 0 && (size_t)displayed_child_index < params.child_indices.size())
    {
        auto child_geometry = visit_child(params.child_indices[displayed_child_index], constraints);
        set_child_local_transform(
            params.child_indices[displayed_child_index], lm::mat4::identity());
        return child_geometry;
    }
    else
    {
        return ElementGeometry{constraints.min, lm::bbox2{{}, constraints.min}};
    }
}

} // namespace hrz_jobs::symbol
