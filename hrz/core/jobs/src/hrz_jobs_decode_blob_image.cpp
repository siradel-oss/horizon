#include "hrz_jobs_declarations.h"
#include "hrz_protocol_image_helper.h"

#include <hrz_common_blob_allocator.h>
#include <hrz_common_blob_image.h>
#include <hrz_common_blob_malloc_adapter.h>
#include <hrz_common_image_processing.h>
#include <hrz_common_profiling.h>
#include <hrz_fnd_bit_cast.h>
#include <hrz_fnd_inlined_vector.h>
#include <hrz_fnd_log.h>

#include <basisu_transcoder.h>
#include <mycelium_backend.h>
#include <stb_image.h>
#include <webp/decode.h>

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
bool is_data_ktx2(gsl::span<const std::byte> data)
{
    // See https://registry.khronos.org/KTX/specs/2.0/ktxspec.v2.html
    static constexpr std::byte ktx2_identifier[] = {0xAB_b, 0x4B_b, 0x54_b, 0x58_b, 0x20_b, 0x32_b,
                                                    0x30_b, 0xBB_b, 0x0D_b, 0x0A_b, 0x1A_b, 0x0A_b};

    if (data.size_bytes() < 12) return false;

    return data.subspan(0, 12) == gsl::span<const std::byte>{ktx2_identifier};
}

hrz::BlobImage finalize_image(
    hrz_proto::ImageFormat encoded_image_format,
    int width,
    int height,
    int channels,
    bool premultiply_alpha,
    bool convert_scalars_to_float,
    hrz::blobs::BlobHandle decoded_image_blob,
    const hrz_jobs::JobContext& context)
{
    if (premultiply_alpha)
    {
        if (encoded_image_format == hrz_proto::ImageFormat::SRGBA_8)
        {
            assert(channels == 4);

            auto decoded_image_data = decoded_image_blob.get_mutable_data();

            auto data_ptr = decoded_image_data.data();

            int pixel_count = width * height;
            for (int i = 0; i < pixel_count; ++i)
            {
                // The intermediary uint32_t and the use of bit_cast here here to
                // avoid warnings. The compiler should be able to optimise this to
                // a single memcpy.
                static_assert(sizeof(lm::ubvec4) == sizeof(uint32_t), "ubvec4 size");
                uint32_t in_uint;
                std::memcpy(&in_uint, data_ptr, sizeof(uint32_t));
                lm::ubvec4 in = hrz::bit_cast<lm::ubvec4, uint32_t>(in_uint);
                lm::ubvec4 out = hrz::premultiply_alpha(in);
                uint32_t out_uint = hrz::bit_cast<uint32_t, lm::ubvec4>(out);
                std::memcpy(data_ptr, &out_uint, sizeof(uint32_t));

                data_ptr += sizeof(uint32_t);
            }
        }
        else
        {
            HRZ_LOG_WARNING(
                "Cannot premultiply alpha of image of format {}",
                hrz_proto::ImageFormat_Name(encoded_image_format));
        }
    }

    if (convert_scalars_to_float)
    {
        if (encoded_image_format == hrz_proto::ImageFormat::R_F32_SILICIUM)
        {
            assert(channels == 4);

            auto decoded_image_data = decoded_image_blob.get_mutable_data();

            auto data_ptr = decoded_image_data.data();

            int pixel_count = width * height;
            for (int i = 0; i < pixel_count; ++i)
            {
                uint32_t in;
                std::memcpy(&in, data_ptr, sizeof(uint32_t));
                float out = hrz::decode_r_f32_silicium_value_to_float(in);
                std::memcpy(data_ptr, &out, sizeof(float));

                data_ptr += sizeof(uint32_t);
            }

            encoded_image_format = hrz_proto::ImageFormat::R_F32;
        }
        else if (encoded_image_format == hrz_proto::ImageFormat::SIGNED_FIXED_24_8)
        {
            assert(channels == 4);

            auto decoded_image_data = decoded_image_blob.get_mutable_data();

            auto data_ptr = decoded_image_data.data();

            int pixel_count = width * height;
            for (int i = 0; i < pixel_count; ++i)
            {
                uint32_t in;
                std::memcpy(&in, data_ptr, sizeof(uint32_t));
                float out = hrz::decode_signed_fixed_24_8_to_float(in);
                std::memcpy(data_ptr, &out, sizeof(float));

                data_ptr += sizeof(uint32_t);
            }

            encoded_image_format = hrz_proto::ImageFormat::R_F32;
        }
    }

    return hrz::BlobImage::make(
        encoded_image_format, width, height, decoded_image_blob, context.get_blob_allocator());
}

