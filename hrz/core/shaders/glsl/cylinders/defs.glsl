#pragma once

layout(std140) uniform Tile
{
    vec4 center_low;
    vec4 center_high;
    uvec2 picking_id;
    uint layer_picking_id;
    int clip_id;
    bool lighting_enabled;
    bool receive_shadows;

    // See `DASH_MODE` defines.
    uint dash_mode;

    // See `DASH_SIZE_UNIT` defines.
    uint dash_period_unit;
    uint dash_length_unit;
    uint animation_speed_unit;
} hrz_tile;
