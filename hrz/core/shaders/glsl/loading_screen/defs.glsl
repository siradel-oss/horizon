#pragma once

layout(std140) uniform LoadScreen
{
    uint num_shaders_total;
    uint num_shaders_ready;
    uint viewport_width;
    uint viewport_height;
    bool draw_logo;
    float fadeout;
} hrz_load;