basist::transcoder_texture_format select_target_compressed_texture_format(
    basist::basis_tex_format source_format,
    bool has_alpha,
    const hrz::PlatformInfo& platform_info,
    const my::Instance::Info& my_instance_info)
{
    if (my_instance_info.has_pvrtc2_texture_compression)
    {
        // Basis Universal has cTFPVRTC2_4_RGB, but the OpenGL ES extension
        // has no such format.
        return basist::transcoder_texture_format::cTFPVRTC2_4_RGBA;
    }
    if (my_instance_info.has_astc_texture_compression
        && (has_alpha || source_format == basist::basis_tex_format::cUASTC4x4))
    {
        return basist::transcoder_texture_format::cTFASTC_4x4_RGBA;
    }
    if (my_instance_info.has_bc7_texture_compression
        && source_format == basist::basis_tex_format::cUASTC4x4)
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
    if (my_instance_info.has_etc2_texture_compression && platform_info.is_mobile())
    {
        return basist::transcoder_texture_format::cTFETC2_RGBA;
    }
    if (my_instance_info.has_etc1_texture_compression && platform_info.is_mobile())
    {
        return has_alpha ? basist::transcoder_texture_format::cTFRGBA32
                         : basist::transcoder_texture_format::cTFETC1_RGB;
    }
    if (my_instance_info.has_astc_texture_compression)
    {
        return basist::transcoder_texture_format::cTFASTC_4x4_RGBA;
    }
    if (my_instance_info.has_bc7_texture_compression)
    {
        return basist::transcoder_texture_format::cTFBC7_RGBA;
    }
    if (my_instance_info.has_etc2_texture_compression)
    {
        return basist::transcoder_texture_format::cTFETC2_RGBA;
    }
    if (my_instance_info.has_etc1_texture_compression)
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
    if (my_instance_info.has_pvrtc_texture_compression)
    {
        return has_alpha ? basist::transcoder_texture_format::cTFPVRTC1_4_RGBA
                         : basist::transcoder_texture_format::cTFPVRTC1_4_RGB;
    }
    return basist::transcoder_texture_format::cTFRGBA32;
}

my::TextureFormat convert_basisu_texture_format(basist::transcoder_texture_format format)
{
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
// @Todo Decode mipmaps. This needs supporting them in decoded images.
hrz::JobResult decode_ktx2(
    gsl::span<const std::byte> encoded_image_data,
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

    auto target_texture_format = allow_decoding_to_compressed_image
        ? select_target_compressed_texture_format(
            transcoder.get_format(), transcoder.get_has_alpha(), platform_info, my_instance_info)
        : basist::transcoder_texture_format::cTFRGBA32;

    my::TextureLayout texture_layout{};
    texture_layout.type = my::TextureLayout::Type2D;
    texture_layout.format = convert_basisu_texture_format(target_texture_format);
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
                    "Unexpected level size: got {}x{}, expected {}x{}", level_width, level_height,
                    texture_layout.get_level_data_width(level),
                    texture_layout.get_level_data_height(level));
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
        convert_basisu_texture_format(target_texture_format), texture_layout.width,
        texture_layout.height, texture_layout.levels, std::move(decoded_image_blob.value()),
        context.get_blob_allocator());

    return hrz::JobResult::SUCCESS;
}

