// SPDX-FileCopyrightText: Copyright 2021 Siradel
// SPDX-License-Identifier: MIT

#pragma once

layout(std140) uniform Shape
{
    vec4 color;
    uvec2 object_reference;
    float line_width;
} hrz_shape;
