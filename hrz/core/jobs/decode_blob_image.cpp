#include "absl/strings/numbers.h"
#include "hrz/common/blob_allocator.h"
#include "hrz/common/blob_image.h"
#include "hrz/common/blob_malloc_adapter.h"
#include "hrz/common/color.h"
#include "hrz/common/profiling.h"
#include "hrz/core/jobs/jobs_declarations.h"
#include "hrz/fnd/defer.h"
#include "hrz/fnd/inlined_vector.h"
#include "hrz/fnd/log.h"
#include "hrz/fnd/mem.h"
#include "hrz/fnd/mime.h"
#include "hrz/fnd/string_utils.h"

#include <basisu_transcoder.h>
#include <mycelium/backend.h>
#include <stb_image.h>
#include <webp/decode.h>

#include <bit>
#include <optional>

namespace
{
static thread_local std::optional<hrz::blobs::MallocAdapter> blob_malloc_adapter;

void* adapter_malloc(size_t size)
{
    if (blob_malloc_adapter.has_value())
    {
        return blob_malloc_adapter->malloc(size);
    }
    else
    {
        return malloc(size);
    }
}

void* adapter_calloc(size_t num, size_t size)
{
    if (blob_malloc_adapter.has_value())
    {
        return blob_malloc_adapter->calloc(num, size);
    }
    else
    {
        return calloc(num, size);
    }
}

void* adapter_realloc(void* ptr, size_t new_size)
{
    if (blob_malloc_adapter.has_value())
    {
        return blob_malloc_adapter->realloc(ptr, new_size);
    }
    else
    {
        return realloc(ptr, new_size);
    }
}

void adapter_free(void* ptr)
{
    if (blob_malloc_adapter.has_value())
    {
        blob_malloc_adapter->free(ptr);
    }
    else
    {
        free(ptr);
    }
}
} // namespace

extern "C"
{
    void* webp_malloc(size_t size)
    {
        return adapter_malloc(size);
    }

    void* webp_calloc(size_t num, size_t size)
    {
        return adapter_calloc(num, size);
    }

    void webp_free(void* ptr)
    {
        return adapter_free(ptr);
    }
}

void* stbi_malloc(size_t size)
{
    return adapter_malloc(size);
}

void* stbi_realloc(void* ptr, size_t new_size)
{
    return adapter_realloc(ptr, new_size);
}

void stbi_free(void* ptr)
{
    return adapter_free(ptr);
}

