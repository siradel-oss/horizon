#include "hrz/fnd/varint.h"

uint64_t hrz::decode_varint_u64(const std::byte** it, const std::byte* end)
{
    if (end - *it >= 10)
    {
        // Fast path, unrolled, without bounds checking
        uint64_t result = 0;

        auto b = (uint8_t) * ((*it)++);
        result |= (uint64_t)(b & 0x7F) << 0;
        if ((b & 0x80) == 0) return result;

        b = (uint8_t) * ((*it)++);
        result |= (uint64_t)(b & 0x7F) << 7;
        if ((b & 0x80) == 0) return result;

        b = (uint8_t) * ((*it)++);
        result |= (uint64_t)(b & 0x7F) << 14;
        if ((b & 0x80) == 0) return result;

        b = (uint8_t) * ((*it)++);
        result |= (uint64_t)(b & 0x7F) << 21;
        if ((b & 0x80) == 0) return result;

        b = (uint8_t) * ((*it)++);
        result |= (uint64_t)(b & 0x7F) << 28;
        if ((b & 0x80) == 0) return result;

        b = (uint8_t) * ((*it)++);
        result |= (uint64_t)(b & 0x7F) << 35;
        if ((b & 0x80) == 0) return result;

        b = (uint8_t) * ((*it)++);
        result |= (uint64_t)(b & 0x7F) << 42;
        if ((b & 0x80) == 0) return result;

        b = (uint8_t) * ((*it)++);
        result |= (uint64_t)(b & 0x7F) << 49;
        if ((b & 0x80) == 0) return result;

        b = (uint8_t) * ((*it)++);
        result |= (uint64_t)(b & 0x7F) << 56;
        if ((b & 0x80) == 0) return result;

        b = (uint8_t) * ((*it)++);
        result |= (uint64_t)(b & 0x7F) << 63;
        return result;
    }
    else
    {
        uint64_t result = 0;
        uint64_t shift = 0;

        while (*it < end && shift < 64)
        {
            auto b = (uint8_t) * *it;
            *it += 1;

            result |= (uint64_t)(b & 0x7F) << shift;
            if ((b & 0x80) == 0)
            {
                return result;
            }
            shift += 7;
        }

        return result;
    }
}
