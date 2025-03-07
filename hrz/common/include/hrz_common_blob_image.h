#pragma once

#include "hrz_common_blob_allocator.h"
#include "hrz_common_metadata.h"
#include "hrz_common_platform_detection.h"

#include <hrz_fnd_log.h>
#include <hrz_protocol_all.h>

#include <mycelium.h>

#include <cassert>
#include <cmath>
#include <optional>

namespace hrz
{
struct BlobImageDecodingParams
{
    hrz::blobs::BlobHandle encoded_image_data;
    hrz_proto::ImageFormat image_format;
    bool premultiply_alpha;
    bool convert_scalars_to_float;
    bool allow_decoding_to_compressed_image;
    PlatformInfo platform_info;
    my::Instance::Info my_instance_info;
};

class BlobImage
{
    my::TextureLayout _layout{};
    std::optional<hrz_proto::ImageFormat> _image_format;
    blobs::BlobHandle _blob;

public:
    BlobImage() = default;

    BlobImage(
        my::TextureFormat format,
        uint32_t width,
        uint32_t height,
        uint32_t levels,
        blobs::BlobHandle blob_handle) :
        _layout({my::TextureLayout::Type2D, format, width, height, 1, levels}),
        _image_format(gpu_format_to_image_format(format)),
        _blob(std::move(blob_handle))
    {
        uint32_t max_levels = std::ceil(std::log2(std::max(width, height))) + 1;
        if (levels > max_levels)
        {
            HRZ_LOG_ERROR(
                "Invalid level count {}, max count is {} for an image of size {}x{}", levels,
                max_levels, width, height);
            assert(false);
            levels = max_levels;
        }

        size_t expected_data_size = 0;
        for (size_t level = 0; level < levels; ++level)
        {
            expected_data_size += _layout.get_level_byte_size(level);
        }

        if (_blob.data_size() != expected_data_size)
        {
            HRZ_LOG_ERROR(
                "Invalid blob size: expected {}, got {}", size_in_bytes(), _blob.data_size());
            assert(false);
        }
    }

    BlobImage(
        my::TextureFormat format,
        uint32_t width,
        uint32_t height,
        blobs::BlobHandle blob_handle) :
        _layout({my::TextureLayout::Type2D, format, width, height, 1, 1}),
        _image_format(gpu_format_to_image_format(format)),
        _blob(std::move(blob_handle))
    {
        if (_blob.data_size() != size_in_bytes())
        {
            HRZ_LOG_ERROR(
                "Invalid blob size: expected {}, got {}", size_in_bytes(), _blob.data_size());
            assert(false);
        }
    }

    BlobImage(
        hrz_proto::ImageFormat format,
        uint32_t width,
        uint32_t height,
        blobs::BlobHandle blob_handle) :
        _layout(
            {my::TextureLayout::Type2D, image_format_to_gpu_format(format), width, height, 1, 1}),
        _image_format(format),
        _blob(std::move(blob_handle))
    {
        assert(_image_format.has_value());
        if (_blob.data_size() != size_in_bytes())
        {
            HRZ_LOG_ERROR(
                "Invalid blob size: expected {}, got {}", size_in_bytes(), _blob.data_size());
            assert(false);
        }
    }

    bool valid() const { return _blob.is_valid(); }

    uint32_t width() const { return _layout.width; }

    uint32_t height() const { return _layout.height; }

    uint32_t levels() const { return _layout.levels; }

    my::TextureFormat format() const { return _layout.format; }

    std::optional<hrz_proto::ImageFormat> proto_format() const { return _image_format; }

    size_t size_in_bytes() const { return _layout.get_level_byte_size(0); }

    size_t level_size_in_bytes(uint32_t level) const
    {
        if (level >= _layout.levels)
        {
            HRZ_LOG_ERROR("Invalid level");
            assert(false);
            return 0;
        }
        else
        {
            return _layout.get_level_byte_size(level);
        }
    }

    const blobs::BlobHandle& blob() const { return _blob; }

    // Only call if valid.
    blobs::BlobData data() const { return _blob.get_data(); }

    // Only call if valid.
    // Only use with non-compressed texture formats.
    // This call gets a lock on the data each time it is called.
    // If more than one pixel value has to be read, consider
    // calling `data()` once and using a structure like `ImageView`.
    template<typename T>
    T data(uint32_t x, uint32_t y, uint32_t level = 0)
    {
        T pixel_data;

        if (my::is_format_compressed(_layout.format))
        {
            HRZ_LOG_ERROR("Cannot set data for individual pixels of compressed images");
            assert(false);
            pixel_data = {};
        }
        else
        {
            auto pixel_size = my::format_external_pixel_byte_size(_layout.format);
            if (sizeof(T) != pixel_size)
            {
                HRZ_LOG_ERROR("Invalid value type size");
                assert(false);
                pixel_data = {};
            }
            else if (level >= _layout.levels)
            {
                HRZ_LOG_ERROR("Invalid level");
                assert(false);
                pixel_data = {};
            }
            else if (
                (level == 0 && (x >= _layout.width || y >= _layout.height))
                || (x >= _layout.get_level_width(level) || y >= _layout.get_level_height(level)))
            {
                HRZ_LOG_ERROR("Invalid pixel coordinates");
                assert(false);
                pixel_data = {};
            }
            else
            {
                auto raw_data = _blob.get_mutable_data();
                std::byte* data_ptr = raw_data.data();

                for (size_t i = 0; i < level; ++i)
                {
                    data_ptr += _layout.get_level_byte_size(level);
                }

                auto offset = (size_t)(x + y * _layout.width) * pixel_size;
                std::memcpy(&pixel_data, data_ptr + offset, sizeof(T));
            }
        }

        return pixel_data;
    }

    void register_layout_blob_metadata(BlobAllocator*);
    void register_blob_metadata(BlobAllocator*, MetadataString key, MetadataString value);
    void register_blob_owner(BlobAllocator*, const monitoring::ResourceOwner&);

    // Consumes the blob handle.
    static BlobImage make(
        my::TextureFormat format,
        uint32_t width,
        uint32_t height,
        uint32_t levels,
        blobs::BlobHandle&&,
        BlobAllocator*);

    // Consumes the blob handle.
    static BlobImage make(
        my::TextureFormat format,
        uint32_t width,
        uint32_t height,
        blobs::BlobHandle&&,
        BlobAllocator*);

    // Consumes the blob handle.
    static BlobImage make(
        my::TextureFormat format,
        uint32_t width,
        uint32_t height,
        const blobs::BlobHandle&,
        BlobAllocator*);

    // Consumes the blob handle.
    static BlobImage make(
        hrz_proto::ImageFormat format,
        uint32_t width,
        uint32_t height,
        blobs::BlobHandle&& blob,
        BlobAllocator* ba);

    // Consumes the blob handle.
    static BlobImage make(
        hrz_proto::ImageFormat format,
        uint32_t width,
        uint32_t height,
        const blobs::BlobHandle& blob,
        BlobAllocator* ba);

private:
    static my::TextureFormat image_format_to_gpu_format(hrz_proto::ImageFormat format);
    static std::optional<hrz_proto::ImageFormat> gpu_format_to_image_format(
        my::TextureFormat format);
};

struct BlobImageCompressionParams
{
    // Takes the ownership of the image.
    BlobImage image;
    my::TextureFormat output_format{};
};

} // namespace hrz
