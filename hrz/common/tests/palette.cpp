#include "hrz/common/palette.h"

#include "hrz/common/color.h"
#include "hrz/fnd/log.h"
#include "hrz/protocol/all.h"

#include <gtest/gtest.h>
#include <lin_maths.h>

#include <cmath> // NAN
#include <limits>

namespace
{
bool test_color_eq(lm::vec4 a, lm::vec4 b)
{
    if (lm::round(a * 255.0f) != lm::round(b * 255.0f))
    {
        HRZ_LOG_ERROR(
            "[{}, {}, {}, {}] vs [{}, {}, {}, {}]", a.x, a.y, a.z, a.w, b.x, b.y, b.z, b.w);
        return false;
    }
    else
    {
        return true;
    }
}

struct GlobalColors
{
    GlobalColors()
    {
        proto_red.set_r(1);
        proto_red.set_g(0);
        proto_red.set_b(0);
        proto_red.set_a(1);

        proto_green.set_r(0);
        proto_green.set_g(1);
        proto_green.set_b(0);
        proto_green.set_a(1);

        proto_blue.set_r(0);
        proto_blue.set_g(0);
        proto_blue.set_b(1);
        proto_blue.set_a(1);

        proto_magenta.set_r(1);
        proto_magenta.set_g(1);
        proto_magenta.set_b(0);
        proto_magenta.set_a(1);

        proto_white.set_r(1);
        proto_white.set_g(1);
        proto_white.set_b(1);
        proto_white.set_a(1);

        proto_black.set_r(0);
        proto_black.set_g(0);
        proto_black.set_b(0);
        proto_black.set_a(1);

        proto_midgray.set_r(0.5);
        proto_midgray.set_g(0.5);
        proto_midgray.set_b(0.5);
        proto_midgray.set_a(1);

        lm_red = hrz::srgb_to_linear(lm::vec4(1, 0, 0, 1));
        lm_green = hrz::srgb_to_linear(lm::vec4(0, 1, 0, 1));
        lm_blue = hrz::srgb_to_linear(lm::vec4(0, 0, 1, 1));
        lm_magenta = hrz::srgb_to_linear(lm::vec4(1, 1, 0, 1));
        lm_white = hrz::srgb_to_linear(lm::vec4(1, 1, 1, 1));
        lm_black = hrz::srgb_to_linear(lm::vec4(0, 0, 0, 1));
        lm_midgray = hrz::srgb_to_linear(lm::vec4(0.388601, 0.388601, 0.388601, 1));
    }

    hrz_proto::Color proto_red;
    hrz_proto::Color proto_green;
    hrz_proto::Color proto_blue;
    hrz_proto::Color proto_magenta;
    hrz_proto::Color proto_white;
    hrz_proto::Color proto_black;
    hrz_proto::Color proto_midgray;

    lm::vec4 lm_red;
    lm::vec4 lm_green;
    lm::vec4 lm_blue;
    lm::vec4 lm_magenta;
    lm::vec4 lm_white;
    lm::vec4 lm_black;
    lm::vec4 lm_midgray;
};

static const GlobalColors global_colors;
} // anonymous namespace.

TEST(CommonPalette, label_palette)
{
    hrz_proto::Palette proto_palette;
    proto_palette.set_type(hrz_proto::PaletteType::LABEL);
    auto* label = proto_palette.mutable_label();

    *label->mutable_default_color() = global_colors.proto_white;

    {
        auto* label_color = label->add_labels();
        label_color->set_label("A");
        *label_color->mutable_color() = global_colors.proto_red;
    }
    {
        auto* label_color = label->add_labels();
        label_color->set_label("B");
        *label_color->mutable_color() = global_colors.proto_green;
    }

    hrz::Palette palette = hrz::palette::from_proto(proto_palette);
    auto c1 = hrz::palette::label_palettization(palette, "B");
    auto c2 = hrz::palette::label_palettization(palette, "A");
    auto c3 = hrz::palette::label_palettization(palette, "D");
    auto c4 = hrz::palette::label_palettization(palette, "96");
    auto c5 = hrz::palette::label_palettization(palette, "");

    EXPECT_TRUE(c1.has_value());
    EXPECT_TRUE(c2.has_value());
    EXPECT_TRUE(c3.has_value());
    EXPECT_TRUE(c4.has_value());
    EXPECT_TRUE(c5.has_value());

    EXPECT_TRUE(c1.value() == global_colors.lm_green);
    EXPECT_TRUE(c2.value() == global_colors.lm_red);
    EXPECT_TRUE(c3.value() == global_colors.lm_white);
    EXPECT_TRUE(c4.value() == global_colors.lm_white);
    EXPECT_TRUE(c5.value() == global_colors.lm_white);
}

