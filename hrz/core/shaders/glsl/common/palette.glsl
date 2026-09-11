// SPDX-FileCopyrightText: Copyright 2023 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "defines.glsl"

#include "common/colors.glsl"
#include "common/maths.glsl"

#define MODE_OKLAB 0
#define MODE_LINEAR_SRGB 2
#define MODE_SRGB 1
#define MODE_THRESHOLD 3

vec4 decode(in int mode, in vec4 color)
{
    switch (mode)
    {
        case MODE_OKLAB: return oklab_to_linear(color);
        case MODE_SRGB: return srgb_to_linear(color);
        default: return color;
    }
}

struct Palette
{
    vec4 packed_color_stops_value[HRZ_S_MAX_PALETTE_COLOR_STOPS / 4];
    vec4 color_stops_encoded[2 * HRZ_S_MAX_PALETTE_COLOR_STOPS];
    vec4 nan_color;
    int color_interpolation_mode;
    int num_color_stops;
};
