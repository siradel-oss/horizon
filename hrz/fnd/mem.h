// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include <bit>
#include <concepts>
#include <cstring>
#include <new>
#include <span>

#define HRZ_ARRAY_COUNT(A) (sizeof(A) / sizeof(A[0]))

constexpr std::byte operator ""_b(unsigned long long int v)
{
    return static_cast<std::byte>(v);
}

namespace hrz
{

constexpr size_t L1CacheLineSize = std::hardware_constructive_interference_size;

// @Todo(C++23) Use std::byteswap
template<std::integral T>
constexpr T swap_bytes(T x)
{
    if constexpr (sizeof(T) == 1)
    {
        return x;
    }

#if defined(_MSC_VER)
    const auto u = std::bit_cast<std::make_unsigned_t<T>>(x);

    if constexpr (sizeof(T) == 2)
    {
        return std::bit_cast<T>(_byteswap_ushort(u));
    }
    else if constexpr (sizeof(T) == 4)
    {
        return std::bit_cast<T>(_byteswap_ulong(u));
    }
    else if constexpr (sizeof(T) == 8)
    {
        return std::bit_cast<T>(_byteswap_uint64(u));
    }
#else
    if constexpr (sizeof(T) == 2)
    {
        return std::bit_cast<T>(__builtin_bswap16(x));
    }
    else if constexpr (sizeof(T) == 4)
    {
        return std::bit_cast<T>(__builtin_bswap32(x));
    }
    else if constexpr (sizeof(T) == 8)
    {
        return std::bit_cast<T>(__builtin_bswap64(x));
    }
#endif
    else
    {
        static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8);
    }
}

// Code below is written assuming little-endian architecture.
static_assert(std::endian::native == std::endian::little);

template<std::integral T>
constexpr T read_le_unsafe(const std::byte* data, size_t offset = 0)
{
    T value{};
    std::memcpy(&value, data + offset, sizeof(T));
    return value;
}

template<std::integral T>
constexpr T read_le_unsafe(std::span<const std::byte> data, size_t offset = 0)
{
    return read_le_unsafe<T>(data.data(), offset);
}

template<std::integral T>
constexpr T read_be_unsafe(std::span<const std::byte> data, size_t offset = 0)
{
    return swap_bytes(read_le_unsafe<T>(data, offset));
}

template<std::integral T>
constexpr void write_le_unsafe(std::span<std::byte> data, size_t offset, T value)
{
    std::memcpy(data.data() + offset, &value, sizeof(T));
}

template<std::integral T>
constexpr void write_be_unsafe(std::span<std::byte> data, size_t offset, T value)
{
    write_le_unsafe(data, offset, swap_bytes(value));
}

} // namespace hrz
