#pragma once

#include <atomic>

namespace hrz
{
template<typename T>
struct TicketGenerator
{
    static_assert(
        std::is_unsigned_v<T>,
        "Ticket type must be unsigned for proper overflow behavior");
    static_assert(sizeof(T) >= 2, "At least 2 bits are needed");

    static constexpr T NO_TICKET = 0;

    TicketGenerator() : generator(0) {}

    T generate()
    {
        T value = generator++;
        value = (value << 1) | 1;
        return value;
    }

private:
    std::atomic<T> generator;
};
} // namespace hrz
