// SPDX-FileCopyrightText: Copyright 2023 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/core/jobs/symbol/baker.h"

#include <cassert>

namespace hrz_jobs::symbol
{

static constexpr size_t InitialComponentCapacity = 256;

hrz_jobs::JobResult SymbolBaker::PlaceholderVisitor::init_element_instances(
    const hrz_jobs::SymbolBakingData::Element& element)
{
    assert(element.z_index.has_value());

    if (element.anchor_index.has_value())
    {
        instances_by_z_index.insert(
            {element.z_index.value(),
             hrz::BlobVector<PlaceholderInstance>(
                 get_context().get_blob_allocator(), InitialComponentCapacity)});
    }

    return hrz_jobs::JobResult::SUCCESS;
}

ElementGeometry SymbolBaker::PlaceholderVisitor::visit_element(
    const hrz_jobs::SymbolBakingData::Element& element,
    const SizeConstraints& constraints)
{
    assert(element.type == hrz_proto::SymbolElement::ElementTypeCase::kPlaceholder);
    const auto& params = element.placeholder();

    auto size = params.default_size;
    load_vec2f_property(params.size_prp, &size);

    size = lm::clamp(size, constraints.min, constraints.max);

    auto color_srgb = params.default_color_srgb;
    load_rgba_color_property(params.color_prp, &color_srgb);

    if (element.anchor_index.has_value() && size.x > 0 && size.y > 0 && color_srgb.a > 0)
    {
        PlaceholderInstance instance;
        instance.anchor_index = element.anchor_index.value();
        instance.transform = lm::mat4::identity();
        instance.size = size;
        instance.color = color_srgb;

        auto& instances = instances_by_z_index.at(element.z_index.value());
        instances.push_back(instance);
        register_element_instance_index(instances.size().value_or(1) - 1);
    }

    return ElementGeometry{size, lm::bbox2{{}, size}};
}

void SymbolBaker::PlaceholderVisitor::finalize_element_instance(
    uint32_t z_index,
    uint32_t element_instance_index,
    const lm::mat4& global_transform)
{
    auto instance_data = instances_by_z_index.at(z_index).data();
    if (instance_data.has_value())
    {
        auto& instance = instance_data.value()[element_instance_index];
        instance.transform = global_transform * instance.transform;
        instance.anchor_index = get_baked_anchor_index(instance.anchor_index);
    }
}

std::optional<std::optional<hrz_jobs::BakedSymbols::ElementInstances>> SymbolBaker::
    PlaceholderVisitor::get_element_instances_at_z_index(uint32_t z_index)
{
    auto instance_array_opt = instances_by_z_index.at(z_index).to_blob_array();
    if (!instance_array_opt.has_value())
    {
        return std::nullopt;
    }

    if (instance_array_opt->empty())
    {
        return {std::optional<hrz_jobs::BakedSymbols::ElementInstances>{}};
    }

    instance_array_opt->register_blob_metadata(
        get_context().get_blob_allocator(), "contents"_ss, "placeholder instance data"_ss);
    instance_array_opt->register_blob_owner(
        get_context().get_blob_allocator(), get_context().get_resource_owner());

    return {{hrz_jobs::BakedSymbols::ElementInstances{
        hrz_proto::SymbolElement::ElementTypeCase::kPlaceholder,
        std::move(instance_array_opt.value())
    }}};
}

} // namespace hrz_jobs::symbol
