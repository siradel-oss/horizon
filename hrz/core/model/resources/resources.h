#pragma once

#include "hrz/common/monitoring_defs.h"
#include "hrz/core/model/blob_library.h"
#include "hrz/core/model/descriptor.h"
#include "hrz/core/model/resources/collection.h"
#include "hrz/core/model/resources/resource.h"
#include "hrz/core/render.h"

#include <mycelium/backend.h>

namespace hrz::model
{

struct Resources
{
    ResourcesCollection<int, GpuBufferResource<my::BufferResource::Vertex>> vertex_buffers;
    ResourcesCollection<int, GpuBufferResource<my::BufferResource::Index>> index_buffers;
    ResourcesCollection<int, GpuDracoMeshResource> draco_meshes;
    ResourcesCollection<SamplerWithParams, GpuSamplerResource> samplers;
    ResourcesCollection<TextureWithCfg, GpuTextureResource> textures;
    ResourcesCollection<int, AnimationResource> animations;

    explicit Resources(const monitoring::ResourceOwner& resource_owner) :
        vertex_buffers(resource_owner),
        index_buffers(resource_owner),
        draco_meshes(resource_owner),
        samplers(resource_owner),
        textures(resource_owner),
        animations(resource_owner)
    {
    }

    void work(
        BlobLibrary* bl,
        BlobAllocator* ba,
        JobScheduler* js,
        ImageDecoder* imgdec,
        std::vector<my::ResourceHandle>& to_destroy)
    {
        vertex_buffers.work(bl, ba, js, imgdec, to_destroy);
        index_buffers.work(bl, ba, js, imgdec, to_destroy);
        draco_meshes.work(bl, ba, js, imgdec, to_destroy);
        samplers.work(bl, ba, js, imgdec, to_destroy);
        textures.work(bl, ba, js, imgdec, to_destroy);
        animations.work(bl, ba, js, imgdec, to_destroy);
    }

    void work_gpu(BlobAllocator* ba, BlobLibrary* bl, Render* render)
    {
        vertex_buffers.work_gpu(ba, bl, render);
        index_buffers.work_gpu(ba, bl, render);
        draco_meshes.work_gpu(ba, bl, render);
        samplers.work_gpu(ba, bl, render);
        textures.work_gpu(ba, bl, render);
        animations.work_gpu(ba, bl, render);
    }

    void destroy_all(
        BlobLibrary* bl,
        BlobAllocator* ba,
        JobScheduler* js,
        std::vector<my::ResourceHandle>& to_destroy)
    {
        vertex_buffers.destroy_all(bl, ba, js, to_destroy);
        index_buffers.destroy_all(bl, ba, js, to_destroy);
        draco_meshes.destroy_all(bl, ba, js, to_destroy);
        samplers.destroy_all(bl, ba, js, to_destroy);
        textures.destroy_all(bl, ba, js, to_destroy);
        animations.destroy_all(bl, ba, js, to_destroy);
    }
};

} // namespace hrz::model
