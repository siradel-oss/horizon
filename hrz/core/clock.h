// SPDX-FileCopyrightText: Copyright 2025 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/fnd/time.h"

#include <cstdint>

namespace hrz::clock
{

inline uint64_t CurrentFrameNumber = 0;

// This clock measures time as perceived in the "real world".
inline TimeVariants CurrentFrameWallTime{};

// This clock is the simulation time inside the engine.
// This is used for anything that moves "inside" the scene and that must not
// be synchronized with the real world.
// This clock can advance at any rate, be paused, but cannot be reversed.
inline TimeVariants CurrentFrameSimTime{};

enum ClockType
{
    kWallClock,
    kSimClock,
};

} // namespace hrz::clock
