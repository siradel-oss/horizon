#pragma once

#include <hrz_fnd_inlined_vector.h>
#include <hrz_protocol_all.h>

#include <lin_maths.h>

#include <optional>
#include <string_view>

namespace hrz
{
struct Palette
{
    hrz_proto::PaletteType type;
    std::string name;

    struct NumericColorPoint
    {
        lm::vec4 first_srgb;
        lm::vec4 second_srgb;
        lm::vec4 first_encoded;
        lm::vec4 second_encoded;
    };

    struct ValuedNumericColorPoint
    {
        float value;
        NumericColorPoint color_point;
    };

    struct
    {
        hrz_proto::ColorInterpolationMode mode;
        hrz::InlinedVector<ValuedNumericColorPoint, 16> color_points;
        lm::vec4 nan_color_srgb;
    } numeric;

    struct LabelColor
    {
        std::string label;
        lm::vec4 color;
    };

    struct
    {
        hrz::InlinedVector<LabelColor, 16> mapping;
        lm::vec4 default_color;
    } label;
};

namespace palette
{
Palette from_proto(const hrz_proto::Palette& proto);
Palette from_proto(const hrz_proto::NumericPalette& proto);
Palette from_proto(const hrz_proto::LabelPalette& proto);
Palette::NumericColorPoint from_proto(
    const hrz_proto::Color& color_low,
    const hrz_proto::Color& color_up,
    hrz_proto::ColorInterpolationMode mode);

std::optional<lm::vec4> label_palettization(const Palette& palette, std::string_view label);

std::optional<lm::vec4> numeric_palettization(const Palette& palette, float value);
} // namespace palette
} // namespace hrz
