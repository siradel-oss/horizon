#pragma once

uniform sampler2D u_peel_depth;

void depth_peel_discard(float frag_depth)
{
    float depth_scene = texelFetch(u_peel_depth, ivec2(gl_FragCoord), 0).r;
    if (depth_scene >= frag_depth) discard;
}
