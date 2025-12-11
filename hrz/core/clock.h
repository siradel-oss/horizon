#pragma once

#include "hrz/fnd/time.h"

#include <cstdint>

namespace hrz::clock
{

inline uint64_t CurrentFrameNumber = 0;
inline TimeVariants CurrentFrameRealTime{};

} // namespace hrz::clock
