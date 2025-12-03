#include <gtest/gtest.h>
#include <mycelium/types.h>

TEST(Float16, zero)
{
    EXPECT_EQ(0.0F, static_cast<float>(my::float16_t{0x0000}));
    EXPECT_EQ(-0.0F, static_cast<float>(my::float16_t{0x8000}));
}

TEST(Float16, subnormal)
{
    EXPECT_EQ(1.96695328e-05F, static_cast<float>(my::float16_t{0x014A}));
    EXPECT_EQ(6.09755516e-05F, static_cast<float>(my::float16_t{0x03FF}));
    EXPECT_EQ(5.96046448e-08F, static_cast<float>(my::float16_t{0x0001}));
    EXPECT_EQ(-2.51531601e-05F, static_cast<float>(my::float16_t{0x81A6}));
}

TEST(Float16, inf)
{
    EXPECT_TRUE(std::isinf(static_cast<float>(my::float16_t{0x7C00})));
    EXPECT_TRUE(static_cast<float>(my::float16_t{0x7C00}) > 0);
    EXPECT_TRUE(std::isinf(static_cast<float>(my::float16_t{0xFC00})));
    EXPECT_TRUE(static_cast<float>(my::float16_t{0xFC00}) < 0);
}

TEST(Float16, nan)
{
    EXPECT_TRUE(std::isnan(static_cast<float>(my::float16_t{0x7FFF})));
    EXPECT_TRUE(std::isnan(static_cast<float>(my::float16_t{0xFFFF})));
    EXPECT_TRUE(std::isnan(static_cast<float>(my::float16_t{0x7C01})));
    EXPECT_TRUE(std::isnan(static_cast<float>(my::float16_t{0xFC01})));
    EXPECT_TRUE(std::isnan(static_cast<float>(my::float16_t{0x7D51})));
}

TEST(Float16, normal)
{
    EXPECT_EQ(85.1875, static_cast<float>(my::float16_t{0x5553}));
    EXPECT_EQ(-5.44140625F, static_cast<float>(my::float16_t{0xC571}));
    EXPECT_EQ(0.000244021416F, static_cast<float>(my::float16_t{0x0BFF}));
    EXPECT_EQ(-0.125F, static_cast<float>(my::float16_t{0xB000}));
    EXPECT_EQ(65504.0F, static_cast<float>(my::float16_t{0x7BFF}));
    EXPECT_EQ(6.10351562e-05F, static_cast<float>(my::float16_t{0x0400}));
}
