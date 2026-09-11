// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "common/ubo_frame.glsl"

vec4 offset_screen_position(vec4 view_pos, vec2 screen_offset)
{
    vec4 proj_pos = hrz_frame.proj_matrix * view_pos;
    vec2 ndc_pos = proj_pos.xy / proj_pos.w;
    vec2 pixel_pos = ndc_pos / hrz_frame.pixel_size_in_clip;
    pixel_pos += screen_offset;
    ndc_pos = pixel_pos * hrz_frame.pixel_size_in_clip;
    proj_pos.xy = ndc_pos * proj_pos.w;
    return proj_pos;
}
