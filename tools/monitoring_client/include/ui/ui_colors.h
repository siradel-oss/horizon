#pragma once

#include <cinttypes>
#include <string_view>

namespace ui
{
namespace color
{
constexpr uint32_t SUCCESS = 0xff22bb44;
constexpr uint32_t ERROR = 0xff2222dd;
constexpr uint32_t WARNING = 0xff2299aa;
constexpr uint32_t WARNING_CRITICAL = 0xff2222dd;

uint32_t multiply(uint32_t color, double value);
uint32_t mix(uint32_t from, uint32_t to, double t);

uint32_t with_alpha(uint32_t color, double alpha);

uint32_t from_hsv(float hue, float saturation, float value, float alpha = 1.0);
uint32_t from_string(
    std::string_view string,
    float saturation,
    float value,
    float hue_shift = 0.0f);

uint32_t get_hover_shadow();
} // namespace color

struct ColorSet
{
    uint32_t base;
    uint32_t active;
    uint32_t hovered;
};

namespace color_set
{
constexpr ColorSet RED = {0xff9999f2, 0xff5252cc, 0xffb3b3fa};
constexpr ColorSet GREEN = {0xff99f299, 0xff52cc52, 0xffb3fab3};
constexpr ColorSet BLUE = {0xfff29999, 0xffcc5252, 0xfffab3b3};
constexpr ColorSet YELLOW = {0xff99d9d9, 0xff52bfbf, 0xffb3e6e6};
constexpr ColorSet MAGENTA = {0xffd999d9, 0xffbf52bf, 0xffe6b3e6};
constexpr ColorSet CYAN = {0xffd9d999, 0xffbfbf52, 0xffe6e6b3};
} // namespace color_set
} // namespace ui
