#pragma once

#include "hrz/fnd/maths.h"

#include <lin_maths.h>
#include <pcg_basic.h>

#include <cmath>
#include <limits>

namespace hrz
{

using RngState = pcg32_random_t;

namespace random
{

// Initializes pseudo-random generator.
static inline void srand(RngState& state, uint64_t init_state, uint64_t init_seq)
{
    pcg32_srandom_r(&state, init_state, init_seq);
}

// Generates a float number in the provided bounds following a uniform distribution.
// Upper bound is exclusive.
static inline float rand_f32(RngState& state, float low = 0.0F, float up = 1.0F)
{
    const auto r = (float)std::ldexp(pcg32_random_r(&state), -32);
    return hrz::lerp(low, up, r);
}

// Generates an unsigned integer in the provided bounds following a uniform distribution.
// Upper bound is exclusive.
static inline uint32_t rand_u32(
    RngState& state,
    uint32_t low = 0,
    uint32_t up = std::numeric_limits<uint32_t>::max())
{
    if (low == 0 && up == UINT32_MAX)
    {
        return pcg32_random_r(&state);
    }
    else
    {
        return low + pcg32_boundedrand_r(&state, up - low);
    }
}

// Generates a signed integer in the provided bounds following a uniform distribution.
// Upper bound is exclusive.
static inline int32_t rand_i32(
    RngState& state,
    int32_t low = std::numeric_limits<int32_t>::min(),
    int32_t up = std::numeric_limits<int32_t>::max())
{
    return low + pcg32_boundedrand_r(&state, up - low);
}

// Generates a float number following a normal distribution of the given parameters.
// https://en.wikipedia.org/wiki/Box%E2%80%93Muller_transform
static inline float rand_norm_f32(RngState& state, float mean = 0.0F, float std_ = 1.0F)
{
    float u1 = rand_f32(state);
    float u2 = rand_f32(state);
    // Returns only one of the two possible normal variables.
    return mean + std_ * std::sqrt(-2 * std::log(u1)) * std::cos(lm::TWO_PIf * u2);
}

// Generates an unsigned integer following a normal distribution of the given parameters.
static inline uint32_t rand_norm_u32(RngState& state, float mean = 0.0F, float std_ = 1.0F)
{
    return std::floor(rand_norm_f32(state, mean, std_));
}

static inline int32_t rand_norm_i32(RngState& state, float mean = 0.0F, float std_ = 1.0F)
{
    return std::floor(rand_norm_f32(state, mean, std_));
}

} // namespace random
} // namespace hrz
