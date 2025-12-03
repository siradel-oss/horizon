#pragma once

#include <assert.h>
#include <inttypes.h>
#include <lin_maths.h>

#include <bit>
#include <stddef.h>
#include <stdint.h>

namespace my
{

// @Todo(C++23) Replace with std::float16_t when available
struct float16_t
{
    uint16_t data;

    constexpr operator float() const
    {
        const uint32_t sign = (uint32_t)(data & 0x8000) << 16;
        uint32_t exponent = (data & 0x7C00) >> 10;
        uint32_t mantissa = (data & 0x03FF);

        if (exponent == 0)
        {
            if (mantissa != 0)
            {
                // Subnormal
                const uint32_t to_offset =
                    (uint32_t)std::countl_zero(static_cast<uint16_t>(mantissa)) - 5;
                assert(to_offset <= 10);

                mantissa = (mantissa << to_offset) & 0x03FF;
                exponent = exponent + 112 - to_offset + 1;
                assert((mantissa & 0xFFFF'FC00) == 0);
            }
        }
        else if (exponent == 31)
        {
            // Inf or NaN
            exponent = 255;
        }
        else
        {
            exponent += 112;
        }

        const uint32_t result = sign | (exponent << 23) | (mantissa << 13);
        return std::bit_cast<float>(result);
    }
};

struct Rect
{
    uint32_t x, y, w, h;
};

using Color = lm::vec4;

} // namespace my
