#pragma once

#include "hrz_core_render.h"
#include "model/hrz_core_model_blob_library.h"
#include "model/hrz_core_model_descriptor.h"

#include <hrz_common_blob_image.h>
#include <hrz_common_model.h>
#include <hrz_common_monitoring_defs.h>
#include <hrz_fnd_flat_hash_set.h>
#include <hrz_fnd_gen_index_pool.h>
#include <hrz_fnd_gen_object_pool.h>
#include <hrz_fnd_hash.h>
#include <hrz_jobs_protocol.h>
#include <hrz_jobs_tickets.h>

#include <mycelium_backend.h>

namespace hrz
{
struct ImageDecoder;

namespace model
{
enum class GpuResourceStatus
{
    Loading, // Streaming-in in main memory, or being processed.
    Loaded,  // Ready to be uploaded to video memory.
    Ready,
    Error,
};

template<my::BufferResource::BufferType TYPE>
struct GpuBufferResource
{
    std::optional<BlobLibrary::Handle> blob_handle;
    my::ResourceHandle render_handle;
    size_t offset_in_source_buffer;
    size_t length;
    size_t stride;

    GpuResourceStatus status;

    monitoring::ResourceOwner owner;

    static std::optional<GpuBufferResource<TYPE>> acquire(
        int view_id,
        BlobLibrary* bl,
        ModelDescriptor* descriptor,
        const monitoring::ResourceOwner& owner);

    GpuBufferResource(
        BlobLibrary* bl,
        BlobLibrary::Handle blob_handle,
        const ModelDescriptor::BufferView& view,
        const monitoring::ResourceOwner& owner);

    void work(BlobLibrary* bl, BlobAllocator* ba, JobScheduler* js, ImageDecoder* imgdec);
    void work_gpu(BlobAllocator* ba, BlobLibrary* bl, Render* render);
    void destroy(
        BlobLibrary* bl,
        BlobAllocator* ba,
        JobScheduler* js,
        std::vector<my::ResourceHandle>& to_destroy);

    constexpr GpuResourceStatus get_status() const { return status; }
};

extern template struct GpuBufferResource<my::BufferResource::Vertex>;
extern template struct GpuBufferResource<my::BufferResource::Index>;

struct GpuDracoMeshResource
{
    enum Status
    {
        LoadingBlob,
        DecodingMesh,
        UploadingData,
        Ready,
        Error,
    };

    Status status;
    std::optional<BlobLibrary::Handle> blob_handle;
    size_t blob_byte_offset;
    size_t blob_byte_length;

    hrz_jobs::DecompressDracoMeshTicket decompression_ticket;
    Mesh mesh;
    hrz::flat_hash_map<int, int> attrib_id_to_index;

    my::ResourceHandle vertex_buffer;
    my::ResourceHandle index_buffer;

    monitoring::ResourceOwner owner;
    std::string uri;

    static std::optional<GpuDracoMeshResource> acquire(
        int view_id,
        BlobLibrary* bl,
        ModelDescriptor* descriptor,
        const monitoring::ResourceOwner& owner);

    GpuDracoMeshResource(
        BlobLibrary* bl,
        BlobLibrary::Handle blob_handle_,
        size_t byte_offset_,
        size_t byte_length_,
        const monitoring::ResourceOwner& owner_);

    void work(BlobLibrary* bl, BlobAllocator* ba, JobScheduler* js, ImageDecoder* imgdec);
    void work_gpu(BlobAllocator* ba, BlobLibrary* bl, Render* render);
    void destroy(
        BlobLibrary* bl,
        BlobAllocator* ba,
        JobScheduler* js,
        std::vector<my::ResourceHandle>& to_destroy);

    constexpr GpuResourceStatus get_status() const
    {
        switch (status)
        {
            case Status::LoadingBlob: return GpuResourceStatus::Loading;
            case Status::DecodingMesh: return GpuResourceStatus::Loading;
            case Status::UploadingData: return GpuResourceStatus::Loaded;
            case Status::Ready: return GpuResourceStatus::Ready;
            case Status::Error: return GpuResourceStatus::Error;
            default: return GpuResourceStatus::Error;
        }
    }
};

struct SamplerWithParams
{
    int sampler_id;
    bool can_use_linear_filtering;
    bool can_use_mipmaps;

