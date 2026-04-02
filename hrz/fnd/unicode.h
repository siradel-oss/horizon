#pragma once

#include <optional>
#include <stdint.h>
#include <string_view>

namespace hrz::unicode
{

struct CodePoint
{
    uint32_t code_point;
    uint8_t utf8_byte_size;
};

/**
 * Returns the first codepoint of the string, or an empty optional if the string
 * does not start with a valid UTF-8 codepoint.
 */
std::optional<CodePoint> get_first_code_point(std::string_view str);

bool is_whitespace(uint32_t code_point);

inline bool is_whitespace(const CodePoint& code_point)
{
    return is_whitespace(code_point.code_point);
}

bool is_hyphen(uint32_t code_point);

inline bool is_hyphen(const CodePoint& code_point)
{
    return is_hyphen(code_point.code_point);
}

// Does not include Hangul
bool is_cjk_character(uint32_t code_point);

inline bool is_cjk_character(const CodePoint& code_point)
{
    return is_cjk_character(code_point.code_point);
}

} // namespace hrz::unicode
