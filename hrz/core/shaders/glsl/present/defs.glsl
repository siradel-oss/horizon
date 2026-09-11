// SPDX-FileCopyrightText: Copyright 2022 Siradel
// SPDX-License-Identifier: MIT

#pragma once

layout(std140) uniform SceneViewport
{
    ivec2 origin;
    ivec2 size;
    ivec2 screen_resolution;
} hrz_scene;
