// SPDX-FileCopyrightText: Copyright 2025 Siradel
// SPDX-License-Identifier: MIT

#pragma once

layout(std140) uniform OverlayPasses
{
    uint pass_id;
    float world_size;
    uint texture_size;
} hrz_overlay_passes;
