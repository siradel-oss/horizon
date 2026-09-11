// SPDX-FileCopyrightText: Copyright 2026 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "flat_vectors/tile_common_defs.glsl"

layout(std140) uniform Tile
{
    TileCommon base;

    // First bit is "inside", second bit is "outside".
    // 1 = show, 0 = hide.
    uint polyline_sides;

    // 0 if in meters, 1 if in pixels.
    uint line_width_unit;

    // See `DASH_MODE` defines.
    uint dash_mode;

    // See `DASH_SIZE_UNIT` defines.
    uint dash_period_unit;
    uint dash_primary_length_unit;
    uint animation_speed_unit;
} hrz_tile;
