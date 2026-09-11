// SPDX-FileCopyrightText: Copyright 2021 Siradel
// SPDX-License-Identifier: MIT

#pragma once

uniform sampler2D u_scene_depth;

vec4 depth_occlusion_apply_opacity(float frag_depth, in vec4 color, in float opacity)
{
    float depth_scene = texelFetch(u_scene_depth, ivec2(gl_FragCoord), 0).r;

    if (depth_scene < frag_depth)
    {
        color.a = opacity;
    }

    return color;
}

void depth_occlusion_discard(float frag_depth)
{
    float depth_scene = texelFetch(u_scene_depth, ivec2(gl_FragCoord), 0).r;
    if (depth_scene < frag_depth) discard;
}