hrz::JobResult decode_webp(
    gsl::span<const std::byte> encoded_image_data,
    hrz_proto::ImageFormat encoded_image_format,
    int width,
    int height,
    int desired_channels,
    bool premultiply_alpha,
    bool convert_scalars_to_float,
    hrz::BlobImage& decoded_image,
    const hrz_jobs::JobContext& context)
{
    assert(desired_channels == 3 || desired_channels == 4);

    auto blob_allocator = context.get_blob_allocator();

    auto data_size = width * height * desired_channels; // in bytes

    auto decoded_image_blob = hrz::blobs::allocate_blob_sync(blob_allocator, data_size);
    if (!decoded_image_blob.has_value())
    {
        HRZ_LOG_ERROR("Could not allocate blob of size {}", data_size);
        return hrz::JobResult::FAILURE;
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
        decoded_image_data.release();

        decoded_image = finalize_image(
            encoded_image_format, width, height, desired_channels, premultiply_alpha,
            convert_scalars_to_float, std::move(decoded_image_blob.value()), context);

        return hrz::JobResult::SUCCESS;
    }
    else
    {
        HRZ_LOG_ERROR("Error when decoding image");

        return hrz::JobResult::FAILURE;
    }
}

hrz::JobResult decode_stbi(
    gsl::span<const std::byte> encoded_image_data,
    hrz_proto::ImageFormat encoded_image_format,
    int desired_channels,
    bool premultiply_alpha,
    bool convert_scalars_to_float,
    hrz::BlobImage& decoded_image,
    const hrz_jobs::JobContext& context)
{
    // @Todo Decode image into the output blob directly.
    // This isn't currently supported by stb_image.
    // See https://github.com/nothings/stb/issues/58
    // "The library is not intended for, and is not billed as, being appropriate for low-memory
    // environments."
    //     -nothings, https://github.com/nothings/stb/issues/964#issuecomment-628301472

    int width;
    int height;
    int channels_in_file;

    stbi_uc* output_data = stbi_load_from_memory(
        (const stbi_uc*)encoded_image_data.data(),
        static_cast<int>(encoded_image_data.size_bytes()), &width, &height, &channels_in_file,
        desired_channels);

    if (output_data)
    {
        auto data_size = width * height * desired_channels; // in bytes

        auto decoded_image_blob =
            hrz::blobs::allocate_blob_sync(context.get_blob_allocator(), data_size);
        if (!decoded_image_blob.has_value())
        {
            HRZ_LOG_ERROR("Could not allocate blob of size {}", data_size);
            stbi_image_free(output_data);
            return hrz::JobResult::FAILURE;
        }

        hrz::blobs::register_owner(
            context.get_blob_allocator(), decoded_image_blob.value(), context.get_resource_owner());
        auto decoded_image_data = decoded_image_blob->get_mutable_data();

        std::memcpy(decoded_image_data.data(), output_data, data_size);

        stbi_image_free(output_data);
        decoded_image_data.release();

        decoded_image = finalize_image(
            encoded_image_format, width, height, desired_channels, premultiply_alpha,
            convert_scalars_to_float, std::move(decoded_image_blob.value()), context);

        return hrz::JobResult::SUCCESS;
    }
    else
    {
        HRZ_LOG_ERROR("Error when decoding image: {}", stbi_failure_reason());

        return hrz::JobResult::FAILURE;
    }
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

    auto encoded_image_data = params.encoded_image_data.get_data();

    // Check if encoded image can be parsed.
    int desired_channels = hrz_proto::byte_count(params.image_format);

    // stbi_load_from_memory() loads images with 8 bits per channel,
    // between 1 and 4 channels.
    assert(desired_channels >= 1 && desired_channels <= 4);

    hrz::JobResult result{};

    if (is_data_ktx2(encoded_image_data))
    {
        result = decode_ktx2(
            encoded_image_data, params.image_format, params.allow_decoding_to_compressed_image,
            params.platform_info, params.my_instance_info, decoded_image, context);

        if (params.premultiply_alpha)
        {
            HRZ_LOG_WARNING("Cannot premultiply alpha of KTX2 image");
        }
    }
    else
    {
        int width, height;
        auto is_webp = WebPGetInfo(
                           (const uint8_t*)encoded_image_data.data(), encoded_image_data.size(),
                           &width, &height)
            != 0;

        if (is_webp)
        {
            result = decode_webp(
                encoded_image_data, params.image_format, width, height, desired_channels,
                params.premultiply_alpha, params.convert_scalars_to_float, decoded_image, context);
        }
        else
        {
            result = decode_stbi(
                encoded_image_data, params.image_format, desired_channels, params.premultiply_alpha,
                params.convert_scalars_to_float, decoded_image, context);
        }
    }

    blob_malloc_adapter = std::nullopt;

    return result;
}

} // namespace hrz_jobs::decode_blob_image
