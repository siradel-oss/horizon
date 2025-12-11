#pragma once

#include "hrz/common/color.h"
#include "hrz/common/image_processing.h"
#include "hrz/common/image_view.h"
#include "hrz/fnd/class.h"
#include "hrz/protocol/raster/nodata.pb.h"
#include "hrz/protocol/raster/sampling.pb.h"

#include <assert.h>
#include <lin_maths.h>

#include <array>
#include <bit>
#include <memory>
#include <type_traits>

namespace hrz::sampling
{
template<typename>
struct subpixel_coord_type;

template<>
struct subpixel_coord_type<float>
{
    using type = float;
};

template<typename T>
struct subpixel_coord_type
{
    using type = std::make_unsigned_t<T>;
};

template<typename>
struct next_size;

template<>
struct next_size<int8_t>
{
    using type = int16_t;
};

template<>
struct next_size<int16_t>
{
    using type = int32_t;
};

template<>
struct next_size<int32_t>
{
    using type = int64_t;
};

template<>
struct next_size<uint8_t>
{
    using type = uint16_t;
};

template<>
struct next_size<uint16_t>
{
    using type = uint32_t;
};

template<>
struct next_size<uint32_t>
{
    using type = uint64_t;
};

template<>
struct next_size<float>
{
    using type = double;
};

template<typename T>
struct PixelChannelValue
{
    T value;
    bool is_nodata;
};

template<typename T, int CHANNELS>
struct PixelValue
{
    std::array<T, CHANNELS> value;
    bool is_nodata;

    PixelChannelValue<T> get_channel(size_t channel) const { return {value[channel], is_nodata}; }
};

struct NodataFunction
{
private:
    struct NodataParams
    {
        uint32_t pattern;
        uint32_t mask;
    };

    static NodataParams make_nodata_pattern_and_mask(
        const hrz_proto::NodataValue& nodata_value,
        hrz_proto::NodataHandling nodata_handling,
        hrz_proto::ImageFormat image_format);

    NodataFunction(const NodataParams& params, hrz_proto::NodataHandling nodata_handling) :
        pattern(params.pattern & params.mask),
        mask(params.mask),
        handling(mask != 0 ? nodata_handling : hrz_proto::NodataHandling::IGNORE_NODATA)
    {
    }

public:
    NodataFunction(
        const hrz_proto::NodataValue& nodata_value,
        hrz_proto::NodataHandling nodata_handling,
        hrz_proto::ImageFormat image_format) :
        NodataFunction(
            make_nodata_pattern_and_mask(nodata_value, nodata_handling, image_format),
            nodata_handling)
    {
    }

    template<typename T, unsigned int CHANNELS>
    inline bool is_nodata(const T* data) const
    {
        static_assert(sizeof(T) * CHANNELS <= sizeof(uint32_t));

        switch (handling)
        {
            case hrz_proto::NodataHandling::IGNORE_NODATA: return false;
            case hrz_proto::NodataHandling::DISCARD_NODATA_PIXELS:
            case hrz_proto::NodataHandling::SET_NODATA_TO_ZERO:
            {
                uint32_t data_bits = 0;
                std::memcpy(&data_bits, data, CHANNELS * sizeof(T));
                data_bits &= mask;
                return memcmp(&data_bits, &pattern, CHANNELS * sizeof(T)) == 0;
            }
            default: assert(!"Unhandled nodata handling"); return false;
        }
    }

    template<typename T, unsigned int CHANNELS>
    inline bool is_nodata(const std::array<T, CHANNELS>& data) const
    {
        return is_nodata<T, CHANNELS>(data.data());
    }

