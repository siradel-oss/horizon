// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/common/palette.h"

#include "hrz/common/color.h"
#include "hrz/fnd/log.h"

#include <algorithm>

namespace
{

constexpr lm::vec4 encode_from_srgb(hrz_proto::ColorInterpolationMode mode, const lm::vec4& color)
{
    switch (mode)
    {
        case hrz_proto::ColorInterpolationMode::OKLAB: return hrz::srgb_to_oklab(color);
        case hrz_proto::ColorInterpolationMode::LINEAR_SRGB: return hrz::srgb_to_linear(color);
        case hrz_proto::ColorInterpolationMode::SRGB: return color;
        case hrz_proto::ColorInterpolationMode::THRESHOLD: return hrz::srgb_to_linear(color);
        default: return color;
    }
}

constexpr lm::vec4 decode_to_linear(hrz_proto::ColorInterpolationMode mode, const lm::vec4& color)
{
    switch (mode)
    {
        case hrz_proto::ColorInterpolationMode::OKLAB: return hrz::oklab_to_linear(color);
        case hrz_proto::ColorInterpolationMode::LINEAR_SRGB: return color;
        case hrz_proto::ColorInterpolationMode::SRGB: return hrz::srgb_to_linear(color);
        case hrz_proto::ColorInterpolationMode::THRESHOLD: return color;
        default: return color;
    }
}

constexpr lm::vec4 mix(
    hrz_proto::ColorInterpolationMode mode,
    const lm::vec4& a,
    const lm::vec4& b,
    float x)
{
    switch (mode)
    {
        case hrz_proto::ColorInterpolationMode::OKLAB:
        case hrz_proto::ColorInterpolationMode::LINEAR_SRGB:
        case hrz_proto::ColorInterpolationMode::SRGB: return lm::mix(a, b, x);
        case hrz_proto::ColorInterpolationMode::THRESHOLD: return (x < 0.5) ? a : b;
        default: return a;
    }
}

} // namespace

namespace hrz::palette
{

std::optional<lm::vec4> label_palettization(const Palette& palette, std::string_view label)
{
    if (palette.type != Palette::kLabel)
    {
        HRZ_LOG_ERROR("Numeric palette used to colorize a label.");
        return {};
    }

    // @Todo @Performance: in the case of a large number of labels it would be better to
    // have a hashtable mapping the label to the color for a O(1) access time.
    for (const auto& mapping : palette.label.mapping)
    {
        if (mapping.label.size() == label.size()
            && strncmp(mapping.label.c_str(), label.data(), label.size()) == 0)
        {
            return mapping.color;
        }
    }

    return palette.label.default_color;
}

std::optional<lm::vec4> numeric_palettization(const Palette& palette, float value)
{
    if (palette.type != Palette::kNumeric)
    {
        HRZ_LOG_ERROR("Label palette used to colorize a number.");
        return {};
    }

    const auto& color_stops = palette.numeric.color_stops;

    // We only use the encoded colors when we do interpolation and we avoid it in
    // all other cases because we want to preserve the original color values as
    // much as possible since that's what we rely on for nodata filtering.
    //      -slerouzic, 2021-10-29

    if (color_stops.empty() || std::isnan(value))
    {
        return palette.numeric.nan_color;
    }

    if (value < color_stops.front().value)
    {
        return color_stops.front().color_stop.first;
    }
    else if (value >= color_stops.back().value)
    {
        return color_stops.back().color_stop.second;
    }

    auto upper = std::ranges::upper_bound(
        color_stops, value, std::less<float>{},
        [](const Palette::ValuedNumericColorStop& p) { return p.value; });

    // Value is exactly the last one.
    if (upper == color_stops.end())
    {
        return (--upper)->color_stop.first;
    }

    auto lower = upper;
    if (upper != color_stops.begin())
    {
        lower--;
    }

    if (upper->color_stop.first == lower->color_stop.second)
    {
        return upper->color_stop.first;
    }
    else
    {
        const float delta = 1.0F - (value - lower->value) / (upper->value - lower->value);
        return decode_to_linear(
            palette.numeric.mode,
            mix(palette.numeric.mode, upper->color_stop.first_encoded,
                lower->color_stop.second_encoded, delta));
    }
}

Palette from_proto(const hrz_proto::Palette& proto)
{
    Palette palette;

    switch (proto.palette_type_case())
    {
        case hrz_proto::Palette::PaletteTypeCase::kLabel:
            palette = from_proto(proto.label());
            break;
        case hrz_proto::Palette::PaletteTypeCase::kNumeric:
            palette = from_proto(proto.numeric());
            break;
        case hrz_proto::Palette::PaletteTypeCase::PALETTE_TYPE_NOT_SET:
            assert(false && "Unhandled palette type.");
            break;
    }

    palette.name = proto.name();
    return palette;
}

Palette from_proto(const hrz_proto::NumericPalette& proto)
{
    Palette palette;
    palette.type = Palette::kNumeric;
    palette.name = "";

    auto& numeric = palette.numeric;

    numeric.mode = proto.interpolation_mode();
    numeric.nan_color = hrz::srgb_to_linear(hrz::convert_proto_color_to_float(proto.nan_color()));

    for (const auto& color_stop : proto.color_stops())
    {
        lm::vec4 first_srgb = hrz::convert_proto_color_to_float(color_stop.first_color());
        lm::vec4 first = hrz::srgb_to_linear(first_srgb);
        lm::vec4 second_srgb = hrz::convert_proto_color_to_float(color_stop.second_color());
        lm::vec4 second = hrz::srgb_to_linear(second_srgb);

        numeric.color_stops.push_back(
            {color_stop.value(),
             {first, second, encode_from_srgb(numeric.mode, first_srgb),
              encode_from_srgb(numeric.mode, second_srgb)}});
    }

    std::ranges::sort(
        numeric.color_stops,
        [](const Palette::ValuedNumericColorStop& p0, const Palette::ValuedNumericColorStop& p1)
        { return p0.value < p1.value; });

    return palette;
}

Palette from_proto(const hrz_proto::LabelPalette& proto)
{
    Palette palette;
    palette.type = Palette::kLabel;
    palette.name = "";

    auto& label = palette.label;

    label.default_color =
        hrz::srgb_to_linear(hrz::convert_proto_color_to_float(proto.default_color()));
    for (const auto& label_color : proto.labels())
    {
        label.mapping.push_back(
            {std::string{label_color.label()},
             hrz::srgb_to_linear(hrz::convert_proto_color_to_float(label_color.color()))});
    }

    return palette;
}

Palette::NumericColorStop from_proto(
    const hrz_proto::Color& first_color,
    const hrz_proto::Color& second_color,
    hrz_proto::ColorInterpolationMode mode)
{
    lm::vec4 first_srgb = hrz::convert_proto_color_to_float(first_color);
    lm::vec4 second_srgb = hrz::convert_proto_color_to_float(second_color);

    Palette::NumericColorStop cp;
    cp.first = hrz::srgb_to_linear(first_srgb);
    cp.second = hrz::srgb_to_linear(second_srgb);
    cp.first_encoded = encode_from_srgb(mode, first_srgb);
    cp.second_encoded = encode_from_srgb(mode, second_srgb);
    return cp;
}

} // namespace hrz::palette