TEST(CommonPalette, numeric_palette_continuous)
{
    hrz_proto::Palette proto_palette;
    proto_palette.set_type(hrz_proto::PaletteType::NUMERIC);
    auto* numeric = proto_palette.mutable_numeric();

    // The continuous palette is:
    //       10      20
    //       |-------|
    //     white   black
    //
    // NaN: red

    *numeric->mutable_nan_color() = global_colors.proto_red;
    numeric->set_interpolation_mode(hrz_proto::ColorInterpolationMode::OKLAB);

    {
        auto* color_point = numeric->add_color_points();
        color_point->set_value(10.0);
        *color_point->mutable_first_color() = global_colors.proto_white;
        *color_point->mutable_second_color() = global_colors.proto_white;
    }

    {
        auto* color_point = numeric->add_color_points();
        color_point->set_value(20.0);
        *color_point->mutable_first_color() = global_colors.proto_black;
        *color_point->mutable_second_color() = global_colors.proto_black;
    }

    hrz::Palette palette = hrz::palette::from_proto(proto_palette);
    auto c1 = hrz::palette::numeric_palettization(palette, 10.0);
    auto c2 = hrz::palette::numeric_palettization(palette, 15.0);
    auto c3 = hrz::palette::numeric_palettization(palette, 20.0);
    auto c4 = hrz::palette::numeric_palettization(palette, -50.0);
    auto c5 = hrz::palette::numeric_palettization(palette, 50.0);
    auto c6 = hrz::palette::numeric_palettization(palette, NAN);
    auto c7 = hrz::palette::numeric_palettization(palette, std::numeric_limits<float>::infinity());
    auto c8 =
        hrz::palette::numeric_palettization(palette, -1 * std::numeric_limits<float>::infinity());

    EXPECT_TRUE(c1.has_value());
    EXPECT_TRUE(c2.has_value());
    EXPECT_TRUE(c3.has_value());
    EXPECT_TRUE(c4.has_value());
    EXPECT_TRUE(c5.has_value());
    EXPECT_TRUE(c6.has_value());
    EXPECT_TRUE(c7.has_value());
    EXPECT_TRUE(c8.has_value());

    // Fuzzy equality is fine here because it's a continuous palette.
    EXPECT_TRUE(test_color_eq(c1.value(), global_colors.lm_white));
    EXPECT_TRUE(test_color_eq(c2.value(), global_colors.lm_midgray));
    EXPECT_TRUE(test_color_eq(c3.value(), global_colors.lm_black));
    EXPECT_TRUE(test_color_eq(c4.value(), global_colors.lm_white));
    EXPECT_TRUE(test_color_eq(c5.value(), global_colors.lm_black));
    EXPECT_TRUE(test_color_eq(c6.value(), global_colors.lm_red));
    EXPECT_TRUE(test_color_eq(c7.value(), global_colors.lm_black));
    EXPECT_TRUE(test_color_eq(c8.value(), global_colors.lm_white));
}

TEST(CommonPalette, numeric_palette_color_point_edges)
{
    hrz_proto::Palette proto_palette;
    proto_palette.set_type(hrz_proto::PaletteType::NUMERIC);
    auto* numeric = proto_palette.mutable_numeric();

    // Palette starts as
    //       0          0.5
    // black | red green | black
    //
    // NaN: white

    *numeric->mutable_nan_color() = global_colors.proto_white;
    numeric->set_interpolation_mode(hrz_proto::ColorInterpolationMode::OKLAB);

    {
        auto* color_point = numeric->add_color_points();
        color_point->set_value(0.0);
        *color_point->mutable_first_color() = global_colors.proto_black;
        *color_point->mutable_second_color() = global_colors.proto_red;
    }

    {
        auto* color_point = numeric->add_color_points();
        color_point->set_value(0.5);
        *color_point->mutable_first_color() = global_colors.proto_green;
        *color_point->mutable_second_color() = global_colors.proto_black;
    }

    {
        hrz::Palette palette = hrz::palette::from_proto(proto_palette);
        auto c1 = hrz::palette::numeric_palettization(palette, 0.0);
        auto c2 = hrz::palette::numeric_palettization(palette, 0.5);

        EXPECT_TRUE(c1.has_value());
        EXPECT_TRUE(c2.has_value());
        EXPECT_TRUE(test_color_eq(c1.value(), global_colors.lm_red));
        EXPECT_TRUE(test_color_eq(c2.value(), global_colors.lm_black));
    }

    // Then grows to
    //       0          0.5           1
    // black | red green | green blue | black

    {
        auto* color_point = numeric->mutable_color_points(1);
        *color_point->mutable_second_color() = global_colors.proto_green;
    }

    {
        auto* color_point = numeric->add_color_points();
        color_point->set_value(1);
        *color_point->mutable_first_color() = global_colors.proto_blue;
        *color_point->mutable_second_color() = global_colors.proto_black;
    }

    {
        hrz::Palette palette = hrz::palette::from_proto(proto_palette);
        auto c1 = hrz::palette::numeric_palettization(palette, 0.5);
        auto c2 = hrz::palette::numeric_palettization(palette, 1.0);

        EXPECT_TRUE(c1.has_value());
        EXPECT_TRUE(c2.has_value());
        EXPECT_TRUE(test_color_eq(c1.value(), global_colors.lm_green));
        EXPECT_TRUE(test_color_eq(c2.value(), global_colors.lm_black));
    }
}

