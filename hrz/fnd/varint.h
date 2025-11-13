#pragma once

#include <cstdint>
#include <span>

// See https://protobuf.dev/programming-guides/encoding/#varints

namespace hrz
{

// After the call, it points to the next byte after the varint.
uint64_t decode_varint_u64(const std::byte** it, const std::byte* end);

inline uint64_t decode_varint_u64(std::span<const std::byte> span)
{
    const auto* it = span.data();
    return decode_varint_u64(&it, span.data() + span.size_bytes());
}

} // namespace hrz
