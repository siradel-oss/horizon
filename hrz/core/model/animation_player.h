// SPDX-FileCopyrightText: Copyright 2025 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/core/model/animation.h"
#include "hrz/fnd/function_ref.h"
#include "hrz/fnd/mem.h"

#include <lin_maths.h>

#include <variant>
#include <vector>

namespace hrz::model
{

struct LerpVec3Interpolator
{
    lm::vec3 v0, v1;

    constexpr LerpVec3Interpolator(const lm::vec3& v0, const lm::vec3& v1) : v0(v0), v1(v1) {}

    constexpr lm::vec3 operator ()(float t) const { return lm::mix(v0, v1, t); }
};

struct SlerpQuatInterpolator
{
    lm::PrecomputedSlerp<float> slerp;

    SlerpQuatInterpolator(const lm::quat& q0, const lm::quat& q1) : slerp(q0, q1) {}

    inline lm::quat operator ()(float t) const { return slerp.slerp(t); }
};

template<typename T>
struct StepInterpolation
{
    T value;

    explicit constexpr StepInterpolation(const T& value_) : value(value_) {}

    constexpr T operator ()(float /*t*/) const { return value; }
};

template<typename T>
struct CubicSplineInterpolator
{
    static const size_t kElementCount = HRZ_ARRAY_COUNT(std::declval<T>().m);
    T p0, p1; // Positions
    T b0, a1; // Tangents

    CubicSplineInterpolator(const T& p0, const T& b0, const T& p1, const T& a1) :
        p0(p0), p1(p1), b0(b0), a1(a1)
    {
    }

    T operator ()(float t) const
    {
        const float t2 = t * t;
        const float t3 = t2 * t;

        const float a = (2 * t3 - 3 * t2 + 1);
        const float b = (t3 - 2 * t2 + t);
        const float c = (-2 * t3 + 3 * t2);
        const float d = (t3 - t2);

        T v;
        for (size_t i = 0; i < kElementCount; ++i)
        {
            v.m[i] = a * p0.m[i] + b * b0.m[i] + c * p1.m[i] + d * a1.m[i];
        }

        if constexpr (std::same_as<T, lm::quat>)
        {
            v = lm::normalize(v);
        }

        return v;
    }
};

// When using this class, the caller is responsible for always passing
// the same Animation in (unchanged). Otherwise, the behavior is undefined.
class AnimationPlayer
{
    struct SamplerState
    {
        enum Status
        {
            kInvalid,
            kUpdate,
            kSameInterval,
        };

        // Current keyframe interval data.
        // t0 <= t < t1

        int i0{}, i1{};              // Indices of the first keyframe and second keyframe.
        float t0 = 0.0F, t1 = -1.0F; // Timestamps of first and second keyframes.

        Status last_update_status{Status::kInvalid};

        void set_time(const Animation::Sampler&, float t);
    };

    struct ChannelState
    {
        using Interpolator = std::variant<
            std::monostate,
            LerpVec3Interpolator,
            SlerpQuatInterpolator,
            StepInterpolation<lm::quat>,
            StepInterpolation<lm::vec3>,
            CubicSplineInterpolator<lm::vec3>,
            CubicSplineInterpolator<lm::quat>
        >;

        Interpolator interpolator;

        void update_interpolator(
            const Animation::Channel& channel,
            const Animation::Sampler& sampler,
            const SamplerState& sampler_state);
    };

    float period;
    float t{}; // Timestamp, modulo period.

    std::vector<SamplerState> _sampler_states;
    std::vector<ChannelState> _channel_states;

    void update_interpolators(const Animation& animation);

public:
    explicit AnimationPlayer(const Animation& animation);

    void set_time(const Animation& animation, double t);
    void advance_time(const Animation& animation, float dt);

    using ChannelValueCallback = void(
        int target_id,
        AnimationTargetProperty target_property,
        const std::variant<lm::vec3, lm::quat>& value);

    void fetch_channel_values(
        const Animation& anim,
        hrz::function_ref<ChannelValueCallback> callback) const;
};

} // namespace hrz::model
