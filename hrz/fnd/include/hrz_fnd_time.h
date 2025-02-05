#pragma once

#include <cstdint>

namespace hrz
{
// Initializes the time point that time will be measured relative
// to to now.
void set_epoch();

// Updates the time returned by the `now_frame` functions.
void set_frame_time();

// Returns the time since the epoch (see `set_epoch`) in seconds.
double now_s();
int64_t now_s_s64();
double now_frame_s();
int64_t now_frame_s_s64();

// Returns the time since the epoch (see `set_epoch`) in milliseconds.
double now_ms();
int64_t now_ms_s64();
double now_frame_ms();
int64_t now_frame_ms_s64();

// Returns the time since the epoch (see `set_epoch`) in microseconds.
double now_us();
int64_t now_us_s64();
double now_frame_us();
int64_t now_frame_us_s64();

} // namespace hrz
