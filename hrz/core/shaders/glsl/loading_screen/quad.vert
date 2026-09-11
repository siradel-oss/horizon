// SPDX-FileCopyrightText: Copyright 2023 Siradel
// SPDX-License-Identifier: MIT

#include "loading_screen/defs.glsl"

layout(location = 0) in vec2 i_pos;

#define varying out
#include "loading_screen/interface.glsl"

void main()
{
    gl_Position = vec4(2.0 * i_pos - 1.0, 0.0, 1.0);

    v_uv = i_pos;
    v_t = float(hrz_load.num_shaders_ready) / float(hrz_load.num_shaders_total);
}
