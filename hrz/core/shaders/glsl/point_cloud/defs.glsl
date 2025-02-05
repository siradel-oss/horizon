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
    uint layer_picking_id;
    uvec2 batch_picking_id;
    uint batch_id_offset;
    uint feature_color_blend_mode;
    float feature_color_blend_strength;
} hrz_point_cloud;
