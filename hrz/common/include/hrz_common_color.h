#pragma once

#include <hrz_common_proto_maths.h>
#include <hrz_fnd_char_utils.h>
#include <hrz_fnd_maths.h>
#include <hrz_protocol_all.h>

#include <lin_maths.h>

#include <bit>
#include <optional>
#include <span>
#include <string_view>

namespace hrz
{
// Converts from HSL colorspace to RGB colorspace with:
// - H, S and L in [0, 1]. Note that the hue isn't in [0°, 360°].
// - R, G and B in [0, 1]
//
lm::vec3 hsl_to_rgb(const lm::vec3& hsl);

// All components are in [0, 1]
lm::vec3 hsv_to_rgb(const lm::vec3& c);

// All components are in [0, 1]
lm::vec3 rgb_to_hsv(const lm::vec3& c);

// All components are in [0, 1]
lm::vec4 srgb_to_oklab(const lm::vec4& srgb);

// All components are in [0, 1]
lm::vec4 oklab_to_srgb(const lm::vec4& lms);

// In OKLab space
lm::vec4 mix_srgb_colors(const lm::vec4& x, const lm::vec4& y, float t);

std::optional<uint32_t> parse_color_string(std::string_view str);

std::string make_color_string(
    uint32_t c,
    std::string_view prefix,
    bool include_alpha = true,
    bool uppercase = false);

// @Note: color channels are expected to be in the [0, 1] range.
static inline lm::ubvec4 convert_rgba_color_to_bytes(const lm::vec4& color)
{
    auto to_byte = [](float f) { return (uint8_t)std::round(hrz::clamp(f, 0.0f, 1.0f) * 255.0f); };
    return {to_byte(color.r), to_byte(color.g), to_byte(color.b), to_byte(color.a)};
}

static inline lm::vec4 convert_bytes_to_rgba_color(const lm::ubvec4& color)
{
    return lm::vec4{
        (float)color.r / 255.0F,
        (float)color.g / 255.0F,
        (float)color.b / 255.0F,
        (float)color.a / 255.0F,
    };
}

static inline lm::ubvec4 convert_uint_color_to_bytes(uint32_t c)
{
    return std::bit_cast<lm::ubvec4, uint32_t>(c);
}

// @Note: color channels are expected to be in the [0, 1] range.
static inline uint32_t convert_rgba_color_to_uint(const lm::vec4& color)
{
    auto rgba = convert_rgba_color_to_bytes(color);
    return rgba.r + (rgba.g << 8) + (rgba.b << 16) + (rgba.a << 24);
}

static inline lm::vec4 convert_uint_color_to_rgba(uint32_t c)
{
    return lm::vec4{
               (float)(c & 0xff),
               (float)((c >> 8) & 0xff),
               (float)((c >> 16) & 0xff),
               (float)(c >> 24),
           }
    / 255.0f;
}

static inline lm::ubvec4 convert_proto_color_to_bytes(const hrz_proto::Color& c)
{
    return convert_rgba_color_to_bytes(hrz::to_lm(c));
}

static inline uint32_t convert_proto_color_to_uint(const hrz_proto::Color& c)
{
    return convert_rgba_color_to_uint(hrz::to_lm(c));
}

static inline hrz_proto::Color convert_uint_to_proto_color(uint32_t color_uint)
{
    lm::vec4 channels = convert_uint_color_to_rgba(color_uint);
    hrz_proto::Color color;
    color.set_r(channels.r);
    color.set_g(channels.g);
    color.set_b(channels.b);
    color.set_a(channels.a);
    return color;
}
} // namespace hrz
