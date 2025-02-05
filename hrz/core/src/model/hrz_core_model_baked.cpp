#include "model/hrz_core_model_baked.h"

#include "model/hrz_core_model_prototype.h"

#include <hrz_common_geo.h>
#include <hrz_fnd_static_vector.h>

namespace
{
template<typename T, typename Index>
static constexpr const T* _get_ptr(const std::vector<T>& v, Index id)
{
    if (id >= 0 && (size_t)id < v.size())
    {
        return &v[(size_t)id];
    }
    else
    {
        return nullptr;
    }
}
} // namespace

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
        case ModelMaterial::Status::Loading: return ModelMaterial::Status::Loading;
        case ModelMaterial::Status::Displayable:
            switch (global_status)
            {
                case ModelMaterial::Status::Loading: return ModelMaterial::Status::Loading;
                case ModelMaterial::Status::Displayable:
                case ModelMaterial::Status::Ready:
                case ModelMaterial::Status::ReadyWithErrors:
                    return ModelMaterial::Status::Displayable;
                default: return global_status;
            }
        case ModelMaterial::Status::Ready: return global_status;
        case ModelMaterial::Status::ReadyWithErrors:
            switch (global_status)
            {
                case ModelMaterial::Status::Loading:
                case ModelMaterial::Status::Displayable: return global_status;
                case ModelMaterial::Status::Ready:
                case ModelMaterial::Status::ReadyWithErrors:
                    return ModelMaterial::Status::ReadyWithErrors;
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
} // namespace

BakedModel* _get_baked_model(ModelPrototype* proto, BakedModelH handle)
{
    auto* ptr = proto->baked_pool.get_object(handle.o);
    assert(ptr);
    return ptr ? ptr->get() : nullptr;
}

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
}

