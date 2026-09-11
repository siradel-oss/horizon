// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#pragma once

layout(std140) uniform Tile
{
    vec4 center_low;
    vec4 center_high;
    mat4 transform;
    mat3 normal_transform;
} hrz_tile;
