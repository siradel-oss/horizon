#pragma once

#include "hrz/core/assets_loader/assets_loader.h"

#include <cstdint>
#include <limits>

namespace hrz
{
inline assets_loader::Queue get_request_queue(
    int8_t layer_loading_priority,
    assets_loader::Queue layer_default_queue)
{
    return layer_loading_priority > 0
        ? assets_loader::Queue::Early
        : (layer_loading_priority < 0 ? assets_loader::Queue::Late : layer_default_queue);
}

inline uint32_t combine_loading_priorities(
    int8_t layer_loading_priority,
    uint16_t asset_loading_priority)
{
    static_assert(
        sizeof(assets_loader::MAX_PRIORITY) >= sizeof(uint8_t) + sizeof(uint16_t),
        "Cannot fit loading priority in MAX_PRIORITY");
    return ((uint32_t)((int32_t)(layer_loading_priority)
                       - (uint32_t)std::numeric_limits<int8_t>::lowest())
            << 16)
        + asset_loading_priority;
}
} // namespace hrz
