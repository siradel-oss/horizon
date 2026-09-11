// SPDX-FileCopyrightText: Copyright 2021 Siradel
// SPDX-License-Identifier: MIT

#pragma once

const uint DATA_TEXTURE_SIZE = uint(HRZ_S_VECTOR_REPR_DATA_TEXTURE_WIDTH);

#ifdef FLAT_SELECTION
#include "common/selection.glsl"
uniform highp usampler2D u_selection;

bool fetch_selection()
{
    return fetch_selection_storage(u_selection, i_feature_index);
}
#endif

#ifdef FLAT_VISUAL
uniform highp usampler2D u_feature_ids;

uvec2 fetch_feature_id()
{
    if (!hrz_tile.base.has_feature_ids)
    {
        return uvec2(0, 0);
    }

    ivec2 data_coords = ivec2(i_feature_index % DATA_TEXTURE_SIZE, i_feature_index / DATA_TEXTURE_SIZE);
    return texelFetch(u_feature_ids, data_coords, 0).rg;
}
#endif
