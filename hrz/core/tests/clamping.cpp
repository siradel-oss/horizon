// SPDX-FileCopyrightText: Copyright 2021 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/core/jobs/feature_clamping.h"
#include "hrz/fnd/defines.h"

#include <gtest/gtest.h>

namespace
{

using namespace hrz;

TEST(FeatureClamping, no_clamping_no_z)
{
    hrz_proto::VectorClamping config;
    config.set_method(hrz_proto::VectorClampMode::NO_CLAMPING);
    config.set_use_z(false);

    static const float clamps[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};

    FeatureClampingGenerator gen(clamps, config);

    {
        PointClampingGenerator c = gen.for_feature(0, 0);
        ASSERT_FLOAT_EQ(c.clamp_point(0, 10.0), 0.0);
        ASSERT_FLOAT_EQ(c.clamp_point(1, 20.0), 0.0);
        ASSERT_FLOAT_EQ(c.clamp_point(2, 30.0), 0.0);
    }

    {
        PointClampingGenerator c = gen.for_feature(1, 3);
        ASSERT_FLOAT_EQ(c.clamp_point(0, 40.0), 0.0);
        ASSERT_FLOAT_EQ(c.clamp_point(1, 50.0), 0.0);
        ASSERT_FLOAT_EQ(c.clamp_point(2, 60.0), 0.0);
    }

    {
        PointClampingGenerator c = gen.for_feature(2, 6);
        ASSERT_FLOAT_EQ(c.clamp_point(0, 70.0), 0.0);
        ASSERT_FLOAT_EQ(c.clamp_point(1, 80.0), 0.0);
        ASSERT_FLOAT_EQ(c.clamp_point(2, 90.0), 0.0);
    }
}

TEST(FeatureClamping, no_clamping_use_z)
{
    hrz_proto::VectorClamping config;
    config.set_method(hrz_proto::VectorClampMode::NO_CLAMPING);
    config.set_use_z(true);

    static const float clamps[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};

    FeatureClampingGenerator gen(clamps, config);

    {
        PointClampingGenerator c = gen.for_feature(0, 0);
        ASSERT_FLOAT_EQ(c.clamp_point(0, 10.0), 10.0);
        ASSERT_FLOAT_EQ(c.clamp_point(1, 20.0), 20.0);
        ASSERT_FLOAT_EQ(c.clamp_point(2, 30.0), 30.0);
    }

    {
        PointClampingGenerator c = gen.for_feature(1, 3);
        ASSERT_FLOAT_EQ(c.clamp_point(0, 40.0), 40.0);
        ASSERT_FLOAT_EQ(c.clamp_point(1, 50.0), 50.0);
        ASSERT_FLOAT_EQ(c.clamp_point(2, 60.0), 60.0);
    }

    {
        PointClampingGenerator c = gen.for_feature(2, 6);
        ASSERT_FLOAT_EQ(c.clamp_point(0, 70.0), 70.0);
        ASSERT_FLOAT_EQ(c.clamp_point(1, 80.0), 80.0);
        ASSERT_FLOAT_EQ(c.clamp_point(2, 90.0), 90.0);
    }
}

TEST(FeatureClamping, anchor_no_z)
{
    hrz_proto::VectorClamping config;
    config.set_method(hrz_proto::VectorClampMode::ANCHOR);
    config.set_use_z(false);

    static const float clamps[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};

    FeatureClampingGenerator gen(clamps, config);

    {
        PointClampingGenerator c = gen.for_feature(0, 0);
        ASSERT_FLOAT_EQ(c.clamp_point(0, 10.0), 1.0);
        ASSERT_FLOAT_EQ(c.clamp_point(1, 20.0), 1.0);
        ASSERT_FLOAT_EQ(c.clamp_point(2, 30.0), 1.0);
    }

    {
        PointClampingGenerator c = gen.for_feature(1, 3);
        ASSERT_FLOAT_EQ(c.clamp_point(0, 40.0), 2.0);
        ASSERT_FLOAT_EQ(c.clamp_point(1, 50.0), 2.0);
        ASSERT_FLOAT_EQ(c.clamp_point(2, 60.0), 2.0);
    }

    {
        PointClampingGenerator c = gen.for_feature(2, 6);
        ASSERT_FLOAT_EQ(c.clamp_point(0, 70.0), 3.0);
        ASSERT_FLOAT_EQ(c.clamp_point(1, 80.0), 3.0);
        ASSERT_FLOAT_EQ(c.clamp_point(2, 90.0), 3.0);
    }
}

TEST(FeatureClamping, anchor_use_z)
{
    hrz_proto::VectorClamping config;
    config.set_method(hrz_proto::VectorClampMode::ANCHOR);
    config.set_use_z(true);

    static const float clamps[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};

    FeatureClampingGenerator gen(clamps, config);

    {
        PointClampingGenerator c = gen.for_feature(0, 0);
        ASSERT_FLOAT_EQ(c.clamp_point(0, 10.0), 11.0);
        ASSERT_FLOAT_EQ(c.clamp_point(1, 20.0), 21.0);
        ASSERT_FLOAT_EQ(c.clamp_point(2, 30.0), 31.0);
    }

    {
        PointClampingGenerator c = gen.for_feature(1, 3);
        ASSERT_FLOAT_EQ(c.clamp_point(0, 40.0), 42.0);
        ASSERT_FLOAT_EQ(c.clamp_point(1, 50.0), 52.0);
        ASSERT_FLOAT_EQ(c.clamp_point(2, 60.0), 62.0);
    }

    {
        PointClampingGenerator c = gen.for_feature(2, 6);
        ASSERT_FLOAT_EQ(c.clamp_point(0, 70.0), 73.0);
        ASSERT_FLOAT_EQ(c.clamp_point(1, 80.0), 83.0);
        ASSERT_FLOAT_EQ(c.clamp_point(2, 90.0), 93.0);
    }
}

TEST(FeatureClamping, per_vertex_no_z)
{
    hrz_proto::VectorClamping config;
    config.set_method(hrz_proto::VectorClampMode::PER_VERTEX);
    config.set_use_z(false);

    static const float clamps[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};

    FeatureClampingGenerator gen(clamps, config);

    {
        PointClampingGenerator c = gen.for_feature(0, 0);
        ASSERT_FLOAT_EQ(c.clamp_point(0, 10.0), 1.0);
        ASSERT_FLOAT_EQ(c.clamp_point(1, 20.0), 2.0);
        ASSERT_FLOAT_EQ(c.clamp_point(2, 30.0), 3.0);
    }

    {
        PointClampingGenerator c = gen.for_feature(1, 3);
        ASSERT_FLOAT_EQ(c.clamp_point(0, 40.0), 4.0);
        ASSERT_FLOAT_EQ(c.clamp_point(1, 50.0), 5.0);
        ASSERT_FLOAT_EQ(c.clamp_point(2, 60.0), 6.0);
    }

    {
        PointClampingGenerator c = gen.for_feature(2, 6);
        ASSERT_FLOAT_EQ(c.clamp_point(0, 70.0), 7.0);
        ASSERT_FLOAT_EQ(c.clamp_point(1, 80.0), 8.0);
        ASSERT_FLOAT_EQ(c.clamp_point(2, 90.0), 9.0);
    }
}

TEST(FeatureClamping, per_vertex_use_z)
{
    hrz_proto::VectorClamping config;
    config.set_method(hrz_proto::VectorClampMode::PER_VERTEX);
    config.set_use_z(true);

    static const float clamps[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};

    FeatureClampingGenerator gen(clamps, config);

    {
        PointClampingGenerator c = gen.for_feature(0, 0);
        ASSERT_FLOAT_EQ(c.clamp_point(0, 10.0), 11.0);
        ASSERT_FLOAT_EQ(c.clamp_point(1, 20.0), 22.0);
        ASSERT_FLOAT_EQ(c.clamp_point(2, 30.0), 33.0);
    }

    {
        PointClampingGenerator c = gen.for_feature(1, 3);
        ASSERT_FLOAT_EQ(c.clamp_point(0, 40.0), 44.0);
        ASSERT_FLOAT_EQ(c.clamp_point(1, 50.0), 55.0);
        ASSERT_FLOAT_EQ(c.clamp_point(2, 60.0), 66.0);
    }

    {
        PointClampingGenerator c = gen.for_feature(2, 6);
        ASSERT_FLOAT_EQ(c.clamp_point(0, 70.0), 77.0);
        ASSERT_FLOAT_EQ(c.clamp_point(1, 80.0), 88.0);
        ASSERT_FLOAT_EQ(c.clamp_point(2, 90.0), 99.0);
    }
}

} // namespace
