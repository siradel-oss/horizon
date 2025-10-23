#include "hrz_core_palette_ubo.h"

#include <hrz_common_palette.h>
#include <hrz_fnd_log.h>

namespace hrz::palette
{
void fill_ubo_data(PaletteUniformData* ubo_data, const Palette& palette)
{
    assert(palette.type == hrz_proto::PaletteType::NUMERIC);

    uint32_t num_color_points = palette.numeric.color_points.size();
    if (num_color_points > HRZ_S_MAX_PALETTE_COLOR_POINTS)
    {
        HRZ_LOG_WARNING(
            "Palettes used on the GPU can have at most {} color points.",
            HRZ_S_MAX_PALETTE_COLOR_POINTS);
        num_color_points = HRZ_S_MAX_PALETTE_COLOR_POINTS;
    }

    ubo_data->num_color_points = num_color_points;
    ubo_data->nan_color = palette.numeric.nan_color;
    ubo_data->color_interpolation_mode = (int32_t)palette.numeric.mode;

    auto it = palette.numeric.color_points.begin();
    for (unsigned int i = 0; i < num_color_points; ++i, ++it)
    {
        ubo_data->color_points_value[i] = (float)it->value;
        ubo_data->color_points_encoded[2 * i + 0] = it->color_point.first_encoded;
        ubo_data->color_points_encoded[2 * i + 1] = it->color_point.second_encoded;
    }
}
} // namespace hrz::palette
