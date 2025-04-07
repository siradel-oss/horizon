#pragma once

#include <gsl/gsl-lite.hpp>

#include <cstddef>

#define HRZ_ARRAY_COUNT(A) (sizeof(A) / sizeof(A[0]))

constexpr std::byte operator""_b(unsigned long long int v)
{
    return static_cast<std::byte>(v);
}

namespace hrz
{

// @Todo(C++20) Remove this
template<typename T>
gsl::span<const std::byte> as_bytes(gsl::span<T> s) noexcept
{
    return gsl::span<const std::byte>((const std::byte*)s.data(), s.size_bytes());
}

// @Todo(C++20) Remove this
template<typename T>
gsl::span<std::byte> as_writable_bytes(gsl::span<T> s) noexcept
{
    return gsl::span<std::byte>((std::byte*)s.data(), s.size_bytes());
}

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
