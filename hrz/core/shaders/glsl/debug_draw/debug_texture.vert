// SPDX-FileCopyrightText: Copyright 2023 Siradel
// SPDX-License-Identifier: MIT

#include "common/ubo_frame.glsl"

layout(location = 0) in vec2 i_pos;

out vec2 v_uv;

void main()
{
    if (hrz_frame.viewport_size.x > hrz_frame.viewport_size.y)
    {
        float aspect_ratio = float(hrz_frame.viewport_size.x) / float(hrz_frame.viewport_size.y);
        gl_Position = vec4(i_pos * vec2(1.0 / aspect_ratio, 1.0), 0, 1);
    }
    else
    {
        float aspect_ratio = float(hrz_frame.viewport_size.y) / float(hrz_frame.viewport_size.x);
        gl_Position = vec4(i_pos * vec2(1, 1.0 / aspect_ratio), 0, 1);
    }

    v_uv = i_pos * 0.5 + vec2(0.5);
}
