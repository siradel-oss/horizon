#pragma once

#include "hrz_common_image_view.h"

#include <hrz_common_image_processing.h>
#include <hrz_fnd_bit_cast.h>
#include <hrz_fnd_class.h>
#include <hrz_fnd_log.h>
#include <hrz_fnd_maths.h>
#include <hrz_protocol_all.h>

#include <assert.h>
#include <gsl/gsl-lite.hpp>
#include <lin_maths.h>

#include <array>
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
PixelValue<float, 1> fetch_r_f32_silicium_pixel(
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

    PixelValue<T, CHANNELS> _interpolate_pixel(
        PixelValue<T, CHANNELS>& data00,
        PixelValue<T, CHANNELS>& data01,
        PixelValue<T, CHANNELS>& data10,
        PixelValue<T, CHANNELS>& data11,
        SubpixelCoordT xf,
        SubpixelCoordT yf) const
    {
        // It's important to switch to premultiplied alpha before interpolating
        // to get good results.
        _apply_alpha(data00);
        _apply_alpha(data01);
        _apply_alpha(data10);
        _apply_alpha(data11);

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

    void _apply_alpha(PixelValue<T, CHANNELS>& value) const
    {
        // Nodata values must be preserved if present.
        if (nodata.handling != hrz_proto::IGNORE_NODATA && value.is_nodata) return;

        if constexpr (std::is_integral_v<T> && CHANNELS == 4)
        {
            T& r = value.value[0];
            T& g = value.value[1];
            T& b = value.value[2];
            T& a = value.value[3];

            static const NextSizeT max = (NextSizeT)std::numeric_limits<SubpixelCoordT>::max() + 1;
            static const NextSizeT shift = sizeof(SubpixelCoordT) * 8;

            switch (alpha_channel_usage)
            {
                case hrz_proto::AlphaChannelUsage::IGNORE_ALPHA_CHANNEL: a = (T)max - 1; break;
                case hrz_proto::AlphaChannelUsage::USE_ALPHA_CHANNEL:
                    r = (T)((((NextSizeT)r + 1) * a) >> shift);
                    g = (T)((((NextSizeT)g + 1) * a) >> shift);
                    b = (T)((((NextSizeT)b + 1) * a) >> shift);
                    break;
                case hrz_proto::AlphaChannelUsage::USE_ALPHA_CHANNEL_PREMULTIPLIED:
                    // Nothing to do
                    break;
                default: assert(!"Unhandled alpha channel usage");
            }
        }
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
            _apply_alpha(data);
            output = data;
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
