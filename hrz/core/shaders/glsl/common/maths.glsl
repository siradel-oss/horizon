// SPDX-FileCopyrightText: Copyright 2019 Siradel
// SPDX-License-Identifier: MIT

#pragma once

const float PI = 3.14159265359;

bool is_nan(float val)
{
    // isnan sometimes get optimized away by shader compilers for some reason.
    // Or maybe it's the FPU of some GPUs that use emulated floats and don't
    // support NaN? I don't know. Anyway...
    if (isnan(val)) return true;
    return (floatBitsToUint(val) & 0x7fffffffu) > 0x7f800000u;
}
