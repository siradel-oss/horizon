#pragma once

#include <lin_maths.h>

#include <cassert>
#include <cstdint>

namespace hrz
{

// https://www.shadertoy.com/view/Mtfyzl
template<uint32_t kPrecision = 16>
inline uint32_t octahedral_compress_normal(lm::vec3 normal)
{
    static_assert(kPrecision > 0 && kPrecision <= 16);

    normal /= (std::abs(normal.x) + std::abs(normal.y) + std::abs(normal.z));
    normal.xy = (normal.z >= 0.0F)
        ? normal.xy
        : (lm::vec2(1.0F) - lm::abs(lm::vec2(normal.y, normal.x))) * lm::sign(normal.xy);
    lm::vec2 v = lm::vec2(0.5F) + 0.5F * normal.xy;

    static constexpr uint32_t mu = (1 << kPrecision) - 1;
    lm::uvec2 d = lm::uvec2(lm::floor(v * lm::vec2(float(mu)) + lm::vec2(0.5F)));
    return (d.y << kPrecision) | d.x;
}

} // namespace hrz
