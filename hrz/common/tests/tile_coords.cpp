#include "hrz/common/tile_coords.h"

#include <gtest/gtest.h>

TEST(TileCoords, identity)
{
    hrz::TileToTileUvTransform<float> xform(hrz::TileCoords{3, 5, 9}, hrz::TileCoords{3, 5, 9});

    lm::vec2 pt = xform({0.0f, 0.0f});
    EXPECT_FLOAT_EQ(0.0f, pt.x);
    EXPECT_FLOAT_EQ(0.0f, pt.y);

    pt = xform({1.0f, 1.0f});
    EXPECT_FLOAT_EQ(1.0f, pt.x);
    EXPECT_FLOAT_EQ(1.0f, pt.y);

    pt = xform({0.5f, 0.5f});
    EXPECT_FLOAT_EQ(0.5f, pt.x);
    EXPECT_FLOAT_EQ(0.5f, pt.y);

    pt = xform({0.25f, 0.75f});
    EXPECT_FLOAT_EQ(0.25f, pt.x);
    EXPECT_FLOAT_EQ(0.75f, pt.y);
}

TEST(TileCoords, same_lod)
{
    hrz::TileToTileUvTransform<float> xform(hrz::TileCoords{3, 0, 9}, hrz::TileCoords{1, 1, 9});

    lm::vec2 pt = xform({0.0f, 0.0f});
    EXPECT_FLOAT_EQ(2.0f, pt.x);
    EXPECT_FLOAT_EQ(-1.0f, pt.y);

    pt = xform({1.0f, 1.0f});
    EXPECT_FLOAT_EQ(3.0f, pt.x);
    EXPECT_FLOAT_EQ(0.0f, pt.y);
}

TEST(TileCoords, to_higher_lod)
{
    hrz::TileToTileUvTransform<float> xform(hrz::TileCoords{3, 0, 9}, hrz::TileCoords{8, 0, 10});

    lm::vec2 pt = xform({0.0f, 0.0f});
    EXPECT_FLOAT_EQ(-2.0f, pt.x);
    EXPECT_FLOAT_EQ(0.0f, pt.y);

    pt = xform({1.0f, 1.0f});
    EXPECT_FLOAT_EQ(0.0f, pt.x);
    EXPECT_FLOAT_EQ(2.0f, pt.y);
}

TEST(TileCoords, to_lower_lod)
{
    hrz::TileToTileUvTransform<float> xform(hrz::TileCoords{8, 0, 10}, hrz::TileCoords{3, 0, 9});

    lm::vec2 pt = xform({0.0f, 0.0f});
    EXPECT_FLOAT_EQ(1.0f, pt.x);
    EXPECT_FLOAT_EQ(0.0f, pt.y);

    pt = xform({1.0f, 1.0f});
    EXPECT_FLOAT_EQ(1.5f, pt.x);
    EXPECT_FLOAT_EQ(0.5f, pt.y);
}

TEST(TileCoords, to_lower_lod_inside)
{
    hrz::TileToTileUvTransform<float> xform(hrz::TileCoords{5, 6, 10}, hrz::TileCoords{1, 1, 8});

    lm::vec2 pt = xform({0.0f, 0.0f});
    EXPECT_FLOAT_EQ(0.25f, pt.x);
    EXPECT_FLOAT_EQ(0.5f, pt.y);

    pt = xform({1.0f, 1.0f});
    EXPECT_FLOAT_EQ(0.5f, pt.x);
    EXPECT_FLOAT_EQ(0.75f, pt.y);
}
