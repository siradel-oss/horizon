// SPDX-FileCopyrightText: Copyright 2023 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/core/palette_ubo.h"

#include "hrz/common/palette.h"
#include "hrz/fnd/log.h"

namespace hrz::palette
{

void fill_ubo_data(PaletteUniformData* ubo_data, const Palette& palette)
{
    assert(palette.type == Palette::kNumeric);

    uint32_t num_color_stops = palette.numeric.color_stops.size();
    if (num_color_stops > HRZ_S_MAX_PALETTE_COLOR_STOPS)
    {
        HRZ_LOG_WARNING(
            "Palettes used on the GPU can have at most {} color stops.",
            HRZ_S_MAX_PALETTE_COLOR_STOPS);
        num_color_stops = HRZ_S_MAX_PALETTE_COLOR_STOPS;
    }

    ubo_data->num_color_stops = num_color_stops;
    ubo_data->nan_color = palette.numeric.nan_color;
    ubo_data->color_interpolation_mode = (int32_t)palette.numeric.mode;

    auto it = palette.numeric.color_stops.begin();
    for (unsigned int i = 0; i < num_color_stops; ++i, ++it)
    {
        ubo_data->color_stops_value[i] = (float)it->value;
        ubo_data->color_stops_encoded[2 * i + 0] = it->color_stop.first_encoded;
        ubo_data->color_stops_encoded[2 * i + 1] = it->color_stop.second_encoded;
    }
}

} // namespace hrz::palette
