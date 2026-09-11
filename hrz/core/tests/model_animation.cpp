// SPDX-FileCopyrightText: Copyright 2025 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/core/model/animation_player.h"

#include <gtest/gtest.h>

using namespace hrz::model;
using hrz::model::Animation;

TEST(AnimationPlayer, animation_valid)
{
    Animation anim;

    anim.samplers.push_back(
        Animation::Sampler{
            .interpolation = AnimationInterpolation::Linear,
            .timestamps = {0.0F, 1.0F, 2.0F, 3.0F},
            .values = {
                0.0F, 1.0F, 2.0F, 10.0F, 11.0F, 12.0F, 20.0F, 21.0F, 22.0F, 30.0F, 31.0F, 32.0F
            },
        });

    anim.channels.push_back(
        Animation::Channel{
            .sampler = 0,
            .target_id = 8,
            .target_property = AnimationTargetProperty::Translation,
        });

    EXPECT_TRUE(anim.is_valid());
}

TEST(AnimationPlayer, animation_invalid_sampler)
{
    Animation anim;

    anim.samplers.push_back(
        Animation::Sampler{
            .interpolation = AnimationInterpolation::Linear,
            .timestamps = {0.0F, 1.0F, 2.0F, 3.0F},
            .values = {
                0.0F, 1.0F, 2.0F, 10.0F, 11.0F, 12.0F, 20.0F, 21.0F, 22.0F, 30.0F, 31.0F, 32.0F
            },
        });

    anim.channels.push_back(
        Animation::Channel{
            .sampler = 2,
            .target_id = 8,
            .target_property = AnimationTargetProperty::Translation,
        });

    EXPECT_FALSE(anim.is_valid());
}

TEST(AnimationPlayer, animation_not_enough_keyframes)
{
    Animation anim;

    anim.samplers.push_back(
        Animation::Sampler{
            .interpolation = AnimationInterpolation::Linear,
            .timestamps = {},
            .values = {
                0.0F, 1.0F, 2.0F, 10.0F, 11.0F, 12.0F, 20.0F, 21.0F, 22.0F, 30.0F, 31.0F, 32.0F
            },
        });

    anim.channels.push_back(
        Animation::Channel{
            .sampler = 2,
            .target_id = 8,
            .target_property = AnimationTargetProperty::Translation,
        });

    EXPECT_FALSE(anim.is_valid());
}

TEST(AnimationPlayer, animation_not_enough_keyframes_2)
{
    Animation anim;

    anim.samplers.push_back(
        Animation::Sampler{
            .interpolation = AnimationInterpolation::CubicSpline,
            .timestamps = {0.0F},
            .values = {
                0.0F, 1.0F, 2.0F, 10.0F, 11.0F, 12.0F, 20.0F, 21.0F, 22.0F, 30.0F, 31.0F, 32.0F
            },
        });

    anim.channels.push_back(
        Animation::Channel{
            .sampler = 2,
            .target_id = 8,
            .target_property = AnimationTargetProperty::Translation,
        });

    EXPECT_FALSE(anim.is_valid());
}

TEST(AnimationPlayer, animation_not_enough_values)
{
    Animation anim;

    anim.samplers.push_back(
        Animation::Sampler{
            .interpolation = AnimationInterpolation::CubicSpline,
            .timestamps = {0.0F, 1.0F},
            .values = {
                0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F,
                0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F,
            },
        });

    anim.channels.push_back(
        Animation::Channel{
            .sampler = 2,
            .target_id = 8,
            .target_property = AnimationTargetProperty::Rotation,
        });

    EXPECT_FALSE(anim.is_valid());
}

