#include <hrz_common_maths.h>

#include <gtest/gtest.h>

#include <random>

TEST(BoundingSphere, one_point)
{
    lm::dvec3 pts[1] = {{1, 5, 6}};
    auto bsphere = hrz::compute_bounding_sphere(std::span<const lm::dvec3>(pts));
    EXPECT_DOUBLE_EQ(bsphere.center.x, 1);
    EXPECT_DOUBLE_EQ(bsphere.center.y, 5);
    EXPECT_DOUBLE_EQ(bsphere.center.z, 6);
    EXPECT_DOUBLE_EQ(bsphere.radius, 0);
}

TEST(BoundingSphere, two_points)
{
    lm::dvec3 pts[2] = {{1, 5, 6}, {2, 7, 10}};
    auto bsphere = hrz::compute_bounding_sphere(std::span<const lm::dvec3>(pts));
    EXPECT_DOUBLE_EQ(bsphere.center.x, 1.5);
    EXPECT_DOUBLE_EQ(bsphere.center.y, 6);
    EXPECT_DOUBLE_EQ(bsphere.center.z, 8);
    EXPECT_DOUBLE_EQ(bsphere.radius, 2.29128784747792000329);
}

TEST(BoundingSphere, three_points)
{
    lm::dvec3 pts[3] = {{-2, 3, 5}, {2, 3, 5}, {-2, 6, 5}};
    auto bsphere = hrz::compute_bounding_sphere(std::span<const lm::dvec3>(pts));
    EXPECT_DOUBLE_EQ(bsphere.center.x, 0);
    EXPECT_DOUBLE_EQ(bsphere.center.y, 4.5);
    EXPECT_DOUBLE_EQ(bsphere.center.z, 5);
    EXPECT_DOUBLE_EQ(bsphere.radius, 2.5);
}

TEST(BoundingSphere, four_points)
{
    lm::dvec3 pts[4] = {
        {sqrt(8.0 / 9.0), 5.0, -1.0 / 3.0},
        {-sqrt(2.0 / 9.0), sqrt(2.0 / 3.0) + 5.0, -1.0 / 3.0},
        {-sqrt(2.0 / 9.0), -sqrt(2.0 / 3.0) + 5.0, -1.0 / 3.0},
        {0, 5.0, 1.0},
    };
    auto bsphere = hrz::compute_bounding_sphere(std::span<const lm::dvec3>(pts));
    EXPECT_NEAR(bsphere.center.x, 0, 1e-8);
    EXPECT_NEAR(bsphere.center.y, 5.0, 1e-8);
    EXPECT_NEAR(bsphere.center.z, 0, 1e-8);
    EXPECT_NEAR(bsphere.radius, 1.0, 1e-8);
}

TEST(BoundingSphere, four_points_coplanar)
{
    lm::dvec3 pts[4] = {
        {sqrt(8.0 / 9.0), 5.0, 1.0},
        {-sqrt(2.0 / 9.0), sqrt(2.0 / 3.0) + 5.0, 1.0},
        {-sqrt(2.0 / 9.0), -sqrt(2.0 / 3.0) + 5.0, 1.0},
        {0, 5.0, 1.0},
    };
    auto bsphere = hrz::compute_bounding_sphere(std::span<const lm::dvec3>(pts));
    EXPECT_NEAR(bsphere.center.x, 0.235702260396, 1e-8);
    EXPECT_NEAR(bsphere.center.y, 5.0, 1e-8);
    EXPECT_NEAR(bsphere.center.z, 1, 1e-8);
    EXPECT_NEAR(bsphere.radius, 1.080123449735, 1e-8);
}

TEST(BoundingSphere, six_points)
{
    const double sqrt2 = sqrt(2) / 2;
    lm::dvec3 pts[6] = {
        {sqrt2, sqrt2, 1},  {-sqrt2, sqrt2, 1}, {-sqrt2, -sqrt2, 1},
        {sqrt2, -sqrt2, 1}, {0, 0, 2},          {0, 0, 0},
    };
    auto bsphere = hrz::compute_bounding_sphere(std::span<const lm::dvec3>(pts));
    EXPECT_NEAR(bsphere.center.x, 0, 1e-8);
    EXPECT_NEAR(bsphere.center.y, 0, 1e-8);
    EXPECT_NEAR(bsphere.center.z, 1, 1e-8);
    EXPECT_NEAR(bsphere.radius, 1.0, 1e-8);
}

TEST(BoundingSphere, cube)
{
    const double sqrt2 = sqrt(1.0 / 3.0) * 4.0;
    lm::dvec3 pts[8] = {
        {sqrt2 + 2.0, sqrt2 - 3.0, sqrt2 + 1.5},   {-sqrt2 + 2.0, sqrt2 - 3.0, sqrt2 + 1.5},
        {sqrt2 + 2.0, -sqrt2 - 3.0, sqrt2 + 1.5},  {-sqrt2 + 2.0, -sqrt2 - 3.0, sqrt2 + 1.5},
        {sqrt2 + 2.0, sqrt2 - 3.0, -sqrt2 + 1.5},  {-sqrt2 + 2.0, sqrt2 - 3.0, -sqrt2 + 1.5},
        {sqrt2 + 2.0, -sqrt2 - 3.0, -sqrt2 + 1.5}, {-sqrt2 + 2.0, -sqrt2 - 3.0, -sqrt2 + 1.5},
    };
    auto bsphere = hrz::compute_bounding_sphere(std::span<const lm::dvec3>(pts));

    for (const auto& p : pts)
    {
        double dist = lm::length(p - bsphere.center);
        EXPECT_TRUE(dist <= bsphere.radius + 1e-8);
    }
}

