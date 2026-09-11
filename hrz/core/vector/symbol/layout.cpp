// SPDX-FileCopyrightText: Copyright 2023 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/core/vector/symbol/layout.h"

#include "hrz/common/proto_maths.h"

namespace hrz::vt::symbol
{

ElementSystem::PrototypeH StackElementSystem::make_prototype(
    const hrz_proto::SymbolElement& element_descriptor,
    uint64_t layer_id,
    uint32_t z_index,
    const std::function<
        uint64_t(std::string_view name, const hrz::vector_data::OwnedAttributeValue&)
    >& register_prp,
    const std::function<uint32_t(const hrz_proto::SymbolElement&)>& make_child_prototype)
{
    assert(element_descriptor.element_type_case() == ElementType);

    const auto& descriptor = element_descriptor.stack();

    Prototype prototype;
    prototype.default_alignment = hrz::to_lm(descriptor.alignment().default_value());

    auto alignment_x_prp_name = fmt::format("{}_x", descriptor.alignment().name());
    auto alignment_y_prp_name = fmt::format("{}_y", descriptor.alignment().name());

    prototype.alignment_prp.x = register_prp(alignment_x_prp_name, prototype.default_alignment.x);
    prototype.alignment_prp.y = register_prp(alignment_y_prp_name, prototype.default_alignment.y);

    for (auto& child_descriptor : descriptor.children())
    {
        prototype.child_indices.push_back(make_child_prototype(child_descriptor));
    }

    return {ElementType, _prototypes.alloc(std::move(prototype))};
}

void StackElementSystem::unregister_properties(
    PrototypeH prototype_handle,
    const std::function<void(uint64_t prp_id)>& unregister_property) const
{
    if (prototype_handle.type != ElementType) return;

    auto prototype = _prototypes.get_object(prototype_handle.handle);
    if (prototype)
    {
        unregister_property(prototype->alignment_prp.x);
        unregister_property(prototype->alignment_prp.y);
    }
}

ElementSystem::PrototypeH StackExpandElementSystem::make_prototype(
    const hrz_proto::SymbolElement& element_descriptor,
    uint64_t layer_id,
    uint32_t z_index,
    const std::function<
        uint64_t(std::string_view name, const hrz::vector_data::OwnedAttributeValue&)
    >& register_prp,
    const std::function<uint32_t(const hrz_proto::SymbolElement&)>& make_child_prototype)
{
    assert(element_descriptor.element_type_case() == ElementType);

    const auto& descriptor = element_descriptor.stack_expand();

    Prototype prototype;
    prototype.child_index = make_child_prototype(descriptor.child());

    return {ElementType, _prototypes.alloc(std::move(prototype))};
}

void StackExpandElementSystem::unregister_properties(
    PrototypeH prototype_handle,
    const std::function<void(uint64_t prp_id)>& unregister_property) const
{
}

ElementSystem::PrototypeH PaddingElementSystem::make_prototype(
    const hrz_proto::SymbolElement& element_descriptor,
    uint64_t layer_id,
    uint32_t z_index,
    const std::function<
        uint64_t(std::string_view name, const hrz::vector_data::OwnedAttributeValue&)
    >& register_prp,
    const std::function<uint32_t(const hrz_proto::SymbolElement&)>& make_child_prototype)
{
    assert(element_descriptor.element_type_case() == ElementType);

    const auto& descriptor = element_descriptor.padding();

    Prototype prototype;
    prototype.default_top_padding = descriptor.top_padding().default_value();
    prototype.default_bottom_padding = descriptor.bottom_padding().default_value();
    prototype.default_left_padding = descriptor.left_padding().default_value();
    prototype.default_right_padding = descriptor.right_padding().default_value();

    prototype.top_padding_prp =
        register_prp(descriptor.top_padding().name(), prototype.default_top_padding);
    prototype.bottom_padding_prp =
        register_prp(descriptor.bottom_padding().name(), prototype.default_bottom_padding);
    prototype.left_padding_prp =
        register_prp(descriptor.left_padding().name(), prototype.default_left_padding);
    prototype.right_padding_prp =
        register_prp(descriptor.right_padding().name(), prototype.default_right_padding);

    prototype.child_index = make_child_prototype(descriptor.child());

    return {ElementType, _prototypes.alloc(std::move(prototype))};
}

void PaddingElementSystem::unregister_properties(
    PrototypeH prototype_handle,
    const std::function<void(uint64_t prp_id)>& unregister_property) const
{
    if (prototype_handle.type != ElementType) return;

    auto prototype = _prototypes.get_object(prototype_handle.handle);
    if (prototype)
    {
        unregister_property(prototype->top_padding_prp);
        unregister_property(prototype->bottom_padding_prp);
        unregister_property(prototype->left_padding_prp);
        unregister_property(prototype->right_padding_prp);
    }
}

ElementSystem::PrototypeH SizedBoxElementSystem::make_prototype(
    const hrz_proto::SymbolElement& element_descriptor,
    uint64_t layer_id,
    uint32_t z_index,
    const std::function<
        uint64_t(std::string_view name, const hrz::vector_data::OwnedAttributeValue&)
    >& register_prp,
    const std::function<uint32_t(const hrz_proto::SymbolElement&)>& make_child_prototype)
{
    assert(element_descriptor.element_type_case() == ElementType);

    const auto& descriptor = element_descriptor.sized_box();

    Prototype prototype;
    prototype.default_size = hrz::to_lm(descriptor.size().default_value());

    auto size_x_prp_name = fmt::format("{}_x", descriptor.size().name());
    auto size_y_prp_name = fmt::format("{}_y", descriptor.size().name());

    prototype.size_prp.x = register_prp(size_x_prp_name, prototype.default_size.x);

    prototype.size_prp.y = register_prp(size_y_prp_name, prototype.default_size.y);

    prototype.child_index = make_child_prototype(descriptor.child());

    return {ElementType, _prototypes.alloc(std::move(prototype))};
}

void SizedBoxElementSystem::unregister_properties(
    PrototypeH prototype_handle,
    const std::function<void(uint64_t prp_id)>& unregister_property) const
{
    if (prototype_handle.type != ElementType) return;

    auto prototype = _prototypes.get_object(prototype_handle.handle);
    if (prototype)
    {
        unregister_property(prototype->size_prp.x);
        unregister_property(prototype->size_prp.y);
    }
}

ElementSystem::PrototypeH FlexElementSystem::make_prototype(
    const hrz_proto::SymbolElement& element_descriptor,
    uint64_t layer_id,
    uint32_t z_index,
    const std::function<
        uint64_t(std::string_view name, const hrz::vector_data::OwnedAttributeValue&)
    >& register_prp,
    const std::function<uint32_t(const hrz_proto::SymbolElement&)>& make_child_prototype)
{
    assert(element_descriptor.element_type_case() == ElementType);

    const auto& descriptor = element_descriptor.flex();

    Prototype prototype;
    prototype.main_axis = descriptor.main_axis();
    prototype.main_axis_alignment = descriptor.main_axis_alignment();
    prototype.cross_axis_alignment = descriptor.cross_axis_alignment();

    for (auto& child_descriptor : descriptor.children())
    {
        prototype.child_indices.push_back(make_child_prototype(child_descriptor));
    }

    return {ElementType, _prototypes.alloc(std::move(prototype))};
}

void FlexElementSystem::unregister_properties(
    PrototypeH prototype_handle,
    const std::function<void(uint64_t prp_id)>& unregister_property) const
{
}

ElementSystem::PrototypeH FlexibleElementSystem::make_prototype(
    const hrz_proto::SymbolElement& element_descriptor,
    uint64_t layer_id,
    uint32_t z_index,
    const std::function<
        uint64_t(std::string_view name, const hrz::vector_data::OwnedAttributeValue&)
    >& register_prp,
    const std::function<uint32_t(const hrz_proto::SymbolElement&)>& make_child_prototype)
{
    assert(element_descriptor.element_type_case() == ElementType);

    const auto& descriptor = element_descriptor.flexible();

    Prototype prototype;
    prototype.fit = descriptor.fit();
    prototype.factor = descriptor.factor();
    prototype.child_index = make_child_prototype(descriptor.child());

    return {ElementType, _prototypes.alloc(std::move(prototype))};
}

void FlexibleElementSystem::unregister_properties(
    PrototypeH prototype_handle,
    const std::function<void(uint64_t prp_id)>& unregister_property) const
{
}

ElementSystem::PrototypeH AlignElementSystem::make_prototype(
    const hrz_proto::SymbolElement& element_descriptor,
    uint64_t layer_id,
    uint32_t z_index,
    const std::function<
        uint64_t(std::string_view name, const hrz::vector_data::OwnedAttributeValue&)
    >& register_prp,
    const std::function<uint32_t(const hrz_proto::SymbolElement&)>& make_child_prototype)
{
    assert(element_descriptor.element_type_case() == ElementType);

    const auto& descriptor = element_descriptor.align();

    Prototype prototype;
    prototype.fit = hrz_proto::BOX_FIT_SCALE_DOWN;
    prototype.default_alignment = hrz::to_lm(descriptor.alignment().default_value());

    auto alignment_x_prp_name = fmt::format("{}_x", descriptor.alignment().name());
    auto alignment_y_prp_name = fmt::format("{}_y", descriptor.alignment().name());

    prototype.alignment_prp.x = register_prp(alignment_x_prp_name, prototype.default_alignment.x);
    prototype.alignment_prp.y = register_prp(alignment_y_prp_name, prototype.default_alignment.y);

    prototype.child_index = make_child_prototype(descriptor.child());

    return {ElementType, _prototypes.alloc(std::move(prototype))};
}

void AlignElementSystem::unregister_properties(
    PrototypeH prototype_handle,
    const std::function<void(uint64_t prp_id)>& unregister_property) const
{
    if (prototype_handle.type != ElementType) return;

    auto prototype = _prototypes.get_object(prototype_handle.handle);
    if (prototype)
    {
        unregister_property(prototype->alignment_prp.x);
        unregister_property(prototype->alignment_prp.y);
    }
}

ElementSystem::PrototypeH ConstrainedBoxElementSystem::make_prototype(
    const hrz_proto::SymbolElement& element_descriptor,
    uint64_t layer_id,
    uint32_t z_index,
    const std::function<
        uint64_t(std::string_view name, const hrz::vector_data::OwnedAttributeValue&)
    >& register_prp,
    const std::function<uint32_t(const hrz_proto::SymbolElement&)>& make_child_prototype)
{
    assert(element_descriptor.element_type_case() == ElementType);

    const auto& descriptor = element_descriptor.constrained_box();

    Prototype prototype;
    prototype.default_min_size = hrz::to_lm(descriptor.min_size().default_value());
    prototype.default_max_size = hrz::to_lm(descriptor.max_size().default_value());

    auto min_size_x_prp_name = fmt::format("{}_x", descriptor.min_size().name());
    auto min_size_y_prp_name = fmt::format("{}_y", descriptor.min_size().name());
    prototype.min_size_prp.x = register_prp(min_size_x_prp_name, prototype.default_min_size.x);
    prototype.min_size_prp.y = register_prp(min_size_y_prp_name, prototype.default_min_size.y);

    auto max_size_x_prp_name = fmt::format("{}_x", descriptor.max_size().name());
    auto max_size_y_prp_name = fmt::format("{}_y", descriptor.max_size().name());
    prototype.max_size_prp.x = register_prp(max_size_x_prp_name, prototype.default_max_size.x);
    prototype.max_size_prp.y = register_prp(max_size_y_prp_name, prototype.default_max_size.y);

    prototype.child_index = make_child_prototype(descriptor.child());

    return {ElementType, _prototypes.alloc(std::move(prototype))};
}

void ConstrainedBoxElementSystem::unregister_properties(
    PrototypeH prototype_handle,
    const std::function<void(uint64_t prp_id)>& unregister_property) const
{
    if (prototype_handle.type != ElementType) return;

    auto prototype = _prototypes.get_object(prototype_handle.handle);
    if (prototype)
    {
        unregister_property(prototype->min_size_prp.x);
        unregister_property(prototype->min_size_prp.y);
        unregister_property(prototype->max_size_prp.x);
        unregister_property(prototype->max_size_prp.y);
    }
}

ElementSystem::PrototypeH RotatedBoxElementSystem::make_prototype(
    const hrz_proto::SymbolElement& element_descriptor,
    uint64_t layer_id,
    uint32_t z_index,
    const std::function<
        uint64_t(std::string_view name, const hrz::vector_data::OwnedAttributeValue&)
    >& register_prp,
    const std::function<uint32_t(const hrz_proto::SymbolElement&)>& make_child_prototype)
{
    assert(element_descriptor.element_type_case() == ElementType);

    const auto& descriptor = element_descriptor.rotated_box();

    Prototype prototype;
    prototype.default_quarter_turns = descriptor.quarter_turns().default_value();
    prototype.quarter_turns_prp =
        register_prp(descriptor.quarter_turns().name(), prototype.default_quarter_turns);

    prototype.child_index = make_child_prototype(descriptor.child());

    return {ElementType, _prototypes.alloc(std::move(prototype))};
}

void RotatedBoxElementSystem::unregister_properties(
    PrototypeH prototype_handle,
    const std::function<void(uint64_t prp_id)>& unregister_property) const
{
    if (prototype_handle.type != ElementType) return;

    auto prototype = _prototypes.get_object(prototype_handle.handle);
    if (prototype)
    {
        unregister_property(prototype->quarter_turns_prp);
    }
}

ElementSystem::PrototypeH AspectRatioElementSystem::make_prototype(
    const hrz_proto::SymbolElement& element_descriptor,
    uint64_t layer_id,
    uint32_t z_index,
    const std::function<
        uint64_t(std::string_view name, const hrz::vector_data::OwnedAttributeValue&)
    >& register_prp,
    const std::function<uint32_t(const hrz_proto::SymbolElement&)>& make_child_prototype)
{
    assert(element_descriptor.element_type_case() == ElementType);

    const auto& descriptor = element_descriptor.aspect_ratio();

    Prototype prototype;
    prototype.default_aspect_ratio = descriptor.aspect_ratio().default_value();
    prototype.aspect_ratio_prp =
        register_prp(descriptor.aspect_ratio().name(), prototype.default_aspect_ratio);

    prototype.child_index = make_child_prototype(descriptor.child());

    return {ElementType, _prototypes.alloc(std::move(prototype))};
}

void AspectRatioElementSystem::unregister_properties(
    PrototypeH prototype_handle,
    const std::function<void(uint64_t prp_id)>& unregister_property) const
{
    if (prototype_handle.type != ElementType) return;

    auto prototype = _prototypes.get_object(prototype_handle.handle);
    if (prototype)
    {
        unregister_property(prototype->aspect_ratio_prp);
    }
}

ElementSystem::PrototypeH FittedBoxElementSystem::make_prototype(
    const hrz_proto::SymbolElement& element_descriptor,
    uint64_t layer_id,
    uint32_t z_index,
    const std::function<
        uint64_t(std::string_view name, const hrz::vector_data::OwnedAttributeValue&)
    >& register_prp,
    const std::function<uint32_t(const hrz_proto::SymbolElement&)>& make_child_prototype)
{
    assert(element_descriptor.element_type_case() == ElementType);

    const auto& descriptor = element_descriptor.fitted_box();

    Prototype prototype;
    prototype.default_alignment = hrz::to_lm(descriptor.alignment().default_value());
    prototype.fit = descriptor.fit();

    auto alignment_x_prp_name = fmt::format("{}_x", descriptor.alignment().name());
    auto alignment_y_prp_name = fmt::format("{}_y", descriptor.alignment().name());

    prototype.alignment_prp.x = register_prp(alignment_x_prp_name, prototype.default_alignment.x);
    prototype.alignment_prp.y = register_prp(alignment_y_prp_name, prototype.default_alignment.y);

    prototype.child_index = make_child_prototype(descriptor.child());

    return {ElementType, _prototypes.alloc(std::move(prototype))};
}

void FittedBoxElementSystem::unregister_properties(
    PrototypeH prototype_handle,
    const std::function<void(uint64_t prp_id)>& unregister_property) const
{
    if (prototype_handle.type != ElementType) return;

    auto prototype = _prototypes.get_object(prototype_handle.handle);
    if (prototype)
    {
        unregister_property(prototype->alignment_prp.x);
        unregister_property(prototype->alignment_prp.y);
    }
}

ElementSystem::PrototypeH TransformElementSystem::make_prototype(
    const hrz_proto::SymbolElement& element_descriptor,
    uint64_t layer_id,
    uint32_t z_index,
    const std::function<
        uint64_t(std::string_view name, const hrz::vector_data::OwnedAttributeValue&)
    >& register_prp,
    const std::function<uint32_t(const hrz_proto::SymbolElement&)>& make_child_prototype)
{
    assert(element_descriptor.element_type_case() == ElementType);

    using Transform = hrz_jobs::SymbolBakingData::Transform;

    const auto& descriptor = element_descriptor.transform();

    Prototype prototype;
    prototype.default_alignment = hrz::to_lm(descriptor.alignment().default_value());
    prototype.default_origin = hrz::to_lm(descriptor.origin().default_value());

    auto alignment_x_prp_name = fmt::format("{}_x", descriptor.alignment().name());
    auto alignment_y_prp_name = fmt::format("{}_y", descriptor.alignment().name());
    prototype.alignment_prp.x = register_prp(alignment_x_prp_name, prototype.default_alignment.x);
    prototype.alignment_prp.y = register_prp(alignment_y_prp_name, prototype.default_alignment.y);

    auto origin_x_prp_name = fmt::format("{}_x", descriptor.origin().name());
    auto origin_y_prp_name = fmt::format("{}_y", descriptor.origin().name());
    prototype.origin_prp.x = register_prp(origin_x_prp_name, prototype.default_origin.x);
    prototype.origin_prp.y = register_prp(origin_y_prp_name, prototype.default_origin.y);

    prototype.child_index = make_child_prototype(descriptor.child());

    for (const auto& component : descriptor.components())
    {
        switch (component.component_type_case())
        {
            case hrz_proto::TransformSymbolComponent::ComponentTypeCase::kTranslation:
            {
                const auto& src = component.translation();

                Transform::Translation dst;
                dst.default_translation = hrz::to_lm(src.default_value());

                auto translation_x_prp_name = fmt::format("{}_x", src.name());
                auto translation_y_prp_name = fmt::format("{}_y", src.name());
                auto translation_z_prp_name = fmt::format("{}_z", src.name());

                dst.translation_prp.x =
                    register_prp(translation_x_prp_name, dst.default_translation.x);
                dst.translation_prp.y =
                    register_prp(translation_y_prp_name, dst.default_translation.y);
                dst.translation_prp.z =
                    register_prp(translation_y_prp_name, dst.default_translation.z);

                prototype.components.push_back(dst);
                break;
            }
            case hrz_proto::TransformSymbolComponent::ComponentTypeCase::kRotation:
            {
                const auto& src = component.rotation();

                Transform::Rotation dst;
                dst.order = src.order();
                dst.default_rotation = hrz::to_lm(src.rotation().default_value());

                auto rotation_x_prp_name = fmt::format("{}_x", src.rotation().name());
                auto rotation_y_prp_name = fmt::format("{}_y", src.rotation().name());
                auto rotation_z_prp_name = fmt::format("{}_z", src.rotation().name());

                dst.rotation_prp.x = register_prp(rotation_x_prp_name, dst.default_rotation.x);
                dst.rotation_prp.y = register_prp(rotation_y_prp_name, dst.default_rotation.y);
                dst.rotation_prp.z = register_prp(rotation_y_prp_name, dst.default_rotation.z);

                prototype.components.push_back(dst);
                break;
            }
            case hrz_proto::TransformSymbolComponent::ComponentTypeCase::kScaling:
            {
                const auto& src = component.scaling();

                Transform::Scaling dst;
                dst.default_scaling = hrz::to_lm(src.default_value());

                auto scaling_x_prp_name = fmt::format("{}_x", src.name());
                auto scaling_y_prp_name = fmt::format("{}_y", src.name());
                auto scaling_z_prp_name = fmt::format("{}_z", src.name());

                dst.scaling_prp.x = register_prp(scaling_x_prp_name, dst.default_scaling.x);
                dst.scaling_prp.y = register_prp(scaling_y_prp_name, dst.default_scaling.y);
                dst.scaling_prp.z = register_prp(scaling_y_prp_name, dst.default_scaling.z);

                prototype.components.push_back(dst);
                break;
            }
            case hrz_proto::TransformSymbolComponent::ComponentTypeCase::kGeneric:
            {
                const auto& src = component.generic();

                Transform::Generic dst;
                dst.matrix = hrz::to_lm(src);

                prototype.components.push_back(dst);
                break;
            }
            case hrz_proto::TransformSymbolComponent::ComponentTypeCase::COMPONENT_TYPE_NOT_SET:
            default: assert(!"Unhandled component type");
        }
    }

    return {ElementType, _prototypes.alloc(std::move(prototype))};
}

void TransformElementSystem::unregister_properties(
    PrototypeH prototype_handle,
    const std::function<void(uint64_t prp_id)>& unregister_property) const
{
    using Transform = typename hrz_jobs::SymbolBakingData::Transform;

    if (prototype_handle.type != ElementType) return;

    auto prototype = _prototypes.get_object(prototype_handle.handle);
    if (prototype)
    {
        unregister_property(prototype->alignment_prp.x);
        unregister_property(prototype->alignment_prp.y);
        unregister_property(prototype->origin_prp.x);
        unregister_property(prototype->origin_prp.y);

        for (const auto& component : prototype->components)
        {
            switch (component.index())
            {
                case hrz::index_of_variant<Transform::Component, Transform::Translation>():
                {
                    const auto& translation = std::get<Transform::Translation>(component);
                    unregister_property(translation.translation_prp.x);
                    unregister_property(translation.translation_prp.y);
                    unregister_property(translation.translation_prp.z);
                    break;
                }
                case hrz::index_of_variant<Transform::Component, Transform::Scaling>():
                {
                    const auto& scaling = std::get<Transform::Scaling>(component);
                    unregister_property(scaling.scaling_prp.x);
                    unregister_property(scaling.scaling_prp.y);
                    unregister_property(scaling.scaling_prp.z);
                    break;
                }
                case hrz::index_of_variant<Transform::Component, Transform::Rotation>():
                {
                    const auto& rotation = std::get<Transform::Rotation>(component);
                    unregister_property(rotation.rotation_prp.x);
                    unregister_property(rotation.rotation_prp.y);
                    unregister_property(rotation.rotation_prp.z);
                    break;
                }
                case hrz::index_of_variant<Transform::Component, Transform::Generic>(): break;
                default: assert(!"Unhandled component type");
            }
        }
    }
}

ElementSystem::PrototypeH OptionalElementSystem::make_prototype(
    const hrz_proto::SymbolElement& element_descriptor,
    uint64_t layer_id,
    uint32_t z_index,
    const std::function<
        uint64_t(std::string_view name, const hrz::vector_data::OwnedAttributeValue&)
    >& register_prp,
    const std::function<uint32_t(const hrz_proto::SymbolElement&)>& make_child_prototype)
{
    assert(element_descriptor.element_type_case() == ElementType);

    const auto& descriptor = element_descriptor.optional();

    Prototype prototype;
    prototype.default_display_child = descriptor.display_child().default_value();
    prototype.display_child_prp =
        register_prp(descriptor.display_child().name(), (int64_t)prototype.default_display_child);

    prototype.child_index = make_child_prototype(descriptor.child());

    return {ElementType, _prototypes.alloc(std::move(prototype))};
}

void OptionalElementSystem::unregister_properties(
    PrototypeH prototype_handle,
    const std::function<void(uint64_t prp_id)>& unregister_property) const
{
    if (prototype_handle.type != ElementType) return;

    auto prototype = _prototypes.get_object(prototype_handle.handle);
    if (prototype)
    {
        unregister_property(prototype->display_child_prp);
    }
}

ElementSystem::PrototypeH VariantElementSystem::make_prototype(
    const hrz_proto::SymbolElement& element_descriptor,
    uint64_t layer_id,
    uint32_t z_index,
    const std::function<
        uint64_t(std::string_view name, const hrz::vector_data::OwnedAttributeValue&)
    >& register_prp,
    const std::function<uint32_t(const hrz_proto::SymbolElement&)>& make_child_prototype)
{
    assert(element_descriptor.element_type_case() == ElementType);

    const auto& descriptor = element_descriptor.variant();

    Prototype prototype;
    prototype.default_displayed_child_index = descriptor.displayed_child_index().default_value();
    prototype.displayed_child_index_prp = register_prp(
        descriptor.displayed_child_index().name(), prototype.default_displayed_child_index);

    for (auto& child_descriptor : descriptor.children())
    {
        prototype.child_indices.push_back(make_child_prototype(child_descriptor));
    }

    return {ElementType, _prototypes.alloc(std::move(prototype))};
}

void VariantElementSystem::unregister_properties(
    PrototypeH prototype_handle,
    const std::function<void(uint64_t prp_id)>& unregister_property) const
{
    if (prototype_handle.type != ElementType) return;

    auto prototype = _prototypes.get_object(prototype_handle.handle);
    if (prototype)
    {
        unregister_property(prototype->displayed_child_index_prp);
    }
}

} // namespace hrz::vt::symbol
