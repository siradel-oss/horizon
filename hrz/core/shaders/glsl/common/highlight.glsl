#pragma once

#include "common/ubo_frame.glsl"
#include "common/colors.glsl"

vec4 apply_quick_highlight_color_premultiplied(in vec4 color)
{
    return mix_premultiplied_colors(color, hrz_frame.quick_highlight_color);
}
