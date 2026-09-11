// SPDX-FileCopyrightText: Copyright 2024 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/fnd/varint.h"

#include "hrz/fnd/mem.h"

#include <gtest/gtest.h>

#include <array>

TEST(VarintDecode, simple)
{
    EXPECT_EQ(hrz::decode_varint_u64(std::array{0x00_b}), 0);
    EXPECT_EQ(hrz::decode_varint_u64(std::array{0x08_b}), 8);
    EXPECT_EQ(
        hrz::decode_varint_u64(std::array{0x08_b, 0_b, 0_b, 0_b, 0_b, 0_b, 0_b, 0_b, 0_b, 0_b}), 8);
}

TEST(VarintDecode, multibyte)
{
    EXPECT_EQ(hrz::decode_varint_u64(std::array{0x82_b, 0x01_b}), 130);
    EXPECT_EQ(
        hrz::decode_varint_u64(std::array{0x82_b, 0x01_b, 0_b, 0_b, 0_b, 0_b, 0_b, 0_b, 0_b, 0_b}),
        130);
    EXPECT_EQ(
        hrz::decode_varint_u64(std::array{0xaa_b, 0xbb_b, 0xcc_b, 0xdd_b, 0xee_b, 0xff_b}),
        4393410960810);
}

TEST(VarintDecode, stream)
{
    std::array data{0x00_b, 0x01_b, 0x82_b, 0x01_b, 0x7f_b};

    const auto* it = data.data();
    const auto* end = data.data() + data.size();

    EXPECT_EQ(hrz::decode_varint_u64(&it, end), 0);
    EXPECT_EQ(hrz::decode_varint_u64(&it, end), 1);
    EXPECT_EQ(hrz::decode_varint_u64(&it, end), 130);
    EXPECT_EQ(hrz::decode_varint_u64(&it, end), 127);
    EXPECT_EQ(it, end);
}

TEST(VarintDecode, stream_large)
{
    std::array data{0x00_b, 0x01_b, 0x82_b, 0x01_b, 0x7f_b, 0x10_b, 0x11_b, 0x12_b, 0x13_b, 0x14_b,
                    0x16_b, 0x17_b, 0x18_b, 0x19_b, 0x1a_b, 0x1b_b, 0x1c_b, 0x1d_b, 0x1e_b, 0x1f_b,
                    0x16_b, 0x17_b, 0x18_b, 0x19_b, 0x1a_b, 0x1b_b, 0x1c_b, 0x1d_b, 0x1e_b, 0x1f_b,
                    0x16_b, 0x17_b, 0x18_b, 0x19_b, 0x1a_b, 0x1b_b, 0x1c_b, 0x1d_b, 0x1e_b, 0x1f_b};

    const auto* it = data.data();
    const auto* end = data.data() + data.size();

    EXPECT_EQ(hrz::decode_varint_u64(&it, end), 0);
    EXPECT_EQ(hrz::decode_varint_u64(&it, end), 1);
    EXPECT_EQ(hrz::decode_varint_u64(&it, end), 130);
    EXPECT_EQ(hrz::decode_varint_u64(&it, end), 127);
    EXPECT_EQ(it, data.data() + 5);
}

TEST(VarintDecode, not_enough_data)
{
    EXPECT_EQ(hrz::decode_varint_u64({}), 0);
    EXPECT_EQ(hrz::decode_varint_u64(std::array{0x80_b, 0x81_b}), 128);
}

TEST(VarintDecode, dont_read_more)
{
    EXPECT_EQ(hrz::decode_varint_u64(std::array{0x00_b, 0x78_b}), 0);
    EXPECT_EQ(hrz::decode_varint_u64(std::array{0x08_b, 0x01_b}), 8);
    EXPECT_EQ(hrz::decode_varint_u64(std::array{0x82_b, 0x01_b, 0x02_b}), 130);
}
