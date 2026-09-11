// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#include "sky/sky.glsl"

layout(location = 0) out vec4 o_color;

uniform sampler2D u_previous;

void main()
{
    ivec2 uv = ivec2(gl_FragCoord.xy) * 4;
    vec4 color = vec4(0);

    for (int y = 0; y < 4; ++y)
    {
        for (int x = 0; x < 4; ++x)
        {
            color += texelFetch(u_previous, uv + ivec2(x, y), 0);
        }
    }

    o_color = color / 16.0;
}
