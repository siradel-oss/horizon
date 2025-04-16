#include <hrz_common_image_processing.h>
#include <hrz_common_raster_sampling.h>
#include <hrz_fnd_defines.h>

#include <gtest/gtest.h>

#include <array>

namespace
{
using namespace hrz::sampling;

int32_t to_fixed_24_8(int32_t v)
{
    return v * 256;
}

uint32_t to_silicium(float v)
{
    return hrz::encode_float_to_r_f32_silicium(v);
}

uint32_t to_terrarium(float v)
{
    return hrz::encode_float_to_terrarium(v);
}

TEST(RasterSampling, nodata_ignore_imagery)
{
    std::array<uint8_t, 4> nodata_color = {64, 65, 66, 67};
    hrz_proto::NodataValue nodata_value;
    nodata_value.set_type(hrz_proto::NodataValueType::BIT_PATTERN_NODATA);
    nodata_value.set_bit_pattern(*(const uint32_t*)nodata_color.data());
    NodataFunction nodata(
        nodata_value, hrz_proto::NodataHandling::IGNORE_NODATA, hrz_proto::ImageFormat::SRGBA_8);

    {
        SCOPED_TRACE("Same value as nodata");
        std::array<uint8_t, 4> v = {64, 65, 66, 67};

        bool is_nodata = nodata.is_nodata<uint8_t, 4>(v);
        EXPECT_FALSE(is_nodata);

        PixelValue<uint8_t, 4> pixel{v, is_nodata};

        bool discard = nodata.apply<uint8_t, 4>(pixel);
        EXPECT_FALSE(discard);

        EXPECT_EQ(pixel.value[0], 64);
        EXPECT_EQ(pixel.value[1], 65);
        EXPECT_EQ(pixel.value[2], 66);
        EXPECT_EQ(pixel.value[3], 67);
    }

    {
        SCOPED_TRACE("Different value");
        std::array<uint8_t, 4> v = {1, 2, 3, 4};

        bool is_nodata = nodata.is_nodata<uint8_t, 4>(v);
        EXPECT_FALSE(is_nodata);

        PixelValue<uint8_t, 4> pixel{v, is_nodata};

        bool discard = nodata.apply<uint8_t, 4>(pixel);
        EXPECT_FALSE(discard);

        EXPECT_EQ(pixel.value[0], 1);
        EXPECT_EQ(pixel.value[1], 2);
        EXPECT_EQ(pixel.value[2], 3);
        EXPECT_EQ(pixel.value[3], 4);
    }

    {
        SCOPED_TRACE("First channel is nodata");
        std::array<uint8_t, 4> v = {64, 2, 3, 4};

        bool is_nodata = nodata.is_nodata<uint8_t, 4>(v);
        EXPECT_FALSE(is_nodata);

        PixelValue<uint8_t, 4> pixel{v, is_nodata};

        bool discard = nodata.apply<uint8_t, 4>(pixel);
        EXPECT_FALSE(discard);

        EXPECT_EQ(pixel.value[0], 64);
        EXPECT_EQ(pixel.value[1], 2);
        EXPECT_EQ(pixel.value[2], 3);
        EXPECT_EQ(pixel.value[3], 4);
    }

    {
        SCOPED_TRACE("Some channel is nodata");
        std::array<uint8_t, 4> v = {87, 2, 66, 4};

        bool is_nodata = nodata.is_nodata<uint8_t, 4>(v);
        EXPECT_FALSE(is_nodata);

        PixelValue<uint8_t, 4> pixel{v, is_nodata};

        bool discard = nodata.apply<uint8_t, 4>(pixel);
        EXPECT_FALSE(discard);

        EXPECT_EQ(pixel.value[0], 87);
        EXPECT_EQ(pixel.value[1], 2);
        EXPECT_EQ(pixel.value[2], 66);
        EXPECT_EQ(pixel.value[3], 4);
    }
}

TEST(RasterSampling, nodata_discard_imagery)
{
    std::array<uint8_t, 4> nodata_color = {64, 65, 66, 67};
    hrz_proto::NodataValue nodata_value;
    nodata_value.set_type(hrz_proto::NodataValueType::BIT_PATTERN_NODATA);
    nodata_value.set_bit_pattern(*(const uint32_t*)nodata_color.data());
    NodataFunction nodata(
        nodata_value, hrz_proto::NodataHandling::DISCARD_NODATA_PIXELS,
        hrz_proto::ImageFormat::SRGBA_8);

    {
        SCOPED_TRACE("Same value as nodata");
        std::array<uint8_t, 4> v = {64, 65, 66, 67};

        bool is_nodata = nodata.is_nodata<uint8_t, 4>(v);
        EXPECT_TRUE(is_nodata);

        PixelValue<uint8_t, 4> pixel{v, is_nodata};

        bool discard = nodata.apply<uint8_t, 4>(pixel);
        EXPECT_TRUE(discard);

        EXPECT_EQ(pixel.value[0], 64);
        EXPECT_EQ(pixel.value[1], 65);
        EXPECT_EQ(pixel.value[2], 66);
        EXPECT_EQ(pixel.value[3], 67);
    }

    {
        SCOPED_TRACE("Different value");
        std::array<uint8_t, 4> v = {1, 2, 3, 4};

        bool is_nodata = nodata.is_nodata<uint8_t, 4>(v);
        EXPECT_FALSE(is_nodata);

        PixelValue<uint8_t, 4> pixel{v, is_nodata};

        bool discard = nodata.apply<uint8_t, 4>(pixel);
        EXPECT_FALSE(discard);

        EXPECT_EQ(pixel.value[0], 1);
        EXPECT_EQ(pixel.value[1], 2);
        EXPECT_EQ(pixel.value[2], 3);
        EXPECT_EQ(pixel.value[3], 4);
    }

    {
        SCOPED_TRACE("First channel is nodata");
        std::array<uint8_t, 4> v = {64, 2, 3, 4};

        bool is_nodata = nodata.is_nodata<uint8_t, 4>(v);
        EXPECT_FALSE(is_nodata);

        PixelValue<uint8_t, 4> pixel{v, is_nodata};

        bool discard = nodata.apply<uint8_t, 4>(pixel);
        EXPECT_FALSE(discard);

        EXPECT_EQ(pixel.value[0], 64);
        EXPECT_EQ(pixel.value[1], 2);
        EXPECT_EQ(pixel.value[2], 3);
        EXPECT_EQ(pixel.value[3], 4);
    }

    {
        SCOPED_TRACE("Some channel is nodata");
        std::array<uint8_t, 4> v = {87, 2, 66, 4};

        bool is_nodata = nodata.is_nodata<uint8_t, 4>(v);
        EXPECT_FALSE(is_nodata);

        PixelValue<uint8_t, 4> pixel{v, is_nodata};

        bool discard = nodata.apply<uint8_t, 4>(pixel);
        EXPECT_FALSE(discard);

        EXPECT_EQ(pixel.value[0], 87);
        EXPECT_EQ(pixel.value[1], 2);
        EXPECT_EQ(pixel.value[2], 66);
        EXPECT_EQ(pixel.value[3], 4);
    }
}

TEST(RasterSampling, nodata_set_to_zero_imagery)
{
    std::array<uint8_t, 4> nodata_color = {64, 65, 66, 67};
    hrz_proto::NodataValue nodata_value;
    nodata_value.set_type(hrz_proto::NodataValueType::BIT_PATTERN_NODATA);
    nodata_value.set_bit_pattern(*(const uint32_t*)nodata_color.data());
    NodataFunction nodata(
        nodata_value, hrz_proto::NodataHandling::SET_NODATA_TO_ZERO,
        hrz_proto::ImageFormat::SRGBA_8);

    {
        SCOPED_TRACE("Same value as nodata");
        std::array<uint8_t, 4> v = {64, 65, 66, 67};

        bool is_nodata = nodata.is_nodata<uint8_t, 4>(v);
        EXPECT_TRUE(is_nodata);

        PixelValue<uint8_t, 4> pixel{v, is_nodata};

        bool discard = nodata.apply<uint8_t, 4>(pixel);
        EXPECT_FALSE(discard);

        EXPECT_EQ(pixel.value[0], 0);
        EXPECT_EQ(pixel.value[1], 0);
        EXPECT_EQ(pixel.value[2], 0);
        EXPECT_EQ(pixel.value[3], 0);
    }

    {
        SCOPED_TRACE("Different value");
        std::array<uint8_t, 4> v = {1, 2, 3, 4};

        bool is_nodata = nodata.is_nodata<uint8_t, 4>(v);
        EXPECT_FALSE(is_nodata);

        PixelValue<uint8_t, 4> pixel{v, is_nodata};

        bool discard = nodata.apply<uint8_t, 4>(pixel);
        EXPECT_FALSE(discard);

        EXPECT_EQ(pixel.value[0], 1);
        EXPECT_EQ(pixel.value[1], 2);
        EXPECT_EQ(pixel.value[2], 3);
        EXPECT_EQ(pixel.value[3], 4);
    }

    {
        SCOPED_TRACE("First channel is nodata");
        std::array<uint8_t, 4> v = {64, 2, 3, 4};

        bool is_nodata = nodata.is_nodata<uint8_t, 4>(v);
        EXPECT_FALSE(is_nodata);

        PixelValue<uint8_t, 4> pixel{v, is_nodata};

        bool discard = nodata.apply<uint8_t, 4>(pixel);
        EXPECT_FALSE(discard);

        EXPECT_EQ(pixel.value[0], 64);
        EXPECT_EQ(pixel.value[1], 2);
        EXPECT_EQ(pixel.value[2], 3);
        EXPECT_EQ(pixel.value[3], 4);
    }

    {
        SCOPED_TRACE("Some channel is nodata");
        std::array<uint8_t, 4> v = {87, 2, 66, 4};

        bool is_nodata = nodata.is_nodata<uint8_t, 4>(v);
        EXPECT_FALSE(is_nodata);

        PixelValue<uint8_t, 4> pixel{v, is_nodata};

        bool discard = nodata.apply<uint8_t, 4>(pixel);
        EXPECT_FALSE(discard);

        EXPECT_EQ(pixel.value[0], 87);
        EXPECT_EQ(pixel.value[1], 2);
        EXPECT_EQ(pixel.value[2], 66);
        EXPECT_EQ(pixel.value[3], 4);
    }
}

TEST(RasterSampling, color_nodata_set_to_zero_imagery)
{
    std::array<uint8_t, 4> nodata_color = {64, 65, 66, 67};
    hrz_proto::NodataValue nodata_value;
    nodata_value.set_type(hrz_proto::NodataValueType::COLOR_NODATA);
    nodata_value.mutable_color()->set_r((float)nodata_color[0] / 255.0f);
    nodata_value.mutable_color()->set_g((float)nodata_color[1] / 255.0f);
    nodata_value.mutable_color()->set_b((float)nodata_color[2] / 255.0f);
    nodata_value.mutable_color()->set_a((float)nodata_color[3] / 255.0f);
    NodataFunction nodata(
        nodata_value, hrz_proto::NodataHandling::SET_NODATA_TO_ZERO,
        hrz_proto::ImageFormat::SRGBA_8);

    {
        SCOPED_TRACE("Same value as nodata");
        std::array<uint8_t, 4> v = {64, 65, 66, 67};

        bool is_nodata = nodata.is_nodata<uint8_t, 4>(v);
        EXPECT_TRUE(is_nodata);

        PixelValue<uint8_t, 4> pixel{v, is_nodata};

        bool discard = nodata.apply<uint8_t, 4>(pixel);
        EXPECT_FALSE(discard);

        EXPECT_EQ(pixel.value[0], 0);
        EXPECT_EQ(pixel.value[1], 0);
        EXPECT_EQ(pixel.value[2], 0);
        EXPECT_EQ(pixel.value[3], 0);
    }

    {
        SCOPED_TRACE("Different value");
        std::array<uint8_t, 4> v = {1, 2, 3, 4};

        bool is_nodata = nodata.is_nodata<uint8_t, 4>(v);
        EXPECT_FALSE(is_nodata);

        PixelValue<uint8_t, 4> pixel{v, is_nodata};

        bool discard = nodata.apply<uint8_t, 4>(pixel);
        EXPECT_FALSE(discard);

        EXPECT_EQ(pixel.value[0], 1);
        EXPECT_EQ(pixel.value[1], 2);
        EXPECT_EQ(pixel.value[2], 3);
        EXPECT_EQ(pixel.value[3], 4);
    }

    {
        SCOPED_TRACE("First channel is nodata");
        std::array<uint8_t, 4> v = {64, 2, 3, 4};

        bool is_nodata = nodata.is_nodata<uint8_t, 4>(v);
        EXPECT_FALSE(is_nodata);

        PixelValue<uint8_t, 4> pixel{v, is_nodata};

        bool discard = nodata.apply<uint8_t, 4>(pixel);
        EXPECT_FALSE(discard);

        EXPECT_EQ(pixel.value[0], 64);
        EXPECT_EQ(pixel.value[1], 2);
        EXPECT_EQ(pixel.value[2], 3);
        EXPECT_EQ(pixel.value[3], 4);
    }

    {
        SCOPED_TRACE("Some channel is nodata");
        std::array<uint8_t, 4> v = {87, 2, 66, 4};

        bool is_nodata = nodata.is_nodata<uint8_t, 4>(v);
        EXPECT_FALSE(is_nodata);

        PixelValue<uint8_t, 4> pixel{v, is_nodata};

        bool discard = nodata.apply<uint8_t, 4>(pixel);
        EXPECT_FALSE(discard);

        EXPECT_EQ(pixel.value[0], 87);
        EXPECT_EQ(pixel.value[1], 2);
        EXPECT_EQ(pixel.value[2], 66);
        EXPECT_EQ(pixel.value[3], 4);
    }
}

TEST(RasterSampling, nodata_ignore_dtm)
{
    hrz_proto::NodataValue nodata_value;
    nodata_value.set_type(hrz_proto::NodataValueType::INT_VALUE_NODATA);
    nodata_value.set_int_value(984);
    NodataFunction nodata(
        nodata_value, hrz_proto::NodataHandling::IGNORE_NODATA,
        hrz_proto::ImageFormat::SIGNED_FIXED_24_8);

    {
        SCOPED_TRACE("Same value as nodata");
        std::array<int32_t, 1> v = {984};

        bool is_nodata = nodata.is_nodata<int32_t, 1>(v);
        EXPECT_FALSE(is_nodata);

        PixelValue<int32_t, 1> pixel{v, is_nodata};

        bool discard = nodata.apply<int32_t, 1>(pixel);
        EXPECT_FALSE(discard);

        EXPECT_EQ(pixel.value[0], 984);
    }

    {
        SCOPED_TRACE("Other value");
        std::array<int32_t, 1> v = {64};

        bool is_nodata = nodata.is_nodata<int32_t, 1>(v);
        EXPECT_FALSE(is_nodata);

        PixelValue<int32_t, 1> pixel{v, is_nodata};

        bool discard = nodata.apply<int32_t, 1>(pixel);
        EXPECT_FALSE(discard);

        EXPECT_EQ(pixel.value[0], 64);
    }
}

TEST(RasterSampling, nodata_discard_dtm)
{
    hrz_proto::NodataValue nodata_value;
    nodata_value.set_type(hrz_proto::NodataValueType::INT_VALUE_NODATA);
    nodata_value.set_int_value(984);
    NodataFunction nodata(
        nodata_value, hrz_proto::NodataHandling::DISCARD_NODATA_PIXELS,
        hrz_proto::ImageFormat::SIGNED_FIXED_24_8);

    {
        SCOPED_TRACE("Same value as nodata");
        std::array<int32_t, 1> v = {to_fixed_24_8(984)};

        bool is_nodata = nodata.is_nodata<int32_t, 1>(v);
        EXPECT_TRUE(is_nodata);

        PixelValue<int32_t, 1> pixel{v, is_nodata};

        bool discard = nodata.apply<int32_t, 1>(pixel);
        EXPECT_TRUE(discard);

        EXPECT_EQ(pixel.value[0], to_fixed_24_8(984));
    }

    {
        SCOPED_TRACE("Other value");
        std::array<int32_t, 1> v = {to_fixed_24_8(64)};

        bool is_nodata = nodata.is_nodata<int32_t, 1>(v);
        EXPECT_FALSE(is_nodata);

        PixelValue<int32_t, 1> pixel{v, is_nodata};

        bool discard = nodata.apply<int32_t, 1>(pixel);
        EXPECT_FALSE(discard);

        EXPECT_EQ(pixel.value[0], to_fixed_24_8(64));
    }
}

TEST(RasterSampling, nodata_set_to_zero_dtm)
{
    hrz_proto::NodataValue nodata_value;
    nodata_value.set_type(hrz_proto::NodataValueType::INT_VALUE_NODATA);
    nodata_value.set_int_value(984);
    NodataFunction nodata(
        nodata_value, hrz_proto::NodataHandling::SET_NODATA_TO_ZERO,
        hrz_proto::ImageFormat::SIGNED_FIXED_24_8);

    {
        SCOPED_TRACE("Same value as nodata");
        std::array<int32_t, 1> v = {to_fixed_24_8(984)};

        bool is_nodata = nodata.is_nodata<int32_t, 1>(v);
        EXPECT_TRUE(is_nodata);

        PixelValue<int32_t, 1> pixel{v, is_nodata};

        bool discard = nodata.apply<int32_t, 1>(pixel);
        EXPECT_FALSE(discard);

        EXPECT_EQ(pixel.value[0], 0);
    }

    {
        SCOPED_TRACE("Other value");
        std::array<int32_t, 1> v = {to_fixed_24_8(64)};

        bool is_nodata = nodata.is_nodata<int32_t, 1>(v);
        EXPECT_FALSE(is_nodata);

        PixelValue<int32_t, 1> pixel{v, is_nodata};

        bool discard = nodata.apply<int32_t, 1>(pixel);
        EXPECT_FALSE(discard);

        EXPECT_EQ(pixel.value[0], to_fixed_24_8(64));
    }
}

TEST(RasterSampling, bit_pattern_nodata_set_to_zero_dtm)
{
    hrz_proto::NodataValue nodata_value;
    nodata_value.set_type(hrz_proto::NodataValueType::BIT_PATTERN_NODATA);
    nodata_value.set_bit_pattern((uint32_t)984 << 8);
    NodataFunction nodata(
        nodata_value, hrz_proto::NodataHandling::SET_NODATA_TO_ZERO,
        hrz_proto::ImageFormat::SIGNED_FIXED_24_8);

    {
        SCOPED_TRACE("Same value as nodata");
        std::array<int32_t, 1> v = {to_fixed_24_8(984)};

        bool is_nodata = nodata.is_nodata<int32_t, 1>(v);
        EXPECT_TRUE(is_nodata);

        PixelValue<int32_t, 1> pixel{v, is_nodata};

        bool discard = nodata.apply<int32_t, 1>(pixel);
        EXPECT_FALSE(discard);

        EXPECT_EQ(pixel.value[0], 0);
    }

    {
        SCOPED_TRACE("Other value");
        std::array<int32_t, 1> v = {to_fixed_24_8(64)};

        bool is_nodata = nodata.is_nodata<int32_t, 1>(v);
        EXPECT_FALSE(is_nodata);

        PixelValue<int32_t, 1> pixel{v, is_nodata};

        bool discard = nodata.apply<int32_t, 1>(pixel);
        EXPECT_FALSE(discard);

        EXPECT_EQ(pixel.value[0], to_fixed_24_8(64));
    }
}

TEST(RasterSampling, float_nodata_set_to_zero_r_f32_dtm)
{
    hrz_proto::NodataValue nodata_value;
    nodata_value.set_type(hrz_proto::NodataValueType::FLOAT_VALUE_NODATA);
    nodata_value.set_float_value(-9999.9f);
    NodataFunction nodata(
        nodata_value, hrz_proto::NodataHandling::SET_NODATA_TO_ZERO, hrz_proto::ImageFormat::R_F32);

    {
        SCOPED_TRACE("Same value as nodata");
        std::array<float, 1> v = {-9999.9f};

        bool is_nodata = nodata.is_nodata<float, 1>(v);
        EXPECT_TRUE(is_nodata);

        PixelValue<float, 1> pixel{v, is_nodata};

        bool discard = nodata.apply<float, 1>(pixel);
        EXPECT_FALSE(discard);

        EXPECT_EQ(pixel.value[0], 0);
    }

    {
        SCOPED_TRACE("Other value");
        std::array<float, 1> v = {1234.56f};

        bool is_nodata = nodata.is_nodata<float, 1>(v);
        EXPECT_FALSE(is_nodata);

        PixelValue<float, 1> pixel{v, is_nodata};

        bool discard = nodata.apply<float, 1>(pixel);
        EXPECT_FALSE(discard);

        EXPECT_EQ(pixel.value[0], 1234.56f);
    }
}

TEST(RasterSampling, int_nodata_set_to_zero_r_f32_dtm)
{
    hrz_proto::NodataValue nodata_value;
    nodata_value.set_type(hrz_proto::NodataValueType::INT_VALUE_NODATA);
    nodata_value.set_int_value(-9999);
    NodataFunction nodata(
        nodata_value, hrz_proto::NodataHandling::SET_NODATA_TO_ZERO, hrz_proto::ImageFormat::R_F32);

    {
        SCOPED_TRACE("Same value as nodata");
        std::array<float, 1> v = {-9999.0f};

        bool is_nodata = nodata.is_nodata<float, 1>(v);
        EXPECT_TRUE(is_nodata);

        PixelValue<float, 1> pixel{v, is_nodata};

        bool discard = nodata.apply<float, 1>(pixel);
        EXPECT_FALSE(discard);

        EXPECT_EQ(pixel.value[0], 0);
    }

    {
        SCOPED_TRACE("Other value");
        std::array<float, 1> v = {1234.56f};

        bool is_nodata = nodata.is_nodata<float, 1>(v);
        EXPECT_FALSE(is_nodata);

        PixelValue<float, 1> pixel{v, is_nodata};

        bool discard = nodata.apply<float, 1>(pixel);
        EXPECT_FALSE(discard);

        EXPECT_EQ(pixel.value[0], 1234.56f);
    }
}

TEST(RasterSampling, bit_pattern_nodata_set_to_zero_r_f32_dtm)
{
    hrz_proto::NodataValue nodata_value;
    nodata_value.set_type(hrz_proto::NodataValueType::BIT_PATTERN_NODATA);
    nodata_value.set_bit_pattern(hrz::bit_cast<uint32_t>(-9999.9f));
    NodataFunction nodata(
        nodata_value, hrz_proto::NodataHandling::SET_NODATA_TO_ZERO, hrz_proto::ImageFormat::R_F32);

    {
        SCOPED_TRACE("Same value as nodata");
        std::array<float, 1> v = {-9999.9f};

        bool is_nodata = nodata.is_nodata<float, 1>(v);
        EXPECT_TRUE(is_nodata);

        PixelValue<float, 1> pixel{v, is_nodata};

        bool discard = nodata.apply<float, 1>(pixel);
        EXPECT_FALSE(discard);

        EXPECT_EQ(pixel.value[0], 0);
    }

    {
        SCOPED_TRACE("Other value");
        std::array<float, 1> v = {1234.56f};

        bool is_nodata = nodata.is_nodata<float, 1>(v);
        EXPECT_FALSE(is_nodata);

        PixelValue<float, 1> pixel{v, is_nodata};

        bool discard = nodata.apply<float, 1>(pixel);
        EXPECT_FALSE(discard);

        EXPECT_EQ(pixel.value[0], 1234.56f);
    }
}

TEST(RasterSampling, nan_nodata_set_to_zero_r_f32_dtm)
{
    hrz_proto::NodataValue nodata_value;
    nodata_value.set_type(hrz_proto::NodataValueType::NAN_NODATA);
    NodataFunction nodata(
        nodata_value, hrz_proto::NodataHandling::SET_NODATA_TO_ZERO, hrz_proto::ImageFormat::R_F32);

    {
        SCOPED_TRACE("Same value as nodata");
        std::array<float, 1> v = {std::numeric_limits<float>::quiet_NaN()};

        bool is_nodata = nodata.is_nodata<float, 1>(v);
        EXPECT_TRUE(is_nodata);

        PixelValue<float, 1> pixel{v, is_nodata};

        bool discard = nodata.apply<float, 1>(pixel);
        EXPECT_FALSE(discard);

        EXPECT_EQ(pixel.value[0], 0);
    }

    {
        SCOPED_TRACE("Other value");
        std::array<float, 1> v = {1234.56f};

        bool is_nodata = nodata.is_nodata<float, 1>(v);
        EXPECT_FALSE(is_nodata);

        PixelValue<float, 1> pixel{v, is_nodata};

        bool discard = nodata.apply<float, 1>(pixel);
        EXPECT_FALSE(discard);

        EXPECT_EQ(pixel.value[0], 1234.56f);
    }
}

TEST(RasterSampling, float_nodata_set_to_zero_r_f32_silicium_dtm)
{
    hrz_proto::NodataValue nodata_value;
    nodata_value.set_type(hrz_proto::NodataValueType::FLOAT_VALUE_NODATA);
    nodata_value.set_float_value(-9999.9f);
    NodataFunction nodata(
        nodata_value, hrz_proto::NodataHandling::SET_NODATA_TO_ZERO,
        hrz_proto::ImageFormat::R_F32_SILICIUM);

    {
        SCOPED_TRACE("Same value as nodata");
        std::array<uint32_t, 1> v = {to_silicium(-9999.9f)};

        bool is_nodata = nodata.is_nodata<uint32_t, 1>(v);
        EXPECT_TRUE(is_nodata);

        PixelValue<uint32_t, 1> pixel{v, is_nodata};

        bool discard = nodata.apply<uint32_t, 1>(pixel);
        EXPECT_FALSE(discard);

        EXPECT_EQ(pixel.value[0], 0);
    }

    {
        SCOPED_TRACE("Other value");
        std::array<uint32_t, 1> v = {to_silicium(1234.56f)};

        bool is_nodata = nodata.is_nodata<uint32_t, 1>(v);
        EXPECT_FALSE(is_nodata);

        PixelValue<uint32_t, 1> pixel{v, is_nodata};

        bool discard = nodata.apply<uint32_t, 1>(pixel);
        EXPECT_FALSE(discard);

        EXPECT_EQ(pixel.value[0], to_silicium(1234.56f));
    }
}

TEST(RasterSampling, int_nodata_set_to_zero_r_f32_silicium_dtm)
{
    hrz_proto::NodataValue nodata_value;
    nodata_value.set_type(hrz_proto::NodataValueType::INT_VALUE_NODATA);
    nodata_value.set_int_value(-9999);
    NodataFunction nodata(
        nodata_value, hrz_proto::NodataHandling::SET_NODATA_TO_ZERO,
        hrz_proto::ImageFormat::R_F32_SILICIUM);

    {
        SCOPED_TRACE("Same value as nodata");
        std::array<uint32_t, 1> v = {to_silicium(-9999.0f)};

        bool is_nodata = nodata.is_nodata<uint32_t, 1>(v);
        EXPECT_TRUE(is_nodata);

        PixelValue<uint32_t, 1> pixel{v, is_nodata};

        bool discard = nodata.apply<uint32_t, 1>(pixel);
        EXPECT_FALSE(discard);

        EXPECT_EQ(pixel.value[0], 0);
    }

    {
        SCOPED_TRACE("Other value");
        std::array<uint32_t, 1> v = {to_silicium(1234.56f)};

        bool is_nodata = nodata.is_nodata<uint32_t, 1>(v);
        EXPECT_FALSE(is_nodata);

        PixelValue<uint32_t, 1> pixel{v, is_nodata};

        bool discard = nodata.apply<uint32_t, 1>(pixel);
        EXPECT_FALSE(discard);

        EXPECT_EQ(pixel.value[0], to_silicium(1234.56f));
    }
}

TEST(RasterSampling, bit_pattern_nodata_set_to_zero_r_f32_silicium_dtm)
{
    hrz_proto::NodataValue nodata_value;
    nodata_value.set_type(hrz_proto::NodataValueType::BIT_PATTERN_NODATA);
    nodata_value.set_bit_pattern(hrz::bit_cast<uint32_t>(to_silicium(-9999.9f)));
    NodataFunction nodata(
        nodata_value, hrz_proto::NodataHandling::SET_NODATA_TO_ZERO,
        hrz_proto::ImageFormat::R_F32_SILICIUM);

    {
        SCOPED_TRACE("Same value as nodata");
        std::array<uint32_t, 1> v = {to_silicium(-9999.9f)};

        bool is_nodata = nodata.is_nodata<uint32_t, 1>(v);
        EXPECT_TRUE(is_nodata);

        PixelValue<uint32_t, 1> pixel{v, is_nodata};

        bool discard = nodata.apply<uint32_t, 1>(pixel);
        EXPECT_FALSE(discard);

        EXPECT_EQ(pixel.value[0], 0);
    }

    {
        SCOPED_TRACE("Other value");
        std::array<uint32_t, 1> v = {to_silicium(1234.56f)};

        bool is_nodata = nodata.is_nodata<uint32_t, 1>(v);
        EXPECT_FALSE(is_nodata);

        PixelValue<uint32_t, 1> pixel{v, is_nodata};

        bool discard = nodata.apply<uint32_t, 1>(pixel);
        EXPECT_FALSE(discard);

        EXPECT_EQ(pixel.value[0], to_silicium(1234.56f));
    }
}

TEST(RasterSampling, nan_nodata_set_to_zero_r_f32_silicium_dtm)
{
    hrz_proto::NodataValue nodata_value;
    nodata_value.set_type(hrz_proto::NodataValueType::NAN_NODATA);
    NodataFunction nodata(
        nodata_value, hrz_proto::NodataHandling::SET_NODATA_TO_ZERO,
        hrz_proto::ImageFormat::R_F32_SILICIUM);

    {
        SCOPED_TRACE("Same value as nodata");
        std::array<uint32_t, 1> v = {to_silicium(std::numeric_limits<float>::quiet_NaN())};

        bool is_nodata = nodata.is_nodata<uint32_t, 1>(v);
        EXPECT_TRUE(is_nodata);

        PixelValue<uint32_t, 1> pixel{v, is_nodata};

        bool discard = nodata.apply<uint32_t, 1>(pixel);
        EXPECT_FALSE(discard);

        EXPECT_EQ(pixel.value[0], 0);
    }

    {
        SCOPED_TRACE("Other value");
        std::array<uint32_t, 1> v = {to_silicium(1234.56f)};

        bool is_nodata = nodata.is_nodata<uint32_t, 1>(v);
        EXPECT_FALSE(is_nodata);

        PixelValue<uint32_t, 1> pixel{v, is_nodata};

        bool discard = nodata.apply<uint32_t, 1>(pixel);
        EXPECT_FALSE(discard);

        EXPECT_EQ(pixel.value[0], to_silicium(1234.56f));
    }
}

TEST(RasterSampling, float_nodata_set_to_zero_terrarium_dtm)
{
    hrz_proto::NodataValue nodata_value;
    nodata_value.set_type(hrz_proto::NodataValueType::FLOAT_VALUE_NODATA);
    nodata_value.set_float_value(-9999.0f);
    NodataFunction nodata(
        nodata_value, hrz_proto::NodataHandling::SET_NODATA_TO_ZERO,
        hrz_proto::ImageFormat::TERRARIUM);

    {
        SCOPED_TRACE("Same value as nodata");
        std::array<uint32_t, 1> v = {to_terrarium(-9999.0f)};

        bool is_nodata = nodata.is_nodata<uint32_t, 1>(v);
        EXPECT_TRUE(is_nodata);

        PixelValue<uint32_t, 1> pixel{v, is_nodata};

        bool discard = nodata.apply<uint32_t, 1>(pixel);
        EXPECT_FALSE(discard);

        EXPECT_EQ(pixel.value[0], 0);
    }

    {
        SCOPED_TRACE("Other value");
        std::array<uint32_t, 1> v = {to_terrarium(1234.56f)};

        bool is_nodata = nodata.is_nodata<uint32_t, 1>(v);
        EXPECT_FALSE(is_nodata);

        PixelValue<uint32_t, 1> pixel{v, is_nodata};

        bool discard = nodata.apply<uint32_t, 1>(pixel);
        EXPECT_FALSE(discard);

        EXPECT_EQ(pixel.value[0], to_terrarium(1234.56f));
    }
}

TEST(RasterSampling, int_nodata_set_to_zero_terrarium_dtm)
{
    hrz_proto::NodataValue nodata_value;
    nodata_value.set_type(hrz_proto::NodataValueType::INT_VALUE_NODATA);
    nodata_value.set_int_value(-9999);
    NodataFunction nodata(
        nodata_value, hrz_proto::NodataHandling::SET_NODATA_TO_ZERO,
        hrz_proto::ImageFormat::TERRARIUM);

    {
        SCOPED_TRACE("Same value as nodata");
        std::array<uint32_t, 1> v = {to_terrarium(-9999.0f)};

        bool is_nodata = nodata.is_nodata<uint32_t, 1>(v);
        EXPECT_TRUE(is_nodata);

        PixelValue<uint32_t, 1> pixel{v, is_nodata};

        bool discard = nodata.apply<uint32_t, 1>(pixel);
        EXPECT_FALSE(discard);

        EXPECT_EQ(pixel.value[0], 0);
    }

    {
        SCOPED_TRACE("Other value");
        std::array<uint32_t, 1> v = {to_terrarium(1234.56f)};

        bool is_nodata = nodata.is_nodata<uint32_t, 1>(v);
        EXPECT_FALSE(is_nodata);

        PixelValue<uint32_t, 1> pixel{v, is_nodata};

        bool discard = nodata.apply<uint32_t, 1>(pixel);
        EXPECT_FALSE(discard);

        EXPECT_EQ(pixel.value[0], to_terrarium(1234.56f));
    }
}

TEST(RasterSampling, bit_pattern_nodata_set_to_zero_terrarium_dtm)
{
    hrz_proto::NodataValue nodata_value;
    nodata_value.set_type(hrz_proto::NodataValueType::BIT_PATTERN_NODATA);
    nodata_value.set_bit_pattern(hrz::bit_cast<uint32_t>(to_terrarium(-9999.0f)));
    NodataFunction nodata(
        nodata_value, hrz_proto::NodataHandling::SET_NODATA_TO_ZERO,
        hrz_proto::ImageFormat::TERRARIUM);

    {
        SCOPED_TRACE("Same value as nodata");
        std::array<uint32_t, 1> v = {to_terrarium(-9999.0f)};

        bool is_nodata = nodata.is_nodata<uint32_t, 1>(v);
        EXPECT_TRUE(is_nodata);

        PixelValue<uint32_t, 1> pixel{v, is_nodata};

        bool discard = nodata.apply<uint32_t, 1>(pixel);
        EXPECT_FALSE(discard);

        EXPECT_EQ(pixel.value[0], 0);
    }

    {
        SCOPED_TRACE("Other value");
        std::array<uint32_t, 1> v = {to_terrarium(1234.56f)};

        bool is_nodata = nodata.is_nodata<uint32_t, 1>(v);
        EXPECT_FALSE(is_nodata);

        PixelValue<uint32_t, 1> pixel{v, is_nodata};

        bool discard = nodata.apply<uint32_t, 1>(pixel);
        EXPECT_FALSE(discard);

        EXPECT_EQ(pixel.value[0], to_terrarium(1234.56f));
    }
}

TEST(RasterSampling, bit_pattern_nodata_set_to_zero_terrarium_dtm_extra_bits)
{
    hrz_proto::NodataValue nodata_value;
    nodata_value.set_type(hrz_proto::NodataValueType::BIT_PATTERN_NODATA);
    nodata_value.set_bit_pattern(hrz::bit_cast<uint32_t>(to_terrarium(-9999.0f)) + 0xff000000);
    NodataFunction nodata(
        nodata_value, hrz_proto::NodataHandling::SET_NODATA_TO_ZERO,
        hrz_proto::ImageFormat::TERRARIUM);

    {
        SCOPED_TRACE("Same value as nodata");
        std::array<uint32_t, 1> v = {to_terrarium(-9999.0f)};

        bool is_nodata = nodata.is_nodata<uint32_t, 1>(v);
        EXPECT_TRUE(is_nodata);

        PixelValue<uint32_t, 1> pixel{v, is_nodata};

        bool discard = nodata.apply<uint32_t, 1>(pixel);
        EXPECT_FALSE(discard);

        EXPECT_EQ(pixel.value[0], 0);
    }

    {
        SCOPED_TRACE("Other value");
        std::array<uint32_t, 1> v = {to_terrarium(1234.56f)};

        bool is_nodata = nodata.is_nodata<uint32_t, 1>(v);
        EXPECT_FALSE(is_nodata);

        PixelValue<uint32_t, 1> pixel{v, is_nodata};

        bool discard = nodata.apply<uint32_t, 1>(pixel);
        EXPECT_FALSE(discard);

        EXPECT_EQ(pixel.value[0], to_terrarium(1234.56f));
    }
}

TEST(RasterSampling, sampling_nearest)
{
    hrz_proto::NodataValue nodata_value;
    nodata_value.set_type(hrz_proto::NodataValueType::INT_VALUE_NODATA);
    nodata_value.set_int_value(0);
    NodataFunction nodata(
        nodata_value, hrz_proto::NodataHandling::IGNORE_NODATA,
        hrz_proto::ImageFormat::SIGNED_FIXED_24_8);
    DtmSamplingFunction sampling(
        fetch_signed_fixed_24_8_pixel, hrz_proto::AlphaChannelUsage::IGNORE_ALPHA_CHANNEL,
        std::move(nodata), hrz_proto::TextureFiltering::NEAREST);

    const int32_t contents[] = {to_fixed_24_8(-100), to_fixed_24_8(-300), to_fixed_24_8(400),
                                to_fixed_24_8(100),  to_fixed_24_8(200),  to_fixed_24_8(500)};

    hrz::ImageView img(
        {(std::byte*)contents, 3 * 2 * sizeof(int32_t)}, hrz_proto::ImageFormat::SIGNED_FIXED_24_8,
        3, 2);

    auto sample = [&](float x, float y)
    {
        float v;
        sampling.sample(img, lm::vec2(x, y), &v);
        return v;
    };

    EXPECT_EQ(-100, sample(0.0, 0.0));
    EXPECT_EQ(-300, sample(1.0 / 3.0, 0.0));
    EXPECT_EQ(400, sample(2.0 / 3.0, 0.0));
    EXPECT_EQ(400, sample(1.0, 0.0));

    EXPECT_EQ(-100, sample(0.1, 0.1));
    EXPECT_EQ(-300, sample(1.0 / 3.0 + 0.1, 0.1));
    EXPECT_EQ(400, sample(2.0 / 3.0 + 0.1, 0.1));

    EXPECT_EQ(100, sample(0.0, 0.5));
    EXPECT_EQ(200, sample(1.0 / 3.0, 0.5));
    EXPECT_EQ(500, sample(2.0 / 3.0, 0.5));
    EXPECT_EQ(500, sample(1.0, 0.5));

    EXPECT_EQ(100, sample(0.0, 1.0));
    EXPECT_EQ(200, sample(1.0 / 3.0, 1.0));
    EXPECT_EQ(500, sample(2.0 / 3.0, 1.0));
    EXPECT_EQ(500, sample(1.0, 1.0));
}

TEST(RasterSampling, sampling_bilinear)
{
    hrz_proto::NodataValue nodata_value;
    nodata_value.set_type(hrz_proto::NodataValueType::INT_VALUE_NODATA);
    nodata_value.set_int_value(0);
    NodataFunction nodata(
        nodata_value, hrz_proto::NodataHandling::IGNORE_NODATA,
        hrz_proto::ImageFormat::SIGNED_FIXED_24_8);
    DtmSamplingFunction sampling(
        fetch_signed_fixed_24_8_pixel, hrz_proto::AlphaChannelUsage::IGNORE_ALPHA_CHANNEL,
        std::move(nodata), hrz_proto::TextureFiltering::BILINEAR);

    const int32_t contents[] = {to_fixed_24_8(-100), to_fixed_24_8(-300), to_fixed_24_8(400),
                                to_fixed_24_8(100),  to_fixed_24_8(200),  to_fixed_24_8(500)};

    hrz::ImageView img(
        {(std::byte*)contents, 3 * 2 * sizeof(int32_t)}, hrz_proto::ImageFormat::SIGNED_FIXED_24_8,
        3, 2);

    auto sample = [&](float x, float y)
    {
        float v;
        sampling.sample(img, lm::vec2(x, y), &v);
        return v;
    };

    EXPECT_FLOAT_EQ(-100, sample(0.0, 0.0));
    EXPECT_FLOAT_EQ(-100, sample(0.1666666, 0.0));
    EXPECT_FLOAT_EQ(-200, sample(0.3333333, 0.0));
    EXPECT_FLOAT_EQ(-300, sample(0.5, 0.0));
    EXPECT_FLOAT_EQ(50, sample(0.6666667, 0.0));
    EXPECT_FLOAT_EQ(400, sample(0.8333333, 0.0));
    EXPECT_FLOAT_EQ(400, sample(1.0, 0.0));

    EXPECT_FLOAT_EQ(-100, sample(0.0, 0.25));
    EXPECT_FLOAT_EQ(-100, sample(0.1666666, 0.25));
    EXPECT_FLOAT_EQ(-200, sample(0.3333333, 0.25));
    EXPECT_FLOAT_EQ(-300, sample(0.5, 0.25));
    EXPECT_FLOAT_EQ(50, sample(0.6666667, 0.25));
    EXPECT_FLOAT_EQ(400, sample(0.8333333, 0.25));
    EXPECT_FLOAT_EQ(400, sample(1.0, 0.25));

    EXPECT_FLOAT_EQ(0, sample(0.0, 0.5));
    EXPECT_FLOAT_EQ(0, sample(0.1666666, 0.5));
    EXPECT_FLOAT_EQ(-25, sample(0.3333333, 0.5));
    EXPECT_FLOAT_EQ(-50, sample(0.5, 0.5));
    EXPECT_FLOAT_EQ(200, sample(0.6666667, 0.5));
    EXPECT_FLOAT_EQ(450, sample(0.8333333, 0.5));
    EXPECT_FLOAT_EQ(450, sample(1.0, 0.5));

    EXPECT_FLOAT_EQ(100, sample(0.0, 0.75));
    EXPECT_FLOAT_EQ(100, sample(0.1666666, 0.75));
    EXPECT_FLOAT_EQ(150, sample(0.3333334, 0.75));
    EXPECT_FLOAT_EQ(200, sample(0.5, 0.75));
    EXPECT_FLOAT_EQ(350, sample(0.6666667, 0.75));
    EXPECT_FLOAT_EQ(500, sample(0.8333333, 0.75));
    EXPECT_FLOAT_EQ(500, sample(1.0, 0.75));

    EXPECT_FLOAT_EQ(100, sample(0.0, 1.0));
    EXPECT_FLOAT_EQ(100, sample(0.1666666, 1.0));
    EXPECT_FLOAT_EQ(150, sample(0.3333334, 1.0));
    EXPECT_FLOAT_EQ(200, sample(0.5, 1.0));
    EXPECT_FLOAT_EQ(350, sample(0.6666667, 1.0));
    EXPECT_FLOAT_EQ(500, sample(0.8333333, 1.0));
    EXPECT_FLOAT_EQ(500, sample(1.0, 1.0));
}

TEST(RasterSampling, sampling_nearest_nodata)
{
    hrz_proto::NodataValue nodata_value;
    nodata_value.set_type(hrz_proto::NodataValueType::INT_VALUE_NODATA);
    nodata_value.set_int_value(-9999);
    NodataFunction nodata(
        nodata_value, hrz_proto::NodataHandling::SET_NODATA_TO_ZERO,
        hrz_proto::ImageFormat::SIGNED_FIXED_24_8);
    DtmSamplingFunction sampling(
        fetch_signed_fixed_24_8_pixel, hrz_proto::AlphaChannelUsage::IGNORE_ALPHA_CHANNEL,
        std::move(nodata), hrz_proto::TextureFiltering::NEAREST);

    const int32_t contents[] = {
        to_fixed_24_8(100), to_fixed_24_8(-9999), to_fixed_24_8(-9999), to_fixed_24_8(-9999)};

    hrz::ImageView img(
        {(std::byte*)contents, 2 * 2 * sizeof(int32_t)}, hrz_proto::ImageFormat::SIGNED_FIXED_24_8,
        2, 2);

    auto sample = [&](float x, float y)
    {
        float v;
        sampling.sample(img, lm::vec2(x / 2, y / 2), &v);
        return v;
    };

    EXPECT_EQ(100, sample(0.2, 0.9));
    EXPECT_EQ(0, sample(0, 1.5));
    EXPECT_EQ(0, sample(0.2, 1.5));
    EXPECT_EQ(0, sample(0.7, 1.5));
    EXPECT_EQ(0, sample(1.0, 1.5));
    EXPECT_EQ(0, sample(1.2, 1.5));
}

// x = nodata
// --------
// |      |
// | xxx  |
// | x x  |
// | xxx  |
// |      |
// |x xxx |
// | x xx |
// --------
// This image gives us all possible 2x2 neighborhoods with nodata values.

TEST(RasterSampling, sampling_bilinear_nodata)
{
    const int32_t ndv = -9999;
    hrz_proto::NodataValue nodata_value;
    nodata_value.set_type(hrz_proto::NodataValueType::INT_VALUE_NODATA);
    nodata_value.set_int_value(ndv);
    NodataFunction nodata(
        nodata_value, hrz_proto::NodataHandling::SET_NODATA_TO_ZERO,
        hrz_proto::ImageFormat::SIGNED_FIXED_24_8);
    DtmSamplingFunction sampling(
        fetch_signed_fixed_24_8_pixel, hrz_proto::AlphaChannelUsage::IGNORE_ALPHA_CHANNEL,
        std::move(nodata), hrz_proto::TextureFiltering::BILINEAR);

    std::array<int32_t, 42> contents = {100, 200, 300, 400, 500, 600, 200, ndv, ndv, ndv, 300,
                                        400, 300, ndv, 400, ndv, 500, 600, 400, ndv, ndv, ndv,
                                        100, 200, 100, 200, 300, 400, 500, 600, ndv, 300, ndv,
                                        ndv, ndv, 400, 100, ndv, 200, ndv, ndv, 300};
    for (int32_t& v : contents)
    {
        v = to_fixed_24_8(v);
    }

    hrz::ImageView img(
        {(std::byte*)contents.data(), 6 * 7 * sizeof(int32_t)},
        hrz_proto::ImageFormat::SIGNED_FIXED_24_8, 6, 7);

    auto sample = [&](float x, float y)
    {
        float v;
        sampling.sample(img, lm::vec2((x + 0.5) / 6, (y + 0.5) / 7), &v);
        return v;
    };

    {
        SCOPED_TRACE("no valid value");
        const float x = 3;
        const float y = 5;
        EXPECT_EQ(0, sample(x + 0.0, y + 0.0));
        EXPECT_EQ(0, sample(x + 0.5, y + 0.0));
        EXPECT_EQ(0, sample(x + 0.0, y + 0.5));
        EXPECT_EQ(0, sample(x + 0.5, y + 0.5));
    }

    {
        SCOPED_TRACE("1 valid value, top-right");
        const float x = 1;
        const float y = 2;
        EXPECT_EQ(400, sample(x + 0.0, y + 0.0));
        EXPECT_EQ(400, sample(x + 0.5, y + 0.0));
        EXPECT_EQ(400, sample(x + 0.0, y + 0.5));
        EXPECT_EQ(400, sample(x + 0.5, y + 0.5));
    }

    {
        SCOPED_TRACE("1 valid value, top-left");
        const float x = 2;
        const float y = 2;
        EXPECT_EQ(400, sample(x + 0.0, y + 0.0));
        EXPECT_EQ(400, sample(x + 0.5, y + 0.0));
        EXPECT_EQ(400, sample(x + 0.0, y + 0.5));
        EXPECT_EQ(400, sample(x + 0.5, y + 0.5));
    }

    {
        SCOPED_TRACE("1 valid value, bottom-right");
        const float x = 1;
        const float y = 1;
        EXPECT_EQ(400, sample(x + 0.0, y + 0.0));
        EXPECT_EQ(400, sample(x + 0.5, y + 0.0));
        EXPECT_EQ(400, sample(x + 0.0, y + 0.5));
        EXPECT_EQ(400, sample(x + 0.5, y + 0.5));
    }

    {
        SCOPED_TRACE("1 valid value, bottom-left");
        const float x = 2;
        const float y = 1;
        EXPECT_EQ(400, sample(x + 0.0, y + 0.0));
        EXPECT_EQ(400, sample(x + 0.5, y + 0.0));
        EXPECT_EQ(400, sample(x + 0.0, y + 0.5));
        EXPECT_EQ(400, sample(x + 0.5, y + 0.5));
    }

    {
        SCOPED_TRACE("2 valid values, top");
        const float x = 1;
        const float y = 0;
        EXPECT_EQ(200, sample(x + 0.0, y + 0.0));
        EXPECT_EQ(250, sample(x + 0.5, y + 0.0));
        EXPECT_EQ(200, sample(x + 0.0, y + 0.5));
        EXPECT_EQ(250, sample(x + 0.5, y + 0.5));
    }

    {
        SCOPED_TRACE("2 valid values, bottom");
        const float x = 1;
        const float y = 3;
        EXPECT_EQ(200, sample(x + 0.0, y + 0.0));
        EXPECT_EQ(250, sample(x + 0.5, y + 0.0));
        EXPECT_EQ(200, sample(x + 0.0, y + 0.5));
        EXPECT_EQ(250, sample(x + 0.5, y + 0.5));
    }

    {
        SCOPED_TRACE("2 valid values, left");
        const float x = 0;
        const float y = 1;
        EXPECT_EQ(200, sample(x + 0.0, y + 0.0));
        EXPECT_EQ(200, sample(x + 0.5, y + 0.0));
        EXPECT_EQ(250, sample(x + 0.0, y + 0.5));
        EXPECT_EQ(250, sample(x + 0.5, y + 0.5));
    }

    {
        SCOPED_TRACE("2 valid values, right");
        const float x = 3;
        const float y = 1;
        EXPECT_EQ(300, sample(x + 0.0, y + 0.0));
        EXPECT_EQ(300, sample(x + 0.5, y + 0.0));
        EXPECT_EQ(400, sample(x + 0.0, y + 0.5));
        EXPECT_EQ(400, sample(x + 0.5, y + 0.5));
    }

    {
        SCOPED_TRACE("2 valid values, diagonal 1");
        const float x = 0;
        const float y = 5;
        EXPECT_EQ(300, sample(x + 0.0, y + 0.0));
        EXPECT_EQ(300, sample(x + 0.5, y + 0.0));
        EXPECT_EQ(200, sample(x + 0.0, y + 0.5));
        EXPECT_EQ(200, sample(x + 0.5, y + 0.5));
    }

    {
        SCOPED_TRACE("2 valid values, diagonal 2");
        const float x = 1;
        const float y = 5;
        EXPECT_EQ(300, sample(x + 0.0, y + 0.0));
        EXPECT_EQ(300, sample(x + 0.5, y + 0.0));
        EXPECT_EQ(250, sample(x + 0.0, y + 0.5));
        EXPECT_EQ(250, sample(x + 0.5, y + 0.5));
    }

    {
        SCOPED_TRACE("3 valid values, nodata top-left");
        const float x = 3;
        const float y = 3;
        EXPECT_EQ(100, sample(x + 0.0, y + 0.0));
        EXPECT_EQ(100, sample(x + 0.5, y + 0.0));
        EXPECT_EQ(250, sample(x + 0.0, y + 0.5));
        EXPECT_EQ(275, sample(x + 0.5, y + 0.5));
    }

    {
        SCOPED_TRACE("3 valid values, nodata top-right");
        const float x = 0;
        const float y = 3;
        EXPECT_EQ(400, sample(x + 0.0, y + 0.0));
        EXPECT_EQ(400, sample(x + 0.5, y + 0.0));
        EXPECT_EQ(250, sample(x + 0.0, y + 0.5));
        EXPECT_EQ(275, sample(x + 0.5, y + 0.5));
    }

    {
        SCOPED_TRACE("3 valid values, nodata bottom-left");
        const float x = 3;
        const float y = 0;
        EXPECT_EQ(400, sample(x + 0.0, y + 0.0));
        EXPECT_EQ(450, sample(x + 0.5, y + 0.0));
        EXPECT_EQ(350, sample(x + 0.0, y + 0.5));
        EXPECT_EQ(375, sample(x + 0.5, y + 0.5));
    }

    {
        SCOPED_TRACE("3 valid values, nodata bottom-right");
        const float x = 0;
        const float y = 0;
        EXPECT_EQ(100, sample(x + 0.0, y + 0.0));
        EXPECT_EQ(150, sample(x + 0.5, y + 0.0));
        EXPECT_EQ(150, sample(x + 0.0, y + 0.5));
        EXPECT_EQ(175, sample(x + 0.5, y + 0.5));
    }

    {
        SCOPED_TRACE("4 valid values");
        const float x = 4;
        const float y = 2;
        EXPECT_EQ(500, sample(x + 0.0, y + 0.0));
        EXPECT_EQ(550, sample(x + 0.5, y + 0.0));
        EXPECT_EQ(300, sample(x + 0.0, y + 0.5));
        EXPECT_EQ(350, sample(x + 0.5, y + 0.5));
    }
}

TEST(RasterSampling, sampling_alpha_ignore)
{
    hrz_proto::NodataValue nodata_value;
    nodata_value.set_type(hrz_proto::NodataValueType::BIT_PATTERN_NODATA);
    nodata_value.set_bit_pattern(0);
    NodataFunction nodata(
        nodata_value, hrz_proto::NodataHandling::IGNORE_NODATA, hrz_proto::ImageFormat::SRGBA_8);
    ImagerySamplingFunction sampling(
        fetch_rgba8_pixel, hrz_proto::AlphaChannelUsage::IGNORE_ALPHA_CHANNEL, std::move(nodata),
        hrz_proto::TextureFiltering::NEAREST);

    const uint8_t contents[] = {
        255, 0, 0, 255, 0, 255, 0, 127, 0, 0, 255, 0,
    };

    hrz::ImageView img(
        {(std::byte*)contents, 3 * 1 * 4 * sizeof(uint8_t)}, hrz_proto::ImageFormat::SRGBA_8, 3, 1);

    auto sample_compare = [&](float x, float y, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        std::array<uint8_t, 4> v;
        sampling.sample(img, lm::vec2(x / 3, y), v.data());
        EXPECT_EQ(r, v[0]);
        EXPECT_EQ(g, v[1]);
        EXPECT_EQ(b, v[2]);
        EXPECT_EQ(a, v[3]);
    };

    {
        SCOPED_TRACE("first");
        sample_compare(0, 0, 255, 0, 0, 255);
    }

    {
        SCOPED_TRACE("second");
        sample_compare(1, 0, 0, 255, 0, 255);
    }

    {
        SCOPED_TRACE("third");
        sample_compare(2, 0, 0, 0, 255, 255);
    }
}

TEST(RasterSampling, sampling_alpha_use_alpha)
{
    hrz_proto::NodataValue nodata_value;
    nodata_value.set_type(hrz_proto::NodataValueType::BIT_PATTERN_NODATA);
    nodata_value.set_bit_pattern(0);
    NodataFunction nodata(
        nodata_value, hrz_proto::NodataHandling::IGNORE_NODATA, hrz_proto::ImageFormat::SRGBA_8);
    ImagerySamplingFunction sampling(
        fetch_rgba8_pixel, hrz_proto::AlphaChannelUsage::USE_ALPHA_CHANNEL, std::move(nodata),
        hrz_proto::TextureFiltering::NEAREST);

    const uint8_t contents[] = {
        255, 0, 0, 255, 0, 255, 0, 127, 0, 0, 255, 0,
    };

    hrz::ImageView img(
        {(std::byte*)contents, 3 * 1 * 4 * sizeof(uint8_t)}, hrz_proto::ImageFormat::SRGBA_8, 3, 1);

    auto sample_compare = [&](float x, float y, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        std::array<uint8_t, 4> v;
        sampling.sample(img, lm::vec2(x / 3, y), v.data());
        EXPECT_EQ(r, v[0]);
        EXPECT_EQ(g, v[1]);
        EXPECT_EQ(b, v[2]);
        EXPECT_EQ(a, v[3]);
    };

    {
        SCOPED_TRACE("first");
        sample_compare(0, 0, 255, 0, 0, 255);
    }

    {
        SCOPED_TRACE("second");
        sample_compare(1, 0, 0, 127, 0, 127);
    }

    {
        SCOPED_TRACE("third");
        sample_compare(2, 0, 0, 0, 0, 0);
    }
}

TEST(RasterSampling, sampling_alpha_use_premultiplied)
{
    hrz_proto::NodataValue nodata_value;
    nodata_value.set_type(hrz_proto::NodataValueType::BIT_PATTERN_NODATA);
    nodata_value.set_bit_pattern(0);
    NodataFunction nodata(
        nodata_value, hrz_proto::NodataHandling::IGNORE_NODATA, hrz_proto::ImageFormat::SRGBA_8);
    ImagerySamplingFunction sampling(
        fetch_rgba8_pixel, hrz_proto::AlphaChannelUsage::USE_ALPHA_CHANNEL_PREMULTIPLIED,
        std::move(nodata), hrz_proto::TextureFiltering::NEAREST);

    const uint8_t contents[] = {
        255, 0, 0, 255, 0, 255, 0, 127, 0, 0, 255, 0,
    };

    hrz::ImageView img(
        {(std::byte*)contents, 3 * 1 * 4 * sizeof(uint8_t)}, hrz_proto::ImageFormat::SRGBA_8, 3, 1);

    auto sample_compare = [&](float x, float y, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        std::array<uint8_t, 4> v;
        sampling.sample(img, lm::vec2(x / 3, y), v.data());
        EXPECT_EQ(r, v[0]);
        EXPECT_EQ(g, v[1]);
        EXPECT_EQ(b, v[2]);
        EXPECT_EQ(a, v[3]);
    };

    {
        SCOPED_TRACE("first");
        sample_compare(0, 0, 255, 0, 0, 255);
    }

    {
        SCOPED_TRACE("second");
        sample_compare(1, 0, 0, 255, 0, 127);
    }

    {
        SCOPED_TRACE("third");
        sample_compare(2, 0, 0, 0, 255, 0);
    }
}

TEST(RasterSampling, sampling_premultiply_alpha_before_bilinear_filtering)
{
    hrz_proto::NodataValue nodata_value;
    nodata_value.set_type(hrz_proto::NodataValueType::BIT_PATTERN_NODATA);
    nodata_value.set_bit_pattern(0);
    NodataFunction nodata(
        nodata_value, hrz_proto::NodataHandling::IGNORE_NODATA, hrz_proto::ImageFormat::SRGBA_8);
    ImagerySamplingFunction sampling(
        fetch_rgba8_pixel, hrz_proto::AlphaChannelUsage::USE_ALPHA_CHANNEL, std::move(nodata),
        hrz_proto::TextureFiltering::BILINEAR);

    const uint8_t contents[] = {
        255, 0, 0, 255, 0, 255, 0, 127, 0, 0, 255, 0,
    };

    hrz::ImageView img(
        {(std::byte*)contents, 3 * 1 * 4 * sizeof(uint8_t)}, hrz_proto::ImageFormat::SRGBA_8, 3, 1);

    auto sample_compare = [&](float x, float y, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        std::array<uint8_t, 4> v;
        sampling.sample(img, lm::vec2((x + 0.5) / 3, y + 0.5), v.data());
        EXPECT_EQ(r, v[0]);
        EXPECT_EQ(g, v[1]);
        EXPECT_EQ(b, v[2]);
        EXPECT_EQ(a, v[3]);
    };

    {
        SCOPED_TRACE("first");
        sample_compare(0.5, 0, 127, 63, 0, 191);
    }

    {
        SCOPED_TRACE("second");
        sample_compare(1.5, 0, 0, 63, 0, 63);
    }
}

TEST(RasterSampling, normal)
{
    ImageryBlendingFunction fn(255);

    auto test_case = [&](uint8_t* src, uint8_t* dst, uint8_t* expected) {};

    {
        SCOPED_TRACE("case 1");
        uint8_t src[4] = {255, 0, 0, 255};
        uint8_t dst[4] = {50, 60, 70, 80};
        uint8_t res[4] = {255, 0, 0, 255};
        test_case(src, dst, res);
    }

    {
        SCOPED_TRACE("case 2");
        uint8_t src[4] = {255, 0, 0, 0};
        uint8_t dst[4] = {50, 60, 70, 80};
        uint8_t res[4] = {50, 60, 70, 80};
        test_case(src, dst, res);
    }

    {
        SCOPED_TRACE("case 3");
        uint8_t src[4] = {127, 0, 0, 127};
        uint8_t dst[4] = {0, 255, 0, 255};
        uint8_t res[4] = {127, 127, 0, 255};
        test_case(src, dst, res);
    }

    {
        SCOPED_TRACE("case 4");
        uint8_t src[4] = {63, 0, 0, 63};
        uint8_t dst[4] = {0, 255, 0, 255};
        uint8_t res[4] = {63, 191, 0, 255};
        test_case(src, dst, res);
    }
}

} // anonymous namespace
