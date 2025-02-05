#pragma once

#include "defines.glsl"

layout(std140) uniform Tile
{
    vec4 center_low;
    vec4 center_high;
    uint picking_id;
    uint layer_picking_id;
    int clip_id;
    bool lighting_enabled;
    uint color_blend_mode;
    float color_blend_strength;
} hrz_tile;

layout(std140) uniform Impostor
{
    vec3 offset_from_origin;
    float scale_correction;
    uvec2 atlas_size;
} hrz_impostor;
