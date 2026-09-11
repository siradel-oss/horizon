// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/common/maths.h"

#include <gtest/gtest.h>

#include <stdio.h>
#include <stdlib.h>

TEST(Quickhull, two_points)
{
    lm::vec2 pts[] = {
        {15, 9},
        {7, 3},
    };

    std::vector<lm::vec2> hull;
    hrz::compute_convex_hull(std::span<const lm::vec2>(pts), hull);

    EXPECT_EQ(pts[1], hull[0]);
    EXPECT_EQ(pts[0], hull[1]);
}

TEST(Quickhull, triangle_1)
{
    lm::vec2 pts[] = {
        {3, 4},
        {1, -1},
        {2, 8},
    };

    std::vector<lm::vec2> hull;
    hrz::compute_convex_hull(std::span<const lm::vec2>(pts), hull);

    EXPECT_EQ(pts[1], hull[0]);
    EXPECT_EQ(pts[2], hull[1]);
    EXPECT_EQ(pts[0], hull[2]);
}

TEST(Quickhull, triangle_2)
{
    lm::vec2 pts[] = {
        {3, 4},
        {1, -1},
        {2, -2},
    };

    std::vector<lm::vec2> hull;
    hrz::compute_convex_hull(std::span<const lm::vec2>(pts), hull);

    EXPECT_EQ(pts[1], hull[0]);
    EXPECT_EQ(pts[0], hull[1]);
    EXPECT_EQ(pts[2], hull[2]);
}

TEST(Quickhull, quad_with_points_inside)
{
    lm::vec2 pts[] = {
        {3, 4}, {-1, 2}, {1, -1}, {-2, -2}, {-1, 1}, {1, 2}, {-1, -1}, {2, 2},
    };

    std::vector<lm::vec2> hull;
    hrz::compute_convex_hull(std::span<const lm::vec2>(pts), hull);

    EXPECT_EQ(pts[3], hull[0]);
    EXPECT_EQ(pts[1], hull[1]);
    EXPECT_EQ(pts[0], hull[2]);
    EXPECT_EQ(pts[2], hull[3]);
}

TEST(Quickhull, quad)
{
    lm::vec2 pts[] = {
        {3, 4},
        {-1, 2},
        {1, -1},
        {-2, -2},
    };

    std::vector<lm::vec2> hull;
    hrz::compute_convex_hull(std::span<const lm::vec2>(pts), hull);

    EXPECT_EQ(pts[3], hull[0]);
    EXPECT_EQ(pts[1], hull[1]);
    EXPECT_EQ(pts[0], hull[2]);
    EXPECT_EQ(pts[2], hull[3]);
}

TEST(Quickhull, quad_2)
{
    lm::vec2 pts[] = {
        {-3, 0},
        {3, 0},
        {1, 2},
        {-1, -2},
    };

    std::vector<lm::vec2> hull;
    hrz::compute_convex_hull(std::span<const lm::vec2>(pts), hull);

    ASSERT_EQ(hull.size(), 4);
    EXPECT_EQ(pts[0], hull[0]);
    EXPECT_EQ(pts[2], hull[1]);
    EXPECT_EQ(pts[1], hull[2]);
    EXPECT_EQ(pts[3], hull[3]);
}

TEST(Quickhull, random_points_inside_a_quad)
{
    lm::dvec2 pts[100];

    srand(78);
    for (int i = 0; i < 100; ++i)
    {
        double x = (double)(rand() % 30) - 19.0;
        double y = (double)(rand() % 30) - 19.0;
        pts[i] = lm::dvec2(x, y);
    }

    pts[18] = lm::dvec2(-20, 10);
    pts[84] = lm::dvec2(20, 20);
    pts[45] = lm::dvec2(10, -20);
    pts[6] = lm::dvec2(-40, -20);
    pts[51] = lm::dvec2(-50, -10);

    std::vector<lm::dvec2> hull;
    hrz::compute_convex_hull(std::span<const lm::dvec2>(pts), hull);

    ASSERT_EQ(hull.size(), 5);
    EXPECT_EQ(pts[51], hull[0]);
    EXPECT_EQ(pts[18], hull[1]);
    EXPECT_EQ(pts[84], hull[2]);
    EXPECT_EQ(pts[45], hull[3]);
    EXPECT_EQ(pts[6], hull[4]);
}
