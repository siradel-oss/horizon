// SPDX-FileCopyrightText: Copyright 2022 Siradel
// SPDX-License-Identifier: MIT

#include "ui/ui_colors.h"

#include "hrz/fnd/hash.h"
#include "ui/ui_helpers.h"
#include "ui/ui_style.h"

#include <imgui.h>

#include <cmath>
#include <limits>

namespace ui::color
{

uint32_t multiply(uint32_t color, double value)
{
    auto vec_color = ImGui::ColorConvertU32ToFloat4(color);
    vec_color.x *= value;
    vec_color.y *= value;
    vec_color.z *= value;
    return (ImColor)vec_color;
}

uint32_t mix(uint32_t from, uint32_t to, double t)
{
    lm::dvec4 vec_from = ImGui::ColorConvertU32ToFloat4(from);
    lm::dvec4 vec_to = ImGui::ColorConvertU32ToFloat4(to);
    return (ImColor)(vec_from * (1.0 - t) + vec_to * t);
}

uint32_t with_alpha(uint32_t color, double alpha)
{
    auto vec_color = ImGui::ColorConvertU32ToFloat4(color);
    vec_color.w = alpha;
    return (ImColor)vec_color;
}

uint32_t from_hsv(float hue, float saturation, float value, float alpha)
{
    float r, g, b;
    ImGui::ColorConvertHSVtoRGB(hue, saturation, value, r, g, b);

    return IM_COL32(r * 255, g * 255, b * 255, alpha * 255);
}

uint32_t from_string(std::string_view string, float saturation, float value, float hue_shift)
{
    int64_t seed = hrz::murmur3_x64_64(string);

    float hue = (float)((double)seed / (double)std::numeric_limits<uint64_t>::max());
    hue = std::fmod(hue + hue_shift, 1.0F);

    return from_hsv(hue, saturation, value, 1.0);
}

uint32_t get_hover_shadow()
{
    return (style::is_dark_mode()) ? 0x18ffffff : 0x08000000;
}

} // namespace ui::color
