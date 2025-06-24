#pragma once

#include "hrz_common_vector_data.h"

#include <hrz_layers.pb.h>

#include <lin_maths.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace hrz::picking
{

// All object and feature references work pretty much the same way.
// There is a system ID that is used to dispatch any request made with
// the reference to the appropriate system, and them additional information
// that is specific, and opaque, to the system.
// The system ID (8-bit) is packed with the 24 bits of the complementary ID
// into a layer ID (which is different from a layer handle).

constexpr uint32_t make_layer_id(uint8_t system_id, uint32_t complementary_id)
{
    assert(complementary_id <= 0xffffff);
    return system_id + (complementary_id << 8);
}

constexpr uint8_t extract_system_id_from_layer_id(uint32_t layer_id)
{
    return layer_id & 0xff;
}

constexpr uint32_t extract_complementary_id_from_layer_id(uint32_t layer_id)
{
    return layer_id >> 8;
}

// Object references use the common system and complementary IDs, and add 32 more bits
// of system specific information. The systems are free to use the 24 bits of complementary ID
// and 32 bits of object ID however they want.
// Object references uniquely identify an object, for example a feature in a specific tile.
// However they don't uniquely identify a feature that is split across multiple tiles.
// They are used to locate the object data in the system. For example for vector tiles, the
// complementary ID and object ID could contain the tile ID and the feature index, and from this
// information, the actual unique feature identifier could be retrieved.
// Object references don't make sense to be exposed externally. They are the identifier
// that are retrieved by the picking framebuffer, but are subsequently transformed into a
// feature reference (see below).
// Object references are usually packed into 64 bits, or two 32-bit integers.

constexpr uint64_t make_packed_object_reference(uint32_t layer_id, uint32_t object_id)
{
    return ((static_cast<uint64_t>(layer_id) << 32) | static_cast<uint64_t>(object_id));
}

constexpr uint64_t make_packed_object_reference(
    uint8_t system_id,
    uint32_t complementary_id,
    uint32_t object_id)
{
    const uint32_t layer_id = make_layer_id(system_id, complementary_id);
    return make_packed_object_reference(layer_id, object_id);
}

constexpr uint32_t extract_layer_id_from_object_reference(uint64_t full_id)
{
    return (uint32_t)(full_id >> 32);
}

inline uint32_t extract_object_id_from_object_reference(uint64_t full_id)
{
    return (uint32_t)(full_id & 0xffffffff);
}

struct ObjectReference
{
    uint8_t system_id = 0;
    uint32_t complementary_id = 0;
    uint32_t object_id = 0;

    static ObjectReference from_packed(uint64_t packed_id)
    {
        const uint32_t layer_id = extract_layer_id_from_object_reference(packed_id);
        const uint32_t object_id = extract_object_id_from_object_reference(packed_id);
        return from_packed(layer_id, object_id);
    }

    static ObjectReference from_packed(uint32_t layer_id, uint32_t object_id)
    {
        ObjectReference ref;
        ref.system_id = extract_system_id_from_layer_id(layer_id);
        ref.complementary_id = extract_complementary_id_from_layer_id(layer_id);
        ref.object_id = object_id;
        return ref;
    }

    inline lm::uvec2 to_uvec2() const
    {
        return lm::uvec2(make_layer_id(system_id, complementary_id), object_id);
    }
};

// Feature references uniquely identify a feature in a specific layer.
// They also use the system ID and the 24-bit complementary ID to identify
// the system and layer (or subsystem), but unlike object references, they
// also contain a 64-bit feature ID hash used to identify the feature id the
// layer, including across tiles.
// This is used for object highlighting.
// Feature references are built by each specific system from the object references.
// Object references are usually used as 3 32-bit integers, or one 32-bit and one 64-bit integer.

struct FeatureReference
{
    uint8_t system_id = 0;
    uint32_t complementary_id = 0;
    vector_data::FeatureIdHash feature_id_hash = 0;

    FeatureReference() = default;

    FeatureReference(
        uint8_t system_id,
        uint32_t complementary_id = 0,
        vector_data::FeatureIdHash feature_id_hash = 0) :
        system_id(system_id), complementary_id(complementary_id), feature_id_hash(feature_id_hash)
    {
    }

    inline lm::uvec3 to_uvec3() const
    {
        const lm::uvec2 feature_id_hash_uvec2 =
            vector_data::feature_id_hash_as_uvec2(feature_id_hash);
        return lm::uvec3(
            make_layer_id(system_id, complementary_id), feature_id_hash_uvec2.x,
            feature_id_hash_uvec2.y);
    }
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

} // namespace hrz::picking
