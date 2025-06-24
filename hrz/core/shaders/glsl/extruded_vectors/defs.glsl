#pragma once

layout(std140) uniform Tile
{
    vec4 center_low;
    vec4 center_high;
    uvec3 feature_reference;
    int  clip_id;
    uvec2 object_reference;
    bool lighting_enabled;
    bool receive_shadows;
} hrz_tile;

const uint DATA_TEXTURE_SIZE = uint(HRZ_S_VECTOR_REPR_DATA_TEXTURE_WIDTH);