void BakedModel::build(ModelPrototype* proto, SharedResources* sr, Render* render)
{
    // The whole mesh will use a single UBO buffer.
    // First we put the mesh data, then we put one primitive data for
    // each renderable primitive.
    const size_t mesh_ubo_align = render::compute_ubo_stride<MeshUniformData>(sr->ubo_alignment);

    // Initial offset for the mesh UBO
    _ubo_size = mesh_ubo_align;

    auto* geometry = _get_model_geometry(proto, _geometry);

    std::array<ModelMaterial*, MaterialCount> materials{};
    for (int i = 0; i < MaterialCount; ++i)
    {
        materials[i] = _get_model_material(proto, _materials[i]);
    }

    if (geometry)
    {
        size_t i = 0;
        proto->iterate_primitives(
            [this, proto, geometry, &materials, &i, sr, render](
                const ModelDescriptor::Mesh*, const ModelDescriptor::MeshInstance*,
                const ModelDescriptor::Primitive*)
            { build_primitive(proto, geometry, materials, i++, sr, render); });
    }

    my::BufferResource res(my::BufferResource::Uniform);
    res.size = _ubo_size;
    res.data = nullptr;
    res.usage = my::UsageHint::Updatable;
    _render_data.ubo =
        render->rc->alloc(&res, proto->resource_owner, {{"model URI"_ss, proto->descriptor_uri}});

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
        const size_t prim_ubo_align =
            render::compute_ubo_stride<PrimitiveUniformData>(sr->ubo_alignment);

        Primitive prim;
        prim.ubo_offset = _ubo_size;

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
        res.attrib_count = (uint32_t)streams.size();
        res.attribs = streams.data();
        prim.vertex_input = render->rc->alloc(
            &res, proto->resource_owner, {{"model URI"_ss, proto->descriptor_uri}});

        _primitives.push_back(prim);
        _ubo_size += prim_ubo_align;
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

void BakedModel::update_primitive(
    ModelPrototype* proto,
    ModelGeometry* geometry,
    const std::array<ModelMaterial*, MaterialCount>& materials,
    size_t primitive_index,
    PrimitiveUniformData* ubo_data,
    SharedResources* sr)
{
    const auto& geometry_primitive = geometry->get_primitive(primitive_index);
    if (geometry_primitive.stream_count == 0) return;

    auto& primitive = _primitives[primitive_index];
    bool has_unlit_material = false;

    const ModelMaterial::Primitive* material_primitives[2];
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

        if (material_primitives[i])
        {
            ubo_data->materials[i].uv_compression = material_primitives[i]->uv_compression;
            has_unlit_material |= material_primitives[i]->unlit;
        }
        else
        {
            ubo_data->materials[i].uv_compression.type = DracoCompressionType::None;
        }
    }

    ubo_data->geometry.position_compression = geometry_primitive.position_compression;
    ubo_data->geometry.normal_compression = geometry_primitive.normal_compression;
    ubo_data->geometry.lighting_enabled =
        _primitive_prps->lighting.lighting_enabled && (!has_unlit_material);
    ubo_data->geometry.receive_shadows = _primitive_prps->lighting.receive_shadows;

    lm::dmat4 world_transform = _primitive_prps->transform * geometry_primitive.transform;

    lm::mat4 linear_transform = lm::mat4(world_transform);
    linear_transform.w = lm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

    ubo_data->geometry.transform = linear_transform;

    // Save the translation part into two single precision vectors.
    // This is used to make calculations in camera-centered coordinates
    // in the vertex shader, so as to minimise precision loss due to
    // large values.
    lm::dvec3 position = world_transform.w.xyz;
    hrz::split_double(
        position.x, ubo_data->geometry.origin_low.x, ubo_data->geometry.origin_high.x);
    hrz::split_double(
        position.y, ubo_data->geometry.origin_low.y, ubo_data->geometry.origin_high.y);
    hrz::split_double(
        position.z, ubo_data->geometry.origin_low.z, ubo_data->geometry.origin_high.z);

    lm::mat3 normal_transform = hrz::compute_normal_transform_matrix(linear_transform);
    ubo_data->geometry.normal_transform = normal_transform;

    my::CullModifier cull_modifier = my::CullModifier::DontChange;
    if (material_primitives[0])
    {
        if (material_primitives[0]->double_sided)
        {
            cull_modifier = my::CullModifier::Disable;
        }
    }

    for (int i = 0; i < MaterialCount; ++i)
    {
        if (material_primitives[i])
        {
            ubo_data->materials[i].alpha_mode = (uint32_t)material_primitives[i]->alpha_mode;
            ubo_data->materials[i].alpha_cutoff = material_primitives[i]->alpha_cutoff;
            ubo_data->materials[i].material_color = material_primitives[i]->color;
            ubo_data->materials[i].use_data_texture = material_primitives[i]->is_data_texture;
        }
        else
        {
            ubo_data->materials[i].material_color = lm::vec4(1.0);
            ubo_data->materials[i].alpha_mode = (uint32_t)ModelDescriptor::AlphaMode::Opaque;
            ubo_data->materials[i].alpha_cutoff = 0.5f;
            ubo_data->materials[i].use_data_texture = false;
        }
    }

    primitive.renderable.bsphere =
        compute_bsphere_from_bbox(geometry_primitive.bbox, world_transform);

    if (cull_modifier != my::CullModifier::Disable && lm::determinant(linear_transform) < 0)
    {
        cull_modifier = my::CullModifier::Swap;
    }

    primitive.renderable.primitive_data.batch =
        my::DrawBatchInfo(geometry_primitive.batch).change_cull(cull_modifier);
    primitive.renderable.primitive_data.vertex_input = primitive.vertex_input;

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

            if (material_primitives[i]->is_data_texture)
            {
                primitive.renderable.primitive_data.samplers[i] = sr->fallback_data_sampler;
            }
            else if (material_primitives[i]->sampler)
            {
                primitive.renderable.primitive_data.samplers[i] = material_primitives[i]->sampler;
            }
        }
    }

    primitive.renderable.primitive_data.prim_ubo_offset = (uint32_t)primitive.ubo_offset;

    primitive.renderable.primitive_data.is_transparent =
        ubo_data->materials[0].alpha_mode == (uint32_t)ModelDescriptor::AlphaMode::Blend;

    // For now the only thing that influences the shader collection used is the
    // geometry for the batched models (select on whether we have float batch
    // ids or not). In the future this may change if we have more complex
    // materials.
    //      -slerouzic, 2022-09-07
    primitive.renderable.primitive_data.shaders_collection =
        geometry->select_shader_collection(primitive_index);
}

