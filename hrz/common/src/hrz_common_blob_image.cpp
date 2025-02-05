#include "hrz_common_blob_image.h"

#include <string>

namespace hrz
{
// @Todo Deduplicate (wrt hrz_core_image_utils.h)
my::TextureFormat BlobImage::image_format_to_gpu_format(hrz_proto::ImageFormat format)
{
    switch (format)
    {
        case hrz_proto::ImageFormat::SRGBA_8: return my::TextureFormat::RGBA8;
        case hrz_proto::ImageFormat::SIGNED_FIXED_24_8: return my::TextureFormat::R32I;
        case hrz_proto::ImageFormat::R_F32: return my::TextureFormat::R32F;
        case hrz_proto::ImageFormat::R_F32_SILICIUM:
        case hrz_proto::ImageFormat::MAPZEN_TERRARIUM: return my::TextureFormat::R32UI;
        case hrz_proto::ImageFormat::SRGB_R_8: return my::TextureFormat::R8;
        default:
            HRZ_LOG_ERROR("Unhandled image format: {}", hrz_proto::ImageFormat_Name(format));
            assert(false);
            return my::TextureFormat::RGBA8;
    }
}

std::optional<hrz_proto::ImageFormat> BlobImage::gpu_format_to_image_format(
    my::TextureFormat format)
{
    switch (format)
    {
        case my::TextureFormat::RGBA8: return hrz_proto::ImageFormat::SRGBA_8;
        case my::TextureFormat::R32I: return hrz_proto::ImageFormat::SIGNED_FIXED_24_8;
        case my::TextureFormat::R32F: return hrz_proto::ImageFormat::R_F32;
        case my::TextureFormat::R8: return hrz_proto::ImageFormat::SRGB_R_8;
        default: return std::nullopt;
    }
}

void BlobImage::register_layout_blob_metadata(BlobAllocator* ba)
{
    assert(ba);

    blobs::register_metadata(ba, _blob, "type"_ss, "image data"_ss);
    blobs::register_metadata(ba, _blob, "format"_ss, my::format_str(_layout.format));
    blobs::register_metadata(ba, _blob, "width"_ss, std::to_string(_layout.width));
    blobs::register_metadata(ba, _blob, "height"_ss, std::to_string(_layout.height));
}

void BlobImage::register_blob_metadata(BlobAllocator* ba, MetadataString key, MetadataString value)
{
    assert(ba);

    blobs::register_metadata(ba, _blob, std::move(key), std::move(value));
}

void BlobImage::register_blob_owner(BlobAllocator* ba, const monitoring::ResourceOwner& owner)
{
    assert(ba);

    blobs::register_owner(ba, _blob, owner);
}

BlobImage BlobImage::make(
    my::TextureFormat format,
    uint32_t width,
    uint32_t height,
    uint32_t levels,
    blobs::BlobHandle&& blob,
    BlobAllocator* ba)
{
    assert(ba);

    auto image = hrz::BlobImage(format, width, height, levels, std::move(blob));
    image.register_layout_blob_metadata(ba);
    return image;
}

BlobImage BlobImage::make(
    my::TextureFormat format,
    uint32_t width,
    uint32_t height,
    blobs::BlobHandle&& blob,
    BlobAllocator* ba)
{
    assert(ba);

    auto image = hrz::BlobImage(format, width, height, std::move(blob));
    image.register_layout_blob_metadata(ba);
    return image;
}

BlobImage BlobImage::make(
    my::TextureFormat format,
    uint32_t width,
    uint32_t height,
    const blobs::BlobHandle& blob,
    BlobAllocator* ba)
{
    assert(ba);

    auto image = hrz::BlobImage(format, width, height, blob);
    image.register_layout_blob_metadata(ba);
    return image;
}

BlobImage BlobImage::make(
    hrz_proto::ImageFormat format,
    uint32_t width,
    uint32_t height,
    blobs::BlobHandle&& blob,
    BlobAllocator* ba)
{
    assert(ba);

    auto image = hrz::BlobImage(format, width, height, std::move(blob));
    image.register_layout_blob_metadata(ba);
    return image;
}

BlobImage BlobImage::make(
    hrz_proto::ImageFormat format,
    uint32_t width,
    uint32_t height,
    const blobs::BlobHandle& blob,
    BlobAllocator* ba)
{
    assert(ba);

    auto image = hrz::BlobImage(format, width, height, blob);
    image.register_layout_blob_metadata(ba);
    return image;
}
} // namespace hrz
