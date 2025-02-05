#pragma once

#include <hrz_common_vector_data.h>
#include <hrz_protocol_all.h>

#include <lin_maths.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace hrz::picking
{
struct ObjectReference
{
    uint32_t complementary_id = 0;
    uint32_t object_id = 0;
    uint8_t system_id = 0;
};

struct PositionResult
{
    std::optional<lm::dvec3> position;
    ObjectReference ref;
    float data_texture_value;
    std::vector<std::pair<uint32_t, float>> heatmap_values;
    std::vector<hrz_proto::LayerHandle> included_rasters;
};

struct AreaResult
{
    ObjectReference ref;
};

struct PositionTicket
{
    uint32_t o;
};

struct AreaTicket
{
    uint32_t o;
};

struct FeatureReference
{
    uint32_t complementary_id = 0;
    uint8_t system_id = 0;
    vector_data::FeatureIdHash feature_id_hash;

    lm::uvec2 feature_id_hash_as_uvec2() const
    {
        static_assert(
            sizeof(vector_data::FeatureIdHash) == sizeof(uint64_t),
            "Unsupported feature ID hash size");

        // @Endianness This relies on hashes being sent as little-endian
        //             values to the GPU.

        return {
            (uint32_t)(feature_id_hash & (((uint64_t)1 << 32) - 1)),
            (uint32_t)(feature_id_hash >> 32)};
    }
};
} // namespace hrz::picking
