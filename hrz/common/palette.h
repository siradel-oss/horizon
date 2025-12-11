#pragma once

#include "hrz/fnd/inlined_vector.h"
#include "hrz/protocol/color/palette.pb.h"

#include <lin_maths.h>

#include <optional>
#include <string_view>

namespace hrz
{
// All colours are linear, except where noted otherwise.
struct Palette
{
    hrz_proto::PaletteType type;
    std::string name;

    // `first` and `second` are linear.
    // `first_encoded` and `second_encoded` are in the colour space used for interpolation.
    struct NumericColorPoint
    {
        lm::vec4 first;
        lm::vec4 second;
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
        lm::vec4 nan_color;
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

// Returns a linear colour
std::optional<lm::vec4> label_palettization(const Palette& palette, std::string_view label);

// Returns a linear colour
std::optional<lm::vec4> numeric_palettization(const Palette& palette, float value);
} // namespace palette
} // namespace hrz
