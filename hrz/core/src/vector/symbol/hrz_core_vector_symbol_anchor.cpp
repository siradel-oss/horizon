#include "vector/symbol/hrz_core_vector_symbol_anchor.h"

#include <hrz_common_profiling.h>
#include <hrz_common_proto_maths.h>

namespace hrz::vt::symbol
{
ElementSystem::PrototypeH AnchorElementSystem::make_prototype(
    const hrz_proto::SymbolElement& element_descriptor,
    uint64_t layer_id,
    uint32_t z_index,
    const std::function<
        uint64_t(std::string_view name, const hrz::vector_data::OwnedAttributeValue&)>&
        register_prp,
    const std::function<uint32_t(const hrz_proto::SymbolElement&)>& make_child_prototype)
{
    assert(element_descriptor.type() == ElementType);

    const auto& descriptor = element_descriptor.anchor();

    Prototype prototype;
    prototype.layer_id = layer_id;

    uint32_t flags = 0;

    switch (descriptor.position_offset_size_unit())
    {
        case hrz_proto::SymbolSizeUnit::SYMBOL_SIZE_IN_METERS:
            flags |= AnchorFlag_Unit_Meters << AnchorFlag_PosOffsetUnitShift;
            break;
        case hrz_proto::SymbolSizeUnit::SYMBOL_SIZE_IN_PIXELS:
        {
            switch (descriptor.position_offset_relative_scaling())
            {
                case hrz_proto::SymbolRelativeScaling::SYMBOL_RELATIVE_SCALING_NONE:
                    flags |= (AnchorFlag_Unit_Pixels << AnchorFlag_PosOffsetUnitShift);
                    break;
                case hrz_proto::SymbolRelativeScaling::SYMBOL_RELATIVE_SCALING_REF_DISTANCE:
                    flags |=
                        (AnchorFlag_Unit_PixelsRelativeToAnchorDistance
                         << AnchorFlag_PosOffsetUnitShift);
                    break;
                case hrz_proto::SymbolRelativeScaling::SYMBOL_RELATIVE_SCALING_CAMERA_HEIGHT:
                    flags |=
                        (AnchorFlag_Unit_PixelsRelativeToCameraHeight
                         << AnchorFlag_PosOffsetUnitShift);
                    break;
                default: assert(false && "Unhandled case"); break;
            }
            break;
        }
        default: assert(false && "Unhandled case"); break;
    }

    switch (descriptor.element_size_unit())
    {
        case hrz_proto::SymbolSizeUnit::SYMBOL_SIZE_IN_METERS:
            flags |= AnchorFlag_Unit_Meters << AnchorFlag_ElementSizeUnitShift;
            break;
        case hrz_proto::SymbolSizeUnit::SYMBOL_SIZE_IN_PIXELS:
        {
            switch (descriptor.element_size_relative_scaling())
            {
                case hrz_proto::SymbolRelativeScaling::SYMBOL_RELATIVE_SCALING_NONE:
                    flags |= (AnchorFlag_Unit_Pixels << AnchorFlag_ElementSizeUnitShift);
                    break;
                case hrz_proto::SymbolRelativeScaling::SYMBOL_RELATIVE_SCALING_REF_DISTANCE:
                    flags |=
                        (AnchorFlag_Unit_PixelsRelativeToAnchorDistance
                         << AnchorFlag_ElementSizeUnitShift);
                    break;
                case hrz_proto::SymbolRelativeScaling::SYMBOL_RELATIVE_SCALING_CAMERA_HEIGHT:
                    flags |=
                        (AnchorFlag_Unit_PixelsRelativeToCameraHeight
                         << AnchorFlag_ElementSizeUnitShift);
                    break;
                default: assert(false && "Unhandled case"); break;
            }
            break;
        }
        default: assert(false && "Unhandled case"); break;
    }

    switch (descriptor.x_axis_alignment())
    {
        case hrz_proto::SymbolAxisAlignment::AXIS_ALIGNMENT_WORLD: break;
        case hrz_proto::SymbolAxisAlignment::AXIS_ALIGNMENT_SCREEN:
            flags |= AnchorFlag_AlignXAxisToScreen;
            break;
        default: assert(false && "Unhandled case"); break;
    }

    switch (descriptor.y_axis_alignment())
    {
        case hrz_proto::SymbolAxisAlignment::AXIS_ALIGNMENT_WORLD: break;
        case hrz_proto::SymbolAxisAlignment::AXIS_ALIGNMENT_SCREEN:
            flags |= AnchorFlag_AlignYAxisToScreen;
            break;
        default: assert(false && "Unhandled case"); break;
    }

    if (descriptor.keep_upright())
    {
        flags |= AnchorFlag_KeepUpright;
    }

    if (descriptor.can_overlap_other_symbols())
    {
        flags |= AnchorFlag_CanOverlapOtherSymbols;
    }

    if (descriptor.hides_other_symbols())
    {
        flags |= AnchorFlag_HidesOtherSymbols;
    }

    if (descriptor.is_optional())
    {
        flags |= AnchorFlag_IsOptional;
    }

    prototype.info.flags = flags;

    prototype.info.reference_distance = descriptor.reference_distance();
    prototype.info.min_relative_scale = descriptor.min_relative_scale();
    prototype.info.max_relative_scale =
        std::max(prototype.info.min_relative_scale, descriptor.max_relative_scale());

    prototype.baking_params.default_position_offset =
        hrz::to_lm(descriptor.position_offset().default_value());
    auto offset_x_prp_name = fmt::format("{}_x", descriptor.position_offset().name());
    auto offset_y_prp_name = fmt::format("{}_y", descriptor.position_offset().name());
    auto offset_z_prp_name = fmt::format("{}_z", descriptor.position_offset().name());
    prototype.baking_params.position_offset_prp.x =
        register_prp(offset_x_prp_name, prototype.baking_params.default_position_offset.x);
    prototype.baking_params.position_offset_prp.y =
        register_prp(offset_y_prp_name, prototype.baking_params.default_position_offset.y);
    prototype.baking_params.position_offset_prp.z =
        register_prp(offset_z_prp_name, prototype.baking_params.default_position_offset.z);

    prototype.baking_params.default_rotation = hrz::to_lm(descriptor.rotation().default_value());
    auto rotation_x_prp_name = fmt::format("{}_x", descriptor.rotation().name());
    auto rotation_y_prp_name = fmt::format("{}_y", descriptor.rotation().name());
    auto rotation_z_prp_name = fmt::format("{}_z", descriptor.rotation().name());
    prototype.baking_params.rotation_prp.x =
        register_prp(rotation_x_prp_name, prototype.baking_params.default_rotation.x);
    prototype.baking_params.rotation_prp.y =
        register_prp(rotation_y_prp_name, prototype.baking_params.default_rotation.y);
    prototype.baking_params.rotation_prp.z =
        register_prp(rotation_z_prp_name, prototype.baking_params.default_rotation.z);
    prototype.baking_params.rotation_order = descriptor.rotation_order();

    prototype.baking_params.default_element_alignment =
        hrz::to_lm(descriptor.element_alignment().default_value());
    auto element_alignment_x_prp_name = fmt::format("{}_x", descriptor.element_alignment().name());
    auto element_alignment_y_prp_name = fmt::format("{}_y", descriptor.element_alignment().name());
    prototype.baking_params.element_alignment_prp.x = register_prp(
        element_alignment_x_prp_name, prototype.baking_params.default_element_alignment.x);
    prototype.baking_params.element_alignment_prp.y = register_prp(
        element_alignment_y_prp_name, prototype.baking_params.default_element_alignment.y);

    prototype.baking_params.default_culling_priority =
        descriptor.culling_priority().default_value();
    auto culling_priority_prp_name = descriptor.culling_priority().name();
    prototype.baking_params.culling_priority_prp =
        register_prp(culling_priority_prp_name, prototype.baking_params.default_culling_priority);

    prototype.baking_params.reset_layout = descriptor.reset_layout();
    prototype.baking_params.child_index = make_child_prototype(descriptor.child());

    prototype.status = Prototype::Status::Uploading;

    auto handle = _prototypes.alloc(std::move(prototype));

    _loading_prototypes.insert(handle);

    return {ElementType, handle};
}

std::optional<vt::AnchorPrototype> AnchorElementSystem::get_anchor_prototype(
    PrototypeH handle) const
{
    if (handle.type != ElementType) return std::nullopt;

    auto prototype = _prototypes.get_object(handle.handle);
    if (!prototype)
    {
        return std::nullopt;
    }

    return prototype->info;
}

void AnchorElementSystem::unregister_properties(
    PrototypeH prototype_handle,
    const std::function<void(uint64_t prp_id)>& unregister_property) const
{
    if (prototype_handle.type != ElementType) return;

    auto prototype = _prototypes.get_object(prototype_handle.handle);
    if (prototype)
    {
        unregister_property(prototype->baking_params.rotation_prp.x);
        unregister_property(prototype->baking_params.rotation_prp.y);
        unregister_property(prototype->baking_params.rotation_prp.z);
        unregister_property(prototype->baking_params.position_offset_prp.x);
        unregister_property(prototype->baking_params.position_offset_prp.y);
        unregister_property(prototype->baking_params.position_offset_prp.z);
        unregister_property(prototype->baking_params.element_alignment_prp.x);
        unregister_property(prototype->baking_params.element_alignment_prp.y);
    }
}

void AnchorElementSystem::delete_prototype(PrototypeH prototype_handle)
{
    if (prototype_handle.type != ElementType) return;

    auto prototype = _prototypes.get_object(prototype_handle.handle);
    if (prototype)
    {
        _loading_prototypes.erase(prototype_handle.handle);
        _deleted_prototypes.insert(prototype_handle.handle);
    }
}

void AnchorElementSystem::work(ReprSystem::WorkCtx& ctx)
{
    HRZ_SCOPED_SAMPLE("vector repr symbol anchor work");

    for (auto handle : _deleted_prototypes)
    {
        auto prototype = _prototypes.get_object(handle);
        if (!prototype) continue;

        if (!prototype->ubo.is_null())
        {
            _unused_resources.push_back(prototype->ubo);
        }

        _prototypes.release(handle);
    }
    _deleted_prototypes.clear();
}

void AnchorElementSystem::work_gpu(Render* render)
{
    HRZ_SCOPED_SAMPLE("vector repr symbol anchor work gpu");

    for (auto resource : _unused_resources)
    {
        render->rc->dealloc(resource);
    }
    _unused_resources.clear();

    for (auto prototype_handle : _loading_prototypes)
    {
        auto prototype = _prototypes.get_object(prototype_handle);
        if (!prototype) continue;

        assert(prototype->status == Prototype::Status::Uploading);

        AnchorUniformData ubo;
        ubo.flags = prototype->info.flags;
        ubo.reference_distance = prototype->info.reference_distance;
        ubo.min_relative_scale = prototype->info.min_relative_scale;
        ubo.max_relative_scale = prototype->info.max_relative_scale;

        my::BufferResource ub_res(my::BufferResource::BufferType::Uniform);
        ub_res.size = sizeof(ubo);
        ub_res.usage = my::UsageHint::Static;
        ub_res.data = &ubo;
        prototype->ubo =
            render->rc->alloc(&ub_res, hrz::monitoring::systems::Symbols, prototype->layer_id);

        prototype->status =
            prototype->ubo.is_null() ? Prototype::Status::Error : Prototype::Status::Ready;
    }
    _loading_prototypes.clear();
}
} // namespace hrz::vt::symbol
