#include "hrz/core/model/material.h"

#include "hrz/core/model/cvt_utils.h"
#include "hrz/core/model/prototype.h"

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

namespace hrz::model
{
static ModelMaterial* _get_model_material(
    ModelPrototype* proto,
    std::optional<ModelMaterialH> handle)
{
    if (!handle.has_value()) return nullptr;

    auto* ptr = proto->material_pool.get_object(handle->o);
    assert(ptr);
    return ptr ? ptr->get() : nullptr;
}

ModelMaterial::ModelMaterial(const hrz_proto::Material& material) :
    _material(material), _palette(palette::from_proto(material.data_texture_palette()))
{
}

void ModelMaterial::destroy(ModelPrototype* proto)
{
    proto->release_attributes(_used_vertex_buffers);
    proto->release_draco_meshes(_used_draco_meshes);
    proto->release_samplers(_used_samplers);
    proto->release_textures(_used_textures);
}

std::array<my::VertexInputStream, 2> ModelMaterial::get_streams(
    const Primitive& prim,
    int material_index)
{
    std::array<my::VertexInputStream, 2> streams;

    streams[0] = prim.uv_stream;
    streams[0].index = Uv0StreamIndex + material_index * UvStreamOffset;

    streams[1] = prim.compressed_uv_stream;
    streams[1].index = Uv0StreamIndex + CompressedStreamOffset + material_index * UvStreamOffset;

    return streams;
}

std::array<my::VertexInputStream, 2> ModelMaterial::get_default_streams(
    const SharedResources* sr,
    int material_index)
{
    std::array<my::VertexInputStream, 2> streams;

    streams[0] = sr->fallback_uv_vertex_stream;
    streams[0].index = Uv0StreamIndex + material_index * UvStreamOffset;

    streams[1] = sr->fallback_compressed_uv_vertex_stream;
    streams[1].index = Uv0StreamIndex + CompressedStreamOffset + material_index * UvStreamOffset;

    return streams;
}

const ModelDescriptor::Attribute* ModelMaterial::get_uv_attribute(
    ModelPrototype* proto,
    const ModelDescriptor::Primitive& primitive)
{
    std::optional<int> material_id = get_primitive_material(primitive, _variant_index);
    int uv_set = get_material_uv_set(proto->descriptor, material_id);

    if (uv_set >= 0 && (size_t)uv_set < ModelDescriptor::MaxUvCount
        && primitive.uv[uv_set].has_value())
    {
        return &primitive.uv[uv_set].value();
    }
    else
    {
        return nullptr;
    }
}

void ModelMaterial::initialize(ModelPrototype* proto)
{
    // For convenience
    const auto& desc = proto->descriptor;

    {
        auto it = desc.material_variants.find(_material.variant_name());
        if (it != desc.material_variants.end())
        {
            _variant_index = it->second;
        }
    }

    std::vector<std::pair<std::string_view, std::string_view>> cfg;
    for (const auto& it : _material.urls())
    {
        cfg.emplace_back(std::string_view(it.name()), std::string_view(it.url()));
    }

    _cfg = proto->blob_library->register_config(cfg);

    proto->iterate_primitives(
        [this, proto, &desc](
            const ModelDescriptor::Mesh*, const ModelDescriptor::MeshInstance*,
            const ModelDescriptor::Primitive* primitive)
        {
            Primitive material_prim;

            if (primitive->draco_buffer_view.has_value())
            {
                proto->start_loading_draco_mesh(
                    primitive->draco_buffer_view.value(), &_used_draco_meshes);
            }

            const auto* uv_attribute = get_uv_attribute(proto, *primitive);
            if (uv_attribute)
            {
                proto->start_loading_attribute(*uv_attribute, &_used_vertex_buffers);
            }

            std::optional<int> material_id = get_primitive_material(*primitive, _variant_index);

            if (material_id.has_value())
            {
                const auto* material = _get_ptr(desc.materials, material_id.value());
                if (material)
                {
                    material_prim.alpha_cutoff = material->alpha_cutoff;
                    material_prim.alpha_mode = material->alpha_mode;
                    material_prim.double_sided = material->double_sided;
                    material_prim.unlit = material->unlit;

                    if (std::holds_alternative<ModelDescriptor::DiffuseMaterial>(
                            material->material))
                    {
                        const auto& diffuse_mat =
                            std::get<ModelDescriptor::DiffuseMaterial>(material->material);

                        material_prim.color = diffuse_mat.color_factor;

                        if (diffuse_mat.color_texture.has_value())
                        {
                            const auto& texture_id = diffuse_mat.color_texture.value();
                            const auto* texture = _get_ptr(desc.textures, texture_id);
                            if (texture && texture->source.has_value())
                            {
                                if (texture->sampler.has_value())
                                {
                                    material_prim.sampler_to_load = texture->sampler.value();
                                }

                                TextureWithCfg texture_cfg{texture_id, false, _cfg};
                                proto->start_loading_texture(texture_cfg, &_used_textures);
                                material_prim.texture_to_load = texture_id;
                                material_prim.is_data_texture = false;
                            }
                        }
                    }
                    else if (std::holds_alternative<ModelDescriptor::DataMaterial>(
                                 material->material))
                    {
                        const auto& data_mat =
                            std::get<ModelDescriptor::DataMaterial>(material->material);

                        if (data_mat.data_texture.has_value())
                        {
                            const auto& texture_id = data_mat.data_texture.value();
                            const auto* texture = _get_ptr(desc.textures, texture_id);
                            if (texture && texture->source.has_value())
                            {
                                if (texture->sampler.has_value())
                                {
                                    material_prim.sampler_to_load = texture->sampler.value();
                                }

                                TextureWithCfg texture_cfg{texture_id, true, _cfg};
                                proto->start_loading_texture(texture_cfg, &_used_textures);
                                material_prim.texture_to_load = texture_id;
                                material_prim.is_data_texture = true;
                            }
                        }
                    }
                }
            }

            _primitives.push_back(std::move(material_prim));
        });

    _initialized = true;
}

ModelMaterial::LoadStatus ModelMaterial::build_primitive_uv(
    ModelPrototype* proto,
    SharedResources* sr,
    const ModelDescriptor::Primitive& desc_primitive,
    Primitive& primitive)
{
    bool has_resource_errors = false;

    const GpuDracoMeshResource* draco_mesh = nullptr;
    if (desc_primitive.draco_buffer_view.has_value())
    {
        int draco_id = desc_primitive.draco_buffer_view.value();
        auto draco_buffer_status_view = proto->gpu_resources.draco_meshes.get_status(draco_id);
        if (draco_buffer_status_view == GpuResourceStatus::Ready)
        {
            draco_mesh = proto->get_draco_mesh(draco_id);
        }
        else
        {
            if (draco_buffer_status_view != GpuResourceStatus::Error)
            {
                assert(false && "Invalid state");
            }
            has_resource_errors = true;
        }
    }

    bool streams_added = false;
    const ModelDescriptor::Attribute* attr = get_uv_attribute(proto, desc_primitive);
    if (attr)
    {
        const auto* accessor = _get_ptr(proto->descriptor.accessors, attr->accessor);

        if (accessor && attr->draco_attribute.has_value() && draco_mesh)
        {
            auto it = draco_mesh->attrib_id_to_index.find(attr->draco_attribute.value());
            if (it != draco_mesh->attrib_id_to_index.end())
            {
                const auto& draco_attr = draco_mesh->mesh.attributes.at((size_t)it->second);

                my::VertexFormat format =
                    _convert_vertex_format(draco_attr.data_type, draco_attr.component_count);

                if (draco_attr.normalized)
                {
                    format = my::to_normalized(format);
                }

                my::VertexInputStream stream;
                stream.index = Uv0StreamIndex;
                stream.buffer = draco_mesh->vertex_buffer;
                stream.format = format;
                stream.offset = draco_attr.offset;
                stream.stride = draco_attr.stride;
                stream.rate = my::VertexRate::PerVertex;

                primitive.uv_compression = _to_compression_uniform_data(draco_attr);

                if (primitive.uv_compression.type == DracoCompressionType::None)
                {
                    primitive.uv_stream = stream;
                    primitive.compressed_uv_stream = sr->fallback_compressed_uv_vertex_stream;
                }
                else
                {
                    primitive.uv_stream = sr->fallback_uv_vertex_stream;

                    stream.index += CompressedStreamOffset;
                    primitive.compressed_uv_stream = stream;
                }

                streams_added = true;
            }
        }
        else if (accessor && accessor->buffer_view.has_value())
        {
            int buffer_view = accessor->buffer_view.value();
            const auto* buffer_view_res = proto->gpu_resources.vertex_buffers.get(buffer_view);

            if (buffer_view_res)
            {
                auto buffer_view_status =
                    proto->gpu_resources.vertex_buffers.get_status(buffer_view);
                if (buffer_view_status == GpuResourceStatus::Ready)
                {
                    my::VertexInputStream stream;
                    stream.index = Uv0StreamIndex;
                    stream.buffer = buffer_view_res->render_handle;
                    stream.format = accessor->type;
                    stream.offset = accessor->byte_offset;
                    stream.stride = buffer_view_res->stride;
                    stream.rate = my::VertexRate::PerVertex;

                    primitive.uv_stream = stream;
                    primitive.compressed_uv_stream = sr->fallback_compressed_uv_vertex_stream;
                    primitive.uv_compression.type = DracoCompressionType::None;

                    streams_added = true;
                }
                else
                {
                    if (buffer_view_status != GpuResourceStatus::Error)
                    {
                        assert(false && "Invalid state");
                    }
                    has_resource_errors = true;
                }
            }
        }
    }

    if (!streams_added)
    {
        // Default, when everything else has failed.
        primitive.uv_stream = sr->fallback_uv_vertex_stream;
        primitive.compressed_uv_stream = sr->fallback_compressed_uv_vertex_stream;
        primitive.uv_compression.type = DracoCompressionType::None;
    }

    return has_resource_errors ? LoadStatus::LoadedWithErrors : LoadStatus::Loaded;
}

RenderRequest ModelMaterial::build(ModelPrototype* proto, SharedResources* sr, Render* render)
{
    RenderRequest render_request;

    bool something_happened = false;
    LoadStatus textures_status = LoadStatus::Loaded;
    bool geometry_ready = _used_draco_meshes.all_ready() && _used_vertex_buffers.all_ready();
    LoadStatus geometries_status = geometry_ready ? LoadStatus::Loaded : LoadStatus::Loading;

    size_t i = 0;
    proto->iterate_primitives(
        [this, proto, sr, &geometry_ready, &something_happened, &geometries_status,
         &textures_status, &i](
            const ModelDescriptor::Mesh*, const ModelDescriptor::MeshInstance*,
            const ModelDescriptor::Primitive* desc_primitive)
        {
            auto& prim = _primitives[i++];

            if (geometry_ready)
            {
                auto uv_status = build_primitive_uv(proto, sr, *desc_primitive, prim);

                if (uv_status == LoadStatus::Loaded || uv_status == LoadStatus::LoadedWithErrors)
                {
                    something_happened = true;

                    if (uv_status == LoadStatus::LoadedWithErrors)
                    {
                        geometries_status = LoadStatus::LoadedWithErrors;
                    }
                }
            }

            auto make_sampler_id = [&]()
            {
                SamplerWithParams sampler{};
                sampler.sampler_id = prim.sampler_to_load.value();
                sampler.can_use_linear_filtering = !prim.is_data_texture;
                sampler.can_use_mipmaps = prim.use_mipmaps && !prim.is_data_texture;
                return sampler;
            };

            if (prim.texture_to_load.has_value())
            {
                TextureWithCfg texture_id{prim.texture_to_load.value(), prim.is_data_texture, _cfg};

                auto status = proto->gpu_resources.textures.get_status(texture_id);
                if (status == GpuResourceStatus::Ready)
                {
                    const auto* texture = proto->gpu_resources.textures.get(texture_id);
                    assert(texture);

                    prim.texture = texture->render_handle;
                    prim.use_mipmaps = texture->use_mipmaps;
                    prim.texture_to_load.reset();
                    something_happened = true;

                    if (prim.sampler_to_load.has_value())
                    {
                        proto->start_loading_sampler(make_sampler_id(), &_used_samplers);
                    }
                }
                else if (status == GpuResourceStatus::Error)
                {
                    prim.texture_to_load.reset();
                    prim.sampler_to_load.reset();
                    textures_status = LoadStatus::LoadedWithErrors;
                }
                else
                {
                    if (textures_status != LoadStatus::LoadedWithErrors)
                    {
                        textures_status = LoadStatus::Loading;
                    }
                }
            }

            if (!prim.texture_to_load.has_value() && prim.sampler_to_load.has_value())
            {
                SamplerWithParams sampler_id = make_sampler_id();

                auto status = proto->gpu_resources.samplers.get_status(sampler_id);
                if (status == GpuResourceStatus::Ready)
                {
                    const auto* sampler = proto->gpu_resources.samplers.get(sampler_id);
                    assert(sampler);

                    prim.sampler = sampler->render_handle;
                    prim.sampler_to_load.reset();
                    something_happened = true;
                }
                else if (status == GpuResourceStatus::Error)
                {
                    prim.sampler_to_load.reset();
                    textures_status = LoadStatus::LoadedWithErrors;
                }
                else
                {
                    if (textures_status != LoadStatus::LoadedWithErrors)
                    {
                        textures_status = LoadStatus::Loading;
                    }
                }
            }
        });

    _textures_status = textures_status;
    _geometries_status = geometries_status;

    if (something_happened)
    {
        render_request.request_visual_render();
        _primitive_revision++;
    }

    return render_request;
}

void ModelMaterial::update_data_texture_palette(const hrz_proto::NumericPalette& palette)
{
    _palette = palette::from_proto(palette);
    _mesh_revision++;
}

void ModelMaterial::work(ModelPrototype* proto)
{
    if (!_initialized && proto->status == ModelPrototype::Status::DescriptorLoaded)
    {
        initialize(proto);
        assert(_initialized);
    }

    if (!_initialized) return;

    if (_textures_status == LoadStatus::Loading || _geometries_status == LoadStatus::Loading)
    {
        proto->update_draco_meshes_load_status(_used_draco_meshes);
        proto->update_attributes_load_status(_used_vertex_buffers);
        proto->update_textures_load_status(_used_textures);
        proto->update_samplers_load_status(_used_samplers);
    }
}

RenderRequest ModelMaterial::work_gpu(ModelPrototype* proto, SharedResources* sr, Render* render)
{
    if (!_initialized) return {};

    if (_textures_status == LoadStatus::Loading || _geometries_status == LoadStatus::Loading)
    {
        return build(proto, sr, render);
    }

    return {};
}

ModelMaterialH create_model_material(
    ModelPrototype* proto,
    const hrz_proto::Material& material_proto)
{
    std::unique_ptr<ModelMaterial> material(new ModelMaterial(material_proto));
    return {proto->material_pool.alloc(std::move(material))};
}

void destroy(ModelPrototype* proto, ModelMaterialH handle)
{
    auto* material = _get_model_material(proto, handle);
    if (material)
    {
        material->destroy(proto);
        proto->material_pool.release(handle.o);
    }
}

void update_data_texture_palette(
    ModelPrototype* proto,
    ModelMaterialH handle,
    const hrz_proto::NumericPalette& palette)
{
    auto* material = _get_model_material(proto, handle);
    if (material)
    {
        material->update_data_texture_palette(palette);
    }
}

} // namespace hrz::model
