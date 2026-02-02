#pragma once

#include "flat_vectors/tile_common_defs.glsl"

layout(std140) uniform Tile
{
    TileCommon base;

    vec4 disc_outline_color;

    float disc_outline_width;

    // 0 if in meters, 1 if in pixels.
    uint disc_radius_unit;
} hrz_tile;
