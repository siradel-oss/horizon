// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#pragma once

// From the "OpenGL Insights" book.
// https://i.imgur.com/A15s0NF.png
float aastep(float threshold, float dist)
{
    float afwidth = 0.7 * length(vec2(dFdx(dist), dFdy(dist)));
    return smoothstep(threshold - afwidth, threshold + afwidth, dist);
}
