#include "hrz_core_blob_image.h"
#include "hrz_core_image_decoder.h"
#include "model/hrz_core_model_gpu_resources.h"

#include <hrz_fnd_log.h>

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
std::optional<GpuTextureResource> GpuTextureResource::acquire(
    TextureWithCfg id,
    BlobLibrary* bl,
    ModelDescriptor* descriptor,
    const monitoring::ResourceOwner& owner)
{
    const auto* texture = _get_ptr(descriptor->textures, id.texture_id);
    if (texture && texture->source.has_value())
    {
        std::optional<hrz_proto::ImageFormat> data_interpretation = std::nullopt;
        bool use_mipmap = !id.is_data;

        if (!id.is_data && texture->sampler.has_value())
        {
            int sampler_id = texture->sampler.value();
            const auto* sampler = _get_ptr(descriptor->samplers, sampler_id);
            if (sampler)
            {
                use_mipmap = sampler->use_mipmap;
            }
        }

        if (texture->data_intepretation.has_value())
        {
            if (id.is_data)
            {
                if (texture->data_intepretation == hrz_proto::ImageFormat::R_F32
                    || texture->data_intepretation == hrz_proto::ImageFormat::R_F32_SILICIUM)
                {
                    data_interpretation = texture->data_intepretation;
                }
                else
                {
                    // The glTF parser should not have let this value through.
                    assert(false);
                }
            }
            else
            {
                HRZ_LOG_WARNING(
                    "glTF texture has data interpretation for non-data material, ignoring it");
            }
        }

        if (id.is_data && !data_interpretation.has_value())
        {
            // Undocumented fallback to R_F32_SILICIUM, for models that have been generated
            // according to the old specification, where data interpretation was not
            // specified but Horizon expected R_F32_SILICIUM.
            HRZ_LOG_WARNING(
                "glTF data texture has no data interpretation, assuming R_F32_SILICIUM");

            data_interpretation = hrz_proto::ImageFormat::R_F32_SILICIUM;
        }

        int source_id = texture->source.value();
        const auto* image = _get_ptr(descriptor->images, source_id);
        if (!image) return std::nullopt;

        if (image->blob.has_value())
        {
            return GpuTextureResource(
                bl, image->blob.value(), id.cfg, id.is_data, data_interpretation, 0, 0, use_mipmap,
                owner);
        }
        else if (image->buffer_view.has_value())
        {
            const auto* view = _get_ptr(descriptor->buffer_views, image->buffer_view.value());
            if (view)
            {
                const auto* buffer = _get_ptr(descriptor->buffers, view->buffer);
                if (buffer)
                {
                    assert(view->byte_length <= buffer->byte_length);
                    assert(view->byte_offset + view->byte_length <= buffer->byte_length);

                    if (buffer->blob.has_value())
                    {
                        return GpuTextureResource(
                            bl, buffer->blob.value(), id.cfg, id.is_data, data_interpretation,
                            view->byte_offset, view->byte_length, use_mipmap, owner);
                    }
                    else if (descriptor->embedded_resources.has_value())
                    {
                        return GpuTextureResource(
                            bl, descriptor->embedded_resources.value(), id.cfg, id.is_data,
                            data_interpretation, view->byte_offset, view->byte_length, use_mipmap,
                            owner);
                    }
                }
            }
        }
    }

    return std::nullopt;
}

GpuTextureResource::GpuTextureResource(
    BlobLibrary* bl,
    BlobLibrary::Handle blob,
    BlobLibrary::ConfigH cfg_,
    bool is_data_texture_,
    std::optional<hrz_proto::ImageFormat> data_interpretation_,
    size_t offset,
    size_t length,
    bool use_mipmaps_,
    const monitoring::ResourceOwner& owner_) :
    cfg(cfg_),
    is_data_texture(is_data_texture_),
    data_interpretation(data_interpretation_),
    use_mipmaps(use_mipmaps_),
    compressed_blob_handle(blob),
    blob_offset(offset),
    blob_length(length),
    render_handle(my::ResourceHandle::null()),
    status(Status::LoadingCompressed),
    owner(owner_)
{
    bl->acquire(blob, cfg);
}

