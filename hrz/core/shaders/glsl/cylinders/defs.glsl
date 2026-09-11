// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#pragma once

layout(std140) uniform Tile
{
    vec4 center_low;
    vec4 center_high;
    uvec2 object_reference;
    int clip_id;
    bool lighting_enabled;
    bool receive_shadows;

    // See `DASH_MODE` defines.
    uint dash_mode;

    // See `DASH_SIZE_UNIT` defines.
    uint dash_period_unit;
    uint dash_primary_length_unit;

    uvec3 feature_reference;

    uint animation_speed_unit;
} hrz_tile;
