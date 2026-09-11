// SPDX-FileCopyrightText: Copyright 2021 Siradel
// SPDX-License-Identifier: MIT

#include "common/ubo_frame.glsl"
#include "defs_mesh.glsl"

layout (location = 0) in vec3 i_vertex;

void main()
{
    gl_Position = hrz_frame.pv_cc_matrix * hrz_gizmo.transform * vec4(i_vertex, 1);
}
