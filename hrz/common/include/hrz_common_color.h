#pragma once

#include <hrz_common_proto_maths.h>
#include <hrz_fnd_char_utils.h>
#include <hrz_fnd_maths.h>
#include <hrz_fnd_mem.h>
#include <hrz_protocol_all.h>

#include <lin_maths.h>

#include <bit>
#include <cmath>
#include <optional>
#include <span>
#include <string_view>

namespace hrz
{
namespace color
{
namespace detail
{
alignas(hrz::L1CacheLineSize) extern float srgb_to_linear_lut[256];
alignas(hrz::L1CacheLineSize) extern uint8_t linear_to_srgb_lut[4096];

static inline float srgb_u8_to_linear_f32(uint8_t c)
{
    return srgb_to_linear_lut[c];
}

static inline uint8_t linear_f32_to_srgb_u8(float c)
{
    // Clamp and convert to 12-bit index
    int idx = static_cast<int>(hrz::clamp(c, 0.0f, 1.0f) * 4095.0f + 0.5f);
    return linear_to_srgb_lut[idx];
}

template<std::floating_point T>
static inline T srgb_to_linear(T c)
{
    if (c <= T(0.04045))
        return c * T(1.0 / 12.92);
    else
        return std::pow((c + T(0.055)) * (T(1.0) / T(1.055)), T(2.4));
}

template<std::floating_point T>
static inline T linear_to_srgb(T c)
{
    if (c <= T(0.0031308))
        return c * T(12.92);
    else
        return T(1.055) * std::pow(c, T(1.0) / T(2.4)) - T(0.055);
}
} // namespace detail

void initialize_srgb_luts();
} // namespace color

static inline lm::vec3 srgb_to_linear_lut(const lm::ubvec3& color)
{
    return lm::vec3{
        color::detail::srgb_u8_to_linear_f32(color.r),
        color::detail::srgb_u8_to_linear_f32(color.g),
        color::detail::srgb_u8_to_linear_f32(color.b),
    };
}

static inline lm::vec4 srgb_to_linear_lut(const lm::ubvec4& color)
{
    return lm::vec4(srgb_to_linear_lut(color.rgb), color.a * (1.0f / 255.0f));
}

static inline lm::ubvec3 linear_to_srgb_lut(const lm::vec3& color)
{
    return lm::ubvec3{
        color::detail::linear_f32_to_srgb_u8(color.r),
        color::detail::linear_f32_to_srgb_u8(color.g),
        color::detail::linear_f32_to_srgb_u8(color.b),
    };
}

static inline lm::ubvec4 linear_to_srgb_lut(const lm::vec4& color)
{
    return lm::ubvec4(
        linear_to_srgb_lut(color.rgb), (uint8_t)(hrz::clamp(color.a, 0.0f, 1.0f) * 255.0f + 0.5f));
}

template<typename T>
static inline lm::Vector<T, 3> srgb_to_linear(const lm::Vector<T, 3>& color)
{
    return lm::Vector<T, 3>{
        color::detail::srgb_to_linear(color.r),
        color::detail::srgb_to_linear(color.g),
        color::detail::srgb_to_linear(color.b),
    };
}

template<typename T>
static inline lm::Vector<T, 4> srgb_to_linear(const lm::Vector<T, 4>& color)
{
    return lm::Vector<T, 4>{srgb_to_linear(color.rgb), color.a};
}

template<typename T>
static inline lm::Vector<T, 3> linear_to_srgb(const lm::Vector<T, 3>& color)
{
    return lm::Vector<T, 3>{
        color::detail::linear_to_srgb(color.r),
        color::detail::linear_to_srgb(color.g),
        color::detail::linear_to_srgb(color.b),
    };
}

template<typename T>
static inline lm::Vector<T, 4> linear_to_srgb(const lm::Vector<T, 4>& color)
{
    return lm::Vector<T, 4>{linear_to_srgb(color.rgb), color.a};
}

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
lm::vec4 linear_to_oklab(const lm::vec4& rgb_lin);

// All components are in [0, 1]
lm::vec4 oklab_to_linear(const lm::vec4& lms);

// All components are in [0, 1]
lm::vec4 srgb_to_oklab(const lm::vec4& srgb);

// All components are in [0, 1]
lm::vec4 oklab_to_srgb(const lm::vec4& lms);

lm::ubvec4 mix_srgb_colors_in_linear(const lm::ubvec4& x, const lm::ubvec4& y, float t);

lm::ubvec4 mix_srgb_colors_in_oklab(const lm::ubvec4& x, const lm::ubvec4& y, float t);

static inline lm::vec4 premultiply_alpha(lm::vec4 c)
{
    c.rgb *= c.a;
    return c;
}

lm::ubvec4 premultiply_alpha(const lm::ubvec4& rgba);

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

static inline lm::ubvec4 convert_uint_color_to_bytes(uint32_t color)
{
    return std::bit_cast<lm::ubvec4, uint32_t>(color);
}

static inline uint32_t convert_byte_color_to_uint(const lm::ubvec4& color)
{
    return std::bit_cast<uint32_t, lm::ubvec4>(color);
}

// @Note: color channels are expected to be in the [0, 1] range.
static inline uint32_t convert_rgba_color_to_uint(const lm::vec4& color)
{
    return convert_byte_color_to_uint(convert_rgba_color_to_bytes(color));
}

static inline lm::vec4 convert_byte_color_to_rgba(const lm::ubvec4& color)
{
    return lm::vec4{(float)color.r, (float)color.g, (float)color.b, (float)color.a} / 255.0f;
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

static inline lm::vec4 convert_proto_color_to_float(const hrz_proto::Color& c)
{
    return lm::vec4(c.r(), c.g(), c.b(), c.a());
}

static inline lm::ubvec4 convert_proto_color_to_bytes(const hrz_proto::Color& c)
{
    return convert_rgba_color_to_bytes(convert_proto_color_to_float(c));
}

static inline uint32_t convert_proto_color_to_uint(const hrz_proto::Color& c)
{
    return convert_rgba_color_to_uint(convert_proto_color_to_float(c));
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