    constexpr bool operator==(const SamplerWithParams& other) const = default;
};

} // namespace model
} // namespace hrz

namespace std
{
template<>
struct hash<hrz::model::SamplerWithParams>
{
    size_t operator()(const hrz::model::SamplerWithParams& k) const
    {
        return hrz::hash_values(k.sampler_id, k.can_use_linear_filtering, k.can_use_mipmaps);
    }
};

} // namespace std

namespace hrz::model
{
struct GpuSamplerResource
{
    ModelDescriptor::Sampler desc;
    bool can_use_linear_filtering;
    bool can_use_mipmaps;
    my::ResourceHandle render_handle;
    GpuResourceStatus status;

    monitoring::ResourceOwner owner;

    static std::optional<GpuSamplerResource> acquire(
        SamplerWithParams sampler,
        BlobLibrary* bl,
        ModelDescriptor* descriptor,
        const monitoring::ResourceOwner& owner);

    GpuSamplerResource(
        const ModelDescriptor::Sampler& desc,
        bool can_use_linear_filtering,
        bool can_use_mipmaps,
        const monitoring::ResourceOwner& owner);

    void work(BlobLibrary* bl, BlobAllocator* ba, JobScheduler* js, ImageDecoder* imgdec);
    void work_gpu(BlobAllocator* ba, BlobLibrary* bl, Render* render);
    void destroy(
        BlobLibrary* bl,
        BlobAllocator* ba,
        JobScheduler* js,
        std::vector<my::ResourceHandle>& to_destroy);

    constexpr GpuResourceStatus get_status() const { return status; }
};

struct TextureWithCfg
{
    int texture_id;
    bool is_data;
    BlobLibrary::ConfigH cfg;

    constexpr bool operator==(const TextureWithCfg& other) const
    {
        return texture_id == other.texture_id && is_data == other.is_data && cfg.o == other.cfg.o;
    }
};

} // namespace hrz::model

namespace std
{
template<>
struct hash<hrz::model::TextureWithCfg>
{
    size_t operator()(const hrz::model::TextureWithCfg& k) const
    {
        return hrz::hash_mix(
            hrz::hash_mix(std::hash<int>{}(k.texture_id), std::hash<uint64_t>{}(k.cfg.o)),
            std::hash<bool>{}(k.is_data));
    }
};

} // namespace std

namespace hrz::model
{
struct GpuTextureResource
{
    enum Status
    {
        LoadingCompressed,
        Decompressing,
        Uploading,
        Ready,
        Error,
    };

    BlobLibrary::ConfigH cfg;
    bool is_data_texture;
    std::optional<hrz_proto::ImageFormat> data_interpretation;
    bool use_mipmaps;
    std::optional<BlobLibrary::Handle> compressed_blob_handle;
    size_t blob_offset;
    size_t blob_length;
    std::optional<blobs::BlobHandle> compressed_data_handle;
    std::string compressed_data_uri;
    hrz_jobs::DecodeBlobImageTicket decompress_ticket;
    std::optional<BlobImage> decompressed_image;
    my::ResourceHandle render_handle;

    Status status;

    monitoring::ResourceOwner owner;

    static std::optional<GpuTextureResource> acquire(
        TextureWithCfg id,
        BlobLibrary* bl,
        ModelDescriptor* descriptor,
        const monitoring::ResourceOwner& owner);

    GpuTextureResource(
        BlobLibrary*,
        BlobLibrary::Handle blob,
        BlobLibrary::ConfigH cfg,
        bool is_data_texture,
        std::optional<hrz_proto::ImageFormat> data_interpretation,
        size_t offset,
        size_t length,
        bool use_mipmaps,
        const monitoring::ResourceOwner& owner);

