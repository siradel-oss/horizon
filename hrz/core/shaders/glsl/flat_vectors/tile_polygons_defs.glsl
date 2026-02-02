#pragma once

#include "flat_vectors/tile_common_defs.glsl"

layout(std140) uniform Tile
{
    TileCommon base;

    vec2 origin_uv_low; // Web Mercator
    vec2 origin_uv_high; // Web Mercator

    float origin_lat; // radians
    float lat_span; // radians
    uint polygon_pattern_unit; // See `IN_WORLD_SIZE` defines
    uint polygon_pattern_tiling_type; // See `POLYGON_PATTERN` defines
    uint polygon_pattern_ref_lat_type; // See `REFERENCE_LATITUDE` defines
    float polygon_pattern_reference_lat_scale_factor_offset;
    uint polygon_pattern_color_blend_mode;
} hrz_tile;
