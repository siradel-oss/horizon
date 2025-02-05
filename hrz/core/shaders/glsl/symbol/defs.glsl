#pragma once

#include "defines.glsl"

layout(std140) uniform Tile
{
    vec4 origin_low;
    vec4 origin_high;
    uvec2 picking_id;
    uint layer_picking_id;
} hrz_tile;

layout(std140) uniform AnchorPrototype
{
    // See the anchor code in C++ for the meaning of flags.
    uint flags;
    float reference_distance;
    float min_relative_scale;
    float max_relative_scale;
} hrz_anchor;

const int DATA_TEXTURE_SIZE = HRZ_S_VECTOR_REPR_DATA_TEXTURE_WIDTH;