    void work(BlobLibrary* bl, BlobAllocator* ba, JobScheduler* js, ImageDecoder* imgdec);
    void work_gpu(BlobAllocator* ba, BlobLibrary* bl, Render* render);
    void destroy(
        BlobLibrary* bl,
        BlobAllocator* ba,
        JobScheduler* js,
        std::vector<my::ResourceHandle>& to_destroy);

    constexpr GpuResourceStatus get_status() const
    {
        switch (status)
        {
            case Status::LoadingCompressed: return GpuResourceStatus::Loading;
            case Status::Decompressing: return GpuResourceStatus::Loading;
            case Status::Uploading: return GpuResourceStatus::Loaded;
            case Status::Ready: return GpuResourceStatus::Ready;
            case Status::Error: return GpuResourceStatus::Error;
            default: return GpuResourceStatus::Error;
        }
    }
};

/**
 * A resource collection stores all reference-counted GPU resources of a given
 * type reference by a given key type. Essentially it acts as a cache in front
 * of the GPU for all resources of a model. When resources are "loading" it
 * means they are still being streamed in on the CPU, or being transformed.
 * Once they are uploading it means they are being transferred to the GPU.
 */
template<typename Key, typename T>
class GpuResourcesCollection
{
    using ResourceId = uint32_t;

    struct RefCount
    {
        size_t rc;
        T value;
    };

    using ResourceIndexPool = GenIndexPool<ResourceId, 16, 16>;
    using ResourcePool = GenObjectPool<RefCount, ResourceIndexPool, 8>;

    monitoring::ResourceOwner _resource_owner;

    hrz::flat_hash_set<Key> _loading_resources;
    hrz::flat_hash_set<Key> _uploading_resources;
    hrz::flat_hash_set<Key> _to_destroy;

    ResourcePool _resources;
    hrz::flat_hash_map<Key, ResourceId> _resources_index;

    inline const RefCount* _get_inner(const Key& key) const
    {
        auto it = _resources_index.find(key);
        if (it != _resources_index.end())
        {
            const auto* ptr = _resources.get_object(it->second);
            assert(ptr);
            return ptr;
        }
        else
        {
            return nullptr;
        }
    }

    inline RefCount* _get_inner(const Key& key)
    {
        auto it = _resources_index.find(key);
        if (it != _resources_index.end())
        {
            auto* ptr = _resources.get_object(it->second);
            assert(ptr);
            return ptr;
        }
        else
        {
            return nullptr;
        }
    }

public:
    explicit GpuResourcesCollection(const monitoring::ResourceOwner& resource_owner) :
        _resource_owner(resource_owner)
    {
    }

    void acquire(const Key& key, BlobLibrary* bl, ModelDescriptor* descriptor)
    {
        auto* resource = _get_inner(key);
        if (!resource)
        {
            std::optional<T> res = T::acquire(key, bl, descriptor, _resource_owner);
            if (res.has_value())
            {
                auto id = _resources.alloc(RefCount{1, std::move(std::move(res).value())});
                auto* obj = _resources.get_object(id);

                _resources_index.insert(std::make_pair(key, id));

                switch (obj->value.get_status())
                {
                    case GpuResourceStatus::Loading:
                    {
                        _loading_resources.insert(key);
                        break;
                    }
                    case GpuResourceStatus::Loaded:
                    {
                        _uploading_resources.insert(key);
                        break;
                    }
                    default:
                    {
                        break;
                    }
                }
            }
        }
        else
        {
            resource->rc += 1;
            if (resource->rc == 1) // Ref count was 0, so remove from the resources to destroy now
            {
                assert(_to_destroy.count(key) > 0);
                _to_destroy.erase(key);
            }
        }
    }

    void release(const Key& key)
    {
        auto* resource = _get_inner(key);
        if (resource)
        {
            assert(resource->rc > 0);
            resource->rc -= 1;
            if (resource->rc == 0)
            {
                assert(_to_destroy.count(key) == 0);
                _to_destroy.insert(key);
            }
        }
    }

    T* get(const Key& key)
    {
        auto* res = _get_inner(key);
        if (res)
        {
            return &res->value;
        }
        else
        {
            return nullptr;
        }
    }

