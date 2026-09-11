// SPDX-FileCopyrightText: Copyright 2021 Siradel
// SPDX-License-Identifier: MIT

#include "common/ubo_frame.glsl"
#include "common/logz.glsl"
#include "common/frag_processing.glsl"
#include "common/depth_occlusion_effects.frag.glsl"
#include "common/depth_peel.frag.glsl"
#include "defs.glsl"

in vec2 v_uv;
in vec3 v_vertex;

out vec4 o_color;

float unit_disc_sdf(vec2 uv)
{
    return length(2.0 * uv - vec2(1.0));
}

float log10(float x)
{
    return log(x) / log(10.0);
}

// https://ourmachinery.com/post/borderland-between-rendering-and-editor-part-1/

void main()
{
    gl_FragDepth = log_depth_value(gl_FragCoord.w);
    depth_occlusion_discard(gl_FragDepth);
    depth_peel_discard(gl_FragDepth);

    float dist = unit_disc_sdf(v_uv);

    if (dist > 1.0) discard;

    vec2 dudv = vec2(length(vec2(dFdx(v_uv.x), dFdy(v_uv.x))), length(vec2(dFdx(v_uv.y), dFdy(v_uv.y))));

    vec2 offset_uv = v_uv + (hrz_grid.offset / hrz_grid.extent);

    const float min_pixels_between_cells = 4.0f;

    float cell_size = hrz_grid.cell_size / hrz_grid.extent;

    // LOD level.
    float lod_level = log10((length(dudv) * min_pixels_between_cells * hrz_frame.device_pixel_ratio) / cell_size) + 1.0;
    float lod_fade = fract(lod_level);

    // Cell sizes for each LOD.
    float lod0_cs = cell_size * pow(10.0, floor(lod_level));
    float lod1_cs = lod0_cs * 10.0;
    float lod2_cs = lod1_cs * 10.0;

    // Allow each anti-aliased line to cover up to 2 pixels.
    dudv *= 2.0 * hrz_frame.device_pixel_ratio;

    vec2 lod0_cross_a = 1.0 - abs(clamp(mod(offset_uv - 0.5, lod0_cs) / dudv, 0.0, 1.0) * 2.0 - 1.0);
    float lod0_a = max(lod0_cross_a.x, lod0_cross_a.y);
    vec2 lod1_cross_a = 1.0 - abs(clamp(mod(offset_uv - 0.5, lod1_cs) / dudv, 0.0, 1.0) * 2.0 - 1.0);
    float lod1_a = max(lod1_cross_a.x, lod1_cross_a.y);
    vec2 lod2_cross_a = 1.0 - abs(clamp(mod(offset_uv - 0.5, lod2_cs) / dudv, 0.0, 1.0) * 2.0 - 1.0);
    float lod2_a = max(lod2_cross_a.x, lod2_cross_a.y);

    vec3 view_dir = normalize(hrz_grid.ecef_cc_pos);
    // Blend between LOD levels.
    float final_lod_fade = max(max(lod2_a, lod1_a), lod0_a * (1.0 - lod_fade));
    float gracing_fade = 1.0;
    float distance_fade = (1.0 - clamp(0.25 * length(v_vertex) / hrz_grid.extent, 0.0, 1.0));
    float precision_fade = min((max(0.0, length(dudv) - 0.000001)) / 0.00001, 1.0);
    float alpha = max(hrz_grid.color.a, final_lod_fade) * gracing_fade * distance_fade * precision_fade;

    // Fade-out disc edges.
    const float fadeout_limit = 0.90;
    if (dist > fadeout_limit) alpha *= (1.0 - dist) / (1.0 - fadeout_limit);

    if (alpha == 0.0) discard;
    o_color = vec4(hrz_grid.color.rgb, alpha);
    o_color.rgb *= o_color.a;
}
