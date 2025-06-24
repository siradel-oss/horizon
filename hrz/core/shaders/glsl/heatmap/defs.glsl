#pragma once

layout(std140) uniform Tile
{
    vec4 center_low;
    vec4 center_high;
    bool size_in_meters;
    float blur_size;
} hrz_tile;


layout(std140) uniform HeatmapPass
{
    mat4 proj;
    float max_scale_factor;
    uint cascade_count;
} hrz_heatmap_pass;
