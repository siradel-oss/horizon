// SPDX-FileCopyrightText: Copyright 2026 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "flat_vectors/tile_common_defs.glsl"

layout(std140) uniform Tile
{
    TileCommon base;

    // 0 if in meters, 1 if in pixels.
    uint disc_radius_unit;

    // 0 if in meters, 1 if in pixels.
    uint outline_width_unit;
} hrz_tile;
