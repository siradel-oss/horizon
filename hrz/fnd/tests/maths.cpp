// SPDX-FileCopyrightText: Copyright 2023 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/fnd/maths.h"

#include <gtest/gtest.h>

static constexpr double rad(double deg)
{
    return deg * lm::PI / 180.0;
}

TEST(FndMaths, NormalizeAnglePositive)
{
    EXPECT_DOUBLE_EQ(hrz::normalize_angle_positive(rad(0)), rad(0));
    EXPECT_DOUBLE_EQ(hrz::normalize_angle_positive(rad(90)), rad(90));
    EXPECT_DOUBLE_EQ(hrz::normalize_angle_positive(rad(180)), rad(180));
    EXPECT_DOUBLE_EQ(hrz::normalize_angle_positive(rad(270)), rad(270));
    EXPECT_DOUBLE_EQ(hrz::normalize_angle_positive(rad(360)), rad(0));
    EXPECT_DOUBLE_EQ(hrz::normalize_angle_positive(rad(-90)), rad(270));
    EXPECT_DOUBLE_EQ(hrz::normalize_angle_positive(rad(-180)), rad(180));
    EXPECT_DOUBLE_EQ(hrz::normalize_angle_positive(rad(-270)), rad(90));
    EXPECT_DOUBLE_EQ(hrz::normalize_angle_positive(rad(-360)), rad(0));
    EXPECT_DOUBLE_EQ(hrz::normalize_angle_positive(rad(-450)), rad(270));
    EXPECT_DOUBLE_EQ(hrz::normalize_angle_positive(rad(450)), rad(90));
}

TEST(FndMaths, NormalizeAngleAroundZero)
{
    EXPECT_DOUBLE_EQ(hrz::normalize_angle_around_zero(rad(0)), rad(0));
    EXPECT_DOUBLE_EQ(hrz::normalize_angle_around_zero(rad(90)), rad(90));
    EXPECT_DOUBLE_EQ(hrz::normalize_angle_around_zero(rad(180)), rad(-180));
    EXPECT_DOUBLE_EQ(hrz::normalize_angle_around_zero(rad(270)), rad(-90));
    EXPECT_DOUBLE_EQ(hrz::normalize_angle_around_zero(rad(360)), rad(0));
    EXPECT_DOUBLE_EQ(hrz::normalize_angle_around_zero(rad(-90)), rad(-90));
    EXPECT_DOUBLE_EQ(hrz::normalize_angle_around_zero(rad(-180)), rad(-180));
    EXPECT_DOUBLE_EQ(hrz::normalize_angle_around_zero(rad(-270)), rad(90));
    EXPECT_DOUBLE_EQ(hrz::normalize_angle_around_zero(rad(-360)), rad(0));
    EXPECT_DOUBLE_EQ(hrz::normalize_angle_around_zero(rad(-450)), rad(-90));
    EXPECT_DOUBLE_EQ(hrz::normalize_angle_around_zero(rad(450)), rad(90));
}