    const T* get(const Key& key) const
    {
        const auto* res = _get_inner(key);
        if (res)
        {
            return &res->value;
        }
        else
        {
            return nullptr;
        }
    }

    void work(
        BlobLibrary* bl,
        BlobAllocator* ba,
        JobScheduler* js,
        ImageDecoder* imgdec,
        std::vector<my::ResourceHandle>& to_destroy)
    {
        for (auto it = _loading_resources.begin(); it != _loading_resources.end();)
        {
            auto* resource = _get_inner(*it);
            assert(resource);
            resource->value.work(bl, ba, js, imgdec);

            switch (resource->value.get_status())
            {
                case GpuResourceStatus::Loaded:
                {
                    _uploading_resources.insert(*it);
                    _loading_resources.erase(it++);
                    break;
                }
                case GpuResourceStatus::Ready:
                case GpuResourceStatus::Error:
                {
                    _loading_resources.erase(it++);
                    break;
                }
                default:
                {
                    ++it;
                    break;
                }
            }
        }

        if (!_to_destroy.empty())
        {
            for (const Key& key : _to_destroy)
            {
                auto it = _resources_index.find(key);
                assert(it != _resources_index.end());
                auto id = it->second;
                auto* resource = _get_inner(key);
                resource->value.destroy(bl, ba, js, to_destroy);

                _loading_resources.erase(key);
                _uploading_resources.erase(key);

                _resources.release(id);
                _resources_index.erase(key);
            }
            _to_destroy.clear();
        }
    }

    void work_gpu(BlobAllocator* ba, BlobLibrary* bl, Render* render)
    {
        for (auto it = _uploading_resources.begin(); it != _uploading_resources.end();)
        {
            auto* resource = _get_inner(*it);
            assert(resource);

            resource->value.work_gpu(ba, bl, render);

            switch (resource->value.get_status())
            {
                case GpuResourceStatus::Ready:
                case GpuResourceStatus::Error:
                {
                    _uploading_resources.erase(it++);
                    break;
                }
                default:
                {
                    ++it;
                    break;
                }
            }
        }
    }

    void destroy_all(
        BlobLibrary* bl,
        BlobAllocator* ba,
        JobScheduler* js,
        std::vector<my::ResourceHandle>& to_destroy)
    {
        for (auto& it : _resources_index)
        {
            auto* resource = _resources.get_object(it.second);
            assert(resource);
            resource->value.destroy(bl, ba, js, to_destroy);
            _resources.release(it.second);
        }

        _resources_index.clear();
        _loading_resources.clear();
        _uploading_resources.clear();
        _to_destroy.clear();
    }

    GpuResourceStatus get_status(const Key& key) const
    {
        const auto* resource = _get_inner(key);
        if (resource)
        {
            return resource->value.get_status();
        }
        else
        {
            return GpuResourceStatus::Error;
        }
    }
};

struct GpuResources
{
    GpuResourcesCollection<int, GpuBufferResource<my::BufferResource::Vertex>> vertex_buffers;
    GpuResourcesCollection<int, GpuBufferResource<my::BufferResource::Index>> index_buffers;
    GpuResourcesCollection<int, GpuDracoMeshResource> draco_meshes;
    GpuResourcesCollection<SamplerWithParams, GpuSamplerResource> samplers;
    GpuResourcesCollection<TextureWithCfg, GpuTextureResource> textures;

    explicit GpuResources(const monitoring::ResourceOwner& resource_owner) :
        vertex_buffers(resource_owner),
        index_buffers(resource_owner),
        draco_meshes(resource_owner),
        samplers(resource_owner),
        textures(resource_owner)
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
    }

    void work_gpu(BlobAllocator* ba, BlobLibrary* bl, Render* render)
    {
        vertex_buffers.work_gpu(ba, bl, render);
        index_buffers.work_gpu(ba, bl, render);
        draco_meshes.work_gpu(ba, bl, render);
        samplers.work_gpu(ba, bl, render);
        textures.work_gpu(ba, bl, render);
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
    }
};

} // namespace hrz::model