void GpuTextureResource::work(
    BlobLibrary* bl,
    BlobAllocator* ba,
    JobScheduler* js,
    ImageDecoder* imgdec)
{
    if (status == Status::LoadingCompressed)
    {
        assert(compressed_blob_handle.has_value());

        auto blob_status = bl->get_status(compressed_blob_handle.value(), cfg);
        switch (blob_status)
        {
            case BlobLibrary::Unloaded:
            {
                assert(!"Blob shouldn't be unloaded at this point");
                break;
            }
            case BlobLibrary::Loaded:
            {
                compressed_data_uri = bl->get_uri(compressed_blob_handle.value(), cfg);
                auto [buffer_blob, mime_type] = bl->get_blob(compressed_blob_handle.value(), cfg);

                if (buffer_blob.data_size() >= blob_offset + blob_length)
                {
                    if (blob_length == 0)
                    {
                        compressed_data_handle = blobs::make_sub_blob(
                            hrz::unsafe("Offset and length are checked above"), ba, buffer_blob,
                            blob_offset);
                    }
                    else
                    {
                        compressed_data_handle = blobs::make_sub_blob(
                            hrz::unsafe("Offset is checked above"), ba, buffer_blob, blob_offset,
                            blob_length);
                    }

                    const hrz_proto::ImageFormat format = is_data_texture
                        ? data_interpretation.value_or(hrz_proto::ImageFormat::R_F32_SILICIUM)
                        : hrz_proto::ImageFormat::SRGBA_8;
                    auto scalar_conversion = is_data_texture
                        ? image_decoder::ConvertScalarsToFloat::Convert
                        : image_decoder::ConvertScalarsToFloat::DoNotConvert;

                    decompress_ticket = image_decoder::decode_async(
                        imgdec, js, std::move(compressed_data_handle.value()), owner, format,
                        mime_type, scalar_conversion,
                        image_decoder::DecodeToCompressedImage::Allow);

                    compressed_data_handle = std::nullopt;

                    status = Status::Decompressing;
                }
                else
                {
                    HRZ_LOG_ERROR("Blob is too small to contain the image");
                    status = Status::Error;
                }

                bl->release(compressed_blob_handle.value(), cfg);
                compressed_blob_handle = std::nullopt;
                break;
            }
            case BlobLibrary::Error:
            {
                status = Status::Error;
                break;
            }
            default: break;
        }
    }
    else if (status == Status::Decompressing)
    {
        if (hrz_jobs::is_job_valid(js, decompress_ticket)
            && hrz_jobs::is_job_finished(js, decompress_ticket))
        {
            if (hrz_jobs::get_job_status(js, decompress_ticket)
                == job_scheduler::JobStatus::Finished_Success)
            {
                BlobImage image;
                hrz_jobs::get_job_response(js, decompress_ticket, image);
                image.register_blob_metadata(ba, "image type"_ss, "model texture"_ss);
                image.register_blob_owner(ba, owner);

                // If the image is compressed and lacks mipmaps, they cannot be generated
                // by the GPU, and therefore cannot be used.
                use_mipmaps &= !my::is_format_compressed(image.format()) || image.levels() > 1;

                decompressed_image = {std::move(image)};
                status = Status::Uploading;
            }
            else
            {
                status = Status::Error;
            }
        }
    }
}

void GpuTextureResource::work_gpu(BlobAllocator* ba, BlobLibrary* bl, Render* render)
{
    if (status == Status::Uploading)
    {
        assert(decompressed_image.has_value());
        render_handle = hrz::to_gpu(
            decompressed_image.value(), render->rc, use_mipmaps, true, owner,
            {{"blob library base URL"_ss, bl->get_base_url().base()},
             {"URI"_ss, compressed_data_uri}});
        decompressed_image = std::nullopt;
        status = render_handle.is_null() ? Status::Error : Status::Ready;

        if (render_handle.is_null())
        {
            HRZ_LOG_ERROR("Could not upload model texture to the GPU");
        }
    }
}

void GpuTextureResource::destroy(
    BlobLibrary* bl,
    BlobAllocator* ba,
    JobScheduler* js,
    std::vector<my::ResourceHandle>& to_destroy)
{
    if (compressed_blob_handle.has_value())
    {
        bl->release(compressed_blob_handle.value(), cfg);
    }

    if (decompressed_image.has_value())
    {
        decompressed_image = std::nullopt;
    }

    if (compressed_data_handle.has_value())
    {
        compressed_data_handle = std::nullopt;
    }

    if (hrz_jobs::is_job_valid(js, decompress_ticket))
    {
        hrz_jobs::cancel_job(js, decompress_ticket);
    }

    if (status == Status::Ready && !render_handle.is_null())
    {
        to_destroy.push_back(render_handle);
    }
}

} // namespace hrz::model
