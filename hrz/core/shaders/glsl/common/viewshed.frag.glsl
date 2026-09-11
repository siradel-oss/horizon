// SPDX-FileCopyrightText: Copyright 2021 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "common/ubo_frame.glsl"
#include "common/colors.glsl"

in vec4 v_viewshed_pos[HRZ_S_VIEWSHED_CNT];
in vec3 v_viewshed_dir[HRZ_S_VIEWSHED_CNT];
uniform highp sampler2DShadow hrz_viewshed_shadow_map[HRZ_S_VIEWSHED_CNT];

bool is_in_viewshed_frustum(vec4 pos)
{
    return pos.x > -pos.w && pos.x < pos.w &&
        pos.y > -pos.w && pos.y < pos.w &&
        pos.z > -pos.w && pos.z < pos.w;
}

float get_shadow_map_value(vec4 pos)
{
    const float bias = 0.0002;
    vec3 uv = ((pos.xyz / pos.w * 0.5) + vec3(0.5)) - vec3(0.0, 0.0, bias);
    return texture(hrz_viewshed_shadow_map[0], uv);
}

vec4 check_visibility(float shadow_map_value)
{
    if (shadow_map_value > 0.5)
    {
        return hrz_frame.vs_visible_color[0];
    }
    else
    {
        return hrz_frame.vs_hidden_color[0];
    }
}

vec4 check_visibility_with_normal_correction(float shadow_map_value, vec3 normal)
{
    float factor = dot(v_viewshed_dir[0], normal);
    if (factor > 0.0 && shadow_map_value > 0.5)
    {
        return hrz_frame.vs_visible_color[0];
    }
    else
    {
        return hrz_frame.vs_hidden_color[0];
    }
}

vec4 apply_viewshed_color(vec4 in_color, vec4 viewshed_color)
{
    if (in_color.a == 0.0 || viewshed_color.a == 0.0)
    {
        return in_color;
    }

    return mix_premultiplied_colors(in_color, viewshed_color * in_color.a);
}

vec4 compute_viewshed_color(vec4 in_color, vec3 normal)
{
    vec4 out_color = in_color;
    if (hrz_frame.viewsheds_enabled && is_in_viewshed_frustum(v_viewshed_pos[0]))
    {
        float shadow_map_value = get_shadow_map_value(v_viewshed_pos[0]);
        vec4 viewshed_color = check_visibility_with_normal_correction(shadow_map_value, normal);

        out_color = apply_viewshed_color(out_color, viewshed_color);
    }

    return out_color;
}

vec4 compute_viewshed_color_no_correction(vec4 in_color)
{
    vec4 out_color = in_color;
    if (hrz_frame.viewsheds_enabled && is_in_viewshed_frustum(v_viewshed_pos[0]))
    {
        float shadow_map_value = get_shadow_map_value(v_viewshed_pos[0]);
        vec4 viewshed_color = check_visibility(shadow_map_value);

        out_color = apply_viewshed_color(out_color, viewshed_color);
    }

    return out_color;
}
