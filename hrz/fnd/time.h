#pragma once

#include <cstdint>

namespace hrz
{

// Initializes the time point that time will be measured relative
// to to now.
void set_epoch();

// Returns the time since the epoch (see `set_epoch`) in seconds.
double now_s();
int64_t now_s_s64();

// Returns the time since the epoch (see `set_epoch`) in milliseconds.
double now_ms();
int64_t now_ms_s64();

// Returns the time since the epoch (see `set_epoch`) in microseconds.
double now_us();
int64_t now_us_s64();

struct TimeVariants
{
    double s;
    int64_t s_s64;
    double ms;
    int64_t ms_s64;
    double us;
    int64_t us_s64;
};

// Returns the time since the epoch (see `set_epoch`) in all variants.
TimeVariants now_all_variants();

} // namespace hrz
