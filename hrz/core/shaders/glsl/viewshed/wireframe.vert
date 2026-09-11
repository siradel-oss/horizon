// SPDX-FileCopyrightText: Copyright 2021 Siradel
// SPDX-License-Identifier: MIT

#include "common/ubo_frame.glsl"
layout(location = 0) in vec4 i_vertex;

void main()
{
    gl_Position = i_vertex;
}
