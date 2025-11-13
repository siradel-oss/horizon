#include "hrz/common/maths.h"

#include <gtest/gtest.h>

TEST(PolygonWinding, triangle_cw)
{
    lm::vec2 pts[3] = {
        {2, 0},
        {0, 0},
        {1, 2},
    };
    EXPECT_TRUE(hrz::is_clockwise(std::span<const lm::vec2>(pts)));
}

TEST(PolygonWinding, triangle_ccw)
{
    lm::vec2 pts[3] = {
        {0, 0},
        {2, 0},
        {1, 2},
    };
    EXPECT_FALSE(hrz::is_clockwise(std::span<const lm::vec2>(pts)));
}

TEST(PolygonWinding, square_cw)
{
    lm::vec2 pts[4] = {
        {0, 0},
        {0, 3},
        {3, 3},
        {3, 0},
    };
    EXPECT_TRUE(hrz::is_clockwise(std::span<const lm::vec2>(pts)));
}

TEST(PolygonWinding, square_ccw)
{
    lm::vec2 pts[4] = {
        {0, 0},
        {3, 0},
        {3, 3},
        {0, 3},
    };
    EXPECT_FALSE(hrz::is_clockwise(std::span<const lm::vec2>(pts)));
}

TEST(PolygonWinding, concave_cw)
{
    lm::vec2 pts[5] = {
        {1, 2}, {1, -1}, {-2, -1}, {-1, 0}, {-3, 2},
    };
    EXPECT_TRUE(hrz::is_clockwise(std::span<const lm::vec2>(pts)));
}

TEST(PolygonWinding, concave_ccw)
{
    lm::vec2 pts[5] = {
        {-1, 0}, {-2, -1}, {1, -1}, {1, 2}, {-3, 2},
    };
    EXPECT_FALSE(hrz::is_clockwise(std::span<const lm::vec2>(pts)));
}
