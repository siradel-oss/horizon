#pragma once

#include <gtest/gtest.h>
#include <proj_lite.h>

static void run_single_case(
    const char* from_str,
    const char* to_str,
    int n,
    const double* src_x,
    const double* src_y,
    const double* src_z,
    const double* dst_x,
    const double* dst_y,
    double eps)
{
    pl_Crs from;
    ASSERT_EQ(pl_Result_Ok, pl_crs_from_proj_zstr(from_str, &from));

    pl_Crs to;
    ASSERT_EQ(pl_Result_Ok, pl_crs_from_proj_zstr(to_str, &to));

    pl_Transform fwd;
    ASSERT_EQ(pl_Result_Ok, pl_bake_transform(&from, &to, &fwd));

    double x[8], y[8], z[8];

    double src_interleaved[24];
    double dst_interleaved[24];

    for (int i = 0; i < n; ++i)
    {
        src_interleaved[i * 3 + 0] = src_x[i];
        src_interleaved[i * 3 + 1] = src_y[i];
        src_interleaved[i * 3 + 2] = src_z[i];
    }

    ASSERT_EQ(
        pl_Result_Ok,
        pl_transform(
            &fwd, n, src_interleaved, src_interleaved + 1, src_interleaved + 2, 8 * 3, x, y, z, 8));

    ASSERT_EQ(
        pl_Result_Ok,
        pl_transform(
            &fwd, n, src_x, src_y, src_z, 8, dst_interleaved, dst_interleaved + 1,
            dst_interleaved + 2, 8 * 3));

    for (int i = 0; i < n; ++i)
    {
        EXPECT_NEAR(x[i], dst_x[i], eps);
        EXPECT_NEAR(y[i], dst_y[i], eps);

        EXPECT_NEAR(dst_interleaved[i * 3 + 0], dst_x[i], eps);
        EXPECT_NEAR(dst_interleaved[i * 3 + 1], dst_y[i], eps);
    }
}

static void run_single_case_3d(
    const char* from_str,
    const char* to_str,
    int n,
    const double* src_x,
    const double* src_y,
    const double* src_z,
    const double* dst_x,
    const double* dst_y,
    const double* dst_z,
    double eps)
{
    pl_Crs from;
    ASSERT_EQ(pl_Result_Ok, pl_crs_from_proj_zstr(from_str, &from));

    pl_Crs to;
    ASSERT_EQ(pl_Result_Ok, pl_crs_from_proj_zstr(to_str, &to));

    pl_Transform fwd;
    ASSERT_EQ(pl_Result_Ok, pl_bake_transform(&from, &to, &fwd));

    double x[8], y[8], z[8];

    double src_interleaved[24];
    double dst_interleaved[24];

    for (int i = 0; i < n; ++i)
    {
        src_interleaved[i * 3 + 0] = src_x[i];
        src_interleaved[i * 3 + 1] = src_y[i];
        src_interleaved[i * 3 + 2] = src_z[i];
    }

    ASSERT_EQ(
        pl_Result_Ok,
        pl_transform(
            &fwd, n, src_interleaved, src_interleaved + 1, src_interleaved + 2, 8 * 3, x, y, z, 8));

    ASSERT_EQ(
        pl_Result_Ok,
        pl_transform(
            &fwd, n, src_x, src_y, src_z, 8, dst_interleaved, dst_interleaved + 1,
            dst_interleaved + 2, 8 * 3));

    for (int i = 0; i < n; ++i)
    {
        EXPECT_NEAR(x[i], dst_x[i], eps);
        EXPECT_NEAR(y[i], dst_y[i], eps);
        EXPECT_NEAR(z[i], dst_z[i], eps);

        EXPECT_NEAR(dst_interleaved[i * 3 + 0], dst_x[i], eps);
        EXPECT_NEAR(dst_interleaved[i * 3 + 1], dst_y[i], eps);
        EXPECT_NEAR(dst_interleaved[i * 3 + 2], dst_z[i], eps);
    }
}
