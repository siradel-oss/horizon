// SPDX-FileCopyrightText: Copyright 2022 Siradel
// SPDX-License-Identifier: MIT

#pragma once

// https://www.shadertoy.com/view/Mtfyzl
// (Rune Stubbe's version)
vec3 octahedral_decompress_normal(uint data, uint precis)
{
    uint mu = (1u << precis) - 1u;

    uvec2 d = uvec2(data, data >> precis) & mu;
    vec2 v = vec2(d) / float(mu);

    v = -1.0 + 2.0 * v;

    vec3 normal = vec3(v, 1.0 - abs(v.x) - abs(v.y));
    float t = max(-normal.z, 0.0);
    normal.x += (normal.x > 0.0) ? -t : t;
    normal.y += (normal.y > 0.0) ? -t : t;

    return normalize(normal);
}

vec3 octahedral_decompress_normal(uint data)
{
    return octahedral_decompress_normal(data, 16u);
}

// From the paper at https://jcgt.org/published/0003/02/01/
// "Fast" variant of the algorithm
vec2 sign_not_zero(vec2 v)
{
    return vec2((v.x >= 0.0) ? 1.0 : -1.0, (v.y >= 0.0) ? 1.0 : -1.0);
}

vec2 vec3_to_octahedral_vec2(vec3 v)
{
    vec2 p = v.xy * (1.0 / (abs(v.x) + abs(v.y) + abs(v.z)));
    return (v.z <= 0.0) ? ((1.0 - abs(p.yx)) * sign_not_zero(p)) : p;
}

vec3 octahedral_vec2_to_vec3(vec2 e)
{
    vec3 v = vec3(e.xy, 1.0 - abs(e.x) - abs(e.y));
    if (v.z < 0.0) v.xy = (1.0 - abs(v.yx)) * sign_not_zero(v.xy);
    return normalize(v);
}
