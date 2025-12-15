#include "hrz/core/model/animation_player.h"
#include "hrz/fnd/meta.h"

#include <assert.h>

#include <algorithm>

namespace
{

float normalize_time(double t, float period)
{
    if (period > 0.0F)
    {
        t = std::fmod(t, (double)period);
        if (t < 0.0)
        {
            t += (double)period;
        }
    }
    else
    {
        t = 0.0;
    }
    return (float)t;
}

inline lm::vec3 fetch_vec3(std::span<const float> values, size_t index)
{
    const float* ptr = values.data() + index * 3;
    return lm::vec3{ptr[0], ptr[1], ptr[2]};
}

inline lm::quat fetch_quat(std::span<const float> values, size_t index)
{
    const float* ptr = values.data() + index * 4;
    return lm::quat{ptr[0], ptr[1], ptr[2], ptr[3]};
}

} // anonymous namespace

namespace hrz::model
{

bool Animation::is_valid() const
{
    for (const auto& channel : channels)
    {
        // Check that each sampler exists.
        if (channel.sampler < 0 || static_cast<size_t>(channel.sampler) >= samplers.size())
        {
            return false;
        }

        const auto& sampler = samplers[(size_t)channel.sampler];

        const size_t keyframe_count = sampler.timestamps.size();
        if (sampler.interpolation == AnimationInterpolation::CubicSpline)
        {
            if (keyframe_count < 2)
            {
                return false;
            }
        }
        else
        {
            if (keyframe_count < 1)
            {
                return false;
            }
        }

        size_t value_count = keyframe_count;

        if (channel.target_property == AnimationTargetProperty::Rotation)
        {
            value_count *= 4;
        }
        else
        {
            value_count *= 3;
        }

        if (sampler.interpolation == AnimationInterpolation::CubicSpline)
        {
            value_count *= 3;
        }

        if (sampler.values.size() < value_count)
        {
            return false;
        }
    }

    return true;
}

void AnimationPlayer::SamplerState::set_time(const Animation::Sampler& sampler, float new_t)
{
    if (sampler.timestamps.empty())
    {
        last_update_status = Status::kInvalid;
        return;
    }

    if (new_t >= t0 && new_t < t1)
    {
        last_update_status = Status::kSameInterval;
        return;
    }

    if (const auto it_t1 = std::ranges::upper_bound(sampler.timestamps, new_t);
        it_t1 == sampler.timestamps.begin())
    {
        // Before first keyframe.
        // We make the value constant from 0 to the first keyframe.
        i0 = 0;
        i1 = 0;
        t0 = std::numeric_limits<float>::lowest();
        t1 = sampler.timestamps[0];
    }
    else if (it_t1 == sampler.timestamps.end())
    {
        // After last keyframe.
        // We make the value constant from the last keyframe to the end of the period.
        i0 = (int)sampler.timestamps.size() - 1;
        i1 = i0;
        t0 = sampler.timestamps[(size_t)i0];
        t1 = std::numeric_limits<float>::max();
    }
    else
    {
        // Between two keyframes
        i1 = static_cast<int>(std::distance(sampler.timestamps.begin(), it_t1));
        i0 = i1 - 1;
        t0 = sampler.timestamps[(size_t)i0];
        t1 = sampler.timestamps[(size_t)i1];
    }

    last_update_status = Status::kUpdate;
}

void AnimationPlayer::ChannelState::update_interpolator(
    const Animation::Channel& channel,
    const Animation::Sampler& sampler,
    const SamplerState& sampler_state)
{
    const bool is_rotation = channel.target_property == AnimationTargetProperty::Rotation;

    switch (sampler.interpolation)
    {
        using enum AnimationInterpolation;

        case Step:
        {
            if (is_rotation)
            {
                const lm::quat value = fetch_quat(sampler.values, (size_t)sampler_state.i0);
                interpolator = StepInterpolation<lm::quat>{value};
            }
            else
            {
                const lm::vec3 value = fetch_vec3(sampler.values, (size_t)sampler_state.i0);
                interpolator = StepInterpolation<lm::vec3>{value};
            }
            break;
        }
        case CubicSpline:
        {
            if (is_rotation)
            {
                const lm::quat q0 = fetch_quat(sampler.values, (size_t)sampler_state.i0 * 3 + 1);
                const lm::quat q1 = fetch_quat(sampler.values, (size_t)sampler_state.i1 * 3 + 1);
                const lm::quat b0 = fetch_quat(sampler.values, (size_t)sampler_state.i0 * 3 + 2);
                const lm::quat a1 = fetch_quat(sampler.values, (size_t)sampler_state.i1 * 3 + 0);
                interpolator = CubicSplineInterpolator<lm::quat>(q0, b0, q1, a1);
            }
            else
            {
                const lm::vec3 v0 = fetch_vec3(sampler.values, (size_t)sampler_state.i0 * 3 + 1);
                const lm::vec3 v1 = fetch_vec3(sampler.values, (size_t)sampler_state.i1 * 3 + 1);
                const lm::vec3 b0 = fetch_vec3(sampler.values, (size_t)sampler_state.i0 * 3 + 2);
                const lm::vec3 a1 = fetch_vec3(sampler.values, (size_t)sampler_state.i1 * 3 + 0);
                interpolator = CubicSplineInterpolator<lm::vec3>(v0, b0, v1, a1);
            }
            break;
        }
        case Linear:
        default:
        {
            if (is_rotation)
            {
                const lm::quat q0 = fetch_quat(sampler.values, (size_t)sampler_state.i0);
                const lm::quat q1 = fetch_quat(sampler.values, (size_t)sampler_state.i1);
                interpolator = SlerpQuatInterpolator(q0, q1);
            }
            else
            {
                const lm::vec3 v0 = fetch_vec3(sampler.values, (size_t)sampler_state.i0);
                const lm::vec3 v1 = fetch_vec3(sampler.values, (size_t)sampler_state.i1);
                interpolator = LerpVec3Interpolator(v0, v1);
            }
            break;
        }
    }
}

AnimationPlayer::AnimationPlayer(const Animation& animation) : period(0.0F)
{
    if (animation.is_valid())
    {
        _sampler_states.resize(animation.samplers.size());
        _channel_states.resize(animation.channels.size());

        // Compute the period as the max timestamp across all samplers.
        for (const auto& sampler : animation.samplers)
        {
            if (!sampler.timestamps.empty())
            {
                period = std::max(period, sampler.timestamps.back());
            }
        }
    }
}

void AnimationPlayer::update_interpolators(const Animation& animation)
{
    for (size_t i = 0; i < animation.channels.size(); ++i)
    {
        const auto& channel = animation.channels[i];
        const auto& sampler_state = _sampler_states[(size_t)channel.sampler];

        switch (sampler_state.last_update_status)
        {
            using enum SamplerState::Status;

            case kSameInterval:
            {
                // Nothing changed, keep the same interpolator.
                break;
            }
            case kUpdate:
            {
                const Animation::Sampler& sampler = animation.samplers[i];
                _channel_states[i].update_interpolator(channel, sampler, sampler_state);
                break;
            }
            case kInvalid:
            default:
            {
                // No keyframes, nothing to do.
                _channel_states[i].interpolator = std::monostate{};
                break;
            }
        }
    }
}

void AnimationPlayer::advance_time(const Animation& animation, float dt)
{
    set_time(animation, (double)(t + dt));
}

void AnimationPlayer::set_time(const Animation& animation, double new_t)
{
    if (_channel_states.empty())
    {
        return;
    }

    t = normalize_time(new_t, period);

    assert(animation.samplers.size() == _sampler_states.size());

    bool needs_update = false;
    for (size_t i = 0; i < animation.samplers.size(); ++i)
    {
        _sampler_states[i].set_time(animation.samplers[i], t);

        needs_update = needs_update
            || _sampler_states[i].last_update_status != SamplerState::Status::kSameInterval;
    }

    if (needs_update)
    {
        update_interpolators(animation);
    }
}

void AnimationPlayer::fetch_channel_values(
    const Animation& anim,
    hrz::function_ref<ChannelValueCallback> callback) const
{
    assert(anim.channels.size() == _channel_states.size());

    for (size_t i = 0; i < anim.channels.size(); ++i)
    {
        const auto& channel = anim.channels[i];
        const SamplerState& sampler_state = _sampler_states[(size_t)channel.sampler];
        const ChannelState& channel_state = _channel_states[i];

        const float interp_t = (sampler_state.t1 - sampler_state.t0) > 0.0F
            ? (t - sampler_state.t0) / (sampler_state.t1 - sampler_state.t0)
            : 0.0F;

        std::visit(
            hrz::overload{
                [](std::monostate) {},
                [&callback, &channel, interp_t](const std::invocable<float> auto& interp)
                {
                    const auto value = interp(interp_t);
                    callback(channel.target_id, channel.target_property, value);
                },

            },
            channel_state.interpolator);
    }
}

} // namespace hrz::model
