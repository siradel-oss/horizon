#include <hrz_fnd_defines.h>
#include <hrz_jobs_clipping.h>

#include <gtest/gtest.h>

namespace
{
TEST(TriangleClipping, full_inside)
{
    std::vector<lm::vec2> p;
    std::vector<lm::vec2> uv;

    hrz::clip_triangle(
        lm::vec2(10, 10), lm::vec2(10, 90), lm::vec2(90, 90), lm::vec2(0, 0), lm::vec2(0, 1),
        lm::vec2(1, 1), lm::bbox2({0, 0}, {100, 100}),
        [&](lm::vec2 p0, lm::vec2 p1, lm::vec2 p2, lm::vec2 uv0, lm::vec2 uv1, lm::vec2 uv2)
        {
            p.push_back(p0);
            p.push_back(p1);
            p.push_back(p2);
            uv.push_back(uv0);
            uv.push_back(uv1);
            uv.push_back(uv2);
        });

    ASSERT_EQ(p.size(), 3);

    EXPECT_EQ(p[0], lm::vec2(10, 10));
    EXPECT_EQ(p[1], lm::vec2(10, 90));
    EXPECT_EQ(p[2], lm::vec2(90, 90));
    EXPECT_EQ(uv[0], lm::vec2(0, 0));
    EXPECT_EQ(uv[1], lm::vec2(0, 1));
    EXPECT_EQ(uv[2], lm::vec2(1, 1));
}

TEST(TriangleClipping, full_outside)
{
    std::vector<lm::vec2> p;
    std::vector<lm::vec2> uv;

    hrz::clip_triangle(
        lm::vec2(10, 10), lm::vec2(10, 90), lm::vec2(90, 90), lm::vec2(0, 0), lm::vec2(0, 1),
        lm::vec2(1, 1), lm::bbox2({100, 100}, {200, 200}),
        [&](lm::vec2 p0, lm::vec2 p1, lm::vec2 p2, lm::vec2 uv0, lm::vec2 uv1, lm::vec2 uv2)
        {
            p.push_back(p0);
            p.push_back(p1);
            p.push_back(p2);
            uv.push_back(uv0);
            uv.push_back(uv1);
            uv.push_back(uv2);
        });

    ASSERT_EQ(p.size(), 0);
}

TEST(TriangleClipping, full_inside_on_border)
{
    std::vector<lm::vec2> p;
    std::vector<lm::vec2> uv;

    hrz::clip_triangle(
        lm::vec2(10, 10), lm::vec2(10, 90), lm::vec2(90, 90), lm::vec2(0, 0), lm::vec2(0, 1),
        lm::vec2(1, 1), lm::bbox2({10, 10}, {90, 90}),
        [&](lm::vec2 p0, lm::vec2 p1, lm::vec2 p2, lm::vec2 uv0, lm::vec2 uv1, lm::vec2 uv2)
        {
            p.push_back(p0);
            p.push_back(p1);
            p.push_back(p2);
            uv.push_back(uv0);
            uv.push_back(uv1);
            uv.push_back(uv2);
        });

    ASSERT_EQ(p.size(), 3);

    EXPECT_EQ(p[0], lm::vec2(10, 10));
    EXPECT_EQ(p[1], lm::vec2(10, 90));
    EXPECT_EQ(p[2], lm::vec2(90, 90));
    EXPECT_EQ(uv[0], lm::vec2(0, 0));
    EXPECT_EQ(uv[1], lm::vec2(0, 1));
    EXPECT_EQ(uv[2], lm::vec2(1, 1));
}

TEST(TriangleClipping, one_outside_one_edge)
{
    std::vector<lm::vec2> p;
    std::vector<lm::vec2> uv;

    hrz::clip_triangle(
        lm::vec2(1, 3), lm::vec2(1, 1), lm::vec2(-1, 1), lm::vec2(1, 1), lm::vec2(1, 0),
        lm::vec2(0, 0), lm::bbox2({0, 0}, {4, 4}),
        [&](lm::vec2 p0, lm::vec2 p1, lm::vec2 p2, lm::vec2 uv0, lm::vec2 uv1, lm::vec2 uv2)
        {
            p.push_back(p0);
            p.push_back(p1);
            p.push_back(p2);
            uv.push_back(uv0);
            uv.push_back(uv1);
            uv.push_back(uv2);
        });

    ASSERT_EQ(p.size(), 6);

    EXPECT_EQ(p[0], lm::vec2(0, 2));
    EXPECT_EQ(p[1], lm::vec2(1, 3));
    EXPECT_EQ(p[2], lm::vec2(1, 1));
    EXPECT_EQ(p[3], lm::vec2(0, 2));
    EXPECT_EQ(p[4], lm::vec2(1, 1));
    EXPECT_EQ(p[5], lm::vec2(0, 1));
    EXPECT_EQ(uv[0], lm::vec2(0.5, 0.5));
    EXPECT_EQ(uv[1], lm::vec2(1, 1));
    EXPECT_EQ(uv[2], lm::vec2(1, 0));
    EXPECT_EQ(uv[3], lm::vec2(0.5, 0.5));
    EXPECT_EQ(uv[4], lm::vec2(1, 0));
    EXPECT_EQ(uv[5], lm::vec2(0.5, 0));
}

TEST(TriangleClipping, two_outside_one_edge)
{
    std::vector<lm::vec2> p;
    std::vector<lm::vec2> uv;

    hrz::clip_triangle(
        lm::vec2(-1, 1), lm::vec2(-1, 4), lm::vec2(2, 1), lm::vec2(0, 0), lm::vec2(0, 3),
        lm::vec2(3, 0), lm::bbox2({0, 0}, {4, 4}),
        [&](lm::vec2 p0, lm::vec2 p1, lm::vec2 p2, lm::vec2 uv0, lm::vec2 uv1, lm::vec2 uv2)
        {
            p.push_back(p0);
            p.push_back(p1);
            p.push_back(p2);
            uv.push_back(uv0);
            uv.push_back(uv1);
            uv.push_back(uv2);
        });

    ASSERT_EQ(p.size(), 3);

    static const float eps = 1e-6;
    EXPECT_NEAR(p[0].x, 0, eps);
    EXPECT_NEAR(p[0].y, 1, eps);
    EXPECT_NEAR(p[1].x, 0, eps);
    EXPECT_NEAR(p[1].y, 3, eps);
    EXPECT_NEAR(p[2].x, 2, eps);
    EXPECT_NEAR(p[2].y, 1, eps);
    EXPECT_NEAR(uv[0].x, 1, eps);
    EXPECT_NEAR(uv[0].y, 0, eps);
    EXPECT_NEAR(uv[1].x, 1, eps);
    EXPECT_NEAR(uv[1].y, 2, eps);
    EXPECT_NEAR(uv[2].x, 3, eps);
    EXPECT_NEAR(uv[2].y, 0, eps);
}

TEST(TriangleClipping, two_outside_two_edges)
{
    std::vector<lm::vec2> p;
    std::vector<lm::vec2> uv;

    hrz::clip_triangle(
        lm::vec2(3, 3), lm::vec2(3, 6), lm::vec2(6, 3), lm::vec2(0, 0), lm::vec2(0, 3),
        lm::vec2(3, 0), lm::bbox2({0, 0}, {4, 4}),
        [&](lm::vec2 p0, lm::vec2 p1, lm::vec2 p2, lm::vec2 uv0, lm::vec2 uv1, lm::vec2 uv2)
        {
            p.push_back(p0);
            p.push_back(p1);
            p.push_back(p2);
            uv.push_back(uv0);
            uv.push_back(uv1);
            uv.push_back(uv2);
        });

    ASSERT_EQ(p.size(), 6);

    static const float eps = 1e-6;
    EXPECT_NEAR(p[0].x, 4, eps);
    EXPECT_NEAR(p[0].y, 4, eps);
    EXPECT_NEAR(p[1].x, 4, eps);
    EXPECT_NEAR(p[1].y, 3, eps);
    EXPECT_NEAR(p[2].x, 3, eps);
    EXPECT_NEAR(p[2].y, 3, eps);
    EXPECT_NEAR(p[3].x, 4, eps);
    EXPECT_NEAR(p[3].y, 4, eps);
    EXPECT_NEAR(p[4].x, 3, eps);
    EXPECT_NEAR(p[4].y, 3, eps);
    EXPECT_NEAR(p[5].x, 3, eps);
    EXPECT_NEAR(p[5].y, 4, eps);
    EXPECT_NEAR(uv[0].x, 1, eps);
    EXPECT_NEAR(uv[0].y, 1, eps);
    EXPECT_NEAR(uv[1].x, 1, eps);
    EXPECT_NEAR(uv[1].y, 0, eps);
    EXPECT_NEAR(uv[2].x, 0, eps);
    EXPECT_NEAR(uv[2].y, 0, eps);
    EXPECT_NEAR(uv[3].x, 1, eps);
    EXPECT_NEAR(uv[3].y, 1, eps);
    EXPECT_NEAR(uv[4].x, 0, eps);
    EXPECT_NEAR(uv[4].y, 0, eps);
    EXPECT_NEAR(uv[5].x, 0, eps);
    EXPECT_NEAR(uv[5].y, 1, eps);
}

TEST(TriangleClipping, two_outside_two_edges_clip_corner)
{
    std::vector<lm::vec2> p;
    std::vector<lm::vec2> uv;

    hrz::clip_triangle(
        lm::vec2(1, 1), lm::vec2(1, 6), lm::vec2(6, 1), lm::vec2(0, 0), lm::vec2(0, 10),
        lm::vec2(10, 0), lm::bbox2({0, 0}, {4, 4}),
        [&](lm::vec2 p0, lm::vec2 p1, lm::vec2 p2, lm::vec2 uv0, lm::vec2 uv1, lm::vec2 uv2)
        {
            p.push_back(p0);
            p.push_back(p1);
            p.push_back(p2);
            uv.push_back(uv0);
            uv.push_back(uv1);
            uv.push_back(uv2);
        });

    ASSERT_EQ(p.size(), 9);

    static const float eps = 1e-6;
    EXPECT_NEAR(p[0].x, 4, eps);
    EXPECT_NEAR(p[0].y, 1, eps);
    EXPECT_NEAR(p[1].x, 1, eps);
    EXPECT_NEAR(p[1].y, 1, eps);
    EXPECT_NEAR(p[2].x, 1, eps);
    EXPECT_NEAR(p[2].y, 4, eps);
    EXPECT_NEAR(p[3].x, 4, eps);
    EXPECT_NEAR(p[3].y, 1, eps);
    EXPECT_NEAR(p[4].x, 1, eps);
    EXPECT_NEAR(p[4].y, 4, eps);
    EXPECT_NEAR(p[5].x, 3, eps);
    EXPECT_NEAR(p[5].y, 4, eps);
    EXPECT_NEAR(p[6].x, 4, eps);
    EXPECT_NEAR(p[6].y, 1, eps);
    EXPECT_NEAR(p[7].x, 3, eps);
    EXPECT_NEAR(p[7].y, 4, eps);
    EXPECT_NEAR(p[8].x, 4, eps);
    EXPECT_NEAR(p[8].y, 3, eps);
    EXPECT_NEAR(uv[0].x, 6, eps);
    EXPECT_NEAR(uv[0].y, 0, eps);
    EXPECT_NEAR(uv[1].x, 0, eps);
    EXPECT_NEAR(uv[1].y, 0, eps);
    EXPECT_NEAR(uv[2].x, 0, eps);
    EXPECT_NEAR(uv[2].y, 6, eps);
    EXPECT_NEAR(uv[3].x, 6, eps);
    EXPECT_NEAR(uv[3].y, 0, eps);
    EXPECT_NEAR(uv[4].x, 0, eps);
    EXPECT_NEAR(uv[4].y, 6, eps);
    EXPECT_NEAR(uv[5].x, 4, eps);
    EXPECT_NEAR(uv[5].y, 6, eps);
    EXPECT_NEAR(uv[6].x, 6, eps);
    EXPECT_NEAR(uv[6].y, 0, eps);
    EXPECT_NEAR(uv[7].x, 4, eps);
    EXPECT_NEAR(uv[7].y, 6, eps);
    EXPECT_NEAR(uv[8].x, 6, eps);
    EXPECT_NEAR(uv[8].y, 4, eps);
}

TEST(TriangleClipping, two_outside_two_edges_opposite)
{
    std::vector<lm::vec2> p;
    std::vector<lm::vec2> uv;

    hrz::clip_triangle(
        lm::vec2(1, 1), lm::vec2(3, 3), lm::vec2(3, -1), lm::vec2(0, 0), lm::vec2(0, 4),
        lm::vec2(4, 0), lm::bbox2({0, 0}, {6, 2}),
        [&](lm::vec2 p0, lm::vec2 p1, lm::vec2 p2, lm::vec2 uv0, lm::vec2 uv1, lm::vec2 uv2)
        {
            p.push_back(p0);
            p.push_back(p1);
            p.push_back(p2);
            uv.push_back(uv0);
            uv.push_back(uv1);
            uv.push_back(uv2);
        });

    ASSERT_EQ(p.size(), 9);

    static const float eps = 1e-6;
    EXPECT_NEAR(p[0].x, 2, eps);
    EXPECT_NEAR(p[0].y, 0, eps);
    EXPECT_NEAR(p[1].x, 1, eps);
    EXPECT_NEAR(p[1].y, 1, eps);
    EXPECT_NEAR(p[2].x, 2, eps);
    EXPECT_NEAR(p[2].y, 2, eps);
    EXPECT_NEAR(p[3].x, 2, eps);
    EXPECT_NEAR(p[3].y, 0, eps);
    EXPECT_NEAR(p[4].x, 2, eps);
    EXPECT_NEAR(p[4].y, 2, eps);
    EXPECT_NEAR(p[5].x, 3, eps);
    EXPECT_NEAR(p[5].y, 2, eps);
    EXPECT_NEAR(p[6].x, 2, eps);
    EXPECT_NEAR(p[6].y, 0, eps);
    EXPECT_NEAR(p[7].x, 3, eps);
    EXPECT_NEAR(p[7].y, 2, eps);
    EXPECT_NEAR(p[8].x, 3, eps);
    EXPECT_NEAR(p[8].y, 0, eps);
    EXPECT_NEAR(uv[0].x, 2, eps);
    EXPECT_NEAR(uv[0].y, 0, eps);
    EXPECT_NEAR(uv[1].x, 0, eps);
    EXPECT_NEAR(uv[1].y, 0, eps);
    EXPECT_NEAR(uv[2].x, 0, eps);
    EXPECT_NEAR(uv[2].y, 2, eps);
    EXPECT_NEAR(uv[3].x, 2, eps);
    EXPECT_NEAR(uv[3].y, 0, eps);
    EXPECT_NEAR(uv[4].x, 0, eps);
    EXPECT_NEAR(uv[4].y, 2, eps);
    EXPECT_NEAR(uv[5].x, 1, eps);
    EXPECT_NEAR(uv[5].y, 3, eps);
    EXPECT_NEAR(uv[6].x, 2, eps);
    EXPECT_NEAR(uv[6].y, 0, eps);
    EXPECT_NEAR(uv[7].x, 1, eps);
    EXPECT_NEAR(uv[7].y, 3, eps);
    EXPECT_NEAR(uv[8].x, 3, eps);
    EXPECT_NEAR(uv[8].y, 1, eps);
}

TEST(TriangleClipping, three_outside_clipping)
{
    std::vector<lm::vec2> p;
    std::vector<lm::vec2> uv;

    hrz::clip_triangle(
        lm::vec2(-3, 3), lm::vec2(1, 7), lm::vec2(5, -1), lm::vec2(-3, 3), lm::vec2(1, 7),
        lm::vec2(5, -1), lm::bbox2({0, 0}, {4, 4}),
        [&](lm::vec2 p0, lm::vec2 p1, lm::vec2 p2, lm::vec2 uv0, lm::vec2 uv1, lm::vec2 uv2)
        {
            p.push_back(p0);
            p.push_back(p1);
            p.push_back(p2);
            uv.push_back(uv0);
            uv.push_back(uv1);
            uv.push_back(uv2);
        });

    ASSERT_EQ(p.size(), 12);

    static const float eps = 1e-6;
    EXPECT_NEAR(p[0].x, 4, eps);
    EXPECT_NEAR(p[0].y, 0, eps);
    EXPECT_NEAR(p[1].x, 3, eps);
    EXPECT_NEAR(p[1].y, 0, eps);
    EXPECT_NEAR(p[2].x, 0, eps);
    EXPECT_NEAR(p[2].y, 1.5, eps);
    EXPECT_NEAR(p[3].x, 4, eps);
    EXPECT_NEAR(p[3].y, 0, eps);
    EXPECT_NEAR(p[4].x, 0, eps);
    EXPECT_NEAR(p[4].y, 1.5, eps);
    EXPECT_NEAR(p[5].x, 0, eps);
    EXPECT_NEAR(p[5].y, 4, eps);
    EXPECT_NEAR(p[6].x, 4, eps);
    EXPECT_NEAR(p[6].y, 0, eps);
    EXPECT_NEAR(p[7].x, 0, eps);
    EXPECT_NEAR(p[7].y, 4, eps);
    EXPECT_NEAR(p[8].x, 2.5, eps);
    EXPECT_NEAR(p[8].y, 4, eps);
    EXPECT_NEAR(p[9].x, 4, eps);
    EXPECT_NEAR(p[9].y, 0, eps);
    EXPECT_NEAR(p[10].x, 2.5, eps);
    EXPECT_NEAR(p[10].y, 4, eps);
    EXPECT_NEAR(p[11].x, 4, eps);
    EXPECT_NEAR(p[11].y, 1, eps);

    EXPECT_NEAR(uv[0].x, 4, eps);
    EXPECT_NEAR(uv[0].y, 0, eps);
    EXPECT_NEAR(uv[1].x, 3, eps);
    EXPECT_NEAR(uv[1].y, 0, eps);
    EXPECT_NEAR(uv[2].x, 0, eps);
    EXPECT_NEAR(uv[2].y, 1.5, eps);
    EXPECT_NEAR(uv[3].x, 4, eps);
    EXPECT_NEAR(uv[3].y, 0, eps);
    EXPECT_NEAR(uv[4].x, 0, eps);
    EXPECT_NEAR(uv[4].y, 1.5, eps);
    EXPECT_NEAR(uv[5].x, 0, eps);
    EXPECT_NEAR(uv[5].y, 4, eps);
    EXPECT_NEAR(uv[6].x, 4, eps);
    EXPECT_NEAR(uv[6].y, 0, eps);
    EXPECT_NEAR(uv[7].x, 0, eps);
    EXPECT_NEAR(uv[7].y, 4, eps);
    EXPECT_NEAR(uv[8].x, 2.5, eps);
    EXPECT_NEAR(uv[8].y, 4, eps);
    EXPECT_NEAR(uv[9].x, 4, eps);
    EXPECT_NEAR(uv[9].y, 0, eps);
    EXPECT_NEAR(uv[10].x, 2.5, eps);
    EXPECT_NEAR(uv[10].y, 4, eps);
    EXPECT_NEAR(uv[11].x, 4, eps);
    EXPECT_NEAR(uv[11].y, 1, eps);
}

TEST(TriangleClipping, three_outside_fill)
{
    std::vector<lm::vec2> p;
    std::vector<lm::vec2> uv;

    hrz::clip_triangle(
        lm::vec2(-1, -1), lm::vec2(-1, 6), lm::vec2(6, -1), lm::vec2(0, 0), lm::vec2(0, 7),
        lm::vec2(7, 0), lm::bbox2({0, 0}, {2, 2}),
        [&](lm::vec2 p0, lm::vec2 p1, lm::vec2 p2, lm::vec2 uv0, lm::vec2 uv1, lm::vec2 uv2)
        {
            p.push_back(p0);
            p.push_back(p1);
            p.push_back(p2);
            uv.push_back(uv0);
            uv.push_back(uv1);
            uv.push_back(uv2);
        });

    ASSERT_EQ(p.size(), 6);

    static const float eps = 1e-6;
    EXPECT_NEAR(p[0].x, 2, eps);
    EXPECT_NEAR(p[0].y, 2, eps);
    EXPECT_NEAR(p[1].x, 2, eps);
    EXPECT_NEAR(p[1].y, 0, eps);
    EXPECT_NEAR(p[2].x, 0, eps);
    EXPECT_NEAR(p[2].y, 0, eps);
    EXPECT_NEAR(p[3].x, 2, eps);
    EXPECT_NEAR(p[3].y, 2, eps);
    EXPECT_NEAR(p[4].x, 0, eps);
    EXPECT_NEAR(p[4].y, 0, eps);
    EXPECT_NEAR(p[5].x, 0, eps);
    EXPECT_NEAR(p[5].y, 2, eps);

    EXPECT_NEAR(uv[0].x, 3, eps);
    EXPECT_NEAR(uv[0].y, 3, eps);
    EXPECT_NEAR(uv[1].x, 3, eps);
    EXPECT_NEAR(uv[1].y, 1, eps);
    EXPECT_NEAR(uv[2].x, 1, eps);
    EXPECT_NEAR(uv[2].y, 1, eps);
    EXPECT_NEAR(uv[3].x, 3, eps);
    EXPECT_NEAR(uv[3].y, 3, eps);
    EXPECT_NEAR(uv[4].x, 1, eps);
    EXPECT_NEAR(uv[4].y, 1, eps);
    EXPECT_NEAR(uv[5].x, 1, eps);
    EXPECT_NEAR(uv[5].y, 3, eps);
}

TEST(SegmentClipping, all_inside)
{
    std::vector<lm::vec3> out;

    hrz::clip_segment<float>(
        lm::vec2(0.25), lm::vec2(0.75), lm::bbox2({0, 0}, {1, 1}),
        [&](const lm::vec2& a, const lm::vec2& b, float t0, float t1)
        {
            out.emplace_back(a, t0);
            out.emplace_back(b, t1);
        });

    ASSERT_EQ(out.size(), 2);

    static const float eps = 1e-6;
    EXPECT_NEAR(out[0].x, 0.25, eps);
    EXPECT_NEAR(out[0].y, 0.25, eps);
    EXPECT_NEAR(out[0].z, 0, eps);

    EXPECT_NEAR(out[1].x, 0.75, eps);
    EXPECT_NEAR(out[1].y, 0.75, eps);
    EXPECT_NEAR(out[1].z, 1, eps);
}

TEST(SegmentClipping, all_outside)
{
    std::vector<lm::vec3> out;

    hrz::clip_segment<float>(
        lm::vec2(0.25), lm::vec2(0.75), lm::bbox2({2, 2}, {4, 4}),
        [&](const lm::vec2& a, const lm::vec2& b, float az, float bz)
        {
            out.emplace_back(a, az);
            out.emplace_back(b, bz);
        });

    ASSERT_EQ(out.size(), 0);
}

TEST(SegmentClipping, first_outside)
{
    std::vector<lm::vec3> out;

    hrz::clip_segment<float>(
        lm::vec2(-0.5, 0.5), lm::vec2(0.5, 0.5), lm::bbox2({0, 0}, {1, 1}),
        [&](const lm::vec2& a, const lm::vec2& b, float az, float bz)
        {
            out.emplace_back(a, az);
            out.emplace_back(b, bz);
        });

    ASSERT_EQ(out.size(), 2);

    static const float eps = 1e-6;
    EXPECT_NEAR(out[0].x, 0.0, eps);
    EXPECT_NEAR(out[0].y, 0.5, eps);
    EXPECT_NEAR(out[0].z, 0.5, eps);

    EXPECT_NEAR(out[1].x, 0.5, eps);
    EXPECT_NEAR(out[1].y, 0.5, eps);
    EXPECT_NEAR(out[1].z, 1, eps);
}

TEST(SegmentClipping, second_outside)
{
    std::vector<lm::vec3> out;

    hrz::clip_segment<float>(
        lm::vec2(0.5, 0.5), lm::vec2(0.5, 1.5), lm::bbox2({0, 0}, {1, 1}),
        [&](const lm::vec2& a, const lm::vec2& b, float az, float bz)
        {
            out.emplace_back(a, az);
            out.emplace_back(b, bz);
        });

    ASSERT_EQ(out.size(), 2);

    static const float eps = 1e-6;
    EXPECT_NEAR(out[0].x, 0.5, eps);
    EXPECT_NEAR(out[0].y, 0.5, eps);
    EXPECT_NEAR(out[0].z, 0.0, eps);

    EXPECT_NEAR(out[1].x, 0.5, eps);
    EXPECT_NEAR(out[1].y, 1.0, eps);
    EXPECT_NEAR(out[1].z, 0.5, eps);
}

TEST(SegmentClipping, both_outside_intersecting)
{
    std::vector<lm::vec3> out;

    hrz::clip_segment<float>(
        lm::vec2(1.5, 2.0), lm::vec2(-0.5, 0), lm::bbox2({0, 0}, {1, 1}),
        [&](const lm::vec2& a, const lm::vec2& b, float az, float bz)
        {
            out.emplace_back(a, az);
            out.emplace_back(b, bz);
        });

    ASSERT_EQ(out.size(), 2);

    static const float eps = 1e-6;
    EXPECT_NEAR(out[0].x, 0.5, eps);
    EXPECT_NEAR(out[0].y, 1.0, eps);
    EXPECT_NEAR(out[0].z, 0.5, eps);

    EXPECT_NEAR(out[1].x, 0.0, eps);
    EXPECT_NEAR(out[1].y, 0.5, eps);
    EXPECT_NEAR(out[1].z, 0.75, eps);
}

} // namespace
