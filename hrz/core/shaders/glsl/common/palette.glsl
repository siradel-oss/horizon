#pragma once

#include "defines.glsl"

#include "common/colors.glsl"
#include "common/maths.glsl"

#define MODE_OKLAB 0
#define MODE_LINEAR_RGB 2
#define MODE_SRGB 1
#define MODE_THRESHOLD 3

vec4 decode(in int mode, in vec4 color)
{
    switch (mode)
    {
        case MODE_OKLAB: return oklab_to_srgb(color);
        case MODE_LINEAR_RGB: return linear_to_srgb(color);
        default: return color;
    }
}

struct Palette
{
    vec4 packed_color_points_value[HRZ_S_MAX_PALETTE_COLOR_POINTS / 4];
    vec4 color_points_encoded[2 * HRZ_S_MAX_PALETTE_COLOR_POINTS];
    vec4 nan_color_srgb;
    int color_interpolation_mode;
    int num_color_points;
};