void BakedModel::update(ModelPrototype* proto, SharedResources* sr, Render* render)
{
    if (_primitive_prps.is_dirty())
    {
        auto* geometry = _get_model_geometry(proto, _geometry);
        if (!geometry) return;

        std::array<ModelMaterial*, MaterialCount> materials{};
        for (int i = 0; i < MaterialCount; ++i)
        {
            materials[i] = _get_model_material(proto, _materials[i]);
        }

        // Update all primitive and mesh properties
        auto data = std::make_unique<std::byte[]>(_ubo_size);

        {
            MeshUniformData ubo_data;
            update_mesh(proto, geometry, materials, &ubo_data, sr);
            memcpy(data.get(), &ubo_data, sizeof(MeshUniformData));
        }

        _render_data.cast_shadows = _primitive_prps->lighting.cast_shadows;
        for (size_t i = 0; i < _primitives.size(); ++i)
        {
            auto& prim = _primitives[i];
            if (!prim.vertex_input) return;

            PrimitiveUniformData ubo_data;
            update_primitive(proto, geometry, materials, i, &ubo_data, sr);

            memcpy(data.get() + prim.ubo_offset, &ubo_data, sizeof(PrimitiveUniformData));
        }

        render->my->update_buffer(_render_data.ubo, 0, _ubo_size, data.get());

        _mesh_prps.reset();
        _primitive_prps.reset();
    }
    else if (_mesh_prps.is_dirty())
    {
        auto* geometry = _get_model_geometry(proto, _geometry);
        if (!geometry) return;

        std::array<ModelMaterial*, MaterialCount> materials{};
        for (int i = 0; i < MaterialCount; ++i)
        {
            materials[i] = _get_model_material(proto, _materials[i]);
        }

        // Update only mesh properties, faster!
        MeshUniformData ubo_data;
        update_mesh(proto, geometry, materials, &ubo_data, sr);

        render->my->update_buffer(_render_data.ubo, 0, sizeof(MeshUniformData), &ubo_data);

        _mesh_prps.reset();
    }
}

BakedModelStatus BakedModel::status() const
{
    if (_geometry_status == ModelGeometry::Status::Error) return BakedModelStatus::Error;

    if (!_built || _geometry_status == ModelGeometry::Status::Loading
        || _material_status == ModelMaterial::Status::Loading)
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

            _primitive_prps.mutate(
                [&](PrimitiveDrawProperties& prps)
                { prps.material_revisions[i] = material->primitive_revision(); });

            _mesh_prps.mutate([&](MeshDrawProperties& prps)
                              { prps.material_revisions[i] = material->mesh_revision(); });
        }
    }

    if (!_built && _geometry_status == ModelGeometry::Status::Ready
        && _material_status != ModelMaterial::Status::Loading)
    {
        build(proto, sr, render);
        rr.request_visual_render();
        assert(_built);
    }

    if (_mesh_prps.is_dirty() || _primitive_prps.is_dirty())
    {
        rr.request_visual_render();
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

    _primitive_prps.mutate(
        [&](PrimitiveDrawProperties& prps)
        {
            prps.transform = draw_prps.transform;
            prps.lighting = draw_prps.lighting;
        });

    _render_data.scene_views_bitset = scene_views;
    _render_data.render_selection = selected | draw_prps.draw_under_flat_overlays;

    if (_mesh_prps.is_dirty() || _primitive_prps.is_dirty())
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
    render->rb->bind(1, &ubo_binding);

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