TEST(AnimationPlayer, sampler_set_time_linear)
{
    Animation anim;

    anim.samplers.push_back(
        Animation::Sampler{
            .interpolation = AnimationInterpolation::Linear,
            .timestamps = {0.0F, 1.0F, 2.0F, 3.0F},
            .values = {
                0.0F, 1.0F, 2.0F, 10.0F, 11.0F, 12.0F, 20.0F, 21.0F, 22.0F, 30.0F, 31.0F, 32.0F
            },
        });

    anim.channels.push_back(
        Animation::Channel{
            .sampler = 0,
            .target_id = 8,
            .target_property = AnimationTargetProperty::Translation,
        });

    AnimationPlayer player(anim);

    auto test_keyframe = [&](float time, lm::vec3 expected)
    {
        player.set_time(anim, time);
        bool seen_channel = false;
        player.fetch_channel_values(
            anim,
            [&seen_channel, &expected](
                int target_id, AnimationTargetProperty target_property,
                const std::variant<lm::vec3, lm::quat>& value)
            {
                EXPECT_EQ(target_id, 8);
                EXPECT_EQ(target_property, AnimationTargetProperty::Translation);
                ASSERT_TRUE(std::holds_alternative<lm::vec3>(value));
                lm::vec3 v = std::get<lm::vec3>(value);
                EXPECT_NEAR(v.x, expected.x, 0.01F);
                EXPECT_NEAR(v.y, expected.y, 0.01F);
                EXPECT_NEAR(v.z, expected.z, 0.01F);

                EXPECT_FALSE(seen_channel);
                seen_channel = true;
            });
        EXPECT_TRUE(seen_channel);
    };

    test_keyframe(1.5F, lm::vec3{15.0F, 16.0F, 17.0F});
    test_keyframe(2.0F, lm::vec3{20.0F, 21.0F, 22.0F});
    test_keyframe(3.1F, lm::vec3{1.0F, 2.0F, 3.0F});
    test_keyframe(-0.2F, lm::vec3{28.0F, 29.0F, 30.0F});
    test_keyframe(0.0F, lm::vec3{0.0F, 1.0F, 2.0F});
    test_keyframe(3.0F, lm::vec3{0.0F, 1.0F, 2.0F});
}

TEST(AnimationPlayer, sampler_set_time_step)
{
    Animation anim;

    anim.samplers.push_back(
        Animation::Sampler{
            .interpolation = AnimationInterpolation::Step,
            .timestamps = {0.0F, 1.0F, 2.0F, 3.0F},
            .values = {
                0.0F, 1.0F, 2.0F, 10.0F, 11.0F, 12.0F, 20.0F, 21.0F, 22.0F, 30.0F, 31.0F, 32.0F
            },
        });

    anim.channels.push_back(
        Animation::Channel{
            .sampler = 0,
            .target_id = 8,
            .target_property = AnimationTargetProperty::Translation,
        });

    AnimationPlayer player(anim);

    auto test_keyframe = [&](float time, lm::vec3 expected)
    {
        player.set_time(anim, time);
        bool seen_channel = false;
        player.fetch_channel_values(
            anim,
            [&seen_channel, &expected](
                int target_id, AnimationTargetProperty target_property,
                const std::variant<lm::vec3, lm::quat>& value)
            {
                EXPECT_EQ(target_id, 8);
                EXPECT_EQ(target_property, AnimationTargetProperty::Translation);
                ASSERT_TRUE(std::holds_alternative<lm::vec3>(value));
                lm::vec3 v = std::get<lm::vec3>(value);
                EXPECT_NEAR(v.x, expected.x, 0.01F);
                EXPECT_NEAR(v.y, expected.y, 0.01F);
                EXPECT_NEAR(v.z, expected.z, 0.01F);

                EXPECT_FALSE(seen_channel);
                seen_channel = true;
            });
        EXPECT_TRUE(seen_channel);
    };

    test_keyframe(1.5F, lm::vec3{10.0F, 11.0F, 12.0F});
    test_keyframe(2.0F, lm::vec3{20.0F, 21.0F, 22.0F});
    test_keyframe(3.1F, lm::vec3{0.0F, 1.0F, 2.0F});
    test_keyframe(-0.2F, lm::vec3{20.0F, 21.0F, 22.0F});
    test_keyframe(0.0F, lm::vec3{0.0F, 1.0F, 2.0F});
    test_keyframe(3.0F, lm::vec3{0.0F, 1.0F, 2.0F});
}

