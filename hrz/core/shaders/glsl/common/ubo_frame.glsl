#pragma once

#include "defines.glsl"

// Make these flags match what is defined in hrz_core_render.h
#define DEBUG_FLAG_DRAW_FLAT_OVERLAY_CASCADES   0x00000001u
#define DEBUG_FLAG_DRAW_HEATMAP_OOB_SAMPLING    0x00000002u

#define DYNAMIC_SUN_LIGHTING        0x00000001u
#define DYNAMIC_AMBIENT_LIGHTING    0x00000002u

struct ClipPlane
{
    mat4 matrix;
    vec3 normal;
    float outline_distance;
    vec4 outline_color;
};

// cc means "camera-centered"
layout(std140) uniform Frame
{
    mat4 proj_matrix;
    mat4 view_matrix;
    mat4 view_cc_matrix;
    mat4 pv_matrix;
    mat4 pv_cc_matrix;
    mat4 view_cc_inv_matrix;
    mat4 proj_inv_matrix;
    vec4 view_pos_low;
    vec4 view_pos_high;
    vec3 view_sun_direction;
    float view_elevation;
    uvec2 viewport_size;
    float pixel_size_in_meters; // at a distance of 1 meter from the camera
    float device_pixel_ratio;
    mat4 sun_matrix[HRZ_S_MAX_SUN_CASCADES];
    mat4 env_sh[3];
    mat4 vs_pv_matrix[HRZ_S_VIEWSHED_CNT];
    vec4 vs_position_from_main_view[HRZ_S_VIEWSHED_CNT];
    vec4 vs_visible_color[HRZ_S_VIEWSHED_CNT];
    vec4 vs_hidden_color[HRZ_S_VIEWSHED_CNT];
    ClipPlane clip_planes[HRZ_S_MAX_CLIP_PLANES];
    bool viewsheds_enabled;
    bool lighting_enabled;
    bool receive_shadows;
    uint atmosphere_flags;
    float shadow_map_far_lin;
    int terrain_clip_id;
    bool terrain_lighting_enabled;
    bool terrain_receive_shadows;
    vec4 terrain_color_opacity;
    uvec3 quick_highlight_feature_reference;
    uint merge_groups_bitset;
    vec4 quick_highlight_color;
    float sun_strength;
    float ambient_strength;
    vec3 sun_color;
    float wrap_lighting;
    uint first_imagery_group;
    uint last_imagery_group;
    uint shadow_map_cascade_count;
    float time;
    float atmosphere_fade_start;
    float atmosphere_fade_end;
    uint debug_flags;
    float view_latitude;
    vec2 pixel_size_in_clip;
    float camera_height_to_perceived_distance;
} hrz_frame;
