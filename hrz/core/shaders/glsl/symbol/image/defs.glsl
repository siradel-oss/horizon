#pragma once

layout(std140) uniform Image
{
    uint z_index;
    uint blend_mode;
    float blend_strength;
} hrz_image;
