// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "defines.glsl"

layout(std140) uniform OverlayCamerasUniform
{
    uint cascade_count;
    vec4 overlay_cams_pos_low;
    vec4 overlay_cams_pos_high;
    mat4 overlay_cams_proj[HRZ_S_MAX_OVERLAY_CASCADES];
    mat4 overlay_cams_view_cc; // camera-centered coordinates
    mat4 overlay_cams_pv_cc_matrix[HRZ_S_MAX_OVERLAY_CASCADES]; // camera-centered coordinates

    mat4 overlay_cams_mvp_inv_main_view_visual[HRZ_S_MAX_OVERLAY_CASCADES];
    mat4 overlay_cams_mvp_inv_main_view_picking[HRZ_S_MAX_OVERLAY_CASCADES];
} hrz_overlay_cameras;

vec3 translate_relative_to_overlay_cameras(vec3 low, vec3 high)
{
    vec3 low_difference = low - hrz_overlay_cameras.overlay_cams_pos_low.xyz;
    vec3 high_difference = high - hrz_overlay_cameras.overlay_cams_pos_high.xyz;

    return low_difference + high_difference;
}
