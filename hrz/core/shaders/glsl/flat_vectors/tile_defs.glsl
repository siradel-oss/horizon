#pragma once

layout(std140) uniform Tile
{
    vec4 center_low;
    vec4 center_high;
    uvec3 feature_reference;
    bool has_feature_ids;
    uvec2 object_reference;
    float disc_outline_width;
    vec4 disc_outline_color;

    // First bit is "inside", second bit is "outside".
    // 1 = show, 0 = hide.
    uint polyline_sides;

    // 0 if in meters, 1 if in pixels.
    uint line_width_unit;

    // See `DASH_MODE` defines.
    uint dash_mode;

    // See `DASH_SIZE_UNIT` defines.
    uint dash_period_unit;
    uint dash_length_unit;
    uint animation_speed_unit;

    vec2 origin_uv_low; // Web Mercator
    vec2 origin_uv_high; // Web Mercator

    float origin_lat; // radians
    float lat_span; // radians
    uint polygon_pattern_unit; // See `IN_WORLD_SIZE` defines
    uint polygon_pattern_tiling_type; // See `POLYGON_PATTERN` defines
    uint polygon_pattern_ref_lat_type; // See `REFERENCE_LATITUDE` defines
    float polygon_pattern_reference_lat_scale_factor_offset;
    uint polygon_pattern_color_blend_mode;

    // 0 if in meters, 1 if in pixels.
    uint disc_radius_unit;
} hrz_tile;
