// SPDX-FileCopyrightText: Copyright 2026 Siradel
// SPDX-License-Identifier: MIT

#pragma once

struct TileCommon
{
    vec4 center_low;
    vec4 center_high;
    uvec3 feature_reference;
    bool has_feature_ids;
    uvec2 object_reference;
};