    // Returns whether this value should be discarded or not
    template<typename T, unsigned int CHANNELS>
    inline bool apply(PixelValue<T, CHANNELS>& data) const
    {
        switch (handling)
        {
            case hrz_proto::NodataHandling::IGNORE_NODATA: return false;
            case hrz_proto::NodataHandling::DISCARD_NODATA_PIXELS: return data.is_nodata;
            case hrz_proto::NodataHandling::SET_NODATA_TO_ZERO:
                if (data.is_nodata)
                {
                    for (size_t i = 0; i < CHANNELS; ++i)
                    {
                        data.value[i] = (T)0;
                    }
                    data.is_nodata = false;
                }
                return false;
            default: assert(!"Unhandled nodata handling"); return false;
        }
    }

    uint32_t pattern;
    uint32_t mask;
    hrz_proto::NodataHandling handling;
};

struct BlendingFunction
{
    explicit BlendingFunction(const uint8_t opacity) : opacity(opacity) {}

    HRZ_DEFAULT_COPY_MOVE(BlendingFunction);
    virtual ~BlendingFunction() = default;

    virtual void blend(const void* src, void* dst) const = 0;

protected:
    uint8_t opacity;
};

struct DtmBlendingFunction : public BlendingFunction
{
    using BlendingFunction::BlendingFunction;
    void blend(const void* src, void* dst) const override;
};

struct ImageryBlendingFunction : public BlendingFunction
{
    using BlendingFunction::BlendingFunction;
    void blend(const void* src, void* dst) const override;
};

PixelValue<uint8_t, 4> fetch_rgba8_pixel(
    const ImageView& input,
    int x,
    int y,
    const NodataFunction& nodata);
PixelValue<float, 1> fetch_r_f32_pixel(
    const ImageView& input,
    int x,
    int y,
    const NodataFunction& nodata);
PixelValue<float, 1> fetch_siradel_legacy_f32_pixel(
    const ImageView& input,
    int x,
    int y,
    const NodataFunction& nodata);
PixelValue<float, 1> fetch_signed_fixed_24_8_pixel(
    const ImageView& input,
    int x,
    int y,
    const NodataFunction& nodata);
PixelValue<float, 1> fetch_terrarium_pixel(
    const ImageView& input,
    int x,
    int y,
    const NodataFunction& nodata);
PixelValue<float, 1> fetch_terrain_rgb_pixel(
    const ImageView& input,
    int x,
    int y,
    const NodataFunction& nodata);

struct SamplingFunction
{
    SamplingFunction() = default;
    HRZ_DEFAULT_COPY_MOVE(SamplingFunction);
    virtual ~SamplingFunction() = default;

    // Returns true if the pixel should be discarded, false otherwise.
    virtual bool sample(const ImageView& input, lm::vec2 uv, void* output_v) const = 0;
};

template<typename T, int CHANNELS>
struct SamplingFunctionImpl : public SamplingFunction
{
    using PixelFetchFunction =
        PixelValue<T, CHANNELS> (*)(const ImageView&, int x, int y, const NodataFunction& nodata);

    SamplingFunctionImpl(
        PixelFetchFunction pixel_fetch_func,
        hrz_proto::AlphaChannelUsage alpha_channel_usage,
        const NodataFunction& nodata,
        hrz_proto::TextureFiltering filtering) :
        pixel_fetch_func(pixel_fetch_func),
        alpha_channel_usage(alpha_channel_usage),
        nodata(nodata),
        filtering(filtering)
    {
    }

    PixelFetchFunction pixel_fetch_func;
    hrz_proto::AlphaChannelUsage alpha_channel_usage;
    NodataFunction nodata;
    hrz_proto::TextureFiltering filtering;

    static_assert(
        std::is_same_v<T, uint8_t> || std::is_same_v<T, int8_t> || std::is_same_v<T, uint16_t>
            || std::is_same_v<T, int16_t> || std::is_same_v<T, uint32_t>
            || std::is_same_v<T, int32_t> || std::is_same_v<T, float>,
        "Supported types are uint8_t, int8_t, uint16_t, int16_t, uint32_t, int32_t, and float");

    using SubpixelCoordT = typename subpixel_coord_type<T>::type;
    using NextSizeT = typename next_size<T>::type;

