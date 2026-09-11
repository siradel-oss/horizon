// SPDX-FileCopyrightText: Copyright 2023 Siradel
// SPDX-License-Identifier: MIT

uniform sampler2D u_depth;

void main()
{
    ivec2 uv = ivec2(gl_FragCoord.xy);
    float depth = texelFetch(u_depth, uv, 0).r;
    if (depth == 1.0)
    {
        discard;
    }
    gl_FragDepth = depth;
}