TEST(CommonPalette, numeric_palette_discrete)
{
    hrz_proto::Palette proto_palette;
    proto_palette.set_type(hrz_proto::PaletteType::NUMERIC);
    auto* numeric = proto_palette.mutable_numeric();

    // The discrete palette is:
    //       10      20       30
    // blue  |  red  |  green |  magenta
    //
    // NaN: white

    *numeric->mutable_nan_color() = global_colors.proto_white;
    numeric->set_interpolation_mode(hrz_proto::ColorInterpolationMode::OKLAB);

    {
        auto* color_point = numeric->add_color_points();
        color_point->set_value(10.0);
        *color_point->mutable_first_color() = global_colors.proto_blue;
        *color_point->mutable_second_color() = global_colors.proto_red;
    }

    {
        auto* color_point = numeric->add_color_points();
        color_point->set_value(20.0);
        *color_point->mutable_first_color() = global_colors.proto_red;
        *color_point->mutable_second_color() = global_colors.proto_green;
    }

    {
        auto* color_point = numeric->add_color_points();
        color_point->set_value(30.0);
        *color_point->mutable_first_color() = global_colors.proto_green;
        *color_point->mutable_second_color() = global_colors.proto_magenta;
    }

    hrz::Palette palette = hrz::palette::from_proto(proto_palette);
    auto c1 = hrz::palette::numeric_palettization(palette, 10.0);
    auto c2 = hrz::palette::numeric_palettization(palette, 15.0);
    auto c3 = hrz::palette::numeric_palettization(palette, 20.0);
    auto c4 = hrz::palette::numeric_palettization(palette, 25.0);
    auto c5 = hrz::palette::numeric_palettization(palette, 30.0);
    auto c6 = hrz::palette::numeric_palettization(palette, -50.0);
    auto c7 = hrz::palette::numeric_palettization(palette, 50.0);
    auto c8 = hrz::palette::numeric_palettization(palette, NAN);
    auto c9 = hrz::palette::numeric_palettization(palette, std::numeric_limits<float>::infinity());
    auto c10 =
        hrz::palette::numeric_palettization(palette, -1 * std::numeric_limits<float>::infinity());

    EXPECT_TRUE(c1.has_value());
    EXPECT_TRUE(c2.has_value());
    EXPECT_TRUE(c3.has_value());
    EXPECT_TRUE(c4.has_value());
    EXPECT_TRUE(c5.has_value());
    EXPECT_TRUE(c6.has_value());
    EXPECT_TRUE(c7.has_value());
    EXPECT_TRUE(c8.has_value());
    EXPECT_TRUE(c9.has_value());
    EXPECT_TRUE(c10.has_value());

    // We want exact color equality here for classified palettes.
    EXPECT_TRUE(c1.value() == global_colors.lm_red);
    EXPECT_TRUE(c2.value() == global_colors.lm_red);
    EXPECT_TRUE(c3.value() == global_colors.lm_green);
    EXPECT_TRUE(c4.value() == global_colors.lm_green);
    EXPECT_TRUE(c5.value() == global_colors.lm_magenta);
    EXPECT_TRUE(c6.value() == global_colors.lm_blue);
    EXPECT_TRUE(c7.value() == global_colors.lm_magenta);
    EXPECT_TRUE(c8.value() == global_colors.lm_white);
    EXPECT_TRUE(c9.value() == global_colors.lm_magenta);
    EXPECT_TRUE(c10.value() == global_colors.lm_blue);
}

