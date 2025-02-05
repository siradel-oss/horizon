#pragma once

#include <hrz_fnd_bit_cast.h>
#include <hrz_fnd_defines.h>

#include <lin_maths.h>

#include <stdint.h>

namespace hrz
{
static lm::ubvec4 premultiply_alpha(lm::ubvec4 rgba)
{
    float alpha = (float)rgba.a / 255.0f;

    auto premultiply_channel = [&](uint8_t c) { return (uint8_t)std::round((float)c * alpha); };

    return {
        premultiply_channel(rgba.r), premultiply_channel(rgba.g), premultiply_channel(rgba.b),
        rgba.a};
}

// IEEE 754 single precision little endian float decoding,
// with slightly modified bit layout for a faster decoding,
// when uintBitsToFloat isn't available and arithmetic must
// be used to reconstruct the float value. (e.g. in WebGL 1)
//
// Bit layouts:
//     IEEE 754: seeeeeeeefffffffffffffffffffffff
//     Silicium: eeeeeeeesfffffffffffffffffffffff
//     color:    aaaaaaaabbbbbbbbggggggggrrrrrrrr
//     s: sign, e: exponent, f: fraction
//
// cf. https://msdn.microsoft.com/en-us/library/yhwsaf3w%28v=vs.110%29.aspx
// cf. https://en.wikipedia.org/wiki/Single-precision_floating-point_format
// cf. http://stackoverflow.com/a/31002725
static float decode_r_f32_silicium_value_to_float(uint32_t rgba)
{
    uint32_t res = (rgba & 0x007fffff) | ((rgba >> 1) & 0x7f800000) | ((rgba << 8) & 0x80000000);
    return hrz::bit_cast<float>(res);
}

static uint32_t encode_float_to_r_f32_silicium(float value)
{
    uint32_t bits = hrz::bit_cast<uint32_t>(value);
    return (bits & 0x007fffff) | ((bits & 0x80000000) >> 8) | ((bits & 0x7f800000) << 1);
}

static float decode_signed_fixed_24_8_to_float(uint32_t rgba)
{
    return (float)(int32_t)rgba / 256.0f;
}

static uint32_t encode_float_to_signed_fixed_24_8(float value)
{
    return (uint32_t)(int32_t)(value * 256.0f);
}

// value = (red * 256 + green + blue / 256) - 32768
//
// See https://www.mapzen.com/blog/terrain-tile-service/
//     https://github.com/tilezen/joerd/blob/master/docs/formats.md
static float decode_mapzen_terrarium_value_to_float(uint32_t rgba)
{
    lm::ubvec4 v;
    std::memcpy(&v, &rgba, sizeof(lm::ubvec4));
    return (v.r * 256.0f + v.g + v.b / 256.0f) - 32768.0f;
}

static uint32_t encode_float_to_mapzen_terrarium(float value)
{
    value += 32768.0f;
    lm::ubvec4 rgba = {
        (uint8_t)(value / 256.0f), (uint8_t)((int)value % 256),
        (uint8_t)((value - std::floor(value)) * 256.0f), 0};
    uint32_t res;
    std::memcpy(&res, &rgba, sizeof(uint32_t));
    return res;
}
} // namespace hrz
