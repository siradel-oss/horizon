#include "hrz/fnd/format.h"

#include <iterator>

namespace hrz
{

const char* bytes_to_string(
    uint64_t byte_count,
    fmt::memory_buffer& buffer,
    bool clear_buffer,
    bool append_null_byte)
{
    constexpr uint64_t kB = (1 << 10), MB = (1 << 20), GB = (1 << 30), TB = ((uint64_t)1 << 40);

    if (clear_buffer) buffer.clear();

    if (byte_count < kB)
    {
        fmt::format_to(std::back_inserter(buffer), "{} B", byte_count);
    }
    else if (byte_count < MB)
    {
        fmt::format_to(std::back_inserter(buffer), "{:.2f} KiB", (double)byte_count / kB);
    }
    else if (byte_count < GB)
    {
        fmt::format_to(std::back_inserter(buffer), "{:.2f} MiB", (double)byte_count / MB);
    }
    else if (byte_count < TB)
    {
        fmt::format_to(std::back_inserter(buffer), "{:.2f} GiB", (double)byte_count / GB);
    }
    else
    {
        fmt::format_to(std::back_inserter(buffer), "{:.2f} TiB", (double)byte_count / TB);
    }

    if (append_null_byte) buffer.push_back('\0');

    return buffer.data();
}

fmt::memory_buffer bytes_to_string(uint64_t byte_count)
{
    fmt::memory_buffer buffer;
    bytes_to_string(byte_count, buffer, false, true);
    return buffer;
}

} // namespace hrz
