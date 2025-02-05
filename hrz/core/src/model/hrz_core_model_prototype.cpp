#include "model/hrz_core_model_prototype.h"

#include <hrz_fnd_string_utils.h>
#include <hrz_fnd_url_utils.h>

namespace
{
template<typename T, typename Index>
constexpr const T* _get_ptr(const std::vector<T>& v, Index id)
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
ModelPrototype* _create_common(
    std::string_view descriptor_uri,
    BaseUrl base_url,
    const HttpHeaders& headers,
    AttributionHandle additional_attribution,
    assets_loader::Queue load_queue,
    uint32_t loading_priority,
    const monitoring::ResourceOwner& resource_owner)
{
    return new ModelPrototype(
        descriptor_uri, base_url.base(), loading_priority,
        BlobLibrary::create(std::move(base_url), headers, load_queue), resource_owner,
        additional_attribution);
}

ModelPrototype* create_from_gltf_url(
    AssetsLoader* al,
    BaseUrl base_url,
    const HttpHeaders& headers,
    AttributionHandle additional_attribution,
    assets_loader::Queue load_queue,
    uint32_t loading_priority,
    monitoring::ResourceOwner resource_owner)
{
    auto model_url = base_url.base();
    ModelPrototype* model = _create_common(
        model_url, std::move(base_url), headers, additional_attribution, load_queue,
        loading_priority, resource_owner);
    model->status = ModelPrototype::LoadingDescriptor;
    model->descriptor_load_ticket = assets_loader::begin(
        al, model_url, headers, load_queue, model->descriptor_priority(), resource_owner);
    return model;
}

void _parse_descriptor_from_blob(
    ModelPrototype* model,
    BlobAllocator* ba,
    AttributionRegistry* attributions,
    const blobs::BlobHandle& blob,
    size_t data_offset)
{
    if (parse_gltf_descriptor(
            model->descriptor_uri, model->additional_attribution, data_offset, blob, ba,
            model->blob_library.get(), attributions, model->buffers_priority(),
            model->textures_priority(), &model->descriptor))
    {
        model->status = ModelPrototype::DescriptorLoaded;
    }
    else
    {
        HRZ_LOG_ERROR("Error parsing glTF descriptor at {}", model->descriptor_uri);
        model->status = ModelPrototype::Error;
    }
}

ModelPrototype* create_from_gltf_blob(
    BlobAllocator* ba,
    AttributionRegistry* attributions,
    const blobs::BlobHandle& blob,
    std::string_view descriptor_uri,
    BaseUrl base_url,
    size_t data_offset,
    const HttpHeaders& headers,
    AttributionHandle additional_attribution,
    assets_loader::Queue load_queue,
    uint32_t loading_priority,
    monitoring::ResourceOwner resource_owner)
{
    ModelPrototype* model = _create_common(
        descriptor_uri, std::move(base_url), headers, additional_attribution, load_queue,
        loading_priority, resource_owner);
    _parse_descriptor_from_blob(model, ba, attributions, blob, data_offset);
    return model;
}

void destroy(
    ModelPrototype* model,
    AssetsLoader* al,
    JobScheduler* js,
    BlobAllocator* ba,
    std::vector<my::ResourceHandle>& to_destroy)
{
    assert(model);

    switch (model->status)
    {
        case ModelPrototype::LoadingDescriptor:
        {
            assets_loader::end(al, model->descriptor_load_ticket);
            break;
        }
        case ModelPrototype::DescriptorLoaded:
        {
            break;
        }
        case ModelPrototype::Error: break;
    }

    model->gpu_resources.destroy_all(model->blob_library.get(), ba, js, to_destroy);

    for (my::ResourceHandle res : model->to_destroy)
    {
        to_destroy.push_back(res);
    }

    model->blob_library->destroy(al);

    delete model;
}

void _work_loading_descriptor(
    ModelPrototype* model,
    AssetsLoader* al,
    BlobAllocator* ba,
    AttributionRegistry* attributions)
{
    assert(model && model->status == ModelPrototype::LoadingDescriptor);
    if (assets_loader::is_finished(al, model->descriptor_load_ticket))
    {
        auto status = assets_loader::get_status(al, model->descriptor_load_ticket);

        if (status == assets_loader::RequestStatus::Loaded)
        {
            auto gltf_blob = assets_loader::get_blob(al, ba, model->descriptor_load_ticket);

            _parse_descriptor_from_blob(model, ba, attributions, gltf_blob, 0);
        }
        else
        {
            HRZ_LOG_ERROR("Error loading glTF descriptor from {}", model->descriptor_uri);
            model->status = ModelPrototype::Error;
        }

        assets_loader::end(al, model->descriptor_load_ticket);
        model->descriptor_load_ticket = 0;
    }
}

void work(
    ModelPrototype* model,
    AssetsLoader* al,
    JobScheduler* js,
    BlobAllocator* ba,
    ImageDecoder* imgdec,
    AttributionRegistry* attributions)
{
    assert(model && al && ba && imgdec);

    switch (model->status)
    {
        case ModelPrototype::LoadingDescriptor:
        {
            _work_loading_descriptor(model, al, ba, attributions);
            break;
        }
        case ModelPrototype::DescriptorLoaded:
        {
            model->blob_library->work(al, ba);
            model->gpu_resources.work(model->blob_library.get(), ba, js, imgdec, model->to_destroy);
            break;
        }
        default: break;
    }
}

void work_gpu(ModelPrototype* model, BlobAllocator* ba, Render* render)
{
    switch (model->status)
    {
        case ModelPrototype::DescriptorLoaded:
        {
            model->gpu_resources.work_gpu(ba, model->blob_library.get(), render);
            break;
        }
        default: break;
    }

    if (!model->to_destroy.empty())
    {
        for (my::ResourceHandle res : model->to_destroy)
        {
            render->rc->dealloc(res);
        }
        model->to_destroy.clear();
    }
}

bool update_http_headers(ModelPrototype* model, const HttpHeaders& http_headers)
{
    assert(model && model->blob_library);
    return model->blob_library->update_http_headers(http_headers);
}

ModelPrototypeStatus get_status(ModelPrototype* model)
{
    assert(model);
    switch (model->status)
    {
        case ModelPrototype::Status::Error: return ModelPrototypeStatus::Error;
        case ModelPrototype::Status::LoadingDescriptor: return ModelPrototypeStatus::Loading;
        case ModelPrototype::Status::DescriptorLoaded: return ModelPrototypeStatus::Ready;
        default: return ModelPrototypeStatus::Error;
    }
}

bool is_working(ModelPrototype* model)
{
    assert(model);
    switch (model->status)
    {
        case ModelPrototype::Status::Error: return false;
        case ModelPrototype::Status::LoadingDescriptor: return true;
        case ModelPrototype::Status::DescriptorLoaded: return false;
        default: return false;
    }
}

AttributionHandle get_attribution(ModelPrototype* model)
{
    return model->descriptor.attribution;
}

void ModelPrototype::start_loading_attribute(
    const ModelDescriptor::Attribute& desc_attr,
    UsedResources<int>* used_vertex_buffers)
{
    const auto* accessor = _get_ptr(descriptor.accessors, desc_attr.accessor);
    if (accessor && accessor->buffer_view.has_value() && !desc_attr.draco_attribute.has_value())
    {
        int buffer_view = accessor->buffer_view.value();
        const auto* buffer_view_ptr = _get_ptr(descriptor.buffer_views, buffer_view);

        if (buffer_view_ptr)
        {
            gpu_resources.vertex_buffers.acquire(buffer_view, blob_library.get(), &descriptor);
            used_vertex_buffers->add(buffer_view);
        }
    }
}

void ModelPrototype::start_loading_indices(
    const ModelDescriptor::Attribute& desc_attr,
    UsedResources<int>* used_index_buffers)
{
    const auto* accessor = _get_ptr(descriptor.accessors, desc_attr.accessor);
    if (accessor && accessor->buffer_view.has_value())
    {
        int buffer_view = accessor->buffer_view.value();
        gpu_resources.index_buffers.acquire(buffer_view, blob_library.get(), &descriptor);
        used_index_buffers->add(buffer_view);
    }
}

void ModelPrototype::start_loading_draco_mesh(
    int draco_mesh_id,
    UsedResources<int>* used_draco_meshes)
{
    gpu_resources.draco_meshes.acquire(draco_mesh_id, blob_library.get(), &descriptor);
    used_draco_meshes->add(draco_mesh_id);
}

void ModelPrototype::start_loading_sampler(
    SamplerWithMipmapUsage sampler,
    UsedResources<SamplerWithMipmapUsage>* used_samplers)
{
    gpu_resources.samplers.acquire(sampler, blob_library.get(), &descriptor);
    used_samplers->add(sampler);
}

void ModelPrototype::start_loading_texture(
    TextureWithCfg texture,
    UsedResources<TextureWithCfg>* used_textures)
{
    gpu_resources.textures.acquire(texture, blob_library.get(), &descriptor);
    used_textures->add(texture);
}

constexpr bool _is_finished_loading_gpu_resource(GpuResourceStatus status)
{
    switch (status)
    {
        case GpuResourceStatus::Error:
        case GpuResourceStatus::Ready: return true;
        default: return false;
    }
}

void ModelPrototype::update_attributes_load_status(UsedResources<int>& used)
{
    used.iterate_waiting_on(
        [this](int key) -> bool {
            return _is_finished_loading_gpu_resource(gpu_resources.vertex_buffers.get_status(key));
        });
}

void ModelPrototype::update_indices_load_status(UsedResources<int>& used)
{
    used.iterate_waiting_on(
        [this](int key) -> bool
        { return _is_finished_loading_gpu_resource(gpu_resources.index_buffers.get_status(key)); });
}

void ModelPrototype::update_draco_meshes_load_status(UsedResources<int>& used)
{
    used.iterate_waiting_on(
        [this](int key) -> bool
        { return _is_finished_loading_gpu_resource(gpu_resources.draco_meshes.get_status(key)); });
}

void ModelPrototype::update_textures_load_status(UsedResources<TextureWithCfg>& used)
{
    used.iterate_waiting_on(
        [this](TextureWithCfg key) -> bool
        { return _is_finished_loading_gpu_resource(gpu_resources.textures.get_status(key)); });
}

void ModelPrototype::update_samplers_load_status(UsedResources<SamplerWithMipmapUsage>& used)
{
    used.iterate_waiting_on(
        [this](SamplerWithMipmapUsage key) -> bool
        { return _is_finished_loading_gpu_resource(gpu_resources.samplers.get_status(key)); });
}

void ModelPrototype::release_attributes(UsedResources<int>& used)
{
    used.iterate_all([this](int id) { gpu_resources.vertex_buffers.release(id); });
}

void ModelPrototype::release_indices(UsedResources<int>& used)
{
    used.iterate_all([this](int id) { gpu_resources.index_buffers.release(id); });
}

void ModelPrototype::release_draco_meshes(UsedResources<int>& used)
{
    used.iterate_all([this](int id) { gpu_resources.draco_meshes.release(id); });
}

void ModelPrototype::release_textures(UsedResources<TextureWithCfg>& used)
{
    used.iterate_all([this](TextureWithCfg id) { gpu_resources.textures.release(id); });
}

void ModelPrototype::release_samplers(UsedResources<SamplerWithMipmapUsage>& used)
{
    used.iterate_all([this](SamplerWithMipmapUsage id) { gpu_resources.samplers.release(id); });
}

GpuDracoMeshResource* ModelPrototype::get_draco_mesh(int id)
{
    if (gpu_resources.draco_meshes.get_status(id) == GpuResourceStatus::Ready)
    {
        return gpu_resources.draco_meshes.get(id);
    }
    else
    {
        return nullptr;
    }
}

void ModelPrototype::iterate_primitives(const std::function<void(
                                            const ModelDescriptor::Mesh*,
                                            const ModelDescriptor::MeshInstance*,
                                            const ModelDescriptor::Primitive*)>& fn)
{
    for (const auto& mesh_instance : descriptor.mesh_instances)
    {
        const auto* mesh = _get_ptr(descriptor.meshes, mesh_instance.mesh_id);
        if (!mesh)
        {
            continue;
        }

        for (size_t i = 0; i < mesh->prim_count; ++i)
        {
            const auto* primitive = _get_ptr(descriptor.primitives, mesh->prim_first + i);
            if (!primitive)
            {
                continue;
            }

            fn(mesh, &mesh_instance, primitive);
        }
    }
}

std::optional<int> get_primitive_material(
    const ModelDescriptor::Primitive& primitive,
    std::optional<int> variant_index)
{
    if (variant_index.has_value())
    {
        uint64_t mask = (uint64_t)1 << variant_index.value();
        for (const auto& mapping : primitive.material_variants_mappings)
        {
            if (mapping.variants_bitset & mask)
            {
                return mapping.material;
            }
        }
    }
    return primitive.material;
}

int get_material_uv_set(const ModelDescriptor& desc, std::optional<int> material_id)
{
    int uv_set = 0;

    if (material_id.has_value())
    {
        const auto* material = _get_ptr(desc.materials, material_id.value());
        if (material)
        {
            if (std::holds_alternative<ModelDescriptor::DiffuseMaterial>(material->material))
            {
                uv_set = std::get<ModelDescriptor::DiffuseMaterial>(material->material).uv_set;
            }
            else if (std::holds_alternative<ModelDescriptor::DataMaterial>(material->material))
            {
                uv_set = std::get<ModelDescriptor::DataMaterial>(material->material).uv_set;
            }
        }
    }

    return uv_set;
}

} // namespace hrz::model
