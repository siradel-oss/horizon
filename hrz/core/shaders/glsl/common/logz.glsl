// SPDX-FileCopyrightText: Copyright 2019 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "defines.glsl"

// @Workaround(003-Firefox-FragmentOutputFragDepth)
// We used to have a function that wrote the log depth to gl_FragDepth here, however
// their was a WebGL implementation bug in Firefox that we had to work around.
// Namely having "gl_FragDepth" referenced in the code before outputs somehow
// make the types of those outputs wrong.

float log_depth_value(float value)
{
    return log(1.0 / value / HRZ_S_NEAR) / log(float(HRZ_S_FAR) / HRZ_S_NEAR);
}

float undo_log_depth(float depth_0_1)
{
    return exp2(depth_0_1 * log2(float(HRZ_S_FAR) / HRZ_S_NEAR)) * HRZ_S_NEAR;
}
