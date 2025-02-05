#pragma once

#include "common/ubo_frame.glsl"

vec3 translate_relative_to_camera(vec3 base, vec3 low, vec3 high)
{
    vec3 low_difference = low - hrz_frame.view_pos_low.xyz;
    vec3 high_difference = high - hrz_frame.view_pos_high.xyz;

    return (base + low_difference) + high_difference;
}

vec3 translate_cc_to_global(vec3 base)
{
    return (base + hrz_frame.view_pos_low.xyz) + hrz_frame.view_pos_high.xyz;
}

vec3 translate(vec3 base, vec3 low, vec3 high)
{
    return (base + low) + high;
}
