#pragma once

#include "common/ubo_frame.glsl"
#include "common/colors.glsl"

vec4 apply_quick_highlight_color(in vec4 color)
{
    color.rgb = linear_to_srgb(mix(srgb_to_linear(color.rgb), srgb_to_linear(hrz_frame.quick_highlight_color.rgb), hrz_frame.quick_highlight_color.a));
    return color;
}

vec4 apply_quick_highlight_color_premultiplied(in vec4 color)
{
    color.rgb = linear_to_srgb(mix(srgb_to_linear(color.rgb), srgb_to_linear(hrz_frame.quick_highlight_color.rgb * color.a), hrz_frame.quick_highlight_color.a));
    return color;
}
