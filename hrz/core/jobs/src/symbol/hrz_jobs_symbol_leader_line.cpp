#include "symbol/hrz_jobs_symbol_baker.h"

namespace hrz_jobs::symbol
{
static constexpr size_t InitialComponentCapacity = 256;

hrz::JobResult SymbolBaker::LeaderLineVisitor::init_element_instances(
    const hrz::vt::SymbolBakingData::Element& element)
{
    assert(element.z_index.has_value());

    if (element.anchor_index.has_value())
    {
        instances_by_z_index.insert(
            {element.z_index.value(),
             hrz::BlobVector<LeaderLineInstance>(
                 get_context().get_blob_allocator(), InitialComponentCapacity)});
    }

    return hrz::JobResult::SUCCESS;
}

ElementGeometry SymbolBaker::LeaderLineVisitor::visit_element(
    const hrz::vt::SymbolBakingData::Element& element,
    const SizeConstraints& constraints)
{
    assert(element.type == hrz_proto::SymbolElementType::LEADER_LINE_SYMBOL_ELEMENT);
    const auto& params = element.leader_line();

    auto color = params.default_color;
    load_rgba_color_property(params.color_prp, &color);

    auto target_offset = params.default_target_offset;
    load_vec3f_property(params.target_offset_prp, &target_offset);

    if (element.anchor_index.has_value() && params.width > 0 && color.a > 0)
    {
        lm::dvec3 feature_position = get_feature_position();
        lm::vec3 in_tile_feature_position = lm::vec3(feature_position - get_tile_center());

        hrz::GeoPosition2 ll = hrz::ecef_to_geo2(feature_position);
        lm::dmat4 enu_matrix = hrz::enu_to_ecef_rotation_matrix_for_geo(ll);

        LeaderLineInstance instance;
        instance.target_in_tile_position =
            in_tile_feature_position + lm::vec4(enu_matrix * lm::dvec4(target_offset, 0)).xyz;
        instance.in_symbol_position = {0, 0, 0};
        instance.color = color;
        instance.anchor_index = element.anchor_index.value();

        auto& instances = instances_by_z_index.at(element.z_index.value());
        instances.push_back(instance);
        register_element_instance_index(instances.size().value_or(1) - 1);
    }

    return ElementGeometry{constraints.min, lm::bbox2{}};
}

void SymbolBaker::LeaderLineVisitor::finalize_element_instance(
    uint32_t z_index,
    uint32_t element_instance_index,
    const lm::mat4& global_transform)
{
    auto instance_data = instances_by_z_index.at(z_index).data();
    if (instance_data.has_value())
    {
        auto& instance = instance_data.value()[element_instance_index];
        instance.in_symbol_position =
            (global_transform * lm::vec4(instance.in_symbol_position, 1)).xyz;
        instance.anchor_index = get_baked_anchor_index(instance.anchor_index);
    }
}

std::optional<std::optional<hrz::vt::BakedSymbols::ElementInstances>> SymbolBaker::
    LeaderLineVisitor::get_element_instances_at_z_index(uint32_t z_index)
{
    auto instance_array_opt = instances_by_z_index.at(z_index).to_blob_array();
    if (!instance_array_opt.has_value())
    {
        return std::nullopt;
    }

    if (instance_array_opt->empty())
    {
        return {std::optional<hrz::vt::BakedSymbols::ElementInstances>{}};
    }

    instance_array_opt->register_blob_metadata(
        get_context().get_blob_allocator(), "contents"_ss, "leader line instance data"_ss);
    instance_array_opt->register_blob_owner(
        get_context().get_blob_allocator(), get_context().get_resource_owner());

    return {{hrz::vt::BakedSymbols::ElementInstances{
        hrz_proto::SymbolElementType::LEADER_LINE_SYMBOL_ELEMENT,
        std::move(instance_array_opt.value())}}};
}
} // namespace hrz_jobs::symbol
