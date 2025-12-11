#include "hrz/fnd/time.h"

#include <chrono>

using Clock = std::chrono::high_resolution_clock;
static Clock::time_point epoch = Clock::now();

template<typename T>
static inline T _cast_duration(Clock::duration duration)
{
    using Duration = std::chrono::duration<T>;
    return std::chrono::duration_cast<Duration>(duration).count();
}

template<typename T, typename U>
static inline T _cast_duration(Clock::duration duration)
{
    using Duration = std::chrono::duration<T, U>;
    return std::chrono::duration_cast<Duration>(duration).count();
}

void hrz::set_epoch()
{
    epoch = Clock::now();
}

double hrz::now_s()
{
    return _cast_duration<double>(Clock::now() - epoch);
}

int64_t hrz::now_s_s64()
{
    return _cast_duration<int64_t>(Clock::now() - epoch);
}

double hrz::now_ms()
{
    return _cast_duration<double, std::milli>(Clock::now() - epoch);
}

int64_t hrz::now_ms_s64()
{
    return _cast_duration<int64_t, std::milli>(Clock::now() - epoch);
}

double hrz::now_us()
{
    return _cast_duration<double, std::micro>(Clock::now() - epoch);
}

int64_t hrz::now_us_s64()
{
    return _cast_duration<int64_t, std::micro>(Clock::now() - epoch);
}

hrz::TimeVariants hrz::now_all_variants()
{
    auto duration = Clock::now() - epoch;
    return TimeVariants{
        _cast_duration<double>(duration),
        _cast_duration<int64_t>(duration),
        _cast_duration<double, std::milli>(duration),
        _cast_duration<int64_t, std::milli>(duration),
        _cast_duration<double, std::micro>(duration),
        _cast_duration<int64_t, std::micro>(duration),
    };
}
