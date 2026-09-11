// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#include "sky/sky_params_ubo.glsl"

layout(location = 0) in vec3 i_pos;

out vec3 v_coords;

void main()
{
    v_coords = i_pos;
    vec4 pos = hrz_sky.sky_box_pv * vec4(i_pos, 1.0);
    gl_Position = pos;
}
