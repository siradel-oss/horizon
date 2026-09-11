// SPDX-FileCopyrightText: Copyright 2021 Siradel
// SPDX-License-Identifier: MIT

#include "common/ubo_frame.glsl"
#include "defs.glsl"

layout (location = 0) in vec2 i_vertex;

out vec2 v_uv;
out vec3 v_vertex;

void main()
{
    v_uv = i_vertex * 0.5 + 0.5;
    vec3 plane_vertex = hrz_grid.axis_x * hrz_grid.extent * i_vertex.x * 0.5
                      + hrz_grid.axis_y * hrz_grid.extent * i_vertex.y * 0.5;
    v_vertex = hrz_grid.ecef_cc_pos + plane_vertex;
    gl_Position = hrz_frame.pv_cc_matrix * vec4(v_vertex, 1.0);
}
