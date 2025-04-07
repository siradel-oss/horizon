#include "model/hrz_core_model_gpu_resources.h"

#include <hrz_fnd_log.h>

namespace hrz::model
{
static constexpr BlobLibrary::ConfigH NullCfg = {0};

std::optional<GpuDracoMeshResource> GpuDracoMeshResource::acquire(
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
                return GpuDracoMeshResource(
                    bl, buffer.blob.value(), view.byte_offset, view.byte_length, owner);
            }
            else if (descriptor->embedded_resources.has_value())
            {
                return GpuDracoMeshResource(
                    bl, descriptor->embedded_resources.value(), view.byte_offset, view.byte_length,
                    owner);
            }
        }
    }

    HRZ_LOG_ERROR("Couldn't create Draco mesh from view {}", view_id);
    return std::nullopt;
}

GpuDracoMeshResource::GpuDracoMeshResource(
    BlobLibrary* bl,
    BlobLibrary::Handle blob_handle_,
    size_t byte_offset_,
    size_t byte_length_,
    const monitoring::ResourceOwner& owner_) :
    status(Status::LoadingBlob),
    blob_handle(blob_handle_),
    blob_byte_offset(byte_offset_),
    blob_byte_length(byte_length_),
    vertex_buffer(my::ResourceHandle::null()),
    index_buffer(my::ResourceHandle::null()),
    owner(owner_)
{
    bl->acquire(blob_handle_, NullCfg);
}

void GpuDracoMeshResource::work(BlobLibrary* bl, BlobAllocator* ba, JobScheduler* js, ImageDecoder*)
{
    if (status == Status::Ready) return;

    if (status == Status::LoadingBlob)
    {
        assert(blob_handle.has_value());

        auto blob_status = bl->get_status(blob_handle.value(), NullCfg);
        switch (blob_status)
        {
            case BlobLibrary::Status::Loading: break;
            case BlobLibrary::Status::Loaded:
            {
                uri = bl->get_uri(blob_handle.value(), NullCfg);

                auto [blob, mime_type] = bl->get_blob(blob_handle.value(), NullCfg);
                auto sub_blob = blobs::make_sub_blob(ba, blob, blob_byte_offset, blob_byte_length);

                decompression_ticket = hrz_jobs::add_job_decompress_draco_mesh(js, sub_blob, owner);

                bl->release(blob_handle.value(), NullCfg);
                blob_handle = std::nullopt;

                status = Status::DecodingMesh;
                break;
            }
            case BlobLibrary::Status::Unloaded:
            {
                assert(!"Shouldn't be unloaded at this time");
                status = Status::Error;
                break;
            }
            default:
            {
                status = Status::Error;
                break;
            }
        }
    }

    if (status == Status::DecodingMesh)
    {
        if (hrz_jobs::is_job_finished(js, decompression_ticket))
        {
            if (hrz_jobs::get_job_status(js, decompression_ticket)
                == job_scheduler::JobStatus::Finished_Success)
            {
                hrz_jobs::get_job_response(js, decompression_ticket, mesh);

                for (size_t i = 0; i < (size_t)mesh.attributes.size(); ++i)
                {
                    attrib_id_to_index.insert(std::make_pair(mesh.attributes.at(i).id, (int)i));
                }

                status = Status::UploadingData;
            }
            else
            {
                hrz_jobs::cancel_job(js, decompression_ticket);
                status = Status::Error;
            }
        }
    }
}

void GpuDracoMeshResource::work_gpu(BlobAllocator* ba, BlobLibrary* bl, Render* render)
{
    if (status == Status::Ready) return;

    if (status == Status::UploadingData)
    {
        {
            auto vertex_data = mesh.vertex_data.get_data();
            my::BufferResource vb_res(my::BufferResource::Vertex);
            vb_res.data = vertex_data.data();
            vb_res.size = vertex_data.size();
            vb_res.usage = my::UsageHint::Static;
            vb_res.allow_allocation_failure = true;
            vertex_buffer = render->rc->alloc(
                &vb_res, owner,
                {{"contents"_ss, "Draco mesh vertex data"_ss},
                 {"blob library base URL"_ss, bl->get_base_url().base()},
                 {"URI"_ss, uri}});

            if (vertex_buffer.is_null())
            {
                HRZ_LOG_ERROR("Could not upload model vertex buffer to the GPU");
            }
        }

        if (!vertex_buffer.is_null())
        {
            auto index_data = mesh.index_data.get_data();
            my::BufferResource ib_res(my::BufferResource::Index);
            ib_res.data = index_data.data();
            ib_res.size = index_data.size();
            ib_res.usage = my::UsageHint::Static;
            ib_res.allow_allocation_failure = true;
            index_buffer = render->rc->alloc(
                &ib_res, owner,
                {{"contents"_ss, "Draco mesh indices"_ss},
                 {"blob library base URL"_ss, bl->get_base_url().base()},
                 {"URI"_ss, uri}});

            if (vertex_buffer.is_null())
            {
                HRZ_LOG_ERROR("Could not upload model index buffer to the GPU");
            }
        }

        mesh.vertex_data.release();
        mesh.index_data.release();

        status =
            (vertex_buffer.is_null() || index_buffer.is_null()) ? Status::Error : Status::Ready;
    }
}

void GpuDracoMeshResource::destroy(
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

    if (!vertex_buffer.is_null())
    {
        to_destroy.push_back(vertex_buffer);
    }

    if (!index_buffer.is_null())
    {
        to_destroy.push_back(index_buffer);
    }

    if (status == Status::DecodingMesh)
    {
        hrz_jobs::cancel_job(js, decompression_ticket);
    }
}

} // namespace hrz::model
