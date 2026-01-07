#pragma once

#include "common/palette.glsl"

layout(std140) uniform HeatmapQuadOverlay
{
    mat4 transform[HRZ_S_MAX_OVERLAY_CASCADES];
    Palette palette;
    float max_scale_factor;
    uint cascade_count;
    float proj_translation_x;
} hrz_heatmap_quad_overlay;
