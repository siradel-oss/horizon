#include "hrz/common/blob_image.h"

#include <string>

namespace hrz
{
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