TEST(AnimationPlayer, sampler_advance_time)
{
    Animation anim;

    anim.samplers.push_back(
        Animation::Sampler{
            .interpolation = AnimationInterpolation::Linear,
            .timestamps = {0.0F, 1.0F, 2.0F, 3.0F},
            .values = {
                0.0F, 1.0F, 2.0F, 10.0F, 11.0F, 12.0F, 20.0F, 21.0F, 22.0F, 30.0F, 31.0F, 32.0F
            },
        });

    anim.channels.push_back(
        Animation::Channel{
            .sampler = 0,
            .target_id = 8,
            .target_property = AnimationTargetProperty::Translation,
        });

    AnimationPlayer player(anim);

    auto test_keyframe = [&](float dt, lm::vec3 expected)
    {
        player.advance_time(anim, dt);
        bool seen_channel = false;
        player.fetch_channel_values(
            anim,
            [&seen_channel, &expected](
                int target_id, AnimationTargetProperty target_property,
                const std::variant<lm::vec3, lm::quat>& value)
            {
                EXPECT_EQ(target_id, 8);
                EXPECT_EQ(target_property, AnimationTargetProperty::Translation);
                ASSERT_TRUE(std::holds_alternative<lm::vec3>(value));
                lm::vec3 v = std::get<lm::vec3>(value);
                EXPECT_NEAR(v.x, expected.x, 0.01F);
                EXPECT_NEAR(v.y, expected.y, 0.01F);
                EXPECT_NEAR(v.z, expected.z, 0.01F);

                EXPECT_FALSE(seen_channel);
                seen_channel = true;
            });
        EXPECT_TRUE(seen_channel);
    };

    player.set_time(anim, 0.0F);

    test_keyframe(0.0F, lm::vec3{0.0F, 1.0F, 2.0F});
    test_keyframe(0.1F, lm::vec3{1.0F, 2.0F, 3.0F});
    test_keyframe(0.5F, lm::vec3{6.0F, 7.0F, 8.0F});
    test_keyframe(0.4F, lm::vec3{10.0F, 11.0F, 12.0F});
    test_keyframe(1.9F, lm::vec3{29.0F, 30.0F, 31.0F});
    test_keyframe(0.2F, lm::vec3{1.0F, 2.0F, 3.0F});
}

TEST(AnimationPlayer, sampler_advance_time_backwards)
{
    Animation anim;

    anim.samplers.push_back(
        Animation::Sampler{
            .interpolation = AnimationInterpolation::Linear,
            .timestamps = {0.0F, 1.0F, 2.0F, 3.0F},
            .values = {
                0.0F, 1.0F, 2.0F, 10.0F, 11.0F, 12.0F, 20.0F, 21.0F, 22.0F, 30.0F, 31.0F, 32.0F
            },
        });

    anim.channels.push_back(
        Animation::Channel{
            .sampler = 0,
            .target_id = 8,
            .target_property = AnimationTargetProperty::Translation,
        });

    AnimationPlayer player(anim);

    auto test_keyframe = [&](float dt, lm::vec3 expected)
    {
        player.advance_time(anim, dt);
        bool seen_channel = false;
        player.fetch_channel_values(
            anim,
            [&seen_channel, &expected](
                int target_id, AnimationTargetProperty target_property,
                const std::variant<lm::vec3, lm::quat>& value)
            {
                EXPECT_EQ(target_id, 8);
                EXPECT_EQ(target_property, AnimationTargetProperty::Translation);
                ASSERT_TRUE(std::holds_alternative<lm::vec3>(value));
                lm::vec3 v = std::get<lm::vec3>(value);
                EXPECT_NEAR(v.x, expected.x, 0.01F);
                EXPECT_NEAR(v.y, expected.y, 0.01F);
                EXPECT_NEAR(v.z, expected.z, 0.01F);

                EXPECT_FALSE(seen_channel);
                seen_channel = true;
            });
        EXPECT_TRUE(seen_channel);
    };

    player.set_time(anim, 0.0F);

    test_keyframe(0.0F, lm::vec3{0.0F, 1.0F, 2.0F});
    test_keyframe(-0.1F, lm::vec3{29.0F, 30.0F, 31.0F});
    test_keyframe(-0.5F, lm::vec3{24.0F, 25.0F, 26.0F});
    test_keyframe(-0.4F, lm::vec3{20.0F, 21.0F, 22.0F});
    test_keyframe(-1.9F, lm::vec3{1.0F, 2.0F, 3.0F});
    test_keyframe(-0.2F, lm::vec3{29.0F, 30.0F, 31.0F});
}

