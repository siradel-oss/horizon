#include "hrz/common/tile_coords.h"

#include <gtest/gtest.h>

TEST(TileCoords, identity)
{
    hrz::TileToTileUvTransform<float> xform(hrz::TileCoords{3, 5, 9}, hrz::TileCoords{3, 5, 9});

    lm::vec2 pt = xform({0.0F, 0.0F});
    EXPECT_FLOAT_EQ(0.0F, pt.x);
    EXPECT_FLOAT_EQ(0.0F, pt.y);

    pt = xform({1.0F, 1.0F});
    EXPECT_FLOAT_EQ(1.0F, pt.x);
    EXPECT_FLOAT_EQ(1.0F, pt.y);

    pt = xform({0.5F, 0.5F});
    EXPECT_FLOAT_EQ(0.5F, pt.x);
    EXPECT_FLOAT_EQ(0.5F, pt.y);

    pt = xform({0.25F, 0.75F});
    EXPECT_FLOAT_EQ(0.25F, pt.x);
    EXPECT_FLOAT_EQ(0.75F, pt.y);
}

TEST(TileCoords, same_lod)
{
    hrz::TileToTileUvTransform<float> xform(hrz::TileCoords{3, 0, 9}, hrz::TileCoords{1, 1, 9});

    lm::vec2 pt = xform({0.0F, 0.0F});
    EXPECT_FLOAT_EQ(2.0F, pt.x);
    EXPECT_FLOAT_EQ(-1.0F, pt.y);

    pt = xform({1.0F, 1.0F});
    EXPECT_FLOAT_EQ(3.0F, pt.x);
    EXPECT_FLOAT_EQ(0.0F, pt.y);
}

TEST(TileCoords, to_higher_lod)
{
    hrz::TileToTileUvTransform<float> xform(hrz::TileCoords{3, 0, 9}, hrz::TileCoords{8, 0, 10});

    lm::vec2 pt = xform({0.0F, 0.0F});
    EXPECT_FLOAT_EQ(-2.0F, pt.x);
    EXPECT_FLOAT_EQ(0.0F, pt.y);

    pt = xform({1.0F, 1.0F});
    EXPECT_FLOAT_EQ(0.0F, pt.x);
    EXPECT_FLOAT_EQ(2.0F, pt.y);
}

TEST(TileCoords, to_lower_lod)
{
    hrz::TileToTileUvTransform<float> xform(hrz::TileCoords{8, 0, 10}, hrz::TileCoords{3, 0, 9});

    lm::vec2 pt = xform({0.0F, 0.0F});
    EXPECT_FLOAT_EQ(1.0F, pt.x);
    EXPECT_FLOAT_EQ(0.0F, pt.y);

    pt = xform({1.0F, 1.0F});
    EXPECT_FLOAT_EQ(1.5F, pt.x);
    EXPECT_FLOAT_EQ(0.5F, pt.y);
}

TEST(TileCoords, to_lower_lod_inside)
{
    hrz::TileToTileUvTransform<float> xform(hrz::TileCoords{5, 6, 10}, hrz::TileCoords{1, 1, 8});

    lm::vec2 pt = xform({0.0F, 0.0F});
    EXPECT_FLOAT_EQ(0.25F, pt.x);
    EXPECT_FLOAT_EQ(0.5F, pt.y);

    pt = xform({1.0F, 1.0F});
    EXPECT_FLOAT_EQ(0.5F, pt.x);
    EXPECT_FLOAT_EQ(0.75F, pt.y);
}
