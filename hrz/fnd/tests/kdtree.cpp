#include <hrz_fnd_kdtree.h>

#include <gtest/gtest.h>

TEST(Kdtree, simple)
{
    hrz::Kdtree tree;

    lm::bbox2 bbox{{-10, -10}, {10, 10}};
    tree.insert(bbox);

    EXPECT_TRUE(tree.intersects(lm::bbox2{{-1, -1}, {1, 1}}));
    EXPECT_TRUE(tree.intersects(lm::bbox2{{-1, -1}, {100, 100}}));
    EXPECT_TRUE(tree.intersects(lm::bbox2{{-1, -1}, {100, 100}}));
    EXPECT_FALSE(tree.intersects(lm::bbox2{{20, 0}, {30, 10}}));
    EXPECT_FALSE(tree.intersects(lm::bbox2{{0, -20}, {1, -15}}));
}

TEST(Kdtree, two_squares)
{
    hrz::Kdtree tree;

    tree.insert(lm::bbox2{{-10, -10}, {-0.5, -0.5}});
    tree.insert(lm::bbox2{{0.5, 0.5}, {10, 10}});

    EXPECT_TRUE(tree.intersects(lm::bbox2{{-1, -1}, {1, 1}}));
    EXPECT_TRUE(tree.intersects(lm::bbox2{{-1, -1}, {100, 100}}));
    EXPECT_TRUE(tree.intersects(lm::bbox2{{-1, -1}, {100, 100}}));
    EXPECT_FALSE(tree.intersects(lm::bbox2{{20, 0}, {30, 10}}));
    EXPECT_FALSE(tree.intersects(lm::bbox2{{0, -20}, {1, -15}}));
}

TEST(Kdtree, grid)
{
    hrz::Kdtree tree;

    for (float x = -10; x < 10; x += 1)
    {
        for (float y = -10; y < 10; y += 1)
        {
            tree.insert(lm::bbox2{{x, y}, {x + 1, y + 1}});
        }
    }

    EXPECT_TRUE(tree.intersects(lm::bbox2{{-1, -1}, {1, 1}}));
    EXPECT_TRUE(tree.intersects(lm::bbox2{{-1, -1}, {100, 100}}));
    EXPECT_TRUE(tree.intersects(lm::bbox2{{-1, -1}, {100, 100}}));
    EXPECT_FALSE(tree.intersects(lm::bbox2{{20, 0}, {30, 10}}));
    EXPECT_FALSE(tree.intersects(lm::bbox2{{0, -20}, {1, -15}}));
}

TEST(Kdtree, split)
{
    hrz::Kdtree tree;

    for (float i = 0; i < 6; i += 1)
    {
        tree.insert(lm::bbox2{{-20, i}, {-10, i + 1}});
        tree.insert(lm::bbox2{{10, i}, {20, i + 1}});
    }

    EXPECT_FALSE(tree.intersects(lm::bbox2{{-1, -1}, {1, 1}}));
    EXPECT_TRUE(tree.intersects(lm::bbox2{{-40, -1}, {1, 1}}));
    EXPECT_TRUE(tree.intersects(lm::bbox2{{-40, -1}, {40, 1}}));
    EXPECT_TRUE(tree.intersects(lm::bbox2{{-15, -1}, {-12, 1}}));
}
