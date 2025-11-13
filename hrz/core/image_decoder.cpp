#include "hrz/core/image_decoder.h"

#include "hrz/common/platform_detection.h"

#include <mycelium/backend.h>

#include <cassert>

namespace hrz
{
struct ImageDecoder
{
    PlatformInfo platform_info;
    my::Instance::Info my_instance_info;
};

namespace image_decoder
{
ImageDecoder* create(const PlatformInfo& platform_info, const my::Instance::Info& my_instance_info)
{
    auto decoder = new ImageDecoder();
    decoder->platform_info = platform_info;
    decoder->my_instance_info = my_instance_info;

    return decoder;
}

void destroy(ImageDecoder* decoder)
{
    assert(decoder);
    delete decoder;
}

hrz_jobs::DecodeBlobImageTicket decode_async(
    ImageDecoder* decoder,
    JobScheduler* job_scheduler,
    blobs::BlobHandle blob,
    hrz::monitoring::ResourceOwner resource_owner,
    hrz_proto::ImageFormat format,
    std::string_view mime_type,
    ConvertScalarsToFloat scalar_conversion,
    DecodeToCompressedImage decode_to_compressed_image)
{
    assert(decoder);

    BlobImageDecodingParams params;
    params.encoded_image_data = std::move(blob);
    params.image_format = format;
    params.mime_type = mime_type;
    params.allow_decoding_to_compressed_image =
        decode_to_compressed_image == DecodeToCompressedImage::Allow;
    params.convert_scalars_to_float = scalar_conversion == ConvertScalarsToFloat::Convert;
    params.platform_info = decoder->platform_info;
    params.my_instance_info = decoder->my_instance_info;

    return hrz_jobs::add_job_decode_blob_image(job_scheduler, params, resource_owner);
}

hrz_jobs::DecodeBlobImageTicket decode_async(
    JobScheduler* job_scheduler,
    blobs::BlobHandle blob,
    hrz::monitoring::ResourceOwner resource_owner,
    hrz_proto::ImageFormat format,
    std::string_view mime_type,
    ConvertScalarsToFloat scalar_conversion)
{
    BlobImageDecodingParams params;
    params.encoded_image_data = std::move(blob);
    params.image_format = format;
    params.mime_type = mime_type;
    params.convert_scalars_to_float = scalar_conversion == ConvertScalarsToFloat::Convert;

    // Platform info is necessary to decode to a compressed image,
    // so without the image decoder instance, it cannot be done.
    params.allow_decoding_to_compressed_image = false;

    return hrz_jobs::add_job_decode_blob_image(job_scheduler, params, resource_owner);
}

BlobImage job_to_image(JobScheduler* job_scheduler, hrz_jobs::DecodeBlobImageTicket ticket)
{
    if (hrz_jobs::get_job_status(job_scheduler, ticket)
        == job_scheduler::JobStatus::Finished_Success)
    {
        hrz::BlobImage response;
        hrz_jobs::get_job_response(job_scheduler, ticket, response);

        return response;
    }
    else
    {
        hrz_jobs::cancel_job(job_scheduler, ticket);
        return {};
    }
}
} // namespace image_decoder
} // namespace hrz