    PixelChannelValue<T> _interpolate_integer_pixel_channel(
        PixelChannelValue<T> data00,
        PixelChannelValue<T> data01,
        PixelChannelValue<T> data10,
        PixelChannelValue<T> data11,
        SubpixelCoordT xf,
        SubpixelCoordT yf) const
    {
        static_assert(std::is_integral_v<T>);

        // Integer arithmetic ftw.

        // @Note Right shifts on signed integers are implementation defined.
        // Usual platforms do the expected computation.
        // Proper divisions should be used when compiling for a platform that doesn't.

        // @Note Conversions to and from unsigned and signed integers are implementation defined.
        // Same thing as above.

        NextSizeT max = (NextSizeT)std::numeric_limits<SubpixelCoordT>::max() + 1;
        NextSizeT shift = sizeof(SubpixelCoordT) * 8;

        if (!data00.is_nodata && !data01.is_nodata && !data10.is_nodata && !data11.is_nodata)
        {
            NextSizeT r0 =
                ((NextSizeT)data00.value * (max - xf) + (NextSizeT)data01.value * xf) >> shift;
            NextSizeT r1 =
                ((NextSizeT)data10.value * (max - xf) + (NextSizeT)data11.value * xf) >> shift;
            return {(T)((r0 * (max - yf) + r1 * yf) >> shift), false};
        }

        // @Todo The interpolation is wrong when only one source value is nodata.
        // (The weight of the value that is alone on its line is too big.)

        NextSizeT r0;
        bool r0_is_nodata = false;
        if (!data00.is_nodata && !data01.is_nodata)
        {
            r0 = ((NextSizeT)data00.value * (max - xf) + (NextSizeT)data01.value * xf) >> shift;
        }
        else if (!data00.is_nodata && data01.is_nodata)
        {
            r0 = data00.value;
        }
        else if (data00.is_nodata && !data01.is_nodata)
        {
            r0 = data01.value;
        }
        else
        {
            r0 = 0;
            r0_is_nodata = true;
        }

        NextSizeT r1;
        bool r1_is_nodata = false;
        if (!data10.is_nodata && !data11.is_nodata)
        {
            r1 = ((NextSizeT)data10.value * (max - xf) + (NextSizeT)data11.value * xf) >> shift;
        }
        else if (!data10.is_nodata && data11.is_nodata)
        {
            r1 = data10.value;
        }
        else if (data10.is_nodata && !data11.is_nodata)
        {
            r1 = data11.value;
        }
        else
        {
            r1 = 0;
            r1_is_nodata = true;
        }

        NextSizeT res;
        if (!r0_is_nodata && !r1_is_nodata)
        {
            res = (r0 * (max - yf) + r1 * yf) >> shift;
        }
        else if (!r0_is_nodata && r1_is_nodata)
        {
            res = r0;
        }
        else if (r0_is_nodata && !r1_is_nodata)
        {
            res = r1;
        }
        else
        {
            // If an interpolated value is nodata on one channel, it is nodata
            // on every channel.
            // Therefore it is safe to construct the final nodata value one
            // channel at a time, as we are assured that the same will happen
            // for every channel.
            return data00;
        }

        return {(T)res, false};
    }

