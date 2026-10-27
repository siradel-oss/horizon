// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "absl/hash/hash.h"
#include "absl/hash/internal/hash.h"
#include "hrz/fnd/int128.h"

#include <concepts>
#include <span>
#include <stdint.h>
#include <string_view>

namespace hrz
{

uint128 murmur3_x64_128(std::span<const std::byte> s);

inline uint128 murmur3_x64_128(std::string_view s)
{
    return murmur3_x64_128(std::span<const std::byte>((const std::byte*)s.data(), s.size()));
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

inline uint64_t murmur3_x64_64(std::span<const std::byte> s)
{
    const uint128 res = murmur3_x64_128(s);
    return hrz::hash_mix(res.low, res.high);
}

inline uint64_t murmur3_x64_64(std::string_view s)
{
    const uint128 res = murmur3_x64_128(s);
    return hrz::hash_mix(res.low, res.high);
}

// Mixes multiples hashes together __in place__.
template<typename T>
constexpr T hash_mix(std::span<T> hashes)
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

// Not deterministic across builds.
template<typename T>
constexpr size_t hash_value(const T& value)
{
    return absl::HashOf(value);
}

// Not deterministic across builds.
template<typename... Args>
constexpr size_t hash_values(const Args&... value)
{
    return absl::HashOf(value...);
}

// This computes a hash for a set of key value pairs.
// The keys are ordered because we don't want their order to be significant obviously.
// However duplicate keys with different values has undefined behaviour. So be careful.
uint64_t hash_kv(std::span<const std::pair<std::string_view, std::string_view>>);

// Very simple hasher that can be used to combine multiple values into a single hash.
// This is guaranteed to be deterministic across builds and platforms.
// Very simple though, only handles basic types.
//
// @Note I originally wanted to use abseil's hashing framework, but that proved
// too complex, and it kept inserting random state which made the hash non
// deterministic. So here we are.
//      -slerouzic, 2026-10-09
struct StaticHasher
{
    uint64_t state = 0;

    static StaticHasher combine(StaticHasher hasher, std::unsigned_integral auto value)
    {
        hasher.state = hrz::hash_mix(hasher.state, hrz::hash_mix<uint64_t>(0, (uint64_t)value));
        return hasher;
    }

    static StaticHasher combine(StaticHasher hasher, std::signed_integral auto value)
    {
        hasher.state = hrz::hash_mix(hasher.state, hrz::hash_mix<uint64_t>(0, (uint64_t)value));
        return hasher;
    }

    static StaticHasher combine(StaticHasher hasher, std::string_view value)
    {
        hasher.state = hrz::hash_mix(hasher.state, murmur3_x64_64(value));
        return hasher;
    }

    template<typename T1, typename T2, typename... Args>
    static StaticHasher combine(
        StaticHasher hasher,
        const T1& value1,
        const T2& value2,
        const Args&... rest)
    {
        hasher = StaticHasher::combine(std::move(hasher), value1);
        hasher = StaticHasher::combine(std::move(hasher), value2);
        ((hasher = StaticHasher::combine(std::move(hasher), rest)), ...);
        return hasher;
    }
};

template<typename T>
inline uint64_t hash_value_static(const T& value)
{
    StaticHasher state{};
    state = StaticHasher::combine(std::move(state), value);
    return state.state;
}

template<typename... Args>
inline uint64_t hash_values_static(const Args&... value)
{
    StaticHasher state{};
    state = StaticHasher::combine(std::move(state), value...);
    return state.state;
}

} // namespace hrz
