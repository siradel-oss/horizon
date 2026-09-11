// SPDX-FileCopyrightText: Copyright 2024 Siradel
// SPDX-License-Identifier: MIT

#pragma once

layout(std140) uniform PointCloud
{
    mat3 linear_transform;
    mat3 normal_matrix;
    vec3 rtc_low;
    bool receive_shadows;
    vec3 rtc_high;
    bool lighting_enabled;
    vec3 quantized_position_scale;
    int clip_id;
    vec3 quantized_position_offset;
    uint object_id_offset;
    uvec2 object_reference;
    uint feature_color_blend_mode;
    uvec3 feature_reference;
    float feature_color_blend_strength;
} hrz_point_cloud;