    PixelChannelValue<T> _interpolate_float_pixel_channel(
        const PixelChannelValue<T>& data00,
        const PixelChannelValue<T>& data01,
        const PixelChannelValue<T>& data10,
        const PixelChannelValue<T>& data11,
        SubpixelCoordT xf,
        SubpixelCoordT yf) const
    {
        static_assert(std::is_floating_point_v<T>);

        static constexpr auto kOne = (NextSizeT)1.0;

        if (!data00.is_nodata && !data01.is_nodata && !data10.is_nodata && !data11.is_nodata)
        {
            NextSizeT r0 = (NextSizeT)data00.value * (kOne - xf) + (NextSizeT)data01.value * xf;
            NextSizeT r1 = (NextSizeT)data10.value * (kOne - xf) + (NextSizeT)data11.value * xf;
            return {(T)(r0 * (kOne - yf) + r1 * yf), false};
        }

        // @Todo The interpolation is wrong when only one source value is nodata.
        // (The weight of the value that is alone on its line is too big.)

        NextSizeT r0;
        bool r0_is_nodata = false;
        if (!data00.is_nodata && !data01.is_nodata)
        {
            r0 = ((NextSizeT)data00.value * (kOne - xf) + (NextSizeT)data01.value * xf);
        }
        else if (!data00.is_nodata && data01.is_nodata)
        {
            r0 = data00.value;
        }
        else if (data00.is_nodata && !data01.is_nodata)
        {
            r0 = data01.value;
        }
        else
        {
            r0 = 0;
            r0_is_nodata = true;
        }

        NextSizeT r1;
        bool r1_is_nodata = false;
        if (!data10.is_nodata && !data11.is_nodata)
        {
            r1 = ((NextSizeT)data10.value * (kOne - xf) + (NextSizeT)data11.value * xf);
        }
        else if (!data10.is_nodata && data11.is_nodata)
        {
            r1 = data10.value;
        }
        else if (data10.is_nodata && !data11.is_nodata)
        {
            r1 = data11.value;
        }
        else
        {
            r1 = 0;
            r1_is_nodata = true;
        }

        NextSizeT res;
        if (!r0_is_nodata && !r1_is_nodata)
        {
            res = (r0 * (kOne - yf) + r1 * yf);
        }
        else if (!r0_is_nodata && r1_is_nodata)
        {
            res = r0;
        }
        else if (r0_is_nodata && !r1_is_nodata)
        {
            res = r1;
        }
        else
        {
            // If an interpolated value is nodata on one channel, it is nodata
            // on every channel.
            // Therefore it is safe to construct the final nodata value one
            // channel at a time, as we are assured that the same will happen
            // for every channel.
            return data00;
        }

        return {(T)res, false};
    }

    PixelChannelValue<T> _interpolate_pixel_channel(
        const PixelChannelValue<T>& data00,
        const PixelChannelValue<T>& data01,
        const PixelChannelValue<T>& data10,
        const PixelChannelValue<T>& data11,
        SubpixelCoordT xf,
        SubpixelCoordT yf) const
    {
        if constexpr (std::is_floating_point_v<T>)
        {
            return _interpolate_float_pixel_channel(data00, data01, data10, data11, xf, yf);
        }
        else
        {
            return _interpolate_integer_pixel_channel(data00, data01, data10, data11, xf, yf);
        }
    }

    PixelValue<float, 4> _interpolate_linear_colors(
        PixelValue<float, 4>& data0,
        PixelValue<float, 4>& data1,
        float f) const
    {
        if (data0.is_nodata) return data1;
        if (data1.is_nodata) return data0;

        lm::vec4 a = std::bit_cast<lm::vec4>(data0.value);
        lm::vec4 b = std::bit_cast<lm::vec4>(data1.value);
        lm::vec4 res = a * (1.0f - f) + b * f;

        PixelValue<float, 4> output;
        std::memcpy(output.value.data(), &res, sizeof(float) * 4);
        output.is_nodata = false;

        return output;
    }

    PixelValue<float, 4> _interpolate_linear_color_pixel(
        PixelValue<float, 4>& data00,
        PixelValue<float, 4>& data01,
        PixelValue<float, 4>& data10,
        PixelValue<float, 4>& data11,
        float xf,
        float yf) const
    {
        // @Todo The interpolation is wrong when only one source value is nodata.
        // (The weight of the value that is alone on its line is too big.)

        auto data0 = _interpolate_linear_colors(data00, data01, xf);
        auto data1 = _interpolate_linear_colors(data10, data11, xf);
        return _interpolate_linear_colors(data0, data1, yf);
    }