namespace
{
bool is_data_ktx2(std::span<const std::byte> data)
{
    // See https://registry.khronos.org/KTX/specs/2.0/ktxspec.v2.html
    static constexpr std::byte ktx2_identifier[] = {0xAB_b, 0x4B_b, 0x54_b, 0x58_b, 0x20_b, 0x32_b,
                                                    0x30_b, 0xBB_b, 0x0D_b, 0x0A_b, 0x1A_b, 0x0A_b};

    if (data.size_bytes() < 12) return false;

    for (size_t i = 0; i < 12; ++i)
    {
        if (data[i] != ktx2_identifier[i])
        {
            return false;
        }
    }

    return true;
}

template<typename F>
void transform_32bit_image_data(std::byte* data, size_t pixel_count, F transform_function)
    requires std::is_invocable_r_v<uint32_t, F, uint32_t>
{
    uint32_t value{};
    for (const auto* end_ptr = data + pixel_count * sizeof(uint32_t); data < end_ptr;
         data += sizeof(uint32_t))
    {
        std::memcpy(&value, data, sizeof(uint32_t));
        value = transform_function(value);
        std::memcpy(data, &value, sizeof(uint32_t));
    }
}

std::optional<hrz::BlobImage> finalize_image(
    hrz_proto::ImageFormat encoded_image_format,
    int width,
    int height,
    int bytes_per_pixel,
    bool convert_scalars_to_float,
    const hrz::blobs::BlobHandle& decoded_image_blob,
    const hrz_jobs::JobContext& context)
{
    if (convert_scalars_to_float)
    {
        auto convert = [&](float (*decode_func)(uint32_t))
        {
            assert(bytes_per_pixel == 4);
            auto decoded_image_data = decoded_image_blob.get_mutable_data();

            transform_32bit_image_data(
                decoded_image_data.data(), width * height,
                [decode_func = decode_func](uint32_t v)
                { return std::bit_cast<uint32_t>(decode_func(v)); });

            encoded_image_format = hrz_proto::ImageFormat::R_F32;
        };

        switch (encoded_image_format)
        {
            case hrz_proto::ImageFormat::R_F32: break;
            case hrz_proto::ImageFormat::R_F32_SILICIUM:
                convert(hrz::decode_r_f32_silicium_value_to_float);
                break;
            case hrz_proto::ImageFormat::SIGNED_FIXED_24_8:
                convert(hrz::decode_signed_fixed_24_8_to_float);
                break;
            case hrz_proto::ImageFormat::TERRARIUM:
                convert(hrz::decode_terrarium_value_to_float);
                break;
            case hrz_proto::ImageFormat::TERRAIN_RGB:
                convert(hrz::decode_terrain_rgb_value_to_float);
                break;
            default:
                HRZ_LOG_ERROR(
                    "Image format cannot be converted to float: {}",
                    hrz_proto::ImageFormat_Name(encoded_image_format));
                return std::nullopt;
        }
    }

    return hrz::BlobImage::make(
        encoded_image_format, width, height, decoded_image_blob, context.get_blob_allocator());
}

basist::transcoder_texture_format select_target_compressed_texture_format(
    basist::basis_tex_format source_format,
    bool has_alpha,
    bool is_srgb,
    const hrz::PlatformInfo& platform_info,
    const my::Instance::Info& my_instance_info)
{
#define HRZ_HAS_TEXTURE_COMPRESSION(COMP)                            \
    ((!is_srgb && my_instance_info.has_##COMP##_texture_compression) \
     || (is_srgb && my_instance_info.has_##COMP##_srgb_texture_compression))

    if (HRZ_HAS_TEXTURE_COMPRESSION(pvrtc2))
    {
        // Basis Universal has cTFPVRTC2_4_RGB, but the OpenGL ES extension
        // has no such format.
        return basist::transcoder_texture_format::cTFPVRTC2_4_RGBA;
    }
    if (HRZ_HAS_TEXTURE_COMPRESSION(astc)
        && (has_alpha || source_format == basist::basis_tex_format::cUASTC4x4))
    {
        return basist::transcoder_texture_format::cTFASTC_4x4_RGBA;
    }
    if (HRZ_HAS_TEXTURE_COMPRESSION(bc7) && source_format == basist::basis_tex_format::cUASTC4x4)
    {
        return basist::transcoder_texture_format::cTFBC7_RGBA;
    }
    if (my_instance_info.has_bc1_bc2_bc3_texture_compression)
    {
        return has_alpha ? basist::transcoder_texture_format::cTFBC3_RGBA
                         : basist::transcoder_texture_format::cTFBC1_RGB;
    }
    // According to some reports, ETC1/2 support on desktop, when present,
    // is likely to be emulated.
    if (HRZ_HAS_TEXTURE_COMPRESSION(etc2) && platform_info.is_mobile())
    {
        return basist::transcoder_texture_format::cTFETC2_RGBA;
    }
    if (HRZ_HAS_TEXTURE_COMPRESSION(etc1) && platform_info.is_mobile())
    {
        return has_alpha ? basist::transcoder_texture_format::cTFRGBA32
                         : basist::transcoder_texture_format::cTFETC1_RGB;
    }
    if (HRZ_HAS_TEXTURE_COMPRESSION(astc))
    {
        return basist::transcoder_texture_format::cTFASTC_4x4_RGBA;
    }
    if (HRZ_HAS_TEXTURE_COMPRESSION(bc7))
    {
        return basist::transcoder_texture_format::cTFBC7_RGBA;
    }
    if (HRZ_HAS_TEXTURE_COMPRESSION(etc2))
    {
        return basist::transcoder_texture_format::cTFETC2_RGBA;
    }
    if (HRZ_HAS_TEXTURE_COMPRESSION(etc1))
    {
        return has_alpha ? basist::transcoder_texture_format::cTFRGBA32
                         : basist::transcoder_texture_format::cTFETC1_RGB;
    }
    // The spec says:
    //     Note: this extension is deprecated. On systems that support this compressed
    //     texture format, please consider using the WEBGL_compressed_texture_etc or
    //     WEBGL_compressed_texture_astc formats instead. They are more widely supported
    //     and offer a larger range of quality controls.
    // @Todo Some textures appear full black when transcoding to PVRTC.
    if (HRZ_HAS_TEXTURE_COMPRESSION(pvrtc))
    {
        return has_alpha ? basist::transcoder_texture_format::cTFPVRTC1_4_RGBA
                         : basist::transcoder_texture_format::cTFPVRTC1_4_RGB;
    }
    return basist::transcoder_texture_format::cTFRGBA32;

#undef HRZ_HAS_TEXTURE_COMPRESSION
}

my::TextureFormat convert_basisu_texture_format(
    basist::transcoder_texture_format format,
    bool is_srgb)
{
    if (is_srgb)
    {
        switch (format)
        {
            case basist::transcoder_texture_format::cTFBC1_RGB: return my::TextureFormat::SRGB_BC1;
            case basist::transcoder_texture_format::cTFBC3_RGBA:
                return my::TextureFormat::SRGBA_BC3;
            case basist::transcoder_texture_format::cTFBC7_RGBA:
                return my::TextureFormat::SRGBA_BC7;
            case basist::transcoder_texture_format::cTFETC1_RGB:
                return my::TextureFormat::SRGB_ETC1;
            case basist::transcoder_texture_format::cTFETC2_RGBA:
                return my::TextureFormat::SRGBA_ETC2_EAC;
            case basist::transcoder_texture_format::cTFASTC_4x4_RGBA:
                return my::TextureFormat::SRGBA_ASTC_4x4;
            case basist::transcoder_texture_format::cTFPVRTC1_4_RGB:
                return my::TextureFormat::SRGB_PVRTC1_4BPP;
            case basist::transcoder_texture_format::cTFPVRTC1_4_RGBA:
                return my::TextureFormat::SRGBA_PVRTC1_4BPP;
            case basist::transcoder_texture_format::cTFPVRTC2_4_RGBA:
                return my::TextureFormat::SRGBA_PVRTC2_4BPP;
            case basist::transcoder_texture_format::cTFRGBA32: return my::TextureFormat::SRGBA8;
            default: assert(false && "Unhandled case"); return my::TextureFormat::SRGBA8;
        }
    }

    switch (format)
    {
        case basist::transcoder_texture_format::cTFBC1_RGB: return my::TextureFormat::RGB_BC1;
        case basist::transcoder_texture_format::cTFBC3_RGBA: return my::TextureFormat::RGBA_BC3;
        case basist::transcoder_texture_format::cTFBC7_RGBA: return my::TextureFormat::RGBA_BC7;
        case basist::transcoder_texture_format::cTFETC1_RGB: return my::TextureFormat::RGB_ETC1;
        case basist::transcoder_texture_format::cTFETC2_RGBA:
            return my::TextureFormat::RGBA_ETC2_EAC;
        case basist::transcoder_texture_format::cTFASTC_4x4_RGBA:
            return my::TextureFormat::RGBA_ASTC_4x4;
        case basist::transcoder_texture_format::cTFPVRTC1_4_RGB:
            return my::TextureFormat::RGB_PVRTC1_4BPP;
        case basist::transcoder_texture_format::cTFPVRTC1_4_RGBA:
            return my::TextureFormat::RGBA_PVRTC1_4BPP;
        case basist::transcoder_texture_format::cTFPVRTC2_4_RGBA:
            return my::TextureFormat::RGBA_PVRTC2_4BPP;
        case basist::transcoder_texture_format::cTFRGBA32: return my::TextureFormat::RGBA8;
        default: assert(false && "Unhandled case"); return my::TextureFormat::RGBA8;
    }
}

// Based partly on
// https://github.com/BinomialLLC/basis_universal/blob/9c5da86dbebf5f6eaf5fe42168d93f46566d8d5a/contrib/single_file_transcoder/examples/emscripten.cpp#L352
hrz::JobResult decode_ktx2(
    std::span<const std::byte> encoded_image_data,
    hrz_proto::ImageFormat encoded_image_format,
    bool allow_decoding_to_compressed_image,
    const hrz::PlatformInfo& platform_info,
    const my::Instance::Info& my_instance_info,
    hrz::BlobImage& decoded_image,
    const hrz_jobs::JobContext& context)
{
    if (encoded_image_format != hrz_proto::ImageFormat::SRGBA_8)
    {
        assert(false);
        HRZ_LOG_ERROR("Unsupported format for compressed images");
        return hrz::JobResult::FAILURE;
    }

    basist::ktx2_transcoder transcoder;
    if (!transcoder.init(encoded_image_data.data(), encoded_image_data.size_bytes()))
    {
        HRZ_LOG_ERROR("Could not initialize KTX2 transcoder for image data");
        return hrz::JobResult::FAILURE;
    }

    if (transcoder.get_level_index().empty())
    {
        HRZ_LOG_ERROR("No levels in KTX2 file");
        return hrz::JobResult::FAILURE;
    }

    if (!transcoder.start_transcoding())
    {
        HRZ_LOG_ERROR("Could not start transcoding KTX2 file");
        return hrz::JobResult::FAILURE;
    }

    bool is_srgb = transcoder.get_dfd_transfer_func() == basist::KTX2_KHR_DF_TRANSFER_SRGB;
    auto target_texture_format = allow_decoding_to_compressed_image
        ? select_target_compressed_texture_format(
            transcoder.get_basis_tex_format(), transcoder.get_has_alpha(), is_srgb, platform_info,
            my_instance_info)
        : basist::transcoder_texture_format::cTFRGBA32;
    auto texture_format = convert_basisu_texture_format(target_texture_format, is_srgb);

    my::TextureLayout texture_layout{};
    texture_layout.type = my::TextureLayout::Type2D;
    texture_layout.format = texture_format;
    texture_layout.width = 0;
    texture_layout.height = 0;
    texture_layout.depth = 1;
    texture_layout.levels = transcoder.get_levels();

    size_t decoded_image_byte_size = 0;
    hrz::InlinedVector<size_t, 16> level_byte_offsets;
    hrz::InlinedVector<size_t, 16> level_block_or_pixel_counts;

    for (uint32_t level = 0; level < texture_layout.levels; ++level)
    {
        basist::ktx2_image_level_info level_info{};
        if (!transcoder.get_image_level_info(level_info, level, 0, 0))
        {
            HRZ_LOG_ERROR("Could not get level {} info from KTX2 file", level);
            return hrz::JobResult::FAILURE;
        }

        auto level_width = level_info.m_width;
        auto level_height = level_info.m_height;

        if (level == 0)
        {
            texture_layout.width = level_width;
            texture_layout.height = level_height;
        }
        else
        {
            if (level_width != texture_layout.get_level_data_width(level)
                || level_height != texture_layout.get_level_data_height(level))
            {
                HRZ_LOG_ERROR(
                    "Unexpected level {} size: got {}x{}, expected {}x{} (image size is {}x{})",
                    level, level_width, level_height, texture_layout.get_level_data_width(level),
                    texture_layout.get_level_data_height(level), transcoder.get_width(),
                    transcoder.get_height());
                return hrz::JobResult::FAILURE;
            }
        }

        level_byte_offsets.push_back(decoded_image_byte_size);
        decoded_image_byte_size += texture_layout.get_level_byte_size(level);

        // Block count must be pixel count for transcoding to uncompressed RGBA.
        level_block_or_pixel_counts.push_back(
            basis_transcoder_format_is_uncompressed(target_texture_format)
                ? level_width * level_height
                : level_info.m_total_blocks);
    }

    auto decoded_image_blob =
        hrz::blobs::allocate_blob_sync(context.get_blob_allocator(), decoded_image_byte_size);
    if (!decoded_image_blob.has_value())
    {
        HRZ_LOG_ERROR("Could not allocate blob of size {}", decoded_image_byte_size);
        return hrz::JobResult::FAILURE;
    }

    {
        auto decoded_image_data = decoded_image_blob->get_mutable_data();

        for (uint32_t level = 0; level < texture_layout.levels; ++level)
        {
            transcoder.transcode_image_level(
                level, 0, 0, (void*)(decoded_image_data.data() + level_byte_offsets.at(level)),
                level_block_or_pixel_counts.at(level), target_texture_format);
        }
    }

    decoded_image = hrz::BlobImage::make(
        texture_format, texture_layout.width, texture_layout.height, texture_layout.levels,
        std::move(decoded_image_blob.value()), context.get_blob_allocator());

    return hrz::JobResult::SUCCESS;
}

std::optional<hrz::blobs::BlobHandle> decode_webp(
    std::span<const std::byte> encoded_image_data,
    int width,
    int height,
    int desired_channels,
    const hrz_jobs::JobContext& context)
{
    assert(desired_channels == 3 || desired_channels == 4);

    auto blob_allocator = context.get_blob_allocator();

    auto data_size = width * height * desired_channels; // in bytes

    auto decoded_image_blob = hrz::blobs::allocate_blob_sync(blob_allocator, data_size);
    if (!decoded_image_blob.has_value())
    {
        HRZ_LOG_ERROR("Could not allocate blob of size {}", data_size);
        return std::nullopt;
    }

    hrz::blobs::register_owner(
        context.get_blob_allocator(), decoded_image_blob.value(), context.get_resource_owner());
    auto decoded_image_data = decoded_image_blob->get_mutable_data();

    uint8_t* data = nullptr;

    if (desired_channels == 3)
    {
        data = WebPDecodeRGBInto(
            (const uint8_t*)encoded_image_data.data(), encoded_image_data.size_bytes(),
            (uint8_t*)decoded_image_data.data(), data_size, width * desired_channels);
    }
    else
    {
        data = WebPDecodeRGBAInto(
            (const uint8_t*)encoded_image_data.data(), encoded_image_data.size_bytes(),
            (uint8_t*)decoded_image_data.data(), data_size, width * desired_channels);
    }

    if (data != nullptr)
    {
        return decoded_image_blob;
    }
    else
    {
        HRZ_LOG_ERROR("Error when decoding image");
        return std::nullopt;
    }
}

std::optional<hrz::blobs::BlobHandle> decode_stbi(
    std::span<const std::byte> encoded_image_data,
    int* width,
    int* height,
    int desired_channels,
    const hrz_jobs::JobContext& context)
{
    // @Todo Decode image into the output blob directly.
    // This isn't currently supported by stb_image.
    // See https://github.com/nothings/stb/issues/58
    // "The library is not intended for, and is not billed as, being appropriate for low-memory
    // environments."
    //     -nothings, https://github.com/nothings/stb/issues/964#issuecomment-628301472

    int channels_in_file = 0;

    stbi_uc* output_data = stbi_load_from_memory(
        (const stbi_uc*)encoded_image_data.data(),
        static_cast<int>(encoded_image_data.size_bytes()), width, height, &channels_in_file,
        desired_channels);

    HRZ_DEFER[output_data]
    {
        stbi_image_free(output_data);
    };

    if (!output_data)
    {
        HRZ_LOG_ERROR("Error when decoding image: {}", stbi_failure_reason());
        return std::nullopt;
    }

    auto data_size = *width * *height * desired_channels; // in bytes

    auto decoded_image_blob =
        hrz::blobs::allocate_blob_sync(context.get_blob_allocator(), data_size);
    if (!decoded_image_blob.has_value())
    {
        HRZ_LOG_ERROR("Could not allocate blob of size {}", data_size);
        return std::nullopt;
    }

    hrz::blobs::register_owner(
        context.get_blob_allocator(), decoded_image_blob.value(), context.get_resource_owner());
    auto decoded_image_data = decoded_image_blob->get_mutable_data();

    std::memcpy(decoded_image_data.data(), output_data, data_size);

    return decoded_image_blob;
}

// @Todo(C++23) Use monadic operations on std::optional.
std::optional<int> atoi_opt(std::optional<std::string_view> sv)
{
    if (int value{}; sv && absl::SimpleAtoi(sv.value(), &value))
    {
        return value;
    }
    return std::nullopt;
}

struct RawImageView
{
    int band_stride;
    int line_stride;
    int pixel_stride;
};

template<typename T, bool kSwapBytes>
void copy_raw_image_data(
    std::byte* decoded_image_data,
    const std::byte* encoded_image_data,
    int width,
    int height,
    const RawImageView& raw_image_view,
    int channels,
    int skip_channels)
{
    const std::byte* in_line_ptr = encoded_image_data;
    std::byte* out_ptr = decoded_image_data;
    for (int y = 0; y < height; ++y, in_line_ptr += raw_image_view.line_stride)
    {
        const std::byte* in_pixel_ptr = in_line_ptr;
        for (int x = 0; x < width; ++x, in_pixel_ptr += raw_image_view.pixel_stride)
        {
            const std::byte* in_band_ptr = in_pixel_ptr;
            for (int c = 0; c < channels; ++c, in_band_ptr += raw_image_view.band_stride)
            {
                if constexpr (kSwapBytes)
                {
                    T value;
                    std::memcpy(&value, in_band_ptr, sizeof(T));
                    value = hrz::swap_bytes(value);
                    std::memcpy(out_ptr, &value, sizeof(T));
                }
                else
                {
                    std::memcpy(out_ptr, in_band_ptr, sizeof(T));
                }
                out_ptr += sizeof(T);
            }
            out_ptr += sizeof(T) * skip_channels;
        }
    }
}

std::optional<hrz::blobs::BlobHandle> decode_raw(
    std::span<const std::byte> encoded_image_data,
    int* width,
    int* height,
    int* byte_per_pixel,
    const hrz_proto::ImageFormat& image_format,
    const hrz::ParsedMime& mime,
    const hrz_jobs::JobContext& context)
{
    *width = atoi_opt(mime.get_parameter("width")).value_or(0);
    *height = atoi_opt(mime.get_parameter("height")).value_or(0);

    if (*width <= 0 || *height <= 0 || *width >= 65536 || *height >= 65536)
    {
        HRZ_LOG_ERROR("Invalid width or height for raw image");
        return std::nullopt;
    }

    const int channels = atoi_opt(mime.get_parameter("channels")).value_or(1);
    const int bits_per_channel = atoi_opt(mime.get_parameter("bit_width")).value_or(8);
    const auto interleaving = mime.get_parameter("interleaving").value_or("pixel");
    const bool swap_bytes =
        hrz::str::iequals(mime.get_parameter("endian").value_or("little"), "big");

    if (bits_per_channel != 8 && bits_per_channel != 16 && bits_per_channel != 32)
    {
        HRZ_LOG_ERROR("Bit width for raw image must be 8, 16, or 32");
        return std::nullopt;
    }

    const int bytes_per_channel = bits_per_channel / 8;

    const int desired_format_size = (int)hrz::image_format_byte_count(image_format);
    *byte_per_pixel = desired_format_size;

    // We accept 3xuint8 as SRGBA8: alpha will be filled with 255.
    const bool use_srgb_alpha_fallback =
        image_format == hrz_proto::ImageFormat::SRGBA_8 && channels == 3 && bytes_per_channel == 1;

    const int skip_channels = use_srgb_alpha_fallback ? 1 : 0;

    if ((channels + skip_channels) * bytes_per_channel != desired_format_size)
    {
        HRZ_LOG_ERROR(
            "Image format {} does not match raw image data: {} channels, {} bits per channel",
            hrz_proto::ImageFormat_Name(image_format), channels, bits_per_channel);
        return std::nullopt;
    }

    if (encoded_image_data.size_bytes() < (size_t)(*width * *height * channels * bytes_per_channel))
    {
        HRZ_LOG_ERROR(
            "Encoded image data size {} is smaller than expected {}",
            encoded_image_data.size_bytes(), *width * *height * bytes_per_channel);
        return std::nullopt;
    }

    RawImageView raw_image_view{};
    if (hrz::str::iequals(interleaving, "line"))
    {
        raw_image_view.line_stride = *width * channels * bytes_per_channel;
        raw_image_view.band_stride = *width * bytes_per_channel;
        raw_image_view.pixel_stride = bytes_per_channel;
    }
    else if (hrz::str::iequals(interleaving, "pixel"))
    {
        raw_image_view.line_stride = *width * channels * bytes_per_channel;
        raw_image_view.band_stride = bytes_per_channel;
        raw_image_view.pixel_stride = channels * bytes_per_channel;
    }
    else if (hrz::str::iequals(interleaving, "none"))
    {
        raw_image_view.line_stride = *width * bytes_per_channel;
        raw_image_view.band_stride = *width * *height * bytes_per_channel;
        raw_image_view.pixel_stride = bytes_per_channel;
    }
    else
    {
        HRZ_LOG_ERROR("Unknown interleaving type: {}", interleaving);
        return std::nullopt;
    }

    const size_t data_size = (size_t)*width * (size_t)*height * (size_t)desired_format_size;

    auto decoded_image_blob =
        hrz::blobs::allocate_blob_sync(context.get_blob_allocator(), data_size);
    if (!decoded_image_blob.has_value())
    {
        HRZ_LOG_ERROR("Could not allocate blob of size {}", data_size);
        return std::nullopt;
    }

    hrz::blobs::register_owner(
        context.get_blob_allocator(), decoded_image_blob.value(), context.get_resource_owner());
    auto decoded_image_data = decoded_image_blob->get_mutable_data();

    // We monomorph on the bit width  & byte swap parameter for increased performance.
    switch (bits_per_channel)
    {
        case 8:
            if (swap_bytes)
            {
                copy_raw_image_data<uint8_t, true>(
                    decoded_image_data.data(), encoded_image_data.data(), *width, *height,
                    raw_image_view, channels, skip_channels);
            }
            else
            {
                copy_raw_image_data<uint8_t, false>(
                    decoded_image_data.data(), encoded_image_data.data(), *width, *height,
                    raw_image_view, channels, skip_channels);
            }
            break;
        case 16:
            if (swap_bytes)
            {
                copy_raw_image_data<uint16_t, true>(
                    decoded_image_data.data(), encoded_image_data.data(), *width, *height,
                    raw_image_view, channels, skip_channels);
            }
            else
            {
                copy_raw_image_data<uint16_t, false>(
                    decoded_image_data.data(), encoded_image_data.data(), *width, *height,
                    raw_image_view, channels, skip_channels);
            }
            break;
        case 32:
            if (swap_bytes)
            {
                copy_raw_image_data<uint32_t, true>(
                    decoded_image_data.data(), encoded_image_data.data(), *width, *height,
                    raw_image_view, channels, skip_channels);
            }
            else
            {
                copy_raw_image_data<uint32_t, false>(
                    decoded_image_data.data(), encoded_image_data.data(), *width, *height,
                    raw_image_view, channels, skip_channels);
            }
            break;
        default: assert(false); break;
    }

    if (use_srgb_alpha_fallback)
    {
        static_assert(alignof(lm::ubvec4) == 1);
        auto decoded_image_data_ptr = std::span<lm::ubvec4>(
            reinterpret_cast<lm::ubvec4*>(decoded_image_data.data()), *width * *height);
        for (auto& pixel : decoded_image_data_ptr)
        {
            pixel.a = 255;
        }
    }

    return decoded_image_blob;
}

} // namespace

namespace hrz_jobs::decode_blob_image
{
hrz::JobResult run(
    const hrz::BlobImageDecodingParams& params,
    hrz::BlobImage& decoded_image,
    const JobContext& context)
{
    HRZ_SCOPED_SAMPLE("decode blob image job");

    assert(!blob_malloc_adapter.has_value());
    blob_malloc_adapter = hrz::blobs::MallocAdapter{context.get_blob_allocator()};
    HRZ_DEFER[]
    {
        blob_malloc_adapter = std::nullopt;
    };

    auto encoded_image_data = params.encoded_image_data.get_data();

    if (is_data_ktx2(encoded_image_data))
    {
        return decode_ktx2(
            encoded_image_data, params.image_format, params.allow_decoding_to_compressed_image,
            params.platform_info, params.my_instance_info, decoded_image, context);
    }

    const int desired_channels = (int)hrz::image_format_byte_count(params.image_format);
    assert(desired_channels >= 1 && desired_channels <= 4);

    std::optional<hrz::blobs::BlobHandle> decoded_image_blob;
    const hrz::ParsedMime mime = hrz::parse_mime(params.mime_type);

    int width = 0;
    int height = 0;
    int byte_per_pixel = 0;

    if (mime.type == "image" && mime.subtype == "x.raw")
    {
        decoded_image_blob = decode_raw(
            encoded_image_data, &width, &height, &byte_per_pixel, params.image_format, mime,
            context);
    }
    else
    {
        auto is_webp = WebPGetInfo(
                           (const uint8_t*)encoded_image_data.data(), encoded_image_data.size(),
                           &width, &height)
            != 0;

        if (is_webp)
        {
            decoded_image_blob =
                decode_webp(encoded_image_data, width, height, desired_channels, context);
            byte_per_pixel = desired_channels;
        }
        else
        {
            decoded_image_blob =
                decode_stbi(encoded_image_data, &width, &height, desired_channels, context);
            byte_per_pixel = desired_channels;
        }
    }

    if (decoded_image_blob.has_value())
    {
        auto decoded_image_opt = finalize_image(
            params.image_format, width, height, byte_per_pixel, params.convert_scalars_to_float,
            decoded_image_blob.value(), context);
        if (decoded_image_opt.has_value())
        {
            decoded_image = std::move(decoded_image_opt.value());
            return hrz::JobResult::SUCCESS;
        }
    }

    return hrz::JobResult::FAILURE;
}

} // namespace hrz_jobs::decode_blob_image
