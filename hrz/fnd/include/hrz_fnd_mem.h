#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#define HRZ_ARRAY_COUNT(A) (sizeof(A) / sizeof(A[0]))

constexpr std::byte operator""_b(unsigned long long int v)
{
    return static_cast<std::byte>(v);
}

namespace hrz
{

constexpr uint8_t swap_bytes(uint8_t x)
{
    return x;
}

constexpr uint16_t swap_bytes(uint16_t x)
{
    return (x << 8) | (x >> 8);
}

constexpr uint32_t swap_bytes(uint32_t x)
{
    return (x << 24) | ((x & 0x0000FF00) << 8) | ((x & 0x00FF0000) >> 8) | (x >> 24);
}

} // namespace hrz