    PixelValue<uint8_t, 4> _interpolate_srgba8_pixel(
        PixelValue<uint8_t, 4>& data00,
        PixelValue<uint8_t, 4>& data01,
        PixelValue<uint8_t, 4>& data10,
        PixelValue<uint8_t, 4>& data11,
        SubpixelCoordT xf,
        SubpixelCoordT yf) const
    {
        PixelValue<float, 4> data00_lin = _to_premultiplied_linear(data00);
        PixelValue<float, 4> data01_lin = _to_premultiplied_linear(data01);
        PixelValue<float, 4> data10_lin = _to_premultiplied_linear(data10);
        PixelValue<float, 4> data11_lin = _to_premultiplied_linear(data11);

        float xff = (float)xf / (float)std::numeric_limits<SubpixelCoordT>::max();
        float yff = (float)yf / (float)std::numeric_limits<SubpixelCoordT>::max();

        return _to_srgb(_interpolate_linear_color_pixel(
            data00_lin, data01_lin, data10_lin, data11_lin, xff, yff));
    }

    PixelValue<T, CHANNELS> _interpolate_pixel(
        PixelValue<T, CHANNELS>& data00,
        PixelValue<T, CHANNELS>& data01,
        PixelValue<T, CHANNELS>& data10,
        PixelValue<T, CHANNELS>& data11,
        SubpixelCoordT xf,
        SubpixelCoordT yf) const
    {
        if constexpr (std::is_same_v<T, uint8_t> && CHANNELS == 4)
        {
            return _interpolate_srgba8_pixel(data00, data01, data10, data11, xf, yf);
        }

        PixelValue<T, CHANNELS> output;
        output.is_nodata = false;

        for (unsigned int i = 0; i < CHANNELS; ++i)
        {
            auto channel_value = _interpolate_pixel_channel(
                data00.get_channel(i), data01.get_channel(i), data10.get_channel(i),
                data11.get_channel(i), xf, yf);
            output.value[i] = channel_value.value;
            output.is_nodata |= channel_value.is_nodata;
        }

        return output;
    }

    PixelValue<float, 4> _to_premultiplied_linear(const PixelValue<uint8_t, 4>& input) const
    {
        PixelValue<float, 4> output{{}, input.is_nodata};

        // Nodata values must be preserved if present.
        if (nodata.handling != hrz_proto::IGNORE_NODATA && input.is_nodata)
        {
            return output;
        }

        lm::vec4 color_lin = hrz::srgb_to_linear_lut(std::bit_cast<lm::ubvec4>(input.value));

        if (input.value[3] != 255)
        {
            switch (alpha_channel_usage)
            {
                case hrz_proto::AlphaChannelUsage::IGNORE_ALPHA_CHANNEL: color_lin.a = 1.0f; break;
                case hrz_proto::AlphaChannelUsage::USE_ALPHA_CHANNEL:
                    color_lin = hrz::premultiply_alpha(color_lin);
                    break;
                case hrz_proto::AlphaChannelUsage::USE_ALPHA_CHANNEL_PREMULTIPLIED:
                    // Nothing to do
                    break;
                default: assert(!"Unhandled alpha channel usage"); break;
            }
        }

        output.value = std::bit_cast<std::array<float, 4>>(color_lin);
        return output;
    }

    PixelValue<uint8_t, 4> _to_srgb(const PixelValue<float, 4>& input) const
    {
        return PixelValue<uint8_t, 4>{
            std::bit_cast<std::array<uint8_t, 4>>(
                hrz::linear_to_srgb_lut(std::bit_cast<lm::vec4>(input.value))),
            input.is_nodata};
    }

