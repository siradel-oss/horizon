// SPDX-FileCopyrightText: Copyright 2023 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "common/flat_overlay_cameras.glsl"

#ifdef FLAT_OVERLAY_VISUAL
uniform lowp sampler2D hrz_flat_overlay_image[HRZ_S_MAX_OVERLAY_CASCADES];
#endif

#ifdef FLAT_OVERLAY_PICKING
uniform highp usampler2D hrz_flat_overlay_picking_image[HRZ_S_MAX_OVERLAY_CASCADES];
#endif

#ifdef FLAT_OVERLAY_SELECTION
uniform lowp sampler2D hrz_flat_overlay_selection_image[HRZ_S_MAX_OVERLAY_CASCADES];
#endif

bool is_in_overlay_cam(vec4 clip_pos)
{
    return clip_pos.x <= clip_pos.w && clip_pos.x >= -clip_pos.w && clip_pos.y <= clip_pos.w && clip_pos.y >= -clip_pos.w;
}

vec4 sample_overlay_texture(lowp sampler2D overlay_texture, vec4 clip_pos)
{
    vec2 uv = clip_pos.xy / clip_pos.w;
    return texture(overlay_texture, uv * 0.5 + 0.5);
}

uvec4 sample_overlay_texture(highp usampler2D overlay_texture, vec4 clip_pos)
{
    vec2 uv = clip_pos.xy / clip_pos.w;
    return texture(overlay_texture, uv * 0.5 + 0.5);
}

// Line continuation is not available on all drivers... :(
#define BLEND_OVERLAY(overlay_texture_array, overlay_cams_clip_pos, N, swizzle) if (uint(N) < hrz_overlay_cameras.cascade_count && is_in_overlay_cam(overlay_cams_clip_pos[N])) { index = N; color = sample_overlay_texture(overlay_texture_array[N], overlay_cams_clip_pos[N]).swizzle; }

#ifdef FLAT_OVERLAY_VISUAL
vec4 compute_overlay_color(const vec4 overlay_cams_clip_pos[HRZ_S_MAX_OVERLAY_CASCADES])
{
    const vec4 debug_colors[4] = vec4[](
        vec4(1, 0, 0, 1),
        vec4(1, 1, 0, 1),
        vec4(0, 1, 0, 1),
        vec4(0, 1, 1, 1)
    );
    int index = -1;
    vec4 color = vec4(0.0);
    BLEND_OVERLAY(hrz_flat_overlay_image, overlay_cams_clip_pos, 0, rgba)
    else BLEND_OVERLAY(hrz_flat_overlay_image, overlay_cams_clip_pos, 1, rgba)
    else BLEND_OVERLAY(hrz_flat_overlay_image, overlay_cams_clip_pos, 2, rgba)
    else BLEND_OVERLAY(hrz_flat_overlay_image, overlay_cams_clip_pos, 3, rgba)

    if ((hrz_frame.debug_flags & DEBUG_FLAG_DRAW_FLAT_OVERLAY_CASCADES) != 0u && index >= 0 && index < HRZ_S_MAX_OVERLAY_CASCADES)
    {
        color = mix_premultiplied_colors(color * 0.7, debug_colors[index] * 0.3);
    }
    return color;
}
#endif

#ifdef FLAT_OVERLAY_PICKING
uvec2 compute_overlay_object_reference(const vec4 overlay_cams_clip_pos[HRZ_S_MAX_OVERLAY_CASCADES])
{
    uvec2 color = uvec2(0);
    int index = 0;
    BLEND_OVERLAY(hrz_flat_overlay_picking_image, overlay_cams_clip_pos, 0, rg)
    else BLEND_OVERLAY(hrz_flat_overlay_picking_image, overlay_cams_clip_pos, 1, rg)
    else BLEND_OVERLAY(hrz_flat_overlay_picking_image, overlay_cams_clip_pos, 2, rg)
    else BLEND_OVERLAY(hrz_flat_overlay_picking_image, overlay_cams_clip_pos, 3, rg)
    return color;
}
#endif

#ifdef FLAT_OVERLAY_SELECTION
float compute_selection_overlay_color(const vec4 overlay_cams_clip_pos[HRZ_S_MAX_OVERLAY_CASCADES])
{
    float color = 0.0;
    int index = 0;
    BLEND_OVERLAY(hrz_flat_overlay_selection_image, overlay_cams_clip_pos, 0, r)
    else BLEND_OVERLAY(hrz_flat_overlay_selection_image, overlay_cams_clip_pos, 1, r)
    else BLEND_OVERLAY(hrz_flat_overlay_selection_image, overlay_cams_clip_pos, 2, r)
    else BLEND_OVERLAY(hrz_flat_overlay_selection_image, overlay_cams_clip_pos, 3, r)
    return color;
}
#endif
