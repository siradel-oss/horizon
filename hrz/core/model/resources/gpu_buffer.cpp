#include "hrz/core/model/resources/resource.h"
#include "hrz/core/render/context.h"
#include "hrz/fnd/log.h"

namespace hrz::model
{

static constexpr BlobLibrary::ConfigH NullCfg = {0};

template<my::BufferResource::BufferType TYPE>
std::optional<GpuBufferResource<TYPE>> GpuBufferResource<TYPE>::acquire(
    int view_id,
    BlobLibrary* bl,
    ModelDescriptor* descriptor,
    const monitoring::ResourceOwner& owner)
{
    if (view_id >= 0 && (size_t)view_id < descriptor->buffer_views.size())
    {
        const auto& view = descriptor->buffer_views[view_id];
        if (view.buffer >= 0 && (size_t)view.buffer < descriptor->buffers.size())
        {
            const auto& buffer = descriptor->buffers[view.buffer];

            assert(view.byte_length <= buffer.byte_length);
            assert(view.byte_offset + view.byte_length <= buffer.byte_length);

            if (buffer.blob.has_value())
            {
                return GpuBufferResource<TYPE>(bl, buffer.blob.value(), view, owner);
            }
            else if (descriptor->embedded_resources.has_value())
            {
                return GpuBufferResource<TYPE>(
                    bl, descriptor->embedded_resources.value(), view, owner);
            }
        }
    }
    HRZ_LOG_ERROR("Couldn't create vertex buffer for view {}", view_id);
    return std::nullopt;
}

template<my::BufferResource::BufferType TYPE>
GpuBufferResource<TYPE>::GpuBufferResource(
    BlobLibrary* bl,
    BlobLibrary::Handle blob_handle_,
    const ModelDescriptor::BufferView& view,
    const monitoring::ResourceOwner& owner_) :
    blob_handle(blob_handle_),
    render_handle(my::ResourceHandle::null()),
    offset_in_source_buffer(view.byte_offset),
    length(view.byte_length),
    stride(view.byte_stride),
    status(ResourceStatus::Loading),
    owner(owner_)
{
    bl->acquire(blob_handle_, NullCfg);
}

template<my::BufferResource::BufferType TYPE>
void GpuBufferResource<TYPE>::work(
    BlobLibrary* bl,
    BlobAllocator* ba,
    JobScheduler* js,
    ImageDecoder*)
{
    if (status == ResourceStatus::Loading)
    {
        assert(blob_handle.has_value());

        auto blob_status = bl->get_status(blob_handle.value(), NullCfg);
        switch (blob_status)
        {
            case BlobLibrary::Unloaded:
            {
                assert(!"Blob shouldn't be unloaded at this point");
                status = ResourceStatus::Error;
                break;
            }
            case BlobLibrary::Loaded:
            {
                status = ResourceStatus::Loaded;
                break;
            }
            case BlobLibrary::Error:
            {
                status = ResourceStatus::Error;
                break;
            }
            default: break;
        }
    }
}

template<my::BufferResource::BufferType TYPE>
void GpuBufferResource<TYPE>::work_gpu(BlobAllocator* ba, BlobLibrary* bl, Render* render)
{
    if (status == ResourceStatus::Loaded)
    {
        assert(blob_handle.has_value());

        auto [buffer_blob, mime_type] = bl->get_blob(blob_handle.value(), NullCfg);

        if (buffer_blob.data_size() >= length)
        {
            auto buffer_data = buffer_blob.get_data();

            my::BufferResource buf_res(TYPE);
            buf_res.size = length;
            buf_res.usage = my::UsageHint::Static;
            buf_res.data = (const void*)(buffer_data.data() + offset_in_source_buffer);
            buf_res.allow_allocation_failure = true;

            render_handle = render->rc->alloc(
                &buf_res, owner,
                {{"blob library base URL"_ss, bl->get_base_url().base()},
                 {"URI"_ss, bl->get_uri(blob_handle.value(), NullCfg)}});

            status = render_handle.is_null() ? ResourceStatus::Error : ResourceStatus::Ready;

            if (render_handle.is_null())
            {
                HRZ_LOG_ERROR("Could not upload model buffer to the GPU");
            }
        }
        else
        {
            status = ResourceStatus::Error;
        }

        bl->release(blob_handle.value(), NullCfg);
        blob_handle = std::nullopt;
    }
}

template<my::BufferResource::BufferType TYPE>
void GpuBufferResource<TYPE>::destroy(
    BlobLibrary* bl,
    BlobAllocator* ba,
    JobScheduler* js,
    std::vector<my::ResourceHandle>& to_destroy)
{
    if (blob_handle.has_value())
    {
        bl->release(blob_handle.value(), NullCfg);
        blob_handle = std::nullopt;
    }

    if (!render_handle.is_null())
    {
        to_destroy.push_back(render_handle);
    }
}

template struct GpuBufferResource<my::BufferResource::Vertex>;
template struct GpuBufferResource<my::BufferResource::Index>;

} // namespace hrz::model