    // Returns true if the pixel should be discarded, false otherwise.
    bool sample(const ImageView& input, lm::vec2 uv, void* output_v) const override
    {
        assert(CHANNELS == hrz::image_format_channel_count(input.format));

        PixelValue<T, CHANNELS> output;
        output.is_nodata = false;

        const int input_width = input.width;
        const int input_height = input.height;

        if (filtering == hrz_proto::TextureFiltering::BILINEAR)
        {
            uv.x = uv.x * input_width - 0.5;
            uv.y = uv.y * input_height - 0.5;

            int x0 = std::floor(uv.x);
            int y0 = std::floor(uv.y);
            int x1 = std::min(x0 + 1, input_width - 1);
            int y1 = std::min(y0 + 1, input_height - 1);
            x0 = std::max(0, x0);
            y0 = std::max(0, y0);

            float xf_f = std::min(std::max(uv.x - (float)x0, 0.0F), 1.0F);
            float yf_f = std::min(std::max(uv.y - (float)y0, 0.0F), 1.0F);

            SubpixelCoordT xf;
            SubpixelCoordT yf;
            if (std::is_integral_v<T>)
            {
                xf = xf_f * ((NextSizeT)std::numeric_limits<SubpixelCoordT>::max() + 1);
                yf = yf_f * ((NextSizeT)std::numeric_limits<SubpixelCoordT>::max() + 1);
            }
            else
            {
                xf = xf_f;
                yf = yf_f;
            }

            auto data00 = pixel_fetch_func(input, x0, y0, nodata);
            auto data01 = pixel_fetch_func(input, x1, y0, nodata);
            auto data10 = pixel_fetch_func(input, x0, y1, nodata);
            auto data11 = pixel_fetch_func(input, x1, y1, nodata);

            output = _interpolate_pixel(data00, data01, data10, data11, xf, yf);
        }
        else
        {
            uv.x *= input_width;
            uv.y *= input_height;

            int x = std::min((int)std::floor(uv.x), input_width - 1);
            int y = std::min((int)std::floor(uv.y), input_height - 1);

            auto data = pixel_fetch_func(input, x, y, nodata);

            if constexpr (std::is_same_v<T, uint8_t> && CHANNELS == 4)
            {
                output = _to_srgb(_to_premultiplied_linear(data));
            }
            else
            {
                output = data;
            }
        }

        nodata.apply<T, CHANNELS>(output);
        std::memcpy(output_v, output.value.data(), sizeof(T) * CHANNELS);
        return output.is_nodata;
    }
};

using ImagerySamplingFunction = SamplingFunctionImpl<uint8_t, 4>;
using DtmSamplingFunction = SamplingFunctionImpl<float, 1>;

std::unique_ptr<SamplingFunction> make_sampling_function(
    hrz_proto::AlphaChannelUsage alpha_channel_usage,
    const hrz_proto::RasterNodata& raster_nodata,
    hrz_proto::NodataHandling nodata_handling,
    hrz_proto::TextureFiltering filtering,
    hrz_proto::ImageFormat image_format);

// Returns whether the pixel was modified or not.
template<typename T, int CHANNELS>
bool sample_and_compose_raster(
    const ImageView& input,
    lm::vec2 uv,
    const SamplingFunction* sampling,
    const BlendingFunction* blending,
    T* output)
{
    assert(CHANNELS == hrz::image_format_channel_count(input.format));
    static_assert(
        std::is_same_v<T, uint8_t> || std::is_same_v<T, int8_t> || std::is_same_v<T, uint16_t>
            || std::is_same_v<T, int16_t> || std::is_same_v<T, uint32_t>
            || std::is_same_v<T, int32_t> || std::is_same_v<T, float>,
        "Supported types are uint8_t, int8_t, uint16_t, int16_t, uint32_t, int32_t and float");

    std::array<T, CHANNELS> pixel;
    bool discard = sampling->sample(input, uv, pixel.data());

    if (discard)
    {
        return false;
    }
    else
    {
        if (blending != nullptr)
        {
            blending->blend(pixel.data(), output);
        }
        else
        {
            memcpy(output, pixel.data(), CHANNELS * sizeof(T));
        }
        return true;
    }
}

} // namespace hrz::sampling
