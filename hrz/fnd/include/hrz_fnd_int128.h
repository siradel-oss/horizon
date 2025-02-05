#pragma once

#include <stdint.h>

// We cannot use absl::uint128 because it's over-aligned in the Clang build
// (wasm) (16 > alignof(max_align_t) == 8) which crashes debug builds.

namespace hrz
{
struct uint128
{
    uint64_t low;
    uint64_t high;

    constexpr bool operator==(const uint128& other) const
    {
        return low == other.low && high == other.high;
    }

    constexpr bool operator!=(const uint128& other) const
    {
        return low != other.low || high != other.high;
    }

    constexpr bool operator<(const uint128& other) const
    {
        if (high != other.high)
            return high < other.high;
        else
            return low < other.low;
    }
};

static constexpr uint64_t high(uint128 x)
{
    return x.high;
}

static constexpr uint64_t low(uint128 x)
{
    return x.low;
}
} // namespace hrz
