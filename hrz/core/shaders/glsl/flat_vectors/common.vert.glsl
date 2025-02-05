#pragma once

uniform highp usampler2D u_selection;

#ifdef FLAT_VISUAL
uniform highp usampler2D u_feature_ids;

const uint DATA_TEXTURE_SIZE = 512u;
#endif

#define varying out
#include "flat_vectors/interface.glsl"

bool fetch_selection()
{
    if (!hrz_tile.has_feature_ids)
    {
        return false;
    }

    uint bucket_index = i_feature_index / 32u;
    uvec2 coord = uvec2(bucket_index % 2048u, bucket_index / 2048u);
    uint bit_index = i_feature_index % 32u;
    uint bitmask = texelFetch(u_selection, ivec2(coord), 0).r;
    return (bitmask & (1u << bit_index)) != 0u;
}

#ifdef FLAT_VISUAL
uvec2 fetch_feature_id()
{
    if (!hrz_tile.has_feature_ids)
    {
        return uvec2(0, 0);
    }

    ivec2 data_coords = ivec2(i_feature_index % DATA_TEXTURE_SIZE, i_feature_index / DATA_TEXTURE_SIZE);
    return texelFetch(u_feature_ids, data_coords, 0).rg;
}
#endif
