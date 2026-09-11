// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/fnd/log.h"
#include "hrz/fnd/maths.h"
#include "hrz/protocol/image.pb.h"

#include <lin_maths.h>
#include <mycelium/backend.h>

#include <bit>
#include <optional>
#include <stdint.h>

namespace hrz
{

static inline uint8_t image_format_channel_count(hrz_proto::ImageFormat format)
{
    switch (format)
    {
        case hrz_proto::ImageFormat::SRGBA_8: return 4;
        case hrz_proto::ImageFormat::SIGNED_FIXED_24_8:
        case hrz_proto::ImageFormat::TERRARIUM:
        case hrz_proto::ImageFormat::TERRAIN_RGB:
        case hrz_proto::ImageFormat::R_F32:
        case hrz_proto::ImageFormat::SIRADEL_LEGACY_F32:
        case hrz_proto::ImageFormat::R_8: return 1;
        default:
        {
            assert(!"Unhandled image format in channel_count");
            return 1;
        }
    }
}

static inline uint8_t image_format_bit_count(hrz_proto::ImageFormat format)
{
    switch (format)
    {
        case hrz_proto::ImageFormat::SRGBA_8: return 32;
        case hrz_proto::ImageFormat::SIGNED_FIXED_24_8:
        case hrz_proto::ImageFormat::TERRARIUM:
        case hrz_proto::ImageFormat::TERRAIN_RGB:
        case hrz_proto::ImageFormat::R_F32:
        case hrz_proto::ImageFormat::SIRADEL_LEGACY_F32: return 32;
        case hrz_proto::ImageFormat::R_8: return 8;
        default:
        {
            assert(!"Unhandled image format in bit_count");
            return 8;
        }
    }
}

static inline uint8_t image_format_byte_count(hrz_proto::ImageFormat format)
{
    return image_format_bit_count(format) / 8;
}

static inline my::TextureFormat image_format_to_gpu_format(hrz_proto::ImageFormat format)
{
    switch (format)
    {
        case hrz_proto::ImageFormat::SRGBA_8: return my::TextureFormat::SRGBA8;
        case hrz_proto::ImageFormat::SIGNED_FIXED_24_8: return my::TextureFormat::R32I;
        case hrz_proto::ImageFormat::R_F32: return my::TextureFormat::R32F;
        case hrz_proto::ImageFormat::SIRADEL_LEGACY_F32: return my::TextureFormat::R32I;
        case hrz_proto::ImageFormat::TERRARIUM:
        case hrz_proto::ImageFormat::TERRAIN_RGB: return my::TextureFormat::R32UI;
        case hrz_proto::ImageFormat::R_8: return my::TextureFormat::R8;
        default:
            HRZ_LOG_ERROR("Unhandled image format: {}", hrz_proto::ImageFormat_Name(format));
            assert(false);
            return my::TextureFormat::RGBA8;
    }
}

static inline std::optional<hrz_proto::ImageFormat> gpu_format_to_image_format(
    my::TextureFormat format)
{
    switch (format)
    {
        case my::TextureFormat::RGBA8: return hrz_proto::ImageFormat::SRGBA_8;
        case my::TextureFormat::R32I: return hrz_proto::ImageFormat::SIGNED_FIXED_24_8;
        case my::TextureFormat::R32F: return hrz_proto::ImageFormat::R_F32;
        case my::TextureFormat::R8: return hrz_proto::ImageFormat::R_8;
        default: return std::nullopt;
    }
}

static inline bool is_scalar_image_format(hrz_proto::ImageFormat format)
{
    return format == hrz_proto::ImageFormat::R_F32
        || format == hrz_proto::ImageFormat::SIRADEL_LEGACY_F32
        || format == hrz_proto::ImageFormat::SIGNED_FIXED_24_8
        || format == hrz_proto::ImageFormat::TERRARIUM
        || format == hrz_proto::ImageFormat::TERRAIN_RGB;
}

// IEEE 754 single precision little endian float decoding,
// with slightly modified bit layout for a faster decoding,
// when uintBitsToFloat isn't available and arithmetic must
// be used to reconstruct the float value. (e.g. in WebGL 1)
//
// Bit layouts:
//     IEEE 754:           seeeeeeeefffffffffffffffffffffff
//     Siradel legacy f32: eeeeeeeesfffffffffffffffffffffff
//     color:              aaaaaaaabbbbbbbbggggggggrrrrrrrr
//     s: sign, e: exponent, f: fraction
//
// cf. https://msdn.microsoft.com/en-us/library/yhwsaf3w%28v=vs.110%29.aspx
// cf. https://en.wikipedia.org/wiki/Single-precision_floating-point_format
// cf. http://stackoverflow.com/a/31002725
static inline float decode_siradel_legacy_f32_value_to_float(uint32_t rgba)
{
    const uint32_t res =
        (rgba & 0x007fffff) | ((rgba >> 1) & 0x7f800000) | ((rgba << 8) & 0x80000000);
    return std::bit_cast<float>(res);
}

static inline uint32_t encode_float_to_siradel_legacy_f32(float value)
{
    const auto bits = std::bit_cast<uint32_t>(value);
    return (bits & 0x007fffff) | ((bits & 0x80000000) >> 8) | ((bits & 0x7f800000) << 1);
}

static inline float decode_signed_fixed_24_8_to_float(uint32_t rgba)
{
    return (float)(int32_t)rgba / 256.0F;
}

static inline uint32_t encode_float_to_signed_fixed_24_8(float value)
{
    return (uint32_t)(int32_t)(value * 256.0F);
}

// value = (red * 256 + green + blue / 256) - 32768
//
// See https://www.mapzen.com/blog/terrain-tile-service/
//     https://github.com/tilezen/joerd/blob/master/docs/formats.md
static inline float decode_terrarium_value_to_float(uint32_t rgba)
{
    auto v = lm::vec4(std::bit_cast<lm::ubvec4>(rgba));
    return (v.r * 256.0F + v.g + v.b / 256.0F) - 32768.0F;
}

static inline uint32_t encode_float_to_terrarium(float value)
{
    const auto int_value = static_cast<uint32_t>(
        hrz::clamp((value + 32768.0F) * 256.0F, 0.0F, 256 * 256 * 256 - 1.0F));
    const lm::ubvec4 rgba = {
        (uint8_t)(int_value / (256 * 256)), (uint8_t)((int_value % (256 * 256)) / 256),
        (uint8_t)(int_value % 256), 0
    };
    return std::bit_cast<uint32_t>(rgba);
}

// value = -10000 + (red * 256 * 256 + green * 256 + blue) * 0.1
//
// See https://docs.mapbox.com/data/tilesets/guides/access-elevation-data/#decode-data
static inline float decode_terrain_rgb_value_to_float(uint32_t rgba)
{
    auto v = lm::vec4(std::bit_cast<lm::ubvec4>(rgba));
    return -10000.0F + (v.r * 256.0F * 256.0F + v.g * 256.0F + v.b) * 0.1F;
}

static inline uint32_t encode_float_to_terrain_rgb(float value)
{
    const auto int_value =
        static_cast<uint32_t>(hrz::clamp((value + 10000.0F) * 10.0F, 0.0F, 256 * 256 * 256 - 1.0F));
    const lm::ubvec4 rgba = {
        (uint8_t)(int_value / (256 * 256)), (uint8_t)((int_value % (256 * 256)) / 256),
        (uint8_t)(int_value % 256), 0
    };
    return std::bit_cast<uint32_t>(rgba);
}

} // namespace hrz
