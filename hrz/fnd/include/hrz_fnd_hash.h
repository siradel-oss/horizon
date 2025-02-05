#pragma once

#include "hrz_fnd_int128.h"
#include "hrz_fnd_mem.h"

#include <gsl/gsl-lite.hpp>
#include <lin_maths.h>

#include <functional>
#include <stdint.h>
#include <string_view>

namespace hrz
{
uint128 murmur3_x64_128(gsl::span<const std::byte> s);

inline uint128 murmur3_x64_128(std::string_view s)
{
    return murmur3_x64_128(gsl::span<const std::byte>((const std::byte*)s.data(), s.size()));
}

// From CityHash and Murmur
template<typename T = size_t>
constexpr T hash_mix(T x_high, T x_low)
{
    // Murmur-inspired hashing.
    constexpr uint64_t kMul = 0x9ddfea08eb382d69ULL;
    uint64_t a = (uint64_t)(x_low ^ x_high) * kMul;
    a ^= (a >> 47);
    uint64_t b = ((uint64_t)x_high ^ a) * kMul;
    b ^= (b >> 47);
    b *= kMul;
    return (T)b;
}

inline uint64_t murmur3_x64_64(gsl::span<const std::byte> s)
{
    uint128 res = murmur3_x64_128(s);
    return hrz::hash_mix(low(res), high(res));
}

inline uint64_t murmur3_x64_64(std::string_view s)
{
    uint128 res = murmur3_x64_128(s);
    return hrz::hash_mix(low(res), high(res));
}

// Mixes multiples hashes together __in place__.
template<typename T>
constexpr T hash_mix(gsl::span<T> hashes)
{
    size_t step = 1;

    // This is like evaluating a binary tree.
    // This about halves the amount of mixes necessary compared to
    // linearly mixing them.
    while (step < hashes.size())
    {
        for (size_t i = 0; i + step < hashes.size(); i += step * 2)
        {
            hashes[i] = hash_mix(hashes[i], hashes[i + step]);
        }
        step *= 2;
    }

    return hashes[0];
}

template<typename T>
constexpr size_t hash_value(const T& value)
{
    return std::hash<T>{}(value);
}

template<typename... Args>
constexpr size_t hash_values(const Args&... value)
{
    size_t hashes[] = {hash_value(value)...};
    return hash_mix(gsl::span<size_t>(hashes));
}

// This computes a hash for a set of key value pairs.
// The keys are ordered because we don't want their order to be significant obviously.
// However duplicate keys with different values has undefined behaviour. So be careful.
uint64_t hash_kv(gsl::span<const std::pair<std::string_view, std::string_view>>);

} // namespace hrz

namespace std
{
template<>
struct hash<hrz::uint128>
{
    size_t operator()(const hrz::uint128& k) const
    {
        auto h = hash<uint64_t>{};
        return hrz::hash_mix(h(hrz::low(k)), h(hrz::high(k)));
    }
};

template<typename T, typename U>
struct hash<std::pair<T, U>>
{
    size_t operator()(const std::pair<T, U>& x) const
    {
        return hrz::hash_mix(std::hash<T>()(x.first), std::hash<U>()(x.second));
    }
};

template<typename T, size_t N>
struct hash<lm::Vector<T, N>>
{
    size_t operator()(const lm::Vector<T, N>& other) const
    {
        gsl::span<const T> span(other.m);
        return (size_t)hrz::murmur3_x64_64(hrz::as_bytes(span));
    }
};

template<typename T, size_t N>
struct hash<lm::Matrix<T, N>>
{
    size_t operator()(const lm::Matrix<T, N>& other) const
    {
        gsl::span<const T> span(other.e);
        return (size_t)hrz::murmur3_x64_64(hrz::as_bytes(span));
    }
};

} // namespace std
