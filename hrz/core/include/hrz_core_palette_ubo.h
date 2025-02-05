#pragma once

#include "hrz_core_render.h"

namespace hrz
{
struct Palette;

struct PaletteUniformData
{
    float color_points_value[HRZ_S_MAX_PALETTE_COLOR_POINTS];
    lm::vec4 color_points_encoded[2 * HRZ_S_MAX_PALETTE_COLOR_POINTS];
    lm::vec4 nan_color_srgb;
    int32_t color_interpolation_mode;
    uint32_t num_color_points;
    uint32_t _padding[2];
};

static_assert(HRZ_S_MAX_PALETTE_COLOR_POINTS % 4 == 0, "Palette color point count");
HRZ_CHECK_UBO_SIZE(PaletteUniformData);

namespace palette
{
void fill_ubo_data(PaletteUniformData*, const Palette&);
}
} // namespace hrz
