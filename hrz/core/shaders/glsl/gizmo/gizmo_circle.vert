// SPDX-FileCopyrightText: Copyright 2021 Siradel
// SPDX-License-Identifier: MIT

#include "common/ubo_frame.glsl"
#include "defs_circle.glsl"

layout (location = 0) in vec3 i_vertex;

out vec2 v_uv;

void main()
{
    v_uv = i_vertex.xy;

    vec3 world_pos = hrz_gizmo.offset;
    vec4 pos_cc = hrz_frame.view_cc_matrix * vec4(world_pos, 1);
    vec4 vertex = pos_cc + vec4(i_vertex.xy * hrz_gizmo.circle_radius * CIRCLE_GIZMO_RADIUS_MULTIPLIER, 0, 0);
    gl_Position = hrz_frame.proj_matrix * vertex;
}
