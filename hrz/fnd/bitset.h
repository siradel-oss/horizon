#pragma once

#include <stdint.h>

namespace hrz
{
template<int N = 32>
class Bitset32
{
    static_assert(N <= 32, "Not enough _bits!");

    static constexpr uint32_t MASK = (1ULL << N) - 1;
    uint32_t _bits{};

public:
    constexpr Bitset32() = default;

    constexpr explicit Bitset32(bool b) : _bits(b ? MASK : 0) {}

    constexpr Bitset32(uint32_t b) : _bits(b & MASK) {}

    static constexpr Bitset32 bit(int i) { return Bitset32(((uint32_t)1 << i) & MASK); }

    constexpr uint32_t bits() const { return _bits; }

    constexpr bool is_set(int i) const { return _bits & ((uint32_t)1 << i) & MASK; }

    constexpr void set(int i) { _bits |= ((uint32_t)1 << i) & MASK; }

    constexpr void reset(int i) { _bits &= (~((uint32_t)1 << i)) & MASK; }

    constexpr void reset(Bitset32 b) { _bits &= (~b._bits) & MASK; }

    constexpr void reset() { _bits = 0; }

    constexpr bool any() const { return _bits; }

    constexpr bool none() const { return _bits == 0; }

    constexpr bool all_of(const Bitset32& v) const { return (_bits & v._bits) == v._bits; }

    constexpr Bitset32 operator~() const { return {(~_bits) & MASK}; }

    constexpr Bitset32 operator!() const { return {(~_bits) & MASK}; }

    constexpr friend bool operator==(const Bitset32<N>& a, const Bitset32<N>& b) = default;

    constexpr Bitset32& operator|=(const Bitset32& v)
    {
        _bits |= v._bits;
        return *this;
    }

    constexpr Bitset32& operator&=(const Bitset32& v)
    {
        _bits &= v._bits;
        return *this;
    }

    friend constexpr Bitset32 operator&(const Bitset32& a, const Bitset32& b)
    {
        return {a._bits & b._bits};
    }

    friend constexpr Bitset32 operator|(const Bitset32& a, const Bitset32& b)
    {
        return {a._bits | b._bits};
    }
};

} // namespace hrz
