#pragma once

#include <fmt/format.h>

#include <cstdint>
#include <iterator>

namespace hrz
{

template<typename... T>
const char* format_to_buffer(fmt::memory_buffer& buffer, fmt::format_string<T...> fmt, T&&... args)
{
    buffer.clear();
    fmt::format_to(std::back_inserter(buffer), fmt, std::forward<T>(args)...);
    buffer.push_back(0);
    return buffer.data();
}

fmt::memory_buffer bytes_to_string(uint64_t byte_count);
const char* bytes_to_string(
    uint64_t byte_count,
    fmt::memory_buffer&,
    bool clear_buffer = true,
    bool append_null_byte = true);

} // namespace hrz
