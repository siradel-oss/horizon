#pragma once

namespace hrz
{
// Non-locale-dependent functions

constexpr inline bool is_ascii_digit(const char c)
{
    return c >= '0' && c <= '9';
}

constexpr inline bool is_ascii_hexdigit(const char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

constexpr inline bool is_ascii_letter(const char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

constexpr inline bool is_ascii_alpha_numeric(const char c)
{
    return is_ascii_letter(c) || is_ascii_digit(c);
}

constexpr inline bool is_ascii_lowercase_letter(const char c)
{
    return c >= 'a' && c <= 'z';
}

constexpr inline bool is_ascii_whitespace(const char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

constexpr inline int ascii_to_lower(int c)
{
    if (c >= 'A' && c <= 'Z') return c + 'a' - 'A';
    return c;
}
} // namespace hrz
