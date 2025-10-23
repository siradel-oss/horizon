#include "hrz_common_raster_sampling.h"

#include "hrz_common_image_processing.h"

#include <hrz_fnd_log.h>

namespace hrz::sampling
{
NodataFunction::NodataParams NodataFunction::make_nodata_pattern_and_mask(
    const hrz_proto::NodataValue& nodata_value,
    hrz_proto::NodataHandling nodata_handling,
    hrz_proto::ImageFormat image_format)
{
    uint32_t nodata_pattern = 0;
    uint32_t nodata_mask = 0xffffffff;

    auto ignore_nodata = [&]()
    {
        nodata_pattern = 0;
        nodata_mask = 0;
    };

    if (nodata_handling != hrz_proto::NodataHandling::IGNORE_NODATA)
    {
        auto make_rgb_scalar_nodata_function = [&](uint32_t (*encode_func)(float))
        {
            // Ignore alpha channel
            nodata_mask = 0x00ffffff;

            switch (nodata_value.type())
            {
                case hrz_proto::NodataValueType::BIT_PATTERN_NODATA:
                {
                    nodata_pattern = nodata_value.bit_pattern();
                    if ((nodata_pattern & ~nodata_mask) != 0)
                    {
                        HRZ_LOG_WARNING(
                            "Too many bits provided for {} nodata bit "
                            "pattern. Ignoring extra bits.",
                            hrz_proto::ImageFormat_Name(image_format));
                        nodata_pattern &= nodata_mask;
                    }
                    break;
                }
                case hrz_proto::NodataValueType::UINT_VALUE_NODATA:
                {
                    nodata_pattern = encode_func((float)nodata_value.uint_value());
                    break;
                }
                case hrz_proto::NodataValueType::INT_VALUE_NODATA:
                {
                    nodata_pattern = encode_func((float)nodata_value.int_value());
                    break;
                }
                case hrz_proto::NodataValueType::FLOAT_VALUE_NODATA:
                {
                    nodata_pattern = encode_func(nodata_value.float_value());
                    break;
                }
                case hrz_proto::NodataValueType::NAN_NODATA:
                case hrz_proto::NodataValueType::COLOR_NODATA:
                {
                    ignore_nodata();
                    break;
                }
                default:
                    assert(!"Unhandled nodata value type");
                    ignore_nodata();
                    break;
            }
        };

        // Yes it's a bit weird to cast the uint and int values to float in some
        // of these cases because we might lose precision. However, in all
        // cases, the nodata value used here would have had to have been encoded
        // as float at some point to be represented in the image data, so the
        // value should be representable as a float without issue.

        switch (image_format)
        {
            case hrz_proto::ImageFormat::SRGBA_8:
                switch (nodata_value.type())
                {
                    case hrz_proto::NodataValueType::BIT_PATTERN_NODATA:
                    {
                        nodata_pattern = nodata_value.bit_pattern();
                        break;
                    }
                    case hrz_proto::NodataValueType::COLOR_NODATA:
                    {
                        const auto& color = nodata_value.color();
                        const lm::ubvec4 rgba = {
                            (uint8_t)std::round(color.r() * 255),
                            (uint8_t)std::round(color.g() * 255),
                            (uint8_t)std::round(color.b() * 255),
                            (uint8_t)std::round(color.a() * 255)};
                        nodata_pattern = std::bit_cast<uint32_t>(rgba);
                        break;
                    }
                    case hrz_proto::NodataValueType::UINT_VALUE_NODATA:
                    {
                        nodata_pattern = nodata_value.uint_value();
                        break;
                    }
                    case hrz_proto::NodataValueType::INT_VALUE_NODATA:
                    {
                        const int32_t value = nodata_value.int_value();
                        nodata_pattern = std::bit_cast<uint32_t>(value);
                        break;
                    }
                    case hrz_proto::NodataValueType::FLOAT_VALUE_NODATA:
                    case hrz_proto::NodataValueType::NAN_NODATA:
                    {
                        ignore_nodata();
                        break;
                    }
                    default:
                        assert(!"Unhandled nodata value type");
                        ignore_nodata();
                        break;
                }
                break;
            case hrz_proto::ImageFormat::R_F32:
            {
                switch (nodata_value.type())
                {
                    case hrz_proto::NodataValueType::BIT_PATTERN_NODATA:
                    {
                        nodata_pattern = nodata_value.bit_pattern();
                        break;
                    }
                    case hrz_proto::NodataValueType::COLOR_NODATA:
                    {
                        ignore_nodata();
                        break;
                    }
                    case hrz_proto::NodataValueType::UINT_VALUE_NODATA:
                    {
                        auto value = (float)nodata_value.uint_value();
                        nodata_pattern = std::bit_cast<uint32_t>(value);
                        break;
                    }
                    case hrz_proto::NodataValueType::INT_VALUE_NODATA:
                    {
                        auto value = (float)nodata_value.int_value();
                        nodata_pattern = std::bit_cast<uint32_t>(value);
                        break;
                    }
                    case hrz_proto::NodataValueType::FLOAT_VALUE_NODATA:
                    {
                        const float value = nodata_value.float_value();
                        nodata_pattern = std::bit_cast<uint32_t>(value);
                        break;
                    }
                    case hrz_proto::NodataValueType::NAN_NODATA:
                    {
                        const uint32_t value = 0x7f800000;
                        nodata_pattern = value;
                        nodata_mask = value;
                        break;
                    }
                    default:
                        assert(!"Unhandled nodata value type");
                        ignore_nodata();
                        break;
                }
                break;
            }
            case hrz_proto::ImageFormat::R_F32_SILICIUM:
            {
                switch (nodata_value.type())
                {
                    case hrz_proto::NodataValueType::BIT_PATTERN_NODATA:
                    {
                        nodata_pattern = nodata_value.bit_pattern();
                        break;
                    }
                    case hrz_proto::NodataValueType::COLOR_NODATA:
                    {
                        ignore_nodata();
                        break;
                    }
                    case hrz_proto::NodataValueType::UINT_VALUE_NODATA:
                    {
                        nodata_pattern =
                            hrz::encode_float_to_r_f32_silicium((float)nodata_value.uint_value());
                        break;
                    }
                    case hrz_proto::NodataValueType::INT_VALUE_NODATA:
                    {
                        nodata_pattern =
                            hrz::encode_float_to_r_f32_silicium((float)nodata_value.int_value());
                        break;
                    }
                    case hrz_proto::NodataValueType::FLOAT_VALUE_NODATA:
                    {
                        nodata_pattern =
                            hrz::encode_float_to_r_f32_silicium(nodata_value.float_value());
                        break;
                    }
                    case hrz_proto::NodataValueType::NAN_NODATA:
                    {
                        const uint32_t value = 0xff000000;
                        nodata_pattern = value;
                        nodata_mask = value;
                        break;
                    }
                    default:
                        assert(!"Unhandled nodata value type");
                        ignore_nodata();
                        break;
                }
                break;
            }
            case hrz_proto::ImageFormat::SIGNED_FIXED_24_8:
            {
                switch (nodata_value.type())
                {
                    case hrz_proto::NodataValueType::BIT_PATTERN_NODATA:
                    {
                        nodata_pattern = nodata_value.bit_pattern();
                        break;
                    }
                    case hrz_proto::NodataValueType::UINT_VALUE_NODATA:
                    {
                        nodata_pattern = nodata_value.uint_value() * 256;
                        break;
                    }
                    case hrz_proto::NodataValueType::INT_VALUE_NODATA:
                    {
                        int32_t value = nodata_value.int_value() * 256;
                        nodata_pattern = std::bit_cast<uint32_t>(value);
                        break;
                    }
                    case hrz_proto::NodataValueType::FLOAT_VALUE_NODATA:
                    {
                        nodata_pattern =
                            hrz::encode_float_to_signed_fixed_24_8(nodata_value.float_value());
                        break;
                    }
                    case hrz_proto::NodataValueType::NAN_NODATA:
                    case hrz_proto::NodataValueType::COLOR_NODATA:
                    {
                        ignore_nodata();
                        break;
                    }
                    default:
                        assert(!"Unhandled nodata value type");
                        ignore_nodata();
                        break;
                }
                break;
            }
            case hrz_proto::ImageFormat::TERRARIUM:
            {
                make_rgb_scalar_nodata_function(hrz::encode_float_to_terrarium);
                break;
            }
            case hrz_proto::ImageFormat::TERRAIN_RGB:
            {
                make_rgb_scalar_nodata_function(hrz::encode_float_to_terrain_rgb);
                break;
            }
            default:
                assert(!"Unhandled image format");
                ignore_nodata();
                break;
        }
    }
    else
    {
        ignore_nodata();
    }

    return {nodata_pattern, nodata_mask};
}

void DtmBlendingFunction::blend(const void* src, void* dst) const
{
    float v{};
    std::memcpy(&v, src, sizeof(float));
    if (!std::isnan(v))
    {
        std::memcpy(dst, &v, sizeof(float));
    }
    else
    {
        std::memset(dst, 0, sizeof(float));
    }
}

void ImageryBlendingFunction::blend(const void* src, void* dst) const
{
    auto src_ui8 = static_cast<const uint8_t*>(src);
    auto dst_ui8 = static_cast<uint8_t*>(dst);

    uint8_t r = src_ui8[0];
    uint8_t g = src_ui8[1];
    uint8_t b = src_ui8[2];
    uint8_t a = src_ui8[3];

    if (opacity < 255)
    {
        r = (uint8_t)(((uint16_t)(r + 1) * opacity) >> 8);
        g = (uint8_t)(((uint16_t)(g + 1) * opacity) >> 8);
        b = (uint8_t)(((uint16_t)(b + 1) * opacity) >> 8);
        a = (uint8_t)(((uint16_t)(a + 1) * opacity) >> 8);
    }

    if (a < 255)
    {
        const uint16_t src_ratio = 256; // Premultiplied
        const uint16_t dst_ratio = 256 - (uint16_t)a;

        dst_ui8[0] = (uint8_t)(((uint16_t)dst_ui8[0] * dst_ratio + (uint16_t)r * src_ratio) >> 8);
        dst_ui8[1] = (uint8_t)(((uint16_t)dst_ui8[1] * dst_ratio + (uint16_t)g * src_ratio) >> 8);
        dst_ui8[2] = (uint8_t)(((uint16_t)dst_ui8[2] * dst_ratio + (uint16_t)b * src_ratio) >> 8);
        dst_ui8[3] = (uint8_t)(((uint16_t)dst_ui8[3] * dst_ratio + (uint16_t)a * src_ratio) >> 8);
    }
    else
    {
        dst_ui8[0] = r;
        dst_ui8[1] = g;
        dst_ui8[2] = b;
        dst_ui8[3] = a;
    }
}

PixelValue<uint8_t, 4> fetch_rgba8_pixel(
    const ImageView& input,
    int x,
    int y,
    const NodataFunction& nodata)
{
    std::array<uint8_t, 4> res{};
    std::memcpy(res.data(), input.pixel_data<uint8_t, 4>(x, y), sizeof(uint8_t) * 4);
    return {res, nodata.is_nodata<uint8_t, 4>(res)};
}

PixelValue<float, 1> fetch_r_f32_pixel(
    const ImageView& input,
    int x,
    int y,
    const NodataFunction& nodata)
{
    std::array<float, 1> res{};
    std::memcpy(res.data(), input.pixel_data<float, 1>(x, y), sizeof(float));
    return {res, nodata.is_nodata<float, 1>(res)};
}

PixelValue<float, 1> fetch_r_f32_silicium_pixel(
    const ImageView& input,
    int x,
    int y,
    const NodataFunction& nodata)
{
    uint32_t value = 0;
    std::memcpy(&value, input.pixel_data<uint32_t, 1>(x, y), sizeof(uint32_t));
    return {
        {hrz::decode_r_f32_silicium_value_to_float(value)},
        nodata.is_nodata<uint32_t, 1>(&value)};
}

PixelValue<float, 1> fetch_signed_fixed_24_8_pixel(
    const ImageView& input,
    int x,
    int y,
    const NodataFunction& nodata)
{
    int32_t value = 0;
    std::memcpy(&value, input.pixel_data<int32_t, 1>(x, y), sizeof(int32_t));
    return {{(float)value / 256.0F}, nodata.is_nodata<int32_t, 1>(&value)};
}

PixelValue<float, 1> fetch_terrarium_pixel(
    const ImageView& input,
    int x,
    int y,
    const NodataFunction& nodata)
{
    uint32_t value = 0;
    std::memcpy(&value, input.pixel_data<uint32_t, 1>(x, y), sizeof(uint32_t));
    return {{hrz::decode_terrarium_value_to_float(value)}, nodata.is_nodata<uint32_t, 1>(&value)};
}

PixelValue<float, 1> fetch_terrain_rgb_pixel(
    const ImageView& input,
    int x,
    int y,
    const NodataFunction& nodata)
{
    uint32_t value = 0;
    std::memcpy(&value, input.pixel_data<uint32_t, 1>(x, y), sizeof(uint32_t));
    return {{hrz::decode_terrain_rgb_value_to_float(value)}, nodata.is_nodata<uint32_t, 1>(&value)};
}

std::unique_ptr<SamplingFunction> make_sampling_function(
    hrz_proto::AlphaChannelUsage alpha_channel_usage,
    const hrz_proto::RasterNodata& raster_nodata,
    hrz_proto::NodataHandling nodata_handling,
    hrz_proto::TextureFiltering filtering,
    hrz_proto::ImageFormat image_format)
{
    const NodataFunction nodata_function(raster_nodata.value(), nodata_handling, image_format);

    if (image_format == hrz_proto::ImageFormat::SRGBA_8)
    {
        return std::make_unique<ImagerySamplingFunction>(
            fetch_rgba8_pixel, alpha_channel_usage, nodata_function, filtering);
    }
    else if (image_format == hrz_proto::ImageFormat::R_F32)
    {
        return std::make_unique<DtmSamplingFunction>(
            fetch_r_f32_pixel, alpha_channel_usage, nodata_function, filtering);
    }
    else if (image_format == hrz_proto::ImageFormat::R_F32_SILICIUM)
    {
        return std::make_unique<DtmSamplingFunction>(
            fetch_r_f32_silicium_pixel, alpha_channel_usage, nodata_function, filtering);
    }
    else if (image_format == hrz_proto::ImageFormat::SIGNED_FIXED_24_8)
    {
        return std::make_unique<DtmSamplingFunction>(
            fetch_signed_fixed_24_8_pixel, alpha_channel_usage, nodata_function, filtering);
    }
    else if (image_format == hrz_proto::ImageFormat::TERRARIUM)
    {
        return std::make_unique<DtmSamplingFunction>(
            fetch_terrarium_pixel, alpha_channel_usage, nodata_function, filtering);
    }
    else if (image_format == hrz_proto::ImageFormat::TERRAIN_RGB)
    {
        return std::make_unique<DtmSamplingFunction>(
            fetch_terrain_rgb_pixel, alpha_channel_usage, nodata_function, filtering);
    }
    else
    {
        HRZ_LOG_ERROR("Unsupported image format: {}", hrz_proto::ImageFormat_Name(image_format));
        assert(false);
        return nullptr;
    }
}
} // namespace hrz::sampling
