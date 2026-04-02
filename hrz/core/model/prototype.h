#pragma once

#include "hrz/core/model/baked.h"
#include "hrz/core/model/geometry.h"
#include "hrz/core/model/instance_group.h"
#include "hrz/core/model/material.h"
#include "hrz/core/model/model.h"
#include "hrz/core/model/resources/resources.h"
#include "hrz/fnd/gen_index_pool.h"
#include "hrz/fnd/gen_object_pool.h"

namespace hrz::model
{

struct ModelPrototype
{
    enum Status
    {
        LoadingDescriptor,
        DescriptorLoaded,
        Error,
    };

    using IndexPool = GenIndexPool<uint64_t, 32, 32>;
    using ModelGeometryPool = GenObjectPool<std::unique_ptr<ModelGeometry>, IndexPool>;
    using ModelMaterialPool = GenObjectPool<std::unique_ptr<ModelMaterial>, IndexPool>;
    using BakedModelPool = GenObjectPool<std::unique_ptr<BakedModel>, IndexPool>;
    using InstanceGroupPool = GenObjectPool<InstanceGroup, IndexPool>;

    std::string descriptor_uri;
    std::string base_url;
    uint32_t loading_priority;
    ModelDescriptor descriptor;
    monitoring::ResourceOwner resource_owner;
    AttributionHandle additional_attribution;

    Status status;
    assets_loader::Ticket descriptor_load_ticket;

    std::unique_ptr<BlobLibrary> blob_library;
    Resources resources;

    ModelGeometryPool geometry_pool;
    ModelMaterialPool material_pool;
    BakedModelPool baked_pool;
    InstanceGroupPool instance_group_pool;

    std::vector<my::ResourceHandle> to_destroy;

    ModelPrototype(
        std::string_view descriptor_uri,
        std::string_view base_url,
        uint32_t loading_priority,
        std::unique_ptr<BlobLibrary> blob_library,
        const monitoring::ResourceOwner& resource_owner,
        AttributionHandle attribution) :
        descriptor_uri(std::string(descriptor_uri)),
        base_url(std::string(base_url)),
        loading_priority(loading_priority),
        resource_owner(resource_owner),
        additional_attribution(attribution),
        status(ModelPrototype::Error),
        blob_library(std::move(blob_library)),
        resources(resource_owner)
    {
    }

    constexpr uint32_t base_priority() const { return loading_priority & ~0x3; }

    constexpr uint32_t descriptor_priority() const { return base_priority() + 3; }

    constexpr uint32_t buffers_priority() const { return base_priority() + 2; }

    constexpr uint32_t textures_priority() const { return base_priority() + 1; }

    void start_loading_attribute(
        const ModelDescriptor::Attribute& desc_attr,
        UsedResources<int>* used_vertex_buffers);

    void start_loading_indices(
        const ModelDescriptor::Attribute& desc_attr,
        UsedResources<int>* used_index_buffers);

    void start_loading_draco_mesh(int draco_mesh_id, UsedResources<int>* used_draco_meshes);

    void start_loading_sampler(
        SamplerWithParams sampler,
        UsedResources<SamplerWithParams>* used_samplers);

    void start_loading_texture(
        TextureWithCfg texture,
        UsedResources<TextureWithCfg>* used_textures);

    void start_loading_animation(int animation_id, UsedResources<int>* used_animations);

    void update_attributes_load_status(UsedResources<int>&);
    void update_indices_load_status(UsedResources<int>&);
    void update_draco_meshes_load_status(UsedResources<int>&);
    void update_textures_load_status(UsedResources<TextureWithCfg>&);
    void update_samplers_load_status(UsedResources<SamplerWithParams>&);
    void update_animations_load_status(UsedResources<int>&);

    void release_attributes(UsedResources<int>&);
    void release_indices(UsedResources<int>&);
    void release_draco_meshes(UsedResources<int>&);
    void release_textures(UsedResources<TextureWithCfg>&);
    void release_samplers(UsedResources<SamplerWithParams>&);
    void release_animations(UsedResources<int>&);

    void iterate_primitives(
        const std::function<void(
            const ModelDescriptor::Mesh*,
            const ModelDescriptor::MeshInstance*,
            const ModelDescriptor::Primitive*)>& fn);

    GpuDracoMeshResource* get_draco_mesh(int id);
};

std::optional<int> get_primitive_material(
    const ModelDescriptor::Primitive& primitive,
    std::optional<int> variant_index);

int get_material_uv_set(const ModelDescriptor& desc, std::optional<int> material_id);

} // namespace hrz::model
