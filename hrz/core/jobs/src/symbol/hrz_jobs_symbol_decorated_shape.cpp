#include "symbol/hrz_jobs_symbol_baker.h"

#include <hrz_fnd_log.h>

namespace hrz_jobs::symbol
{
static constexpr size_t InitialComponentCapacity = 256;

hrz::JobResult SymbolBaker::DecoratedShapeVisitor::init_element_instances(
    const hrz::vt::SymbolBakingData::Element& element)
{
    if (element.anchor_index.has_value())
    {
        instances_by_z_index.insert(
            {element.z_index.value(),
             hrz::BlobVector<DecoratedShapeInstance>(
                 get_context().get_blob_allocator(), InitialComponentCapacity)});
    }

    return hrz::JobResult::SUCCESS;
}

ElementGeometry SymbolBaker::DecoratedShapeVisitor::visit_element(
    const hrz::vt::SymbolBakingData::Element& element,
    const SizeConstraints& constraints)
{
    assert(element.type == hrz_proto::SymbolElementType::DECORATED_SHAPE_SYMBOL_ELEMENT);
    const auto& params = element.decorated_shape();

    float aspect_ratio =
        params.aspect_ratio > 0 ? params.aspect_ratio : constraints.min.x / constraints.min.y;

    Size candidate_size = {constraints.min.x, constraints.min.x / aspect_ratio};

    Size fitted_size = constrain_box_fit(
        constraints.min, candidate_size, params.fit_mode, hrz_proto::BOX_FIT_AXES_BOTH);

    auto alignment = params.alignment * 0.5f + lm::vec2(0.5f);
    Size clamped_size = lm::clamp(fitted_size, constraints.min, constraints.max);
    lm::vec2 alignment_offset = (clamped_size - fitted_size) * alignment;

    lm::ubvec4 color = params.default_color;
    load_rgba_color_property(params.color_prp, &color);

    lm::ubvec4 border_color = params.default_border_color;
    load_rgba_color_property(params.border_color_prp, &border_color);

    float border_size = params.default_border_size;
    load_float_property(params.border_size_prp, &border_size);

    float border_radius = params.default_border_radius;
    load_float_property(params.border_radius_prp, &border_radius);

    if (element.anchor_index.has_value())
    {
        DecoratedShapeInstance instance;
        instance.transform = lm::translation(lm::vec3(alignment_offset, 0.0f));
        instance.size = fitted_size;
        instance.color = color;
        instance.border_color = border_color;
        instance.border_size_radius = lm::vec2(border_size, border_radius);
        instance.anchor_index = element.anchor_index.value();

        auto& instances = instances_by_z_index.at(element.z_index.value());
        instances.push_back(instance);
        register_element_instance_index(instances.size().value_or(1) - 1);
    }
    lm::bbox2 visual_rect = transform_rect_2d_offset(lm::bbox2{{}, fitted_size}, alignment_offset);

    return ElementGeometry{clamped_size, visual_rect};
}

void SymbolBaker::DecoratedShapeVisitor::finalize_element_instance(
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

std::optional<std::optional<hrz::vt::BakedSymbols::ElementInstances>> SymbolBaker::
    DecoratedShapeVisitor::get_element_instances_at_z_index(uint32_t z_index)
{
    auto instance_array_opt = instances_by_z_index.at(z_index).to_blob_array();
    if (!instance_array_opt.has_value())
    {
        return std::nullopt;
    }

    if (instance_array_opt->empty())
    {
        return {{hrz::vt::BakedSymbols::ElementInstances{}}};
    }

    instance_array_opt->register_blob_metadata(
        get_context().get_blob_allocator(), "contents"_ss, "decorated shape instance data"_ss);
    instance_array_opt->register_blob_owner(
        get_context().get_blob_allocator(), get_context().get_resource_owner());

    return {{hrz::vt::BakedSymbols::ElementInstances{
        hrz_proto::SymbolElementType::DECORATED_SHAPE_SYMBOL_ELEMENT,
        std::move(instance_array_opt.value())}}};
}
} // namespace hrz_jobs::symbol
