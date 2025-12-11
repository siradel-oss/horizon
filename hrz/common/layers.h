#pragma once

#include "hrz/fnd/flat_hash_map.h"
#include "hrz/protocol/layer/handle.pb.h"

#include <string>

namespace hrz
{
struct LayersInfo
{
    struct Layer
    {
        hrz_proto::LayerType type;
        std::string name;
    };

    hrz::flat_hash_map<uint64_t, Layer> layers;
    hrz::flat_hash_map<hrz_proto::LayerType, uint32_t> layer_type_counters;
};
} // namespace hrz
