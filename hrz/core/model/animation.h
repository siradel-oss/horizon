// SPDX-FileCopyrightText: Copyright 2025 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include <vector>

namespace hrz::model
{

enum class AnimationInterpolation
{
    Linear,
    Step,
    CubicSpline,
};

enum class AnimationTargetProperty
{
    Translation,
    Rotation,
    Scale,
};

struct Animation
{
    struct Sampler
    {
        AnimationInterpolation interpolation;

        // @Todo Samplers could share the same timestamps, so we could deduplicate them.
        // Probably not a huge issue right now since animations tend to be small.

        // One per keyframe. Must be sorted.
        std::vector<float> timestamps;

        // lm::vec3 for translation and scale, lm::quat for rotation.
        // Also, 3x the number of keyframes for cubic spline (in tangent, vertex, out tangent).
        std::vector<float> values;
    };

    struct Channel
    {
        // Sampler index in the samplers array.
        int sampler;

        // Target of this channel, externally managed.
        // For instance for glTF this is the node index.
        int target_id;

        AnimationTargetProperty target_property;
    };

    std::vector<Sampler> samplers;
    std::vector<Channel> channels;

    bool is_valid() const;
};

} // namespace hrz::model
