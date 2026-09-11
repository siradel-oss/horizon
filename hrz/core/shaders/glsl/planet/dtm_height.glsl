// SPDX-FileCopyrightText: Copyright 2021 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "planet/clipmap.glsl"
#include "common/maths.glsl"

uvec3 get_clipmap_slot(vec2 base_wmerc_pos, vec2 partial_wmerc_pos)
{
    // Always query the most detailed LOD (0).
    // Actual LOD depend on the distance to the ground. At low zooms,
    // it is dominated by the distance to the ellipsoid and could be
    // calculated, but when close to the ground the distance depends
    // mostly on the terrain elevation. Because the whole point of
    // this operation is to get the elevation, it cannot depend on it.
    // The atlas is populated using the results of the feedback render
    // anyway, so it shouldn't have a huge impact.
    return get_clipmap_slot_for_lod(0u, base_wmerc_pos, partial_wmerc_pos);
}

void fetch_tile_from_dtm_clipmap(
    in highp usampler2DArray indirection, in uvec3 slot, out uvec2 tile_position, out uint lod)
{
    fetch_tile_from_clipmap(indirection, slot, tile_position, lod);
}

float fetch_height(in highp sampler2D atlas, in uvec2 tile, in vec2 uv)
{
    vec2 atlas_uv = (vec2(tile) + uv) * float(ATLAS_TILE_SIZE);
    atlas_uv /= vec2(textureSize(atlas, 0).xy);
    return textureLod(atlas, atlas_uv, 0.0).r;
}

float compute_height_from_dtm(
    in highp usampler2DArray indirection, in highp sampler2D atlas,
    vec2 base_wmerc, vec2 partial_wmerc)
{
    uvec3 slot = get_clipmap_slot(base_wmerc, partial_wmerc);

    if (slot.z < uint(LOD_COUNT))
    {
        uint lod;
        uvec2 tile_position;
        fetch_tile_from_dtm_clipmap(indirection, slot, tile_position, lod);

        vec2 uv = get_tile_uv(tile_position, lod, base_wmerc, partial_wmerc);
        if (is_nan(uv.x) || is_nan(uv.y))
        {
            return 0.0;
        }
        else
        {
            return fetch_height(atlas, tile_position, uv);
        }
    }
}
