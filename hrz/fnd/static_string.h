#pragma once

#include <string_view>

namespace hrz
{
struct StaticString;
}

static hrz::StaticString operator""_ss(const char* ptr, size_t size);

namespace hrz
{
// Strings pointed to by instances of this struct are guaranteed
// to be static and const.
struct StaticString
{
public:
    StaticString() = default;

    operator std::string_view() const { return view; }

private:
    StaticString(const char* ptr, size_t size) : view({ptr, size}) {}

    static StaticString from_ptr(const char* ptr, size_t size) { return StaticString(ptr, size); }

    friend StaticString(::operator""_ss)(const char* ptr, size_t size);

    std::string_view view;
};
} // namespace hrz

static inline hrz::StaticString operator""_ss(const char* ptr, size_t size)
{
    return hrz::StaticString::from_ptr(ptr, size);
}
