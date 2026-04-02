#pragma once

#include <compare>
#include <stdint.h>

// We cannot use absl::uint128 because it's over-aligned in the Clang build
// (wasm) (16 > alignof(max_align_t) == 8) which crashes debug builds.

namespace hrz
{

struct uint128
{
    uint64_t low;
    uint64_t high;

    friend constexpr bool operator ==(uint128 a, uint128 b) = default;

    friend constexpr std::strong_ordering operator <=>(uint128 a, uint128 b)
    {
        if (a.high != b.high)
            return a.high <=> b.high;
        else
            return a.low <=> b.low;
    }

    template<typename H>
    friend H AbslHashValue(H h, uint128 v)
    {
        return H::combine(std::move(h), v.low, v.high);
    }
};

} // namespace hrz
