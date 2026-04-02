#pragma once

#include "hrz/common/blob_image.h"
#include "hrz/common/monitoring_defs.h"
#include "hrz/core/jobs/decompress_draco_mesh.h"
#include "hrz/core/jobs/jobs_tickets.h"
#include "hrz/core/model/blob_library.h"
#include "hrz/core/model/common.h"
#include "hrz/core/model/descriptor.h"

#include <mycelium/backend.h>

namespace hrz
{

struct ImageDecoder;
struct Render;

} // namespace hrz

namespace hrz::model
{

enum class ResourceStatus
{
    Loading, // Streaming-in in main memory, or being processed.
    Loaded,  // Ready to be uploaded to video memory.
    Ready,
    Error,
};

template<typename T>
concept ResourceKey = std::equality_comparable<T> && requires(const T& a) {
    { absl::Hash<T>{}(a) } -> std::convertible_to<size_t>;
};

template<typename T, typename Key>
concept Resource = ResourceKey<Key>
    && requires(const T& res_const,
                T res,
                const Key& key,
                BlobLibrary* bl,
                BlobAllocator* ba,
                JobScheduler* js,
                ImageDecoder* imgdec,
                ModelDescriptor* descriptor,
                Render* render,
                const monitoring::ResourceOwner& owner) {
           { T::acquire(key, bl, descriptor, owner) } -> std::convertible_to<std::optional<T>>;
           { res.work(bl, ba, js, imgdec) };
           { res.work_gpu(ba, bl, render) };
           { res.destroy(bl, ba, js, std::declval<std::vector<my::ResourceHandle>&>()) };
           { res_const.get_status() } -> std::same_as<ResourceStatus>;
       };

template<my::BufferResource::BufferType TYPE>
struct GpuBufferResource
{
    std::optional<BlobLibrary::Handle> blob_handle;
    my::ResourceHandle render_handle;
    size_t offset_in_source_buffer;
    size_t length;
    size_t stride;

    ResourceStatus status;

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

    constexpr ResourceStatus get_status() const { return status; }
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
    hrz_jobs::DecompressedDracoMesh mesh;
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

    constexpr ResourceStatus get_status() const
    {
        switch (status)
        {
            case Status::LoadingBlob: return ResourceStatus::Loading;
            case Status::DecodingMesh: return ResourceStatus::Loading;
            case Status::UploadingData: return ResourceStatus::Loaded;
            case Status::Ready: return ResourceStatus::Ready;
            case Status::Error: return ResourceStatus::Error;
            default: return ResourceStatus::Error;
        }
    }
};

struct SamplerWithParams
{
    int sampler_id;
    bool can_use_linear_filtering;
    bool can_use_mipmaps;

    constexpr bool operator ==(const SamplerWithParams& other) const = default;

    template<typename H>
    friend H AbslHashValue(H h, const SamplerWithParams& k)
    {
        return H::combine(
            std::move(h), k.sampler_id, k.can_use_linear_filtering, k.can_use_mipmaps);
    }
};

struct GpuSamplerResource
{
    ModelDescriptor::Sampler desc;
    bool can_use_linear_filtering;
    bool can_use_mipmaps;
    my::ResourceHandle render_handle;
    ResourceStatus status;

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

    constexpr ResourceStatus get_status() const { return status; }
};

struct TextureWithCfg
{
    int texture_id;
    bool is_data;
    BlobLibrary::ConfigH cfg;

    constexpr bool operator ==(const TextureWithCfg& other) const
    {
        return texture_id == other.texture_id && is_data == other.is_data && cfg.o == other.cfg.o;
    }

    template<typename H>
    friend H AbslHashValue(H h, const TextureWithCfg& k)
    {
        return H::combine(std::move(h), k.texture_id, k.is_data, k.cfg.o);
    }
};

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

    constexpr ResourceStatus get_status() const
    {
        switch (status)
        {
            case Status::LoadingCompressed: return ResourceStatus::Loading;
            case Status::Decompressing: return ResourceStatus::Loading;
            case Status::Uploading: return ResourceStatus::Loaded;
            case Status::Ready: return ResourceStatus::Ready;
            case Status::Error: return ResourceStatus::Error;
            default: return ResourceStatus::Error;
        }
    }
};

struct AnimationResource
{
    struct Accessor
    {
        BlobLibrary::Handle blob_handle;

        my::VertexFormat type;
        size_t byte_offset;
        size_t byte_stride;
        size_t count;

        std::vector<float> data;
    };

    struct Sampler
    {
        int timestamp_accessor;
        int value_accessor;
    };

    // Temporary data used during loading & decoding.
    hrz::flat_hash_map<int, Accessor> accessor_defs;
    UsedResources<BlobLibrary::Handle> used_buffers;
    std::vector<Sampler> samplers_defs;

    Animation animation;
    ResourceStatus status = ResourceStatus::Loading;

    static std::optional<AnimationResource> acquire(
        int animation_id,
        BlobLibrary* bl,
        ModelDescriptor* descriptor,
        const monitoring::ResourceOwner& owner);

    void work(BlobLibrary* bl, BlobAllocator* ba, JobScheduler* js, ImageDecoder* imgdec);

    void work_gpu(BlobAllocator* ba, BlobLibrary* bl, Render* render);

    void destroy(
        BlobLibrary* bl,
        BlobAllocator* ba,
        JobScheduler* js,
        std::vector<my::ResourceHandle>& to_destroy);

    constexpr ResourceStatus get_status() const { return status; }

    void expunge_tmp_data(BlobLibrary* bl);

    bool fetch_data(BlobLibrary* bl);

    void build_animation();
};

} // namespace hrz::model