TEST(AnimationPlayer, multiple_samplers)
{
    Animation anim;

    anim.samplers.push_back(
        Animation::Sampler{
            .interpolation = AnimationInterpolation::Linear,
            .timestamps = {1.0F, 3.0F},
            .values = {0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F},
        });

    anim.samplers.push_back(
        Animation::Sampler{
            .interpolation = AnimationInterpolation::Linear,
            .timestamps = {0.0F, 4.0F},
            .values = {0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F},
        });

    anim.channels.push_back(
        Animation::Channel{
            .sampler = 0,
            .target_id = 0,
            .target_property = AnimationTargetProperty::Translation,
        });

    anim.channels.push_back(
        Animation::Channel{
            .sampler = 1,
            .target_id = 1,
            .target_property = AnimationTargetProperty::Translation,
        });

    AnimationPlayer player(anim);

    auto test_keyframe = [&](float dt, lm::vec3 expected0, lm::vec3 expected1)
    {
        player.advance_time(anim, dt);
        int seen_channel[2] = {0, 0};

        player.fetch_channel_values(
            anim,
            [&seen_channel, &expected0, &expected1](
                int target_id, AnimationTargetProperty target_property,
                const std::variant<lm::vec3, lm::quat>& value)
            {
                EXPECT_EQ(target_id, target_id);
                EXPECT_EQ(target_property, AnimationTargetProperty::Translation);
                ASSERT_TRUE(std::holds_alternative<lm::vec3>(value));
                lm::vec3 v = std::get<lm::vec3>(value);

                if (target_id == 0)
                {
                    EXPECT_NEAR(v.x, expected0.x, 0.01F);
                    EXPECT_NEAR(v.y, expected0.y, 0.01F);
                    EXPECT_NEAR(v.z, expected0.z, 0.01F);

                    seen_channel[0] += 1;
                    return;
                }
                else if (target_id == 1)
                {
                    EXPECT_NEAR(v.x, expected1.x, 0.01F);
                    EXPECT_NEAR(v.y, expected1.y, 0.01F);
                    EXPECT_NEAR(v.z, expected1.z, 0.01F);

                    seen_channel[1] += 1;
                    return;
                }
            });

        EXPECT_EQ(seen_channel[0], 1);
        EXPECT_EQ(seen_channel[1], 1);
    };

    player.set_time(anim, 0.0F);

    test_keyframe(0.0F, lm::vec3{0.0F}, lm::vec3{0.0F}); // 0
    // test_keyframe(0.5F, lm::vec3{0.0F}, lm::vec3{0.125F}); // 0.5
    // test_keyframe(0.5F, lm::vec3{0.0F}, lm::vec3{0.25F}); // 1
    // test_keyframe(0.5F, lm::vec3{0.5F}, lm::vec3{0.375F}); // 1.5
    // test_keyframe(0.5F, lm::vec3{1.0F}, lm::vec3{0.5F}); // 2
    // test_keyframe(0.5F, lm::vec3{1.0F}, lm::vec3{0.625F}); // 2.5
    // test_keyframe(0.5F, lm::vec3{1.0F}, lm::vec3{0.75F}); // 3
    // test_keyframe(0.5F, lm::vec3{1.0F}, lm::vec3{0.875F}); // 3.5
    // test_keyframe(0.5F, lm::vec3{0.0F}, lm::vec3{0.0F}); // 4
}
