#include "hrz/core/jobs/generate_mipmaps.h"

#include "hrz/common/blob_allocator.h"
#include "hrz/common/color.h"
#include "hrz/common/image_processing.h"
#include "hrz/common/profiling.h"
#include "hrz/common/raster_sampling.h"
#include "hrz/core/jobs/context.h"
#include "hrz/core/jobs/job_result.h"
#include "hrz/fnd/log.h"

namespace hrz_jobs::generate_mipmaps
{
unsigned int half_size(unsigned int size)
{
    return (size + (size % 2)) / 2;
}

const uint8_t* get_data(const hrz_proto::Image* image)
{
    return (const uint8_t*)(const void*)(&(image->data().data()[0]));
}

uint8_t* get_data(hrz_proto::Image* image)
{
    return (uint8_t*)(void*)(&(image->data().data()[0]));
}

// @Todo Other interpolation strategies (min, max, etc.)
template<typename T, unsigned int CHANNELS, hrz_proto::ImageFormat IMAGE_FORMAT>
void interpolate(const T* a, const T* b, T* res)
{
    for (unsigned int channel = 0; channel < CHANNELS; channel++)
    {
        res[channel] = (a[channel] + b[channel]) / 2;
    }
}

template<>
void interpolate<uint8_t, 4, hrz_proto::ImageFormat::SRGBA_8>(
    const uint8_t* a,
    const uint8_t* b,
    uint8_t* res)
{
    lm::ubvec4 a_rgba;
    std::memcpy(&a_rgba, a, sizeof(lm::ubvec4));
    lm::ubvec4 b_rgba;
    std::memcpy(&b_rgba, b, sizeof(lm::ubvec4));
    lm::ubvec4 res_rgba = hrz::mix_srgb_colors_in_linear(a_rgba, b_rgba, 0.5f);
    std::memcpy(res, &res_rgba, sizeof(lm::ubvec4));
}

template<>
void interpolate<uint32_t, 1, hrz_proto::ImageFormat::SIRADEL_LEGACY_F32>(
    const uint32_t* a,
    const uint32_t* b,
    uint32_t* res)
{
    float a_f = hrz::decode_siradel_legacy_f32_value_to_float(*a);
    float b_f = hrz::decode_siradel_legacy_f32_value_to_float(*b);
    float res_f = (a_f + b_f) / 2.0f;
    *res = hrz::encode_float_to_siradel_legacy_f32(res_f);
}

template<>
void interpolate<uint32_t, 1, hrz_proto::ImageFormat::TERRARIUM>(
    const uint32_t* a,
    const uint32_t* b,
    uint32_t* res)
{
    float a_f = hrz::decode_terrarium_value_to_float(*a);
    float b_f = hrz::decode_terrarium_value_to_float(*b);
    float res_f = (a_f + b_f) / 2.0f;
    *res = hrz::encode_float_to_terrarium(res_f);
}

template<>
void interpolate<uint32_t, 1, hrz_proto::ImageFormat::TERRAIN_RGB>(
    const uint32_t* a,
    const uint32_t* b,
    uint32_t* res)
{
    float a_f = hrz::decode_terrain_rgb_value_to_float(*a);
    float b_f = hrz::decode_terrain_rgb_value_to_float(*b);
    float res_f = (a_f + b_f) / 2.0f;
    *res = hrz::encode_float_to_terrain_rgb(res_f);
}

// `void*` in order to have the same signature for all template instanciations
template<typename T, unsigned int CHANNELS, hrz_proto::ImageFormat IMAGE_FORMAT>
void generate_pixel(
    const void* src_data,
    unsigned int src_width,
    void* dst_data,
    unsigned int dst_width,
    bool src_is_border_x,
    bool src_is_border_y,
    const hrz::sampling::NodataFunction& nodata)
{
    // @Todo The interpolation is wrong when only one source value is nodata.
    // (The weight of the value that is alone on its line is too big.)

    auto interpolate_func = &interpolate<T, CHANNELS, IMAGE_FORMAT>;

    const T* src_data_t = (const T*)src_data;
    T* dst_data_t = (T*)dst_data;

    unsigned int src_line = src_width * CHANNELS;

    const T* data00 = src_data_t;
    const T* data01 = src_data_t + CHANNELS;
    const T* data10 = src_data_t + src_line;
    const T* data11 = src_data_t + src_line + CHANNELS;

    auto size = CHANNELS * sizeof(T);

    bool data00_is_nodata = nodata.is_nodata<T, CHANNELS>(data00);
    bool data01_is_nodata = nodata.is_nodata<T, CHANNELS>(data01);
    bool data10_is_nodata = nodata.is_nodata<T, CHANNELS>(data10);
    bool data11_is_nodata = nodata.is_nodata<T, CHANNELS>(data11);

    if (src_is_border_x && src_is_border_y)
    {
        std::memcpy(dst_data_t, data00, sizeof(T) * CHANNELS);
    }
    else if (src_is_border_x)
    {
        if (!data00_is_nodata && !data10_is_nodata)
        {
            interpolate_func(data00, data10, dst_data_t);
        }
        else
        {
            data00_is_nodata ? memcpy(dst_data_t, data10, size) : memcpy(dst_data_t, data00, size);
        }
    }
    else if (src_is_border_y)
    {
        if (!data00_is_nodata && !data01_is_nodata)
        {
            interpolate_func(data00, data01, dst_data_t);
        }
        else
        {
            data00_is_nodata ? memcpy(dst_data_t, data01, size) : memcpy(dst_data_t, data00, size);
        }
    }
    else
    {
        std::array<T, CHANNELS> top{}, bottom{};

        if (!data00_is_nodata && !data01_is_nodata && !data10_is_nodata && !data11_is_nodata)
        {
            interpolate_func(data00, data01, top.data());
            interpolate_func(data10, data11, bottom.data());
            interpolate_func(top.data(), bottom.data(), dst_data_t);
        }
        else
        {
            bool top_is_nodata = false;
            if (!data00_is_nodata && !data01_is_nodata)
            {
                interpolate_func(data00, data01, top.data());
            }
            else if (!data00_is_nodata || !data01_is_nodata)
            {
                data00_is_nodata ? memcpy(top.data(), data01, size)
                                 : memcpy(top.data(), data00, size);
            }
            else
            {
                top_is_nodata = true;
            }

            bool bottom_is_nodata = false;
            if (!data10_is_nodata && !data11_is_nodata)
            {
                interpolate_func(data10, data11, bottom.data());
            }
            else if (!data10_is_nodata || !data11_is_nodata)
            {
                data10_is_nodata ? memcpy(bottom.data(), data11, size)
                                 : memcpy(bottom.data(), data10, size);
            }
            else
            {
                bottom_is_nodata = true;
            }

            if (!bottom_is_nodata && !top_is_nodata)
            {
                interpolate_func(top.data(), bottom.data(), dst_data_t);
            }
            else if (bottom_is_nodata && top_is_nodata)
            {
                // all is nodata so return first pixel.
                memcpy(dst_data_t, data00, size);
            }
            else
            {
                bottom_is_nodata ? memcpy(dst_data_t, top.data(), size)
                                 : memcpy(dst_data_t, bottom.data(), size);
            }
        }
    }
}

decltype(&generate_pixel<uint8_t, 4, hrz_proto::ImageFormat::SRGBA_8>) get_generate_pixel_func(
    hrz_proto::ImageFormat format)
{
    switch (format)
    {
        case hrz_proto::ImageFormat::SRGBA_8:
            return &generate_pixel<uint8_t, 4, hrz_proto::ImageFormat::SRGBA_8>;
        case hrz_proto::ImageFormat::SIGNED_FIXED_24_8:
            return &generate_pixel<int32_t, 1, hrz_proto::ImageFormat::SIGNED_FIXED_24_8>;
        case hrz_proto::ImageFormat::R_F32:
            return &generate_pixel<float, 1, hrz_proto::ImageFormat::R_F32>;
        case hrz_proto::ImageFormat::SIRADEL_LEGACY_F32:
            return &generate_pixel<uint32_t, 1, hrz_proto::ImageFormat::SIRADEL_LEGACY_F32>;
        case hrz_proto::ImageFormat::TERRARIUM:
            return &generate_pixel<uint32_t, 1, hrz_proto::ImageFormat::TERRARIUM>;
        case hrz_proto::ImageFormat::TERRAIN_RGB:
            return &generate_pixel<uint32_t, 1, hrz_proto::ImageFormat::TERRAIN_RGB>;
        default:
            assert(false);
            HRZ_LOG_ERROR("Unhandled image format: {}", hrz_proto::ImageFormat_Name(format));
            return nullptr;
    }
}

bool generate_tiles_at_level(
    unsigned int lod,
    const uint8_t* image_data,
    uint32_t image_width,
    uint32_t image_height,
    hrz_proto::ImageFormat image_format,
    unsigned int tile_size,
    hrz_jobs::Mipmaps& response,
    const JobContext& context)
{
    auto blob_allocator = context.get_blob_allocator();

    unsigned int bytes_per_pixel = hrz::image_format_byte_count(image_format);

    unsigned int tile_count_x = std::ceil((float)image_width / tile_size);
    unsigned int tile_count_y = std::ceil((float)image_height / tile_size);

    for (unsigned int tile_y = 0; tile_y < tile_count_y; tile_y++)
    {
        for (unsigned int tile_x = 0; tile_x < tile_count_x; tile_x++)
        {
            hrz_jobs::Mipmaps::Tile tile;
            tile.coords.lod = lod;
            tile.coords.x = tile_x;
            tile.coords.y = tile_y;

            unsigned int tile_width = std::min(image_width - tile_x * tile_size, tile_size);
            unsigned int tile_height = std::min(image_height - tile_y * tile_size, tile_size);
            size_t data_size = tile_width * tile_height * bytes_per_pixel;

            auto blob_opt = hrz::blobs::allocate_blob_sync(blob_allocator, data_size);
            if (!blob_opt.has_value())
            {
                HRZ_LOG_ERROR("Could not allocate output image");
                return false;
            }

            hrz::blobs::register_owner(
                context.get_blob_allocator(), blob_opt.value(), context.get_resource_owner());

            {
                auto blob_data = blob_opt->get_mutable_data();
                auto dst_data = (uint8_t*)blob_data.data();

                for (unsigned int y = 0; y < tile_height; y++)
                {
                    for (unsigned int x = 0; x < tile_width; x++)
                    {
                        unsigned int index_src =
                            ((tile_y * tile_size + y) * image_width + tile_x * tile_size + x)
                            * bytes_per_pixel;
                        unsigned int index_dst = (y * tile_width + x) * bytes_per_pixel;

                        std::memcpy(&dst_data[index_dst], &image_data[index_src], bytes_per_pixel);
                    }
                }
            }

            tile.image = hrz::BlobImage::make(
                image_format, tile_width, tile_height, std::move(blob_opt.value()),
                context.get_blob_allocator());

            response.tiles.push_back(std::move(tile));
        }
    }

    return true;
}

hrz_jobs::JobResult run(
    const hrz_jobs::MipmapGenerationParams& params,
    hrz_jobs::Mipmaps& response,
    const JobContext& context)
{
    HRZ_SCOPED_SAMPLE("generate mipmaps job");

    auto format_opt = params.image.proto_format();
    if (!format_opt.has_value())
    {
        HRZ_LOG_ERROR("Unsupported image format");
        return hrz_jobs::JobResult::FAILURE;
    }
    auto format = format_opt.value();

    hrz::sampling::NodataFunction nodata_function(
        params.nodata.value(),
        params.nodata.has_nodata() ? hrz_proto::NodataHandling::DISCARD_NODATA_PIXELS
                                   : hrz_proto::NodataHandling::IGNORE_NODATA,
        format);

    google::protobuf::Arena arena;

    auto generate_pixel_func = get_generate_pixel_func(format);

    unsigned int byte_count = hrz::image_format_byte_count(format);
    unsigned int image_size = std::max(params.image.width(), params.image.height());
    unsigned int expanded_image_size = hrz::next_power_of_two(image_size);
    unsigned int mipmap_count = (unsigned int)std::ceil(std::log2(expanded_image_size)) + 1;

    auto input_image_data = params.image.data();

    bool success = generate_tiles_at_level(
        mipmap_count - 1, (const uint8_t*)input_image_data.data(), params.image.width(),
        params.image.height(), format, params.tile_size, response, context);

    auto previous_data = (const uint8_t*)input_image_data.data();
    auto previous_width = params.image.width();
    auto previous_height = params.image.height();

    // Ping-pong between two buffers.
    // They are large enough for the first two levels, and hence all the smaller ones.

    auto pixel_buffer_0_blob = hrz::blobs::allocate_blob_sync(
        context.get_blob_allocator(),
        half_size(previous_width) * half_size(previous_height)
            * hrz::image_format_byte_count(format));
    auto pixel_buffer_1_blob = hrz::blobs::allocate_blob_sync(
        context.get_blob_allocator(),
        half_size(half_size(previous_width)) * half_size(half_size(previous_height))
            * hrz::image_format_byte_count(format));
    if (!pixel_buffer_0_blob.has_value() || !pixel_buffer_1_blob.has_value())
    {
        HRZ_LOG_ERROR("Could not allocate buffers for mipmap generation");
        return hrz_jobs::JobResult::FAILURE;
    }

    hrz::blobs::register_owner(
        context.get_blob_allocator(), pixel_buffer_0_blob.value(), context.get_resource_owner());
    hrz::blobs::register_owner(
        context.get_blob_allocator(), pixel_buffer_1_blob.value(), context.get_resource_owner());

    auto pixel_buffer_0_data = pixel_buffer_0_blob->get_mutable_data();
    auto pixel_buffer_1_data = pixel_buffer_1_blob->get_mutable_data();

    for (unsigned int level = 1; level < mipmap_count; level++)
    {
        unsigned int width = half_size(previous_width);
        unsigned int height = half_size(previous_height);

        auto mipmap_data =
            (uint8_t*)((level % 2) == 1 ? pixel_buffer_0_data.data() : pixel_buffer_1_data.data());

        const uint8_t* src_data = previous_data;
        uint8_t* dst_data = (uint8_t*)mipmap_data;

        for (unsigned int y = 0; y < height; y++)
        {
            bool src_is_border_y = y == height - 1 && (previous_height % 2) == 1;

            for (unsigned int x = 0; x < width; x++)
            {
                bool src_is_border_x = x == width - 1 && (previous_width % 2) == 1;

                generate_pixel_func(
                    src_data, previous_width, dst_data, width, src_is_border_x, src_is_border_y,
                    nodata_function);

                src_data += byte_count * (src_is_border_x ? 1 : 2);
                dst_data += byte_count;
            }

            src_data += byte_count * previous_width;
        }

        success &= generate_tiles_at_level(
            mipmap_count - 1 - level, mipmap_data, width, height, format, params.tile_size,
            response, context);

        previous_data = mipmap_data;
        previous_width = width;
        previous_height = height;
    }

    return success ? hrz_jobs::JobResult::SUCCESS : hrz_jobs::JobResult::FAILURE;
}

} // namespace hrz_jobs::generate_mipmaps
