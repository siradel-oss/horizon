#pragma once

#include "hrz_jobs_tickets.h"

#include <hrz_common_blob_image.h>
#include <hrz_common_monitoring_defs.h>
#include <hrz_protocol_all.h>

#include <gsl/gsl-lite.hpp>

#include <optional>

namespace hrz
{
struct JobScheduler;
struct ImageDecoder;
struct PlatformInfo;

namespace image_decoder
{
enum class PremultiplyAlpha
{
    DoNotPremultiply,
    Premultiply,
};

enum class ConvertScalarsToFloat
{
    DoNotConvert,
    Convert,
};

enum class DecodeToCompressedImage
{
    Disallow,
    Allow,
};

ImageDecoder* create(const PlatformInfo&, const my::Instance::Info&);

void destroy(ImageDecoder*);

/**
 * Decodes the image contained in the given blob in a job.
 * The encoding type is guessed from the blob data.
 * The decoding image will have the format passed as para-
 * meter.
 * The return value is a ticket that can be used to track the
 * advancement of the job.
 */
hrz_jobs::DecodeBlobImageTicket decode_async(
    JobScheduler*,
    blobs::BlobHandle,
    hrz::monitoring::ResourceOwner resource_owner,
    hrz_proto::ImageFormat format = hrz_proto::ImageFormat::SRGBA_8,
    PremultiplyAlpha alpha_premultiplication = PremultiplyAlpha::DoNotPremultiply,
    ConvertScalarsToFloat scalar_conversion = ConvertScalarsToFloat::DoNotConvert);

/**
 * Decodes the image contained in the given blob in a job.
 * The encoding type is guessed from the blob data.
 * The decoding image will have the format passed as para-
 * meter, unless decoding to a compressed image is allowed,
 * in which case the result format can be compressed, but
 * is usable in the same contexts on the GPU.
 * The return value is a ticket that can be used to track the
 * advancement of the job.
 */
hrz_jobs::DecodeBlobImageTicket decode_async(
    ImageDecoder*,
    JobScheduler*,
    blobs::BlobHandle,
    hrz::monitoring::ResourceOwner resource_owner,
    hrz_proto::ImageFormat format = hrz_proto::ImageFormat::SRGBA_8,
    PremultiplyAlpha alpha_premultiplication = PremultiplyAlpha::DoNotPremultiply,
    ConvertScalarsToFloat scalar_conversion = ConvertScalarsToFloat::DoNotConvert,
    DecodeToCompressedImage decode_to_compressed_image = DecodeToCompressedImage::Disallow);

/**
 * Build an image from the given finished blob image decoding job.
 * This can only be called once after the job has finished.
 */
BlobImage job_to_image(JobScheduler*, hrz_jobs::DecodeBlobImageTicket);

} // namespace image_decoder
} // namespace hrz