TEST(FndMaths, ClampAngle)
{
    static constexpr double eps = 0.00001;

    EXPECT_NEAR(hrz::clamp_angle(rad(40), rad(30), rad(60)), rad(40), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(30), rad(30), rad(60)), rad(30), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(60), rad(30), rad(60)), rad(60), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(20), rad(30), rad(60)), rad(30), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(80), rad(30), rad(60)), rad(60), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(80), rad(30), rad(60)), rad(60), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(225), rad(30), rad(60)), rad(30), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(-135), rad(30), rad(60)), rad(30), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(220), rad(30), rad(60)), rad(60), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(-140), rad(30), rad(60)), rad(60), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(230), rad(30), rad(60)), rad(30), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(-130), rad(30), rad(60)), rad(30), eps);

    EXPECT_NEAR(hrz::clamp_angle(rad(10), rad(-20), rad(20)), rad(10), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(-10), rad(-20), rad(20)), rad(-10), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(350), rad(-20), rad(20)), rad(350), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(20), rad(-20), rad(20)), rad(20), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(-20), rad(-20), rad(20)), rad(-20), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(340), rad(-20), rad(20)), rad(340), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(30), rad(-20), rad(20)), rad(20), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(330), rad(-20), rad(20)), rad(-20), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(180), rad(-20), rad(20)), rad(-20), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(-180), rad(-20), rad(20)), rad(-20), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(170), rad(-20), rad(20)), rad(20), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(-190), rad(-20), rad(20)), rad(20), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(-170), rad(-20), rad(20)), rad(-20), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(190), rad(-20), rad(20)), rad(-20), eps);

    EXPECT_NEAR(hrz::clamp_angle(rad(10), rad(340), rad(20)), rad(10), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(-10), rad(340), rad(20)), rad(-10), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(350), rad(340), rad(20)), rad(350), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(20), rad(340), rad(20)), rad(20), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(-20), rad(340), rad(20)), rad(-20), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(340), rad(340), rad(20)), rad(340), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(30), rad(340), rad(20)), rad(20), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(330), rad(340), rad(20)), rad(-20), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(180), rad(340), rad(20)), rad(-20), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(-180), rad(340), rad(20)), rad(-20), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(170), rad(340), rad(20)), rad(20), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(-190), rad(340), rad(20)), rad(20), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(-170), rad(340), rad(20)), rad(-20), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(190), rad(340), rad(20)), rad(-20), eps);

    EXPECT_NEAR(hrz::clamp_angle(rad(180), rad(160), rad(200)), rad(180), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(-180), rad(160), rad(200)), rad(-180), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(90), rad(160), rad(200)), rad(160), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(-90), rad(160), rad(200)), rad(-160), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(270), rad(160), rad(200)), rad(-160), eps);

    EXPECT_NEAR(hrz::clamp_angle(rad(180), rad(160), rad(-160)), rad(180), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(-180), rad(160), rad(-160)), rad(-180), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(90), rad(160), rad(-160)), rad(160), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(-90), rad(160), rad(-160)), rad(-160), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(270), rad(160), rad(-160)), rad(-160), eps);

    EXPECT_NEAR(hrz::clamp_angle(rad(90), rad(60), rad(30)), rad(90), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(-90), rad(60), rad(30)), rad(-90), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(40), rad(60), rad(30)), rad(30), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(50), rad(60), rad(30)), rad(60), eps);

    EXPECT_NEAR(hrz::clamp_angle(rad(45), rad(90), rad(-90)), rad(90), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(-45), rad(90), rad(-90)), rad(-90), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(135), rad(90), rad(-90)), rad(135), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(-135), rad(90), rad(-90)), rad(-135), eps);

    EXPECT_NEAR(hrz::clamp_angle(rad(45), rad(-90), rad(90)), rad(45), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(-45), rad(-90), rad(90)), rad(-45), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(135), rad(-90), rad(90)), rad(90), eps);
    EXPECT_NEAR(hrz::clamp_angle(rad(-135), rad(-90), rad(90)), rad(-90), eps);
}

TEST(FndMaths, AlignUpPo2)
{
    EXPECT_EQ(hrz::align_up_po2<uint32_t>(0U, 1U), 0U);
    EXPECT_EQ(hrz::align_up_po2<uint32_t>(0U, 2U), 0U);
    EXPECT_EQ(hrz::align_up_po2<uint32_t>(0U, 4U), 0U);
    EXPECT_EQ(hrz::align_up_po2<uint32_t>(0U, 8U), 0U);

    EXPECT_EQ(hrz::align_up_po2<uint32_t>(3U, 1U), 3U);
    EXPECT_EQ(hrz::align_up_po2<uint32_t>(3U, 2U), 4U);
    EXPECT_EQ(hrz::align_up_po2<uint32_t>(3U, 4U), 4U);
    EXPECT_EQ(hrz::align_up_po2<uint32_t>(3U, 8U), 8U);
}

TEST(FndMaths, AlignUpAny)
{
    EXPECT_EQ(hrz::align_up_any<uint32_t>(0U, 1U), 0U);
    EXPECT_EQ(hrz::align_up_any<uint32_t>(0U, 2U), 0U);
    EXPECT_EQ(hrz::align_up_any<uint32_t>(0U, 3U), 0U);
    EXPECT_EQ(hrz::align_up_any<uint32_t>(0U, 4U), 0U);
    EXPECT_EQ(hrz::align_up_any<uint32_t>(0U, 5U), 0U);
    EXPECT_EQ(hrz::align_up_any<uint32_t>(0U, 8U), 0U);

    EXPECT_EQ(hrz::align_up_any<uint32_t>(11U, 1U), 11U);
    EXPECT_EQ(hrz::align_up_any<uint32_t>(11U, 2U), 12U);
    EXPECT_EQ(hrz::align_up_any<uint32_t>(11U, 3U), 12U);
    EXPECT_EQ(hrz::align_up_any<uint32_t>(11U, 4U), 12U);
    EXPECT_EQ(hrz::align_up_any<uint32_t>(11U, 5U), 15U);
    EXPECT_EQ(hrz::align_up_any<uint32_t>(11U, 8U), 16U);
}
