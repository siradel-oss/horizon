// SPDX-FileCopyrightText: Copyright 2022 Siradel
// SPDX-License-Identifier: MIT

#pragma once

layout(std140) uniform Line
{
    vec4 color;
    vec3 ecef_cc_pos;
    float extent;
    vec3 axis;
    float width_px;
} hrz_line;