TEST(CommonPalette, numeric_color_interpolation_modes)
{
    auto make_palette = [&](hrz_proto::ColorInterpolationMode mode)
    {
        hrz_proto::Palette proto_palette;
        proto_palette.set_type(hrz_proto::PaletteType::NUMERIC);
        auto* numeric = proto_palette.mutable_numeric();

        *numeric->mutable_nan_color() = global_colors.proto_white;
        numeric->set_interpolation_mode(mode);

        {
            auto* color_point = numeric->add_color_points();
            color_point->set_value(0.0);
            *color_point->mutable_first_color() = global_colors.proto_red;
            *color_point->mutable_second_color() = global_colors.proto_red;
        }

        {
            auto* color_point = numeric->add_color_points();
            color_point->set_value(1.0);
            *color_point->mutable_first_color() = global_colors.proto_green;
            *color_point->mutable_second_color() = global_colors.proto_green;
        }

        return hrz::palette::from_proto(proto_palette);
    };

    {
        hrz::Palette palette = make_palette(hrz_proto::ColorInterpolationMode::THRESHOLD);
        auto c1 = hrz::palette::numeric_palettization(palette, 0.0);
        auto c2 = hrz::palette::numeric_palettization(palette, 0.5);
        auto c3 = hrz::palette::numeric_palettization(palette, 1.0);

        EXPECT_TRUE(c1.has_value());
        EXPECT_TRUE(c2.has_value());
        EXPECT_TRUE(c3.has_value());

        EXPECT_TRUE(c1.value() == global_colors.lm_red);
        EXPECT_TRUE(c2.value() == global_colors.lm_red);
        EXPECT_TRUE(c3.value() == global_colors.lm_green);
    }

    {
        hrz::Palette palette = make_palette(hrz_proto::ColorInterpolationMode::SRGB);
        auto c1 = hrz::palette::numeric_palettization(palette, 0.0);
        auto c2 = hrz::palette::numeric_palettization(palette, 0.5);
        auto c3 = hrz::palette::numeric_palettization(palette, 1.0);

        EXPECT_TRUE(c1.has_value());
        EXPECT_TRUE(c2.has_value());
        EXPECT_TRUE(c3.has_value());

        EXPECT_TRUE(test_color_eq(c1.value(), global_colors.lm_red));
        EXPECT_TRUE(test_color_eq(c2.value(), hrz::srgb_to_linear(lm::vec4(0.5, 0.5, 0.0, 1.0))));
        EXPECT_TRUE(test_color_eq(c3.value(), global_colors.lm_green));
    }

    {
        hrz::Palette palette = make_palette(hrz_proto::ColorInterpolationMode::OKLAB);
        auto c1 = hrz::palette::numeric_palettization(palette, 0.0);
        auto c2 = hrz::palette::numeric_palettization(palette, 0.5);
        auto c3 = hrz::palette::numeric_palettization(palette, 1.0);

        EXPECT_TRUE(c1.has_value());
        EXPECT_TRUE(c2.has_value());
        EXPECT_TRUE(c3.has_value());

        EXPECT_TRUE(test_color_eq(c1.value(), global_colors.lm_red));
        // Value obtained on https://observablehq.com/@aras-p/oklab-interpolation-test
        // to guarantee implementation independence.
        EXPECT_TRUE(test_color_eq(
            c2.value(), hrz::srgb_to_linear(lm::vec4(0.81630f, 0.66036f, 0.00177f, 1.0f))));
        EXPECT_TRUE(test_color_eq(c3.value(), global_colors.lm_green));
    }

    {
        hrz::Palette palette = make_palette(hrz_proto::ColorInterpolationMode::LINEAR_SRGB);
        auto c1 = hrz::palette::numeric_palettization(palette, 0.0);
        auto c2 = hrz::palette::numeric_palettization(palette, 0.5);
        auto c3 = hrz::palette::numeric_palettization(palette, 1.0);

        EXPECT_TRUE(c1.has_value());
        EXPECT_TRUE(c2.has_value());
        EXPECT_TRUE(c3.has_value());

        EXPECT_TRUE(test_color_eq(c1.value(), global_colors.lm_red));
        EXPECT_TRUE(test_color_eq(c2.value(), lm::vec4(0.5f, 0.5f, 0.0f, 1.0f)));
        EXPECT_TRUE(test_color_eq(c3.value(), global_colors.lm_green));
    }
}

TEST(CommonPalette, invalid_palette_type)
{
    hrz_proto::Palette label_palette;
    label_palette.set_type(hrz_proto::PaletteType::LABEL);

    hrz_proto::Palette numeric_palette;
    numeric_palette.set_type(hrz_proto::PaletteType::NUMERIC);

    ASSERT_FALSE(
        hrz::palette::label_palettization(hrz::palette::from_proto(numeric_palette), "Test")
            .has_value());
    ASSERT_FALSE(hrz::palette::numeric_palettization(hrz::palette::from_proto(label_palette), 10.0)
                     .has_value());
}
