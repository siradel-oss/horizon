#pragma once

#include "hrz/protocol/lighting_settings.pb.h"

namespace hrz::render
{

struct LightingSettings
{
    bool lighting_enabled{};
    bool cast_shadows{};
    bool receive_shadows{};

    constexpr bool operator==(const LightingSettings& other) const = default;

    template<typename H>
    friend H AbslHashValue(H h, const LightingSettings& settings)
    {
        return H::combine(
            std::move(h), settings.lighting_enabled, settings.cast_shadows,
            settings.receive_shadows);
    }
};

inline LightingSettings from_proto(const hrz_proto::LightingSettings& proto)
{
    return {proto.enable_lighting(), proto.cast_shadows(), proto.receive_shadows()};
}

} // namespace hrz::render
