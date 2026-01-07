#include "hrz/core/model/baked.h"

#include "hrz/core/clock.h"
#include "hrz/core/model/prototype.h"
#include "hrz/fnd/static_vector.h"

namespace hrz::model
{
namespace
{
ModelMaterial::Status _combine_model_material_statuses(
    ModelMaterial::Status global_status,
    ModelMaterial::Status material_status)
{
    switch (material_status)
    {
        using enum ModelMaterial::Status;
        case Loading: return Loading;
        case Displayable:
            switch (global_status)
            {
                case Loading: return Loading;
                case Displayable:
                case Ready:
                case ReadyWithErrors: return Displayable;
                default: return global_status;
            }
        case Ready: return global_status;
        case ReadyWithErrors:
            switch (global_status)
            {
                case Loading:
                case Displayable: return global_status;
                case Ready:
                case ReadyWithErrors: return ReadyWithErrors;
                default: return global_status;
            }
        default: assert(false && "Unhandled case"); break;
    }

    return ModelMaterial::Status::ReadyWithErrors;
}

ModelGeometry* _get_model_geometry(ModelPrototype* proto, ModelGeometryH handle)
{
    auto* ptr = proto->geometry_pool.get_object(handle.o);
    assert(ptr);
    return ptr ? ptr->get() : nullptr;
}

ModelMaterial* _get_model_material(ModelPrototype* proto, std::optional<ModelMaterialH> handle)
{
    if (!handle.has_value()) return nullptr;

    auto* ptr = proto->material_pool.get_object(handle->o);
    assert(ptr);
    return ptr ? ptr->get() : nullptr;
}

BakedModel* _get_baked_model(ModelPrototype* proto, BakedModelH handle)
{
    auto* ptr = proto->baked_pool.get_object(handle.o);
    assert(ptr);
    return ptr ? ptr->get() : nullptr;
}
} // namespace

BakedModel::BakedModel(
    ModelGeometryH geometry,
    std::optional<ModelMaterialH> base_material,
    std::optional<ModelMaterialH> overlay_material) :
    _geometry(geometry), _materials{base_material, overlay_material}
{
}

void BakedModel::destroy(ModelPrototype* proto)
{
    if (_render_data.ubo)
    {
        proto->to_destroy.push_back(_render_data.ubo);
    }

    for (const auto& prim : _primitives)
    {
        if (prim.vertex_input)
        {
            proto->to_destroy.push_back(prim.vertex_input);
        }
    }

    proto->release_animations(_used_animations);
}

void BakedModel::build(ModelPrototype* proto, SharedResources* sr, Render* render)
{
    build_pre_bake_nodes(proto);

    auto* geometry = _get_model_geometry(proto, _geometry);

    std::array<ModelMaterial*, MaterialCount> materials{};
    for (size_t i = 0; i < MaterialCount; ++i)
    {
        materials[i] = _get_model_material(proto, _materials[i]);
    }

    if (geometry)
    {
        proto->iterate_primitives([this](
                                      const ModelDescriptor::Mesh*,
                                      const ModelDescriptor::MeshInstance*,
                                      const ModelDescriptor::Primitive*) { _prim_count += 1; });

        size_t i = 0;
        proto->iterate_primitives(
            [this, proto, geometry, &materials, &i, sr, render](
                const ModelDescriptor::Mesh*, const ModelDescriptor::MeshInstance*,
                const ModelDescriptor::Primitive*)
            { build_primitive(proto, geometry, materials, i++, sr, render); });
    }

    _ubo_data = std::make_unique<std::byte[]>(full_ubo_size(sr));

    my::BufferResource res(my::BufferResource::Uniform);
    res.size = full_ubo_size(sr);
    res.data = nullptr;
    res.usage = my::UsageHint::Updatable;
    _render_data.ubo =
        render->rc->alloc(&res, proto->resource_owner, {{"model URI"_ss, proto->descriptor_uri}});

    for (size_t i = 0; i < proto->descriptor.animations.size(); ++i)
    {
        if (const auto& anim = proto->descriptor.animations[i]; !anim.name.empty())
        {
            if (_animation_name_to_id.insert_or_assign(anim.name, static_cast<int>(i)).second
                == false)
            {
                HRZ_LOG_WARNING(
                    "Duplicate animation name '{}' in model '{}'", anim.name,
                    proto->descriptor_uri);
            }
        }

        if (_animation_name_to_id.insert_or_assign(fmt::format("{}", i), static_cast<int>(i)).second
            == false)
        {
            HRZ_LOG_WARNING(
                "Duplicate animation name '{}' in model '{}'", i, proto->descriptor_uri);
        }
    }

    _built = true;
}

void BakedModel::build_primitive(
    ModelPrototype* proto,
    ModelGeometry* geometry,
    const std::array<ModelMaterial*, MaterialCount>& materials,
    size_t primitive_index,
    SharedResources* sr,
    Render* render)
{
    assert(_primitives.size() == primitive_index);

    const auto& geometry_primitive = geometry->get_primitive(primitive_index);

    const ModelMaterial::Primitive* material_primitives[MaterialCount];
    for (int i = 0; i < MaterialCount; ++i)
    {
        if (materials[i])
        {
            material_primitives[i] = &materials[i]->get_primitive(primitive_index);
        }
        else
        {
            material_primitives[i] = nullptr;
        }
    }

    if (geometry_primitive.stream_count > 0)
    {
        Primitive prim;
        prim.draw_ubo_offset = primitive_draw_ubo_offset(sr, primitive_index);
        prim.transform_ubo_offset = primitive_transform_ubo_offset(sr, primitive_index);

        StaticVector<my::VertexInputStream, 20> streams;
        for (const auto& stream : geometry->get_streams(geometry_primitive))
        {
            streams.push_back(stream);
        }

        for (int i = 0; i < MaterialCount; ++i)
        {
            if (materials[i] && material_primitives[i])
            {
                for (const auto& stream : materials[i]->get_streams(*material_primitives[i], i))
                {
                    streams.push_back(stream);
                }
            }
            else
            {
                for (const auto& stream : ModelMaterial::get_default_streams(sr, i))
                {
                    streams.push_back(stream);
                }
            }
        }

        my::VertexInputResource res;
        res.indices = geometry_primitive.index_buffer;
        res.attribs = streams;
        prim.vertex_input = render->rc->alloc(
            &res, proto->resource_owner, {{"model URI"_ss, proto->descriptor_uri}});

        _primitives.push_back(prim);
    }
    else
    {
        Primitive prim;
        prim.vertex_input = my::ResourceHandle::null();
        _primitives.push_back(prim);
    }
}

void BakedModel::update_mesh(
    ModelPrototype* proto,
    ModelGeometry* model,
    const std::array<ModelMaterial*, MaterialCount>& materials,
    MeshUniformData* mesh_ubo_data,
    SharedResources* sr)
{
    assert(model);

    for (int i = 0; i < MaterialCount; ++i)
    {
        if (materials[i])
        {
            palette::fill_ubo_data(&mesh_ubo_data->palettes[i], materials[i]->get_palette());
        }
        else
        {
            mesh_ubo_data->palettes[i].num_color_points = 0;
        }
    }

    mesh_ubo_data->geometry.mesh_color = _mesh_prps->color;
    mesh_ubo_data->geometry.feature_color_blend_strength = _mesh_prps->feature_color_blend_strength;
    mesh_ubo_data->geometry.feature_color_blend_mode = _mesh_prps->feature_color_blend_mode;
    mesh_ubo_data->geometry.overlay_material_enabled = _mesh_prps->overlay_material_enabled;
    mesh_ubo_data->geometry.overlay_material_opacity = _mesh_prps->overlay_material_opacity;
    mesh_ubo_data->geometry.apply_feature_color_to_overlay =
        _mesh_prps->apply_feature_color_to_overlay;
    mesh_ubo_data->geometry.clip_id = _mesh_prps->clip_id;
    mesh_ubo_data->geometry.draw_under_flat_overlays = _mesh_prps->draw_under_flat_overlays;

    model->fill_ubo_data(&mesh_ubo_data->geometry);
    assign_shaders(sr);

    _render_data.is_transparent = _mesh_prps->color.a < 1;
}

void BakedModel::update_primitive_draw(
    ModelPrototype* proto,
    ModelGeometry* geometry,
    const std::array<ModelMaterial*, MaterialCount>& materials,
    size_t primitive_index,
    PrimitiveDrawUniformData* draw_ubo_data,
    SharedResources* sr)
{
    const auto& geometry_primitive = geometry->get_primitive(primitive_index);
    if (geometry_primitive.stream_count == 0) return;

    auto& primitive = _primitives[primitive_index];
    bool has_unlit_material = false;

    const ModelMaterial::Primitive* material_primitives[2];
    for (size_t i = 0; i < MaterialCount; ++i)
    {
        if (materials[i])
        {
            material_primitives[i] = &materials[i]->get_primitive(primitive_index);
        }
        else
        {
            material_primitives[i] = nullptr;
        }

        if (material_primitives[i])
        {
            draw_ubo_data->materials[i].uv_compression = material_primitives[i]->uv_compression;
            has_unlit_material |= material_primitives[i]->unlit;
        }
        else
        {
            draw_ubo_data->materials[i].uv_compression.type = DracoCompressionType::None;
        }
    }

    draw_ubo_data->geometry.position_compression = geometry_primitive.position_compression;
    draw_ubo_data->geometry.normal_compression = geometry_primitive.normal_compression;

    draw_ubo_data->geometry.lighting_enabled =
        _primitive_draw_prps->lighting.lighting_enabled && (!has_unlit_material);
    draw_ubo_data->geometry.receive_shadows = _primitive_draw_prps->lighting.receive_shadows;

    for (int i = 0; i < MaterialCount; ++i)
    {
        if (material_primitives[i])
        {
            draw_ubo_data->materials[i].alpha_mode = (uint32_t)material_primitives[i]->alpha_mode;
            draw_ubo_data->materials[i].alpha_cutoff = material_primitives[i]->alpha_cutoff;
            draw_ubo_data->materials[i].material_color = material_primitives[i]->color;
            draw_ubo_data->materials[i].use_data_texture = material_primitives[i]->is_data_texture;
        }
        else
        {
            draw_ubo_data->materials[i].material_color = lm::vec4(1.0);
            draw_ubo_data->materials[i].alpha_mode = (uint32_t)ModelDescriptor::AlphaMode::Opaque;
            draw_ubo_data->materials[i].alpha_cutoff = 0.5F;
            draw_ubo_data->materials[i].use_data_texture = false;
        }
    }

    for (int i = 0; i < MaterialCount; ++i)
    {
        primitive.renderable.primitive_data.textures[i] = sr->fallback_texture;
        primitive.renderable.primitive_data.samplers[i] = sr->fallback_color_sampler;

        if (material_primitives[i])
        {
            if (material_primitives[i]->texture)
            {
                primitive.renderable.primitive_data.textures[i] = material_primitives[i]->texture;
            }

            if (material_primitives[i]->sampler)
            {
                primitive.renderable.primitive_data.samplers[i] = material_primitives[i]->sampler;
            }
            else if (material_primitives[i]->is_data_texture)
            {
                primitive.renderable.primitive_data.samplers[i] = sr->fallback_data_sampler;
            }
        }
    }

    primitive.renderable.primitive_data.prim_draw_ubo_offset = (uint32_t)primitive.draw_ubo_offset;

    primitive.renderable.primitive_data.is_transparent =
        draw_ubo_data->materials[0].alpha_mode == (uint32_t)ModelDescriptor::AlphaMode::Blend;

    // For now the only thing that influences the shader collection used is the
    // geometry for the batched models (select on whether we have float batch
    // ids or not). In the future this may change if we have more complex
    // materials.
    //      -slerouzic, 2022-09-07
    primitive.renderable.primitive_data.shaders_collection =
        geometry->select_shader_collection(primitive_index);
}

void BakedModel::build_pre_bake_nodes(ModelPrototype* proto)
{
    const auto& descriptor = proto->descriptor;

    _root_transform = proto->descriptor.root_transform;

    _baked_nodes.reserve(descriptor.nodes.size());
    _baked_nodes_instances.reserve(descriptor.node_instances.size());

    for (const auto& node : descriptor.nodes)
    {
        _baked_nodes.push_back(BakedNode{
            .default_transform = node.transform,
            .local_transform = node.transform,
        });
    }

    for (const auto& node_instance : descriptor.node_instances)
    {
        _baked_nodes_instances.push_back(BakedNodeInstance{
            .node_id = node_instance.node_id,
            .parent = node_instance.parent_node_instance_id,
            .transform = lm::dmat4{},
        });
    }
}

void BakedModel::bake_nodes(
    std::span<const BakedNode> baked_nodes,
    std::span<BakedNodeInstance> baked_node_instances,
    const lm::dmat4& root_transform)
{
    // Node instances are in prefix order so we can update them
    // in order and we're sure that parent nodes will have been updated
    // before their children.
    for (auto& baked_node_instance : baked_node_instances)
    {
        const auto parent_transform = baked_node_instance.parent.has_value()
            ? baked_node_instances[(size_t)baked_node_instance.parent.value()].transform
            : root_transform;

        baked_node_instance.transform = parent_transform
            * baked_nodes[(size_t)baked_node_instance.node_id].local_transform.to_matrix();
    }
}

void BakedModel::update_baked_nodes()
{
    bake_nodes(
        _baked_nodes, _baked_nodes_instances,
        _primitive_transform_prps->transform * _root_transform);
}

// Unlike the baked model's bounding sphere, we don't used the cached transforms because they
// might not be up to date, and we don't use the precomputed primitive bspheres either
// because the root transform (equivalent to draw properties transform) is not the same.
BSphere<double> BakedModel::compute_bsphere(ModelPrototype* proto, const lm::dmat4& transform) const
{
    auto* geometry = _get_model_geometry(proto, _geometry);
    if (!geometry)
    {
        return {};
    }

    BSphere<double> result = {{0, 0, 0}, 0};

    hrz::InlinedVector<BakedNodeInstance, 32> baked_nodes_instances{
        _baked_nodes_instances.begin(), _baked_nodes_instances.end()};
    bake_nodes(_baked_nodes, baked_nodes_instances, transform * _root_transform);

    for (size_t i = 0; i < _primitives.size(); ++i)
    {
        const auto& prim = geometry->get_primitive(i);
        const auto& node_transform = baked_nodes_instances[(size_t)prim.node_instance_id].transform;

        auto bsphere = compute_bsphere_from_bbox(prim.bbox, node_transform);

        if (i == 0)
        {
            result = bsphere;
        }
        else
        {
            result = merge_bounding_spheres(result, bsphere);
        }
    }

    return result;
}

BSphere<double> compute_model_bsphere(
    ModelPrototype* proto,
    BakedModelH handle,
    const lm::dmat4& transform)
{
    const auto* model = _get_baked_model(proto, handle);
    if (!model)
    {
        return {};
    }

    return model->compute_bsphere(proto, transform);
}

void BakedModel::update_primitive_transform(
    ModelPrototype* proto,
    ModelGeometry* geometry,
    const std::array<ModelMaterial*, MaterialCount>& materials,
    size_t primitive_index,
    PrimitiveTransformUniformData* transform_ubo_data,
    SharedResources* sr)
{
    const auto& geometry_primitive = geometry->get_primitive(primitive_index);
    if (geometry_primitive.stream_count == 0) return;

    auto& primitive = _primitives[primitive_index];

    const ModelMaterial::Primitive* base_material_primitives = nullptr;
    if (materials[0])
    {
        base_material_primitives = &materials[0]->get_primitive(primitive_index);
    }

    const lm::dmat4 world_transform =
        _baked_nodes_instances[geometry_primitive.node_instance_id].transform;

    lm::mat4 linear_transform(world_transform);
    linear_transform.w = lm::vec4(0.0F, 0.0F, 0.0F, 1.0F);

    transform_ubo_data->transform = linear_transform;

    // Save the translation part into two single precision vectors.
    // This is used to make calculations in camera-centered coordinates
    // in the vertex shader, so as to minimise precision loss due to
    // large values.
    const lm::dvec3 position = world_transform.w.xyz;
    hrz::split_double(
        position.x, transform_ubo_data->origin_low.x, transform_ubo_data->origin_high.x);
    hrz::split_double(
        position.y, transform_ubo_data->origin_low.y, transform_ubo_data->origin_high.y);
    hrz::split_double(
        position.z, transform_ubo_data->origin_low.z, transform_ubo_data->origin_high.z);

    const bool is_inside_out = lm::determinant(linear_transform) < 0;

    lm::mat3 normal_transform = hrz::compute_normal_transform_matrix(linear_transform);
    if (is_inside_out)
    {
        normal_transform = normal_transform * -1.0F;
    }

    transform_ubo_data->normal_transform = normal_transform;

    my::CullModifier cull_modifier = my::CullModifier::DontChange;
    if (base_material_primitives && base_material_primitives->double_sided)
    {
        cull_modifier = my::CullModifier::Disable;
    }
    else if (is_inside_out)
    {
        cull_modifier = my::CullModifier::Swap;
    }

    primitive.renderable.bsphere =
        compute_bsphere_from_bbox(geometry_primitive.bbox, world_transform);

    primitive.renderable.primitive_data.batch =
        my::DrawBatchInfo(geometry_primitive.batch).change_cull(cull_modifier);
    primitive.renderable.primitive_data.vertex_input = primitive.vertex_input;

    primitive.renderable.primitive_data.prim_transform_ubo_offset =
        (uint32_t)primitive.transform_ubo_offset;
}

void BakedModel::update(ModelPrototype* proto, SharedResources* sr, Render* render)
{
    size_t update_start_offset = std::numeric_limits<size_t>::max();
    size_t update_end_offset = 0;

    bool must_update_mesh = false;
    bool must_update_transform = std::exchange(_has_animated, false);

    std::optional<std::array<ModelMaterial*, MaterialCount>> materials_opt;
    auto get_materials = [&]() -> const std::array<ModelMaterial*, MaterialCount>&
    {
        if (!materials_opt.has_value())
        {
            std::array<ModelMaterial*, MaterialCount> materials{};
            for (size_t i = 0; i < MaterialCount; ++i)
            {
                materials[i] = _get_model_material(proto, _materials[i]);
            }
            materials_opt = materials;
        }
        return *materials_opt;
    };

    std::optional<ModelGeometry*> geometry_opt;
    auto get_geometry = [&]() -> ModelGeometry*
    {
        if (!geometry_opt.has_value())
        {
            geometry_opt = _get_model_geometry(proto, _geometry);
        }
        return *geometry_opt;
    };

    if (_primitive_draw_prps.is_dirty())
    {
        auto* geometry = get_geometry();
        if (!geometry) return;

        const auto& materials = get_materials();

        _render_data.cast_shadows = _primitive_draw_prps->lighting.cast_shadows;

        for (size_t i = 0; i < _primitives.size(); ++i)
        {
            auto& prim = _primitives[i];
            if (!prim.vertex_input) return;

            PrimitiveDrawUniformData draw_ubo_data;
            update_primitive_draw(proto, geometry, materials, i, &draw_ubo_data, sr);

            memcpy(
                _ubo_data.get() + prim.draw_ubo_offset, &draw_ubo_data,
                sizeof(PrimitiveDrawUniformData));
        }

        update_start_offset = std::min(update_start_offset, primitive_draw_ubo_offset(sr, 0));
        update_end_offset = std::max(
            update_end_offset,
            primitive_draw_ubo_offset(sr, _primitives.size() - 1)
                + sizeof(PrimitiveDrawUniformData));

        // Materials might have changed, so we need to update the mesh UBO for the
        // new material properties, and the transform stuff because the culling modifiers
        // are influenced by the double-sided property of the materials.
        must_update_mesh = true;
        must_update_transform = true;

        _primitive_draw_prps.reset();
    }

    if (_primitive_transform_prps.is_dirty() || must_update_transform)
    {
        auto* geometry = get_geometry();
        if (!geometry) return;

        const auto& materials = get_materials();

        update_baked_nodes();

        for (size_t i = 0; i < _primitives.size(); ++i)
        {
            const auto& prim = _primitives[i];
            if (!prim.vertex_input) return;

            PrimitiveTransformUniformData transform_ubo_data;
            update_primitive_transform(proto, geometry, materials, i, &transform_ubo_data, sr);

            memcpy(
                _ubo_data.get() + prim.transform_ubo_offset, &transform_ubo_data,
                sizeof(PrimitiveTransformUniformData));
        }

        update_start_offset = std::min(update_start_offset, primitive_transform_ubo_offset(sr, 0));
        update_end_offset = std::max(
            update_end_offset,
            primitive_transform_ubo_offset(sr, _primitives.size() - 1)
                + sizeof(PrimitiveTransformUniformData));

        _primitive_transform_prps.reset();

        update_animation_times();
    }

    if (_mesh_prps.is_dirty() || must_update_mesh)
    {
        auto* geometry = get_geometry();
        if (!geometry) return;

        const auto& materials = get_materials();

        // Update only mesh properties, faster!
        MeshUniformData ubo_data;
        update_mesh(proto, geometry, materials, &ubo_data, sr);

        memcpy(_ubo_data.get(), &ubo_data, sizeof(MeshUniformData));

        update_start_offset = std::min(update_start_offset, size_t(0));
        update_end_offset = std::max(update_end_offset, sizeof(MeshUniformData));

        _mesh_prps.reset();
    }

    if (update_start_offset < update_end_offset)
    {
        render->my->update_buffer(
            _render_data.ubo, update_start_offset, update_end_offset - update_start_offset,
            _ubo_data.get() + update_start_offset);
    }
}

bool BakedModel::is_animation_loading() const
{
    return !_used_animations.all_ready();
}

BakedModelStatus BakedModel::status() const
{
    if (_geometry_status == ModelGeometry::Status::Error) return BakedModelStatus::Error;

    if (!_built || _geometry_status == ModelGeometry::Status::Loading
        || _material_status == ModelMaterial::Status::Loading)
    {
        return BakedModelStatus::Loading;
    }

    if (is_animation_loading())
    {
        return BakedModelStatus::Loading;
    }

    switch (_material_status)
    {
        case ModelMaterial::Status::Loading: return BakedModelStatus::Loading;
        case ModelMaterial::Status::Displayable: return BakedModelStatus::Displayable;
        case ModelMaterial::Status::Ready: return BakedModelStatus::Ready;
        case ModelMaterial::Status::ReadyWithErrors: return BakedModelStatus::ReadyWithErrors;
        default: assert(false && "Unhandled case"); break;
    }

    return BakedModelStatus::Error;
}

void BakedModel::update_animation_times()
{
    const double frame_time = hrz::clock::CurrentFrameRealTime.s;
    for (auto& anim : _playing_animations)
    {
        anim.player.set_time(
            *anim.animation,
            (frame_time + (double)_primitive_transform_prps->animation_phase)
                * (double)_primitive_transform_prps->animation_speed);
    }
}

void BakedModel::set_animations(ModelPrototype* proto, std::span<const std::string> animations)
{
    if (!_built)
    {
        _queued_animations.assign(animations.begin(), animations.end());
        return;
    }

    proto->release_animations(_used_animations);
    _used_animations.clear();
    _playing_animations.clear();

    if (animations.empty())
    {
        for (size_t i = 0; i < proto->descriptor.animations.size(); ++i)
        {
            proto->start_loading_animation(static_cast<int>(i), &_used_animations);
        }
    }
    else
    {
        for (const auto& animation_name : animations)
        {
            if (auto it = _animation_name_to_id.find(animation_name);
                it != _animation_name_to_id.end())
            {
                const int animation_id = it->second;
                proto->start_loading_animation(animation_id, &_used_animations);
            }
        }
    }
}

void BakedModel::work(ModelPrototype* proto)
{
    auto* geometry = _get_model_geometry(proto, _geometry);
    if (geometry)
    {
        _geometry_status = geometry->status();

        if (_geometry_status == ModelGeometry::Status::Loading)
        {
            geometry->work(proto);
        }
    }
    else
    {
        _geometry_status = ModelGeometry::Status::Ready;
    }

    _material_status = ModelMaterial::Status::Ready;

    for (int i = 0; i < MaterialCount; ++i)
    {
        auto* material = _get_model_material(proto, _materials[i]);
        if (material)
        {
            auto material_status = material->status();

            if (material_status == ModelMaterial::Status::Loading)
            {
                material->work(proto);
            }

            _material_status = _combine_model_material_statuses(_material_status, material_status);
        }
    }

    if (is_animation_loading())
    {
        proto->update_animations_load_status(_used_animations);
        if (_used_animations.all_ready())
        {
            for (const int animation_id : _used_animations.in_use)
            {
                const auto* animation = &proto->resources.animations.get(animation_id)->animation;
                _playing_animations.push_back(PlayingAnimation{
                    .animation = animation,
                    .player = AnimationPlayer(*animation),
                });
            }
            update_animation_times();
        }
    }
}

RenderRequest BakedModel::work_gpu(ModelPrototype* proto, SharedResources* sr, Render* render)
{
    RenderRequest rr;

    auto* geometry = _get_model_geometry(proto, _geometry);
    if (geometry)
    {
        _geometry_status = geometry->status();

        if (_geometry_status == ModelGeometry::Status::Loading)
        {
            geometry->work_gpu(proto, sr, render);
        }
    }
    else
    {
        _geometry_status = ModelGeometry::Status::Ready;
    }

    _material_status = ModelMaterial::Status::Ready;

    for (int i = 0; i < MaterialCount; ++i)
    {
        auto* material = _get_model_material(proto, _materials[i]);
        if (material)
        {
            auto material_status = material->status();

            if (material_status == ModelMaterial::Status::Loading
                || material_status == ModelMaterial::Status::Displayable)
            {
                rr |= material->work_gpu(proto, sr, render);
            }

            _material_status = _combine_model_material_statuses(_material_status, material_status);

            _primitive_draw_prps.mutate(
                [i, &material](PrimitiveDrawProperties& prps)
                { prps.material_revisions[i] = material->primitive_revision(); });

            _mesh_prps.mutate([i, &material](MeshDrawProperties& prps)
                              { prps.material_revisions[i] = material->mesh_revision(); });
        }
    }

    if (!_built && _geometry_status == ModelGeometry::Status::Ready
        && _material_status != ModelMaterial::Status::Loading)
    {
        build(proto, sr, render);
        rr.request_visual_render();
        assert(_built);

        set_animations(proto, _queued_animations);
    }

    if (!_baked_nodes.empty() && !_playing_animations.empty()
        && _primitive_transform_prps->animation_speed != 0.0F)
    {
        update_animation_times();

        for (const auto& playing_animation : _playing_animations)
        {
            playing_animation.player.fetch_channel_values(
                *playing_animation.animation,
                [this](
                    int target_id, AnimationTargetProperty target_property,
                    const std::variant<lm::vec3, lm::quat>& value)
                {
                    auto& node = _baked_nodes[(size_t)target_id];
                    node.local_transform = node.default_transform;
                    if (std::holds_alternative<ModelDescriptor::Transform::TRS>(
                            node.local_transform.data))
                    {
                        auto& trs =
                            std::get<ModelDescriptor::Transform::TRS>(node.local_transform.data);
                        switch (target_property)
                        {
                            case AnimationTargetProperty::Translation:
                                trs.translation = std::get<lm::vec3>(value);
                                break;
                            case AnimationTargetProperty::Rotation:
                                trs.rotation = std::get<lm::quat>(value);
                                break;
                            case AnimationTargetProperty::Scale:
                                trs.scale = std::get<lm::vec3>(value);
                                break;
                        }
                    }
                });
        }

        _has_animated = true;
    }

    if (_built
        && (_mesh_prps.is_dirty() || _primitive_draw_prps.is_dirty()
            || _primitive_transform_prps.is_dirty() || _has_animated))
    {
        // Some systems (like 3D Tiles) always call work_gpu but don't always call draw.
        // If we did not update here, the model would not be updated until the next draw call,
        // and the dirty flags would remain set, causing a render request to be issued every frame.
        rr.request_visual_render(
            _has_animated ? RenderRequest::VisualCause::Animation
                          : RenderRequest::VisualCause::Scene);
        update(proto, sr, render);
    }

    return rr;
}

void BakedModel::assign_shaders(SharedResources* sr)
{
    _render_data.shaders_collections[0].visual_opaque = sr->single_opaque_shader;
    _render_data.shaders_collections[0].visual_transparent = sr->single_transparent_shader;
    _render_data.shaders_collections[0].depth = sr->single_depth_shader;
    _render_data.shaders_collections[0].picking = sr->single_picking_shader;
    _render_data.shaders_collections[0].selection = sr->single_selection_shader;
}

void BakedModel::inner_draw(
    ModelPrototype* proto,
    Render* render,
    SharedResources* sr,
    AttributionRegistry* attributions,
    void* draw_data)
{
    auto* render_data_arena = render->rd->as_queue().write(_render_data);
    for (const auto& prim : _primitives)
    {
        render->rd->collect_renderable(prim.renderable, render_data_arena);
    }
    attribution::use_this_frame(attributions, proto->descriptor.attribution);
}

void BakedModel::draw(
    ModelPrototype* proto,
    const DrawProperties& draw_prps,
    bool selected,
    uint32_t scene_views,
    SharedResources* sr,
    Render* render,
    AttributionRegistry* attributions,
    void* draw_data)
{
    if (!_built || _geometry_status != ModelGeometry::Status::Ready
        || _material_status == ModelMaterial::Status::Loading)
    {
        return;
    }

    _mesh_prps.mutate(
        [&](MeshDrawProperties& prps)
        {
            prps.clip_id = draw_prps.clip_id;
            prps.color = draw_prps.color;
            prps.feature_color_blend_strength = draw_prps.feature_color_blend_strength;
            prps.feature_color_blend_mode = draw_prps.feature_color_blend_mode;
            prps.overlay_material_enabled = draw_prps.overlay_material_enabled;
            prps.overlay_material_opacity = draw_prps.overlay_material_opacity;
            prps.apply_feature_color_to_overlay = draw_prps.apply_feature_color_to_overlay;
            prps.draw_under_flat_overlays = draw_prps.draw_under_flat_overlays;
        });

    _primitive_draw_prps.mutate([&](PrimitiveDrawProperties& prps)
                                { prps.lighting = draw_prps.lighting; });

    _primitive_transform_prps.mutate(
        [&](PrimitiveTransformProperties& prps)
        {
            prps.transform = draw_prps.transform;
            prps.animation_phase = draw_prps.animation_phase;
            prps.animation_speed = draw_prps.animation_speed;
        });

    _render_data.scene_views_bitset = scene_views;
    _render_data.render_selection = selected | draw_prps.draw_under_flat_overlays;

    if (_mesh_prps.is_dirty() || _primitive_draw_prps.is_dirty()
        || _primitive_transform_prps.is_dirty() || _has_animated)
    {
        update(proto, sr, render);
    }

    inner_draw(proto, render, sr, attributions, draw_data);
}

ImpostorBakingBakedModel::ImpostorBakingBakedModel(
    ImpostorBakingModelGeometryH geometry,
    std::optional<ModelMaterialH> material) :
    BakedModel(geometry, material, std::nullopt)
{
}

void ImpostorBakingBakedModel::destroy(ModelPrototype* proto)
{
    BakedModel::destroy(proto);

    if (_impostor_ubo)
    {
        proto->to_destroy.push_back(_impostor_ubo);
    }
}

void ImpostorBakingBakedModel::assign_shaders(SharedResources* sr)
{
    _render_data.shaders_collections[0].visual_opaque = sr->impostor_baking_shader;
    _render_data.shaders_collections[0].visual_transparent = sr->impostor_baking_shader;
    _render_data.shaders_collections[0].depth = my::ResourceHandle::null();
    _render_data.shaders_collections[0].picking = my::ResourceHandle::null();
    _render_data.shaders_collections[0].selection = my::ResourceHandle::null();
}

void ImpostorBakingBakedModel::update_impostor_baking_matrices(
    const lm::mat4& proj,
    const lm::mat4& view)
{
    _impostor_ubo_data.proj_matrix = proj;
    _impostor_ubo_data.view_matrix = view;
    _impostor_ubo_dirty = true;
}

void ImpostorBakingBakedModel::inner_draw(
    ModelPrototype* proto,
    Render* render,
    SharedResources* sr,
    AttributionRegistry*,
    void* draw_data)
{
    if (!_impostor_ubo || _impostor_ubo_dirty)
    {
        if (!_impostor_ubo)
        {
            my::BufferResource res(my::BufferResource::Uniform);
            res.size = sizeof(ImpostorBakingUniformData);
            res.usage = my::UsageHint::Updatable;
            res.data = &_impostor_ubo_data;

            _impostor_ubo = render->rc->alloc(
                &res, proto->resource_owner,
                {{"model URI"_ss, proto->descriptor_uri},
                 {"contents"_ss, "impostor baking UBO"_ss}});
        }
        else if (_impostor_ubo_dirty)
        {
            render->my->update_buffer(
                _impostor_ubo, 0, sizeof(ImpostorBakingUniformData), &_impostor_ubo_data);
        }

        _impostor_ubo_dirty = false;
    }

    assert(!_impostor_ubo_dirty && _impostor_ubo);

    render->rb->push_state();

    my::UboBinding ubo_binding = {
        UboImpostorBaking, _impostor_ubo, 0, sizeof(ImpostorBakingUniformData)};
    render->rb->bind({&ubo_binding, 1});

    for (const auto& prim : _primitives)
    {
        if (!prim.vertex_input) continue;

        RenderablePrimitive::PrimitiveRenderData render_data = prim.renderable.primitive_data;
        render_data.mesh = &_render_data;

        bool is_transparent = render_data.is_transparent || render_data.mesh->is_transparent;
        const auto& shaders = render_data.mesh->shaders_collections[render_data.shaders_collection];
        my::ResourceHandle shader =
            is_transparent ? shaders.visual_transparent : shaders.visual_opaque;

        RenderablePrimitive::render_callback(shader, render->my, render->rb, &render_data);
    }

    render->rb->pop_state();
}

InstancedBakedModel::InstancedBakedModel(
    InstancedModelGeometryH geometry,
    std::optional<ModelMaterialH> material) :
    BakedModel(geometry, material, std::nullopt)
{
}

void InstancedBakedModel::assign_shaders(SharedResources* sr)
{
    _render_data.shaders_collections[0].visual_opaque = sr->instanced_opaque_shader;
    _render_data.shaders_collections[0].visual_transparent = sr->instanced_transparent_shader;
    _render_data.shaders_collections[0].depth = sr->instanced_depth_shader;
    _render_data.shaders_collections[0].picking = sr->instanced_picking_shader;
    _render_data.shaders_collections[0].selection = sr->instanced_selection_shader;
}

void InstancedBakedModel::inner_draw(
    ModelPrototype* proto,
    Render* render,
    SharedResources* sr,
    AttributionRegistry* attributions,
    void* draw_data)
{
    InstanceGroup* group = (InstanceGroup*)draw_data;

    _render_data.ubo_bindings = group->write_ubo_bindings(render, sr);
    _render_data.texture_bindings = group->write_texture_bindings(render, sr);

    auto* render_data_arena = render->rd->as_queue().write(_render_data);
    for (const auto& prim : _primitives)
    {
        RenderablePrimitive instanced_prim = prim.renderable;
        group->patch_primitive(&instanced_prim);
        render->rd->collect_renderable(instanced_prim, render_data_arena);
    }
    attribution::use_this_frame(attributions, proto->descriptor.attribution);
}

InstancedBakedModelH create_baked_model(
    ModelPrototype* proto,
    InstancedModelGeometryH geometry,
    std::optional<ModelMaterialH> material)
{
    std::unique_ptr<BakedModel> model(new InstancedBakedModel(geometry, material));
    return {proto->baked_pool.alloc(std::move(model))};
}

BatchedBakedModel::BatchedBakedModel(
    ModelPrototype* proto,
    BatchedModelGeometryH geometry_h,
    std::optional<ModelMaterialH> material,
    std::optional<ModelMaterialH> base) :
    BakedModel(geometry_h, material, base)
{
}

void BatchedBakedModel::inner_draw(
    ModelPrototype* proto,
    Render* render,
    SharedResources* sr,
    AttributionRegistry* attributions,
    void* draw_data)
{
    auto* geometry = (BatchedModelGeometry*)_get_model_geometry(proto, _geometry);
    if (!geometry) return;

    geometry->update_gpu_data(render);

    auto* render_data_arena = render->rd->as_queue().write(_render_data);
    geometry->patch_render_data(render, sr, render_data_arena);

    for (const auto& prim : _primitives)
    {
        render->rd->collect_renderable(prim.renderable, render_data_arena);
    }
    attribution::use_this_frame(attributions, proto->descriptor.attribution);
}

void BatchedBakedModel::assign_shaders(SharedResources* sr)
{
    _render_data.shaders_collections[0].visual_opaque = sr->b3dm_opaque_shader;
    _render_data.shaders_collections[0].visual_transparent = sr->b3dm_transparent_shader;
    _render_data.shaders_collections[0].depth = sr->b3dm_depth_shader;
    _render_data.shaders_collections[0].picking = sr->b3dm_picking_shader;
    _render_data.shaders_collections[0].selection = sr->b3dm_selection_shader;

    _render_data.shaders_collections[1].visual_opaque = sr->b3dm_opaque_float_batch_ids_shader;
    _render_data.shaders_collections[1].visual_transparent =
        sr->b3dm_transparent_float_batch_ids_shader;
    _render_data.shaders_collections[1].depth = sr->b3dm_depth_float_batch_ids_shader;
    _render_data.shaders_collections[1].picking = sr->b3dm_picking_float_batch_ids_shader;
    _render_data.shaders_collections[1].selection = sr->b3dm_selection_float_batch_ids_shader;
}

BatchedBakedModelH create_baked_model(
    ModelPrototype* proto,
    BatchedModelGeometryH geometry,
    std::optional<ModelMaterialH> base,
    std::optional<ModelMaterialH> overlay)
{
    std::unique_ptr<BakedModel> model(new BatchedBakedModel(proto, geometry, base, overlay));
    return {proto->baked_pool.alloc(std::move(model))};
}

SingleBakedModelH create_baked_model(
    ModelPrototype* proto,
    SingleModelGeometryH geometry,
    std::optional<ModelMaterialH> base_material,
    std::optional<ModelMaterialH> overlay_material)
{
    std::unique_ptr<BakedModel> model(new BakedModel(geometry, base_material, overlay_material));
    return {proto->baked_pool.alloc(std::move(model))};
}

ImpostorBakingBakedModelH create_baked_model(
    ModelPrototype* proto,
    ImpostorBakingModelGeometryH geometry,
    std::optional<ModelMaterialH> material)
{
    std::unique_ptr<BakedModel> model(new ImpostorBakingBakedModel(geometry, material));
    return {proto->baked_pool.alloc(std::move(model))};
}

void destroy(ModelPrototype* proto, BakedModelH handle)
{
    auto* model = _get_baked_model(proto, handle);
    if (model)
    {
        model->destroy(proto);
        proto->baked_pool.release(handle.o);
    }
}

void work(ModelPrototype* proto, BakedModelH handle)
{
    auto* model = _get_baked_model(proto, handle);
    if (model)
    {
        model->work(proto);
    }
}

void set_animations(
    ModelPrototype* proto,
    BakedModelH handle,
    std::span<const std::string> animations)
{
    auto* model = _get_baked_model(proto, handle);
    if (model)
    {
        model->set_animations(proto, animations);
    }
}

RenderRequest work_gpu(
    ModelPrototype* proto,
    BakedModelH handle,
    SharedResources* sr,
    Render* render)
{
    auto* model = _get_baked_model(proto, handle);
    if (model)
    {
        return model->work_gpu(proto, sr, render);
    }
    return {};
}

BakedModelStatus get_status(ModelPrototype* proto, BakedModelH handle)
{
    auto* model = _get_baked_model(proto, handle);
    if (model)
    {
        return model->status();
    }
    else
    {
        return BakedModelStatus::Error;
    }
}

bool is_working(ModelPrototype* proto, BakedModelH handle)
{
    return get_status(proto, handle) == BakedModelStatus::Loading;
}

void work_gpu(ModelPrototype* proto, InstanceGroupH group_handle, Render* render)
{
    auto* group = proto->instance_group_pool.get_object(group_handle.o);
    if (!group) return;

    group->work_gpu(proto, render);
}

void draw(
    ModelPrototype* proto,
    SingleBakedModelH handle,
    const DrawProperties& draw_prps,
    bool selected,
    uint32_t scene_views,
    SharedResources* sr,
    Render* render,
    AttributionRegistry* attributions)
{
    auto* model = _get_baked_model(proto, handle);
    if (model)
    {
        return model->draw(
            proto, draw_prps, selected, scene_views, sr, render, attributions, nullptr);
    }
}

void draw(
    ModelPrototype* proto,
    ImpostorBakingBakedModelH handle,
    const DrawProperties& draw_prps,
    const lm::mat4& proj,
    const lm::mat4& view,
    SharedResources* sr,
    Render* render,
    AttributionRegistry* attributions)
{
    auto* model = (ImpostorBakingBakedModel*)_get_baked_model(proto, handle);
    if (model)
    {
        model->update_impostor_baking_matrices(proj, view);
        return model->draw(proto, draw_prps, false, 0xffffffff, sr, render, attributions, nullptr);
    }
}

void draw(
    ModelPrototype* proto,
    InstancedBakedModelH handle,
    InstanceGroupH group_handle,
    const DrawProperties& draw_prps,
    uint32_t scene_views,
    SharedResources* sr,
    Render* render,
    AttributionRegistry* attributions)
{
    auto* group = proto->instance_group_pool.get_object(group_handle.o);
    if (!group || group->status() != InstanceGroupStatus::Ready) return;

    auto* model = (InstancedBakedModel*)_get_baked_model(proto, handle);
    if (model)
    {
        return model->draw(
            proto, draw_prps, group->has_selected_features(), scene_views, sr, render, attributions,
            group);
    }
}

void draw(
    ModelPrototype* proto,
    BatchedBakedModelH handle,
    const DrawProperties& draw_prps,
    uint32_t scene_views,
    SharedResources* sr,
    Render* render,
    AttributionRegistry* attributions)
{
    auto* model = _get_baked_model(proto, handle);
    if (model)
    {
        // "selected" will be filled internally.
        return model->draw(proto, draw_prps, false, scene_views, sr, render, attributions, nullptr);
    }
}

} // namespace hrz::model
