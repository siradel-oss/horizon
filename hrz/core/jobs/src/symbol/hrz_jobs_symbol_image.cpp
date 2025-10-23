#include "symbol/hrz_jobs_symbol_baker.h"

namespace hrz_jobs::symbol
{
static constexpr size_t InitialComponentCapacity = 256;

hrz::JobResult SymbolBaker::ImageVisitor::init_element_instances(
    const hrz::vt::SymbolBakingData::Element& element)
{
    if (element.anchor_index.has_value())
    {
        BakingData baking_data{
            element.image().sprite_geometries,
            {},
            hrz::BlobVector<ImageInstance>(
                get_context().get_blob_allocator(), InitialComponentCapacity)};

        instances_by_z_index.insert({element.z_index.value(), std::move(baking_data)});
    }

    return hrz::JobResult::SUCCESS;
}

ElementGeometry SymbolBaker::ImageVisitor::visit_element(
    const hrz::vt::SymbolBakingData::Element& element,
    const SizeConstraints& constraints)
{
    assert(element.type == hrz_proto::SymbolElementType::IMAGE_SYMBOL_ELEMENT);
    const auto& params = element.image();

    auto color_srgb = params.default_color_srgb;
    load_rgba_color_property(params.color_prp, &color_srgb);

    float scale = params.default_scale;
    load_float_property(params.scale_prp, &scale);

    auto alignment = params.default_alignment;
    load_vec2f_property(params.alignment_prp, &alignment);
    alignment = alignment * 0.5f + lm::vec2(0.5f);

    std::optional<int64_t> sprite_index = std::nullopt;
    std::string_view sprite_name = params.default_sprite_name;
    load_string_property(params.sprite_name_prp, &sprite_name);

    auto sprite_name_it = params.sprite_name_to_index.find(sprite_name);
    if (sprite_name_it != params.sprite_name_to_index.end())
    {
        sprite_index = sprite_name_it->second;
    }

    if (!sprite_index)
    {
        sprite_index = params.default_sprite_index;
        load_int_property(params.sprite_index_prp, &*sprite_index);
    }

    sprite_index = hrz::clamp(*sprite_index, 0, params.sprites.size() - 1);
    const auto& sprite = params.sprites[*sprite_index];

    auto original_content_size = lm::vec2(sprite.content_size_fixed + sprite.content_size_stretch);
    auto fitted_content_size = constrain_box_fit(
        constraints.min / scale, original_content_size, params.fit_mode, params.fit_axes);

    lm::vec2 stretch_factor = lm::max(lm::vec2(0), fitted_content_size - sprite.content_size_fixed)
        / sprite.content_size_stretch;

    lm::vec2 content_offset =
        sprite.content_offset_stretch * stretch_factor + sprite.content_offset_fixed;

    lm::vec2 content_size =
        (sprite.content_size_stretch * stretch_factor + sprite.content_size_fixed) * scale;

    // There are cases where the content might not respect the constraints. We don't want to stretch
    // or resize the image beyond what is acceptable so instead we treat what doesn't fit as the
    // margin, essentially the same thing as what is outside the content box. We have the alignment
    // parameter to help us do that.
    lm::vec2 clamped_content_size = lm::clamp(content_size, constraints.min, constraints.max);
    lm::vec2 alignment_offset = (clamped_content_size - content_size) * alignment;

    lm::mat4 transform = lm::translation(lm::vec3(alignment_offset, 0.0f))
        * lm::scaling(lm::vec3(lm::vec2(scale), 1.0f))
        * lm::translation(lm::vec3(-content_offset, 0.0f));

    lm::vec2 full_size_no_scale =
        (sprite.full_size_stretch * stretch_factor + sprite.full_size_fixed);

    lm::bbox2 visual_rect = {{}, full_size_no_scale};
    visual_rect = transform_rect_2d_no_rotation(visual_rect, transform);

    if (element.anchor_index.has_value())
    {
        ImageInstance instance;
        instance.anchor_index = element.anchor_index.value();
        instance.transform = transform;
        instance.stretch_size = stretch_factor;
        instance.color = color_srgb;
        // Offset the sprite in the atlas so that (0,0) and (1,1) sample at the center of texels.
        instance.uv_offset =
            (lm::vec2(sprite.atlas_offset) + lm::vec2(0.5f)) / lm::vec2(params.image_size);
        instance.uv_size =
            (lm::vec2(sprite.atlas_size) - lm::vec2(1.0f)) / lm::vec2(params.image_size);

        auto& instances = instances_by_z_index.at(element.z_index.value());
        auto instance_index = (uint32_t)instances.gpu_instances.size().value_or(0);

        register_element_instance_index(instance_index);
        instances.instance_index_geometry_index.push_back({instance_index, sprite.geometry_index});
        instances.gpu_instances.push_back(instance);
    }

    return ElementGeometry{clamped_content_size, visual_rect};
}

void SymbolBaker::ImageVisitor::finalize_element_instance(
    uint32_t z_index,
    uint32_t element_instance_index,
    const lm::mat4& global_transform)
{
    auto instance_data = instances_by_z_index.at(z_index).gpu_instances.data();
    if (instance_data.has_value())
    {
        auto& instance = instance_data.value()[element_instance_index];
        instance.transform = global_transform * instance.transform;
        instance.anchor_index = get_baked_anchor_index(instance.anchor_index);
    }
}

std::optional<std::optional<hrz::vt::BakedSymbols::ElementInstances>> SymbolBaker::ImageVisitor::
    get_element_instances_at_z_index(uint32_t z_index)
{
    auto& image_data = instances_by_z_index.at(z_index);
    auto instance_array_opt = image_data.gpu_instances.to_blob_array();
    if (!instance_array_opt.has_value())
    {
        return std::nullopt;
    }

    size_t instance_count = instance_array_opt->size();

    if (instance_count == 0)
    {
        return {std::optional<hrz::vt::BakedSymbols::ElementInstances>{}};
    }

    // Sort such that all instances that share the same geometry index are consecutive.
    std::ranges::sort(
        image_data.instance_index_geometry_index,
        [](const std::pair<uint32_t, int>& a, const std::pair<uint32_t, int>& b)
        { return a.second < b.second; });

    auto* blob_allocator = get_context().get_blob_allocator();
    hrz::BlobVector<symbol::ImageInstance> sorted_instances(blob_allocator);
    sorted_instances.resize(instance_count);

    auto sorted_instance_array_opt = sorted_instances.to_blob_array();
    if (!instance_array_opt.has_value())
    {
        return std::nullopt;
    }

    const auto* indirection = image_data.instance_index_geometry_index.data();
    auto unsorted = instance_array_opt->get_data();
    auto sorted = sorted_instance_array_opt->get_mutable_data();

    std::vector<hrz::vt::BakedSymbols::ImageInstances::Batch> batches;
    uint32_t current_batch_instance_count = 0;
    uint32_t current_batch_first_instance = 0;
    int current_batch_geometry_index = -1;

    auto emit_batch = [&]()
    {
        if (current_batch_geometry_index < 0 || current_batch_instance_count == 0) return;

        const auto& geometry = image_data.sprite_geometries[current_batch_geometry_index];

        batches.push_back(
            {geometry.first_index, geometry.index_count, current_batch_first_instance,
             current_batch_instance_count});
    };

    // Copy all instances so that they match their sorted order.
    // I couldn't find a reasonable way of doing this in place... :(
    for (size_t i = 0; i < instance_count; ++i)
    {
        if (indirection[i].second != current_batch_geometry_index)
        {
            emit_batch();
            current_batch_instance_count = 0;
            current_batch_first_instance = i;
            current_batch_geometry_index = indirection[i].second;
        }

        sorted[i] = unsorted[indirection[i].first];
        current_batch_instance_count += 1;
    }

    emit_batch();

    sorted_instance_array_opt->register_blob_metadata(
        blob_allocator, "contents"_ss, "image instance data"_ss);
    sorted_instance_array_opt->register_blob_owner(
        blob_allocator, get_context().get_resource_owner());

    hrz::vt::BakedSymbols::ImageInstances baked_instances;
    baked_instances.instances = std::move(sorted_instance_array_opt.value());
    baked_instances.batches = std::move(batches);

    return {{hrz::vt::BakedSymbols::ElementInstances{
        hrz_proto::IMAGE_SYMBOL_ELEMENT,
        {std::move(baked_instances)}}}};
}
} // namespace hrz_jobs::symbol
