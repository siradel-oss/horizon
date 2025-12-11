#include "hrz/common/color.h"

#include "hrz/fnd/log.h"

#include <cmath>
#include <string_view>

#define CHECK_ERR(...)                                             \
    if (!(__VA_ARGS__))                                            \
    {                                                              \
        HRZ_LOG_ERROR("Invalid color string '{}'.", input.data()); \
        return false;                                              \
    }

namespace
{
inline bool is_digit(uint8_t c)
{
    return c >= '0' && c <= '9';
}

inline bool is_hexdigit(uint8_t c)
{
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

inline uint8_t hexdigit_char_to_u8_or_zero(char c)
{
    if (!is_hexdigit(c)) return 0;

    if (is_digit(c))
    {
        return c - '0';
    }
    else if (c <= 'F')
    {
        return c - 'A' + 10;
    }
    else
    {
        return c - 'a' + 10;
    }
}

inline uint8_t hexbyte_char_to_u8_or_zero(char low, char high)
{
    return hexdigit_char_to_u8_or_zero(low) + (hexdigit_char_to_u8_or_zero(high) << 4);
}

inline float clamp(float x, float min, float max)
{
    return x <= min ? min : (x >= max ? max : x);
}

inline float glsl_mod(float x, float y)
{
    return x - std::floor(x / y) * y;
}

// From https://www.shadertoy.com/view/lsS3Wc
void hsl_to_rgb(const float hsl[3], float rgb[3])
{
    float temp[3] = {
        clamp(std::abs(glsl_mod(hsl[0] * 6 + 0, 6) - 3) - 1.5f, -0.5f, 0.5f),
        clamp(std::abs(glsl_mod(hsl[0] * 6 + 4, 6) - 3) - 1.5f, -0.5f, 0.5f),
        clamp(std::abs(glsl_mod(hsl[0] * 6 + 2, 6) - 3) - 1.5f, -0.5f, 0.5f)};

    float factor = (1.0f - std::abs(2.0f * hsl[2] - 1.0f));

    rgb[0] = hsl[2] + hsl[1] * temp[0] * factor;
    rgb[1] = hsl[2] + hsl[1] * temp[1] * factor;
    rgb[2] = hsl[2] + hsl[1] * temp[2] * factor;
}

inline std::string_view advance_cur(std::string_view input, size_t offset)
{
    if (offset >= input.size())
    {
        return input.substr(offset, 0);
    }
    return input.substr(offset, input.size() - offset);
}

inline std::string_view skip_spaces(std::string_view input)
{
    size_t offset = 0;
    while (offset < input.size() && input[offset] == ' ')
        offset++;
    return advance_cur(input, offset);
}

inline uint32_t color_bytes_to_uint(uint8_t rgba[4])
{
    return (uint32_t)rgba[0] | ((uint32_t)rgba[1] << 8) | ((uint32_t)rgba[2] << 16)
        | ((uint32_t)rgba[3] << 24);
}

// Parses a color string with one of the following format:
// - "#rgb"
// - "#rrggbb"
// - "#rrggbbaa"
bool parse_hexadecimal_color_string(std::string_view input, uint32_t* color)
{
    CHECK_ERR(input[0] == '#');

    input = advance_cur(input, 1);

    uint8_t rgba[4] = {0, 0, 0, 255};

    if (input.size() == 3)
    {
        rgba[0] = hexbyte_char_to_u8_or_zero(input[0], input[0]);
        rgba[1] = hexbyte_char_to_u8_or_zero(input[1], input[1]);
        rgba[2] = hexbyte_char_to_u8_or_zero(input[2], input[2]);
    }
    else if (input.size() == 6)
    {
        rgba[0] = hexbyte_char_to_u8_or_zero(input[1], input[0]);
        rgba[1] = hexbyte_char_to_u8_or_zero(input[3], input[2]);
        rgba[2] = hexbyte_char_to_u8_or_zero(input[5], input[4]);
    }
    else if (input.size() == 8)
    {
        rgba[0] = hexbyte_char_to_u8_or_zero(input[1], input[0]);
        rgba[1] = hexbyte_char_to_u8_or_zero(input[3], input[2]);
        rgba[2] = hexbyte_char_to_u8_or_zero(input[5], input[4]);
        rgba[3] = hexbyte_char_to_u8_or_zero(input[7], input[6]);
    }
    else
    {
        CHECK_ERR(false);
    }

    *color = color_bytes_to_uint(rgba);
    return true;
}

enum class FunctionType
{
    Rgb,
    Rgba,
    Hsl,
    Hsla,
};

int64_t parse_i64(std::string_view& input)
{
    size_t offset = 0;
    while (offset < input.size() && is_digit(input[offset]))
        offset++;

    int64_t value = std::strtol(input.data(), nullptr, 10);
    input = advance_cur(input, offset);
    return value;
}

double parse_f64(std::string_view& input)
{
    size_t offset = 0;
    while (offset < input.size() && is_digit(input[offset]))
        offset++;
    if (offset < input.size() && input[offset] == '.') offset++;
    while (offset < input.size() && is_digit(input[offset]))
        offset++;

    double value = std::strtof(input.data(), nullptr);
    input = advance_cur(input, offset);
    return value;
}

// Parses a color string with one of the following format:
// - "rgb(r, g, b)"
// - "rgba(r, g, b, a)"
uint32_t parse_rgba_function_color_string(
    std::string_view input,
    FunctionType func,
    uint32_t* color)
{
    CHECK_ERR(func == FunctionType::Rgb || func == FunctionType::Rgba);

    uint8_t rgba[4] = {0, 0, 0, 255};
    size_t num_operands = func == FunctionType::Rgb ? 3 : 4;
    input = advance_cur(input, num_operands + 1); // Skip the parenthesis as well.

    for (size_t i = 0; i < num_operands; ++i)
    {
        input = skip_spaces(input);
        rgba[i] = i == 3 ? (uint8_t)std::round(parse_f64(input) * 255) : (uint8_t)parse_i64(input);
        input = skip_spaces(input);

        if (i != num_operands - 1)
        {
            CHECK_ERR(input.size() > 0 && input[0] == ',');
            input = advance_cur(input, 1);
        }
    }

    input = skip_spaces(input);

    CHECK_ERR(input.size() > 0 && input[0] == ')');

    *color = color_bytes_to_uint(rgba);
    return true;
}

// Parses a color string with one of the following format:
// - "hsl(h, s%, l%)"
// - "hsl(h, s%, l%, a)"
uint32_t parse_hsla_function_color_string(
    std::string_view input,
    FunctionType func,
    uint32_t* color)
{
    CHECK_ERR(func == FunctionType::Hsl || func == FunctionType::Hsla);

    float hsla[4] = {0, 0, 0, 1};
    size_t num_operands = func == FunctionType::Hsl ? 3 : 4;

    input = advance_cur(input, num_operands + 1); // Skip the parenthesis as well.

    for (size_t i = 0; i < num_operands; ++i)
    {
        input = skip_spaces(input);

        hsla[i] = (float)parse_f64(input);

        if (i == 1 || i == 2)
        {
            input = advance_cur(input, 1);
        }

        input = skip_spaces(input);

        if (i != num_operands - 1)
        {
            CHECK_ERR(input.size() > 0 && input[0] == ',');
            input = advance_cur(input, 1);
        }
    }

    input = skip_spaces(input);

    CHECK_ERR(input.size() > 0 && input[0] == ')');

    // Sanitize the input and put everything in the [0, 1] range before HSL to RGB conversion.
    hsla[0] = clamp(hsla[0], 0, 360) / 360.0f;
    hsla[1] = clamp(hsla[1], 0, 100) / 100.0f;
    hsla[2] = clamp(hsla[2], 0, 100) / 100.0f;
    hsla[3] = clamp(hsla[3], 0, 1);

    float rgb[3];
    hsl_to_rgb(hsla, rgb);
    uint8_t rgba[4] = {
        (uint8_t)std::round(rgb[0] * 255.0f), (uint8_t)std::round(rgb[1] * 255.0f),
        (uint8_t)std::round(rgb[2] * 255.0f), (uint8_t)std::round(hsla[3] * 255.0f)};

    *color = color_bytes_to_uint(rgba);
    return true;
}
} // anonymous namespace

namespace hrz_mapbox
{
// Parse a Mapbox style color string. The color string can be one of the following format:
// - "#rgb"
// - "#rrggbb"
// - "#rrggbbaa"
// - "rgb(r, g, b)"
// - "rgba(r, g, b, a)"
// - "hsl(h, s%, l%)"
// - "hsla(h, s%, l%, a)"
// - A HTML color name like "yellow", "navy", etc.
// Returns the color encoded in a uint32_t with the following format `0xaabbggrr`.
bool parse_mapbox_color_string(std::string_view input, uint32_t* color)
{
    if (input.size() == 0) return 0;

    if (input[0] == '#')
    {
        CHECK_ERR(parse_hexadecimal_color_string(input, color));
    }
    else if (input.starts_with("rgb("))
    {
        CHECK_ERR(parse_rgba_function_color_string(input, FunctionType::Rgb, color));
    }
    else if (input.starts_with("rgba("))
    {
        CHECK_ERR(parse_rgba_function_color_string(input, FunctionType::Rgba, color));
    }
    else if (input.starts_with("hsl("))
    {
        CHECK_ERR(parse_hsla_function_color_string(input, FunctionType::Hsl, color));
    }
    else if (input.starts_with("hsla("))
    {
        CHECK_ERR(parse_hsla_function_color_string(input, FunctionType::Hsla, color));
    }
    else
    {
        auto color_opt = hrz::parse_color_string(input);
        CHECK_ERR(color_opt.has_value());
        *color = color_opt.value();
    }

    return true;
}
} // namespace hrz_mapbox
