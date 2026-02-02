#pragma once

#include "defines.glsl"

bool fetch_selection_storage(highp usampler2D selection_sampler, uint feature_index)
{
    const uint TEXTURE_WIDTH = uint(HRZ_S_SELECTION_STORAGE_UINT32_TEXTURE_WIDTH);
    uint bucket_index = feature_index / 32u;
    uvec2 coord = uvec2(bucket_index % TEXTURE_WIDTH, bucket_index / TEXTURE_WIDTH);
    uint bit_index = feature_index % 32u;
    uint bitmask = texelFetch(selection_sampler, ivec2(coord), 0).r;
    return (bitmask & (1u << bit_index)) != 0u;
}
