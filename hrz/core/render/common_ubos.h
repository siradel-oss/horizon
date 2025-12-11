#pragma once

#include "hrz/core/render/resources.h"

#include <lin_maths.h>

namespace hrz
{

struct ClipPlaneUniformData
{
    lm::mat4 matrix;
    lm::vec3 normal;
    float outline_distance;
    lm::vec4 outline_color;
};

HRZ_CHECK_UBO_SIZE(ClipPlaneUniformData);

struct FrameUniformData
{
    enum DebugFlags : uint32_t
    {
        DrawFlatOverlayCascades = 0x00000001,
        DrawHeatmapOobSampling = 0x00000002,
    };

    enum AtmosphereFlags : uint32_t
    {
        DynamicSunLighting = 0x00000001,
        DynamicAmbientLighting = 0x00000002,
    };

    lm::mat4 proj;
    lm::mat4 view;
    lm::mat4 view_cc;
    lm::mat4 pv;
    lm::mat4 pv_cc;
    lm::mat4 view_cc_inv;
    lm::mat4 proj_inv;
    lm::vec4 view_pos_low;
    lm::vec4 view_pos_high;

    lm::vec3 view_sun_direction;
    float view_elevation;

    lm::uvec2 viewport_size;
    float pixel_size_in_meters; // at a distance of 1 meter from the camera
    float device_pixel_ratio;

    lm::mat4 sun_matrix[HRZ_S_MAX_SUN_CASCADES];
    lm::mat4 env_sh[3];
    lm::mat4 vs_pv_matrix[HRZ_S_VIEWSHED_CNT];
    lm::vec4 vs_position_from_main_view[HRZ_S_VIEWSHED_CNT];
    lm::vec4 vs_seen_color[HRZ_S_VIEWSHED_CNT];
    lm::vec4 vs_hidden_color[HRZ_S_VIEWSHED_CNT];
    HRZ_UBO_STRUCT_FIELD(ClipPlaneUniformData) clip_planes[HRZ_S_MAX_CLIP_PLANES];

    bool32 viewsheds_enabled;
    bool32 lighting_enabled;
    bool32 receive_shadows;
    uint32_t atmosphere_flags;

    float shadow_map_far_lin;
    int32_t terrain_clip_id;
    bool32 terrain_lighting_enabled;
    bool32 terrain_receive_shadows;

    lm::vec4 terrain_color_opacity;

    lm::uvec3 quick_highlight_feature_reference{0, 0, 0};
    uint32_t merge_groups_bitset;

    lm::vec4 quick_highlight_color;

    float sun_strength;
    float ambient_strength;
    uint32_t _padding1[2];

    lm::vec3 sun_color;
    float wrap_lighting;

    uint32_t first_imagery_group;
    uint32_t last_imagery_group;
    uint32_t shadow_map_cascade_count;
    float time;

    float atmosphere_fade_start;
    float atmosphere_fade_end;
    uint32_t debug_flags{0};
    float view_latitude;

    lm::vec2 pixel_size_in_clip;
    float camera_height_to_perceived_distance;
    uint32_t _padding2[1];
};

HRZ_CHECK_UBO_SIZE(FrameUniformData);

struct AuxViewUniformData
{
    // This is the proj * view matrix starting after the view transform
    // of the main view.
    lm::mat4 pv_from_main_view;
    lm::mat4 pv_cc;
};

HRZ_CHECK_UBO_SIZE(AuxViewUniformData);

} // namespace hrz
