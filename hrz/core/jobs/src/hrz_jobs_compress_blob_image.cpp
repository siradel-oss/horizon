#include "hrz_jobs_declarations.h"

#include <hrz_common_blob_allocator.h>
#include <hrz_common_blob_image.h>
#include <hrz_common_profiling.h>
#include <hrz_fnd_log.h>

#include <etcpak/ProcessDxtc.hpp>
#include <etcpak/ProcessEtc.hpp>
#include <mycelium_backend.h>

#include <cassert>

namespace hrz_jobs::compress_blob_image
{
hrz::JobResult run(
    const hrz::BlobImageCompressionParams& params,
    hrz::BlobImage& compressed_image,
    const JobContext& context)
{
    HRZ_SCOPED_SAMPLE("compress blob image job");

    if (!my::is_format_compressed(params.output_format))
    {
        HRZ_LOG_ERROR("Target format is not compressed");
        return hrz::JobResult::FAILURE;
    }

    if (params.output_format == params.image.format())
    {
        compressed_image = std::move(params.image);
        return hrz::JobResult::SUCCESS;
    }

    if (params.image.format() != my::TextureFormat::RGBA8)
    {
        HRZ_LOG_ERROR("Unsupported input image format");
        return hrz::JobResult::FAILURE;
    }

    if (params.image.width() % 4 != 0 || params.image.height() % 4 != 0)
    {
        HRZ_LOG_ERROR("Image dimensions must be multiples of 4");
        return hrz::JobResult::FAILURE;
    }

    if (params.output_format != my::TextureFormat::RGBA_BC3
        && params.output_format != my::TextureFormat::RGBA_ETC2_EAC)
    {
        HRZ_LOG_ERROR("Unsupported compressed texture format");
        return hrz::JobResult::FAILURE;
    }

    my::TextureLayout layout;
    layout.type = my::TextureLayout::Type2D;
    layout.format = params.output_format;
    layout.width = params.image.width();
    layout.height = params.image.height();
    layout.depth = 1;
    layout.levels = 1;

    auto compressed_image_byte_size = layout.get_level_byte_size(0);

    auto compressed_image_blob =
        hrz::blobs::allocate_blob_sync(context.get_blob_allocator(), compressed_image_byte_size);
    if (!compressed_image_blob.has_value())
    {
        HRZ_LOG_ERROR("Could not allocate blob of size {}", compressed_image_byte_size);
        return hrz::JobResult::FAILURE;
    }

    {
        if (params.image.blob().data_alignment() < alignof(uint32_t)
            || compressed_image_blob->data_alignment() < alignof(uint64_t))
        {
            return hrz::JobResult::FAILURE;
        }

        auto uncompressed_image_data = params.image.blob().get_mutable_data();
        auto rgba_data = std::span<uint32_t>{
            (uint32_t*)uncompressed_image_data.data(),
            uncompressed_image_data.size() / sizeof(uint32_t)};

        auto compressed_image_data = compressed_image_blob->get_mutable_data();
        auto block_data = std::span<uint64_t>{
            (uint64_t*)compressed_image_data.data(),
            compressed_image_data.size() / sizeof(uint64_t)};

        auto block_count_width = layout.get_level_data_width(0) / 4;
        auto block_count_height = layout.get_level_data_height(0) / 4;

        switch (params.output_format)
        {
            case my::TextureFormat::RGBA_BC3:
                etcpak::CompressDxt5(
                    rgba_data.data(), block_data.data(), block_count_width * block_count_height,
                    block_count_width * 4);
                break;
            case my::TextureFormat::RGBA_ETC2_EAC:
            {
                // Unfortunately, etcpak expects data in BGRA format when compressing to ETC.
                // See https://github.com/wolfpld/etcpak/issues/34

                size_t rgba_data_size = rgba_data.size();
                for (size_t i = 0; i < rgba_data_size; ++i)
                {
                    uint32_t rgba = rgba_data[i];
                    uint32_t bgra = (rgba & 0xff00ff00) + ((rgba & 0x000000ff) << 16)
                        + ((rgba & 0x00ff0000) >> 16);
                    rgba_data[i] = bgra;
                }

                etcpak::CompressEtc2Rgba(
                    rgba_data.data(), block_data.data(), block_count_width * block_count_height,
                    block_count_width * 4, true);
                break;
            }
            default: assert(false && "Unhandled case"); return hrz::JobResult::FAILURE;
        }
    }

    compressed_image = hrz::BlobImage::make(
        params.output_format, layout.width, layout.height, layout.levels,
        std::move(compressed_image_blob.value()), context.get_blob_allocator());

    return hrz::JobResult::SUCCESS;
}
} // namespace hrz_jobs::compress_blob_image
