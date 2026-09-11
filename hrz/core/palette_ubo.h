// SPDX-FileCopyrightText: Copyright 2023 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/core/render/resources.h"

namespace hrz
{

struct Palette;

struct PaletteUniformData
{
    float color_stops_value[HRZ_S_MAX_PALETTE_COLOR_STOPS];
    lm::vec4 color_stops_encoded[2 * HRZ_S_MAX_PALETTE_COLOR_STOPS];
    lm::vec4 nan_color;
    int32_t color_interpolation_mode;
    uint32_t num_color_stops;
    uint32_t _padding[2];
};

static_assert(HRZ_S_MAX_PALETTE_COLOR_STOPS % 4 == 0, "Palette color stop count");
HRZ_CHECK_UBO_SIZE(PaletteUniformData);

namespace palette
{

void fill_ubo_data(PaletteUniformData*, const Palette&);

}
} // namespace hrz
