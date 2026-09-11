// SPDX-FileCopyrightText: Copyright 2022 Siradel
// SPDX-License-Identifier: MIT

#include "common/camera.glsl"
#include "common/ubo_frame.glsl"
#include "defs_line.glsl"

layout (location = 0) in vec2 i_uv;

// We simulate noperspective on v_uv_x: https://stackoverflow.com/a/72620057
out vec2 v_uv_x;
out float v_uv_y;

void main()
{
    vec4 pos0 = hrz_frame.pv_cc_matrix * vec4(hrz_line.ecef_cc_pos - (hrz_line.axis * hrz_line.extent) * 0.5, 1.0);
    vec4 pos1 = hrz_frame.pv_cc_matrix * vec4(hrz_line.ecef_cc_pos + (hrz_line.axis * hrz_line.extent) * 0.5, 1.0);

    vec2 screen_dir = normalize(pos1.xy - pos0.xy);
    vec2 normal = vec2(-screen_dir.y, screen_dir.x);
    vec2 width = normal * hrz_line.width_px * hrz_frame.pixel_size_in_clip;

    pos0.xy += width * ((i_uv.x * 2.0) - vec2(1.0)) * pos0.w;
    pos1.xy += width * ((i_uv.x * 2.0) - vec2(1.0)) * pos1.w;

    gl_Position = mix(pos0, pos1, vec4(i_uv.y));

    v_uv_x = vec2(i_uv.x * gl_Position.w, gl_Position.w);
    v_uv_y = i_uv.y;
}
