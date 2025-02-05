#pragma once

#include <hrz_fnd_flat_hash_map.h>
#include <hrz_fnd_flat_hash_set.h>
#include <hrz_protocol_all.h>

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
