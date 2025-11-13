#include "hrz/common/maths.h"

#include <gtest/gtest.h>

#include <stdio.h>
#include <stdlib.h>

TEST(MinimumBbox, axis_aligned)
{
    lm::vec2 pts[] = {
        {-1, 6},
        {3, 6},
        {-1, 0},
        {3, 0},
    };

    hrz::OrientedBBox2<float> obb = hrz::compute_minimum_bbox(std::span<const lm::vec2>(pts));

    EXPECT_FLOAT_EQ(obb.center.x, 1);
    EXPECT_FLOAT_EQ(obb.center.y, 3);
    EXPECT_FLOAT_EQ(obb.angle, 0);
    EXPECT_FLOAT_EQ(obb.half_width, 2);
    EXPECT_FLOAT_EQ(obb.half_height, 3);
}

TEST(MinimumBbox, rotated_square)
{
    lm::vec2 pts[] = {
        {-1, 0},
        {-1, -2},
        {0, -1},
        {-2, -1},
    };

    hrz::OrientedBBox2<float> obb = hrz::compute_minimum_bbox(std::span<const lm::vec2>(pts));

    EXPECT_FLOAT_EQ(obb.center.x, -1);
    EXPECT_FLOAT_EQ(obb.center.y, -1);
    EXPECT_FLOAT_EQ(obb.angle, 0.78539816339744830962);
    EXPECT_FLOAT_EQ(obb.half_width, 0.70710677);
    EXPECT_FLOAT_EQ(obb.half_height, 0.70710677);
}

TEST(MinimumBbox, squished_rotated_rectangle)
{
    lm::vec2 pts[] = {
        {-3, 0},
        {3, 0},
        {1, 2},
        {-1, -2},
    };

    hrz::OrientedBBox2<float> obb = hrz::compute_minimum_bbox(std::span<const lm::vec2>(pts));

    EXPECT_FLOAT_EQ(obb.center.x, 0);
    EXPECT_FLOAT_EQ(obb.center.y, 0);
    EXPECT_FLOAT_EQ(obb.angle, 0.45378560551852569);
    EXPECT_FLOAT_EQ(obb.half_width, 2.696382);
    EXPECT_FLOAT_EQ(obb.half_height, 1.3592169);
}

TEST(MinimumBbox, diamond)
{
    lm::vec2 pts[] = {
        {2.5, 1.5}, {1, 3}, {4, 3}, {2, 4}, {3, 4},
    };

    hrz::OrientedBBox2<float> obb = hrz::compute_minimum_bbox(std::span<const lm::vec2>(pts));

    EXPECT_FLOAT_EQ(obb.center.x, 2.5);
    EXPECT_FLOAT_EQ(obb.center.y, 3);
    EXPECT_FLOAT_EQ(obb.angle, 0.78539816339744830962);
    EXPECT_FLOAT_EQ(obb.half_width, 1.0606601717798212866);
    EXPECT_FLOAT_EQ(obb.half_height, 1.0606601717798212866);
}
