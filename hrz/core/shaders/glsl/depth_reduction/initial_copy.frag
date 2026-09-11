// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#include "common/ubo_frame.glsl"
#include "common/logz.glsl"
#include "depth_reduction/ubo.glsl"
#include "depth_reduction/reduction.glsl"

uniform sampler2D u_depth;
out vec2 o_min_max;

void main()
{
    float inv_size = 1.0 / hrz_depth_reduction.size;
    vec2 offset = vec2(0, inv_size * 0.5);
    vec2 uv = (gl_FragCoord.xy - 0.5) * inv_size;

    float d0 = texture(u_depth, uv + offset.xx).x;
    float d1 = texture(u_depth, uv + offset.xy).x;
    float d2 = texture(u_depth, uv + offset.yx).x;
    float d3 = texture(u_depth, uv + offset.yy).x;

    // We remove the 1 value because it's background.
    filter_value(d0, d1, 1.0);
    filter_value(d2, d3, 1.0);

    float d4_min = min(d0, d1);
    float d4_max = max(d0, d1);

    float d5_min = min(d2, d3);
    float d5_max = max(d2, d3);

    filter_value(d4_min, d5_min, 1.0);
    filter_value(d4_max, d5_max, 1.0);

    float min_depth = min(d4_min, d5_min);
    float max_depth = max(d4_max, d5_max);

    o_min_max = vec2(min_depth, max_depth);
}
