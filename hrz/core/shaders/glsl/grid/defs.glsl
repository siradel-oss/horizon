// SPDX-FileCopyrightText: Copyright 2021 Siradel
// SPDX-License-Identifier: MIT

#pragma once

layout(std140) uniform Grid
{
    vec4 color;
    vec3 axis_x;
    float extent;
    vec3 axis_y;
    float cell_size;
    vec3 ecef_cc_pos;
    vec2 offset;
} hrz_grid;
