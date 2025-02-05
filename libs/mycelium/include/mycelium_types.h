#pragma once

#include <inttypes.h>
#include <lin_maths.h>

#include <stddef.h>
#include <stdint.h>

namespace my
{
struct Rect
{
    uint32_t x, y, w, h;
};

using Color = lm::vec4;

} // namespace my
