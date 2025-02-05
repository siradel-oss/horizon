
#include "common/ubo_frame.glsl"
#include "common/logz.glsl"
#include "depth_reduction/reduction.glsl"

uniform sampler2D u_depth;
out vec2 o_min_max;

void main()
{
    ivec2 offset = ivec2(0, 1);
    ivec2 uv = ivec2((gl_FragCoord.xy - 0.5) * 2.0);

    vec2 d0 = texelFetch(u_depth, uv + offset.xx, 0).rg;
    vec2 d1 = texelFetch(u_depth, uv + offset.xy, 0).rg;
    vec2 d2 = texelFetch(u_depth, uv + offset.yx, 0).rg;
    vec2 d3 = texelFetch(u_depth, uv + offset.yy, 0).rg;

    filter_value(d0.r, d1.r, 1.0);
    filter_value(d0.g, d1.g, 1.0);
    filter_value(d2.r, d3.r, 1.0);
    filter_value(d2.g, d3.g, 1.0);

    vec2 d4 = vec2(min(d0.r, d1.r), max(d0.g, d1.g));
    vec2 d5 = vec2(min(d2.r, d3.r), max(d2.g, d3.g));

    filter_value(d4.r, d5.r, 1.0);
    filter_value(d4.g, d5.g, 1.0);

    float min_depth = min(d4.r, d5.r);
    float max_depth = max(d4.g, d5.g);

    o_min_max = vec2(min_depth, max_depth);
}
