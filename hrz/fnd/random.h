// SPDX-FileCopyrightText: Copyright 2024 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include <random>

namespace hrz
{

// Some utilities to use <random> without all the cruft.
// For a high performance generator, use PCG from hrz_common_random.h instead.

template<typename T>
T random_int(T min, T max)
{
    thread_local static std::minstd_rand gen(std::random_device{}());
    std::uniform_int_distribution<T> distrib(min, max);
    return distrib(gen);
}

} // namespace hrz
