// SPDX-FileCopyrightText: Copyright 2022 Siradel
// SPDX-License-Identifier: MIT

#include "present/defs.glsl"

in vec2 i_pos;

#define varying out
#include "present/interface.glsl"

void main()
{
    vec2 pos = ((i_pos * vec2(hrz_scene.size) + vec2(hrz_scene.origin)) / vec2(hrz_scene.screen_resolution)) * 2.0 - vec2(1.0);
    gl_Position = vec4(pos, 0.0, 1.0);

    v_uv = i_pos;
}