TEST(BoundingSphere, random)
{
    std::default_random_engine engine(0);
    std::uniform_real_distribution<double> distribution(2.0, 4.0);

    std::vector<lm::dvec3> pts;

    for (int i = 0; i < 100; ++i)
    {
        lm::dvec3 p(distribution(engine), distribution(engine), distribution(engine));

        pts.push_back(p);
    }

    auto bsphere = hrz::compute_bounding_sphere(std::span<const lm::dvec3>(pts));

    for (const auto& p : pts)
    {
        double dist = lm::length(p - bsphere.center);
        EXPECT_TRUE(dist <= bsphere.radius + 1e-8);
    }

    // We can't know exact bounds because the algorithm is not deterministic.
    // However we can approximate sane bounds for the result.

    EXPECT_LT(bsphere.radius, 2.0);
    EXPECT_NEAR(bsphere.center.x, 3, 2.0);
    EXPECT_NEAR(bsphere.center.y, 3, 2.0);
    EXPECT_NEAR(bsphere.center.z, 3, 2.0);
}

TEST(BoundingSphere, regression_1)
{
    lm::dvec3 pts[8] = {
        {4196287.709, 170816.8794, 4784230.94},  {4196300.829, 170830.071, 4784219.081},
        {4196287.709, 170816.8794, 4784230.94},  {4196300.829, 170830.071, 4784219.081},
        {4196294.468, 170805.2497, 4784225.482}, {4196307.589, 170818.4413, 4784213.623},
        {4196294.468, 170805.2497, 4784225.482}, {4196307.589, 170818.4413, 4784213.623},
    };

    auto bsphere = hrz::compute_bounding_sphere(std::span<const lm::dvec3>(pts));

    for (const auto& p : pts)
    {
        double dist = lm::length(p - bsphere.center);
        EXPECT_TRUE(dist <= bsphere.radius * 1.01);
    }

    EXPECT_LT(bsphere.radius, 100);
}

TEST(BoundingSphere, regression_2)
{
    lm::dvec3 pts[8] = {
        {4196295.388, 170808.9997, 4784233.788}, {4196307.674, 170821.3517, 4784222.684},
        {4196295.388, 170808.9997, 4784233.788}, {4196307.674, 170821.3517, 4784222.684},
        {4196317.564, 170770.8446, 4784215.881}, {4196329.85, 170783.1965, 4784204.777},
        {4196317.564, 170770.8446, 4784215.881}, {4196329.85, 170783.1965, 4784204.777},
    };

    auto bsphere = hrz::compute_bounding_sphere(std::span<const lm::dvec3>(pts));

    for (const auto& p : pts)
    {
        double dist = lm::length(p - bsphere.center);
        EXPECT_TRUE(dist <= bsphere.radius * 1.01);
    }

    EXPECT_LT(bsphere.radius, 100);
}

TEST(BoundingSphere, regression_3)
{
    lm::dvec3 pts[8] = {
        {4196296.284, 170813.3857, 4784216.26},  {4196296.401, 170811.9715, 4784216.202},
        {4196295.888, 170813.3366, 4784216.656}, {4196296.005, 170811.9224, 4784216.597},
        {4196295.582, 170813.3567, 4784215.555}, {4196295.699, 170811.9425, 4784215.497},
        {4196295.187, 170813.3076, 4784215.95},  {4196295.304, 170811.8934, 4784215.892},
    };

    auto bsphere = hrz::compute_bounding_sphere(std::span<const lm::dvec3>(pts));

    for (const auto& p : pts)
    {
        double dist = lm::length(p - bsphere.center);
        EXPECT_TRUE(dist <= bsphere.radius * 1.01);
    }

    EXPECT_LT(bsphere.radius, 100);
}

TEST(BoundingSphere, regression_4)
{
    lm::dvec3 pts[8] = {
        {4196294.255, 170815.939, 4784217.931},  {4196294.372, 170814.5249, 4784217.873},
        {4196293.859, 170815.8899, 4784218.327}, {4196293.977, 170814.4758, 4784218.268},
        {4196293.554, 170815.91, 4784217.226},   {4196293.671, 170814.4959, 4784217.168},
        {4196293.158, 170815.8609, 4784217.622}, {4196293.275, 170814.4468, 4784217.563},
    };

    auto bsphere = hrz::compute_bounding_sphere(std::span<const lm::dvec3>(pts));

    for (const auto& p : pts)
    {
        double dist = lm::length(p - bsphere.center);
        EXPECT_TRUE(dist <= bsphere.radius * 1.01);
    }

    EXPECT_LT(bsphere.radius, 100);
}
