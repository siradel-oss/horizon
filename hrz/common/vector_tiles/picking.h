#pragma once

#include "hrz/common/picking_types.h"

#include <lin_maths.h>

namespace hrz::vt
{
// Picking IDs (object reference):
//
// 2 bits per character, least significant bits are on the right.
//
// rrrrrrrrrrrrrrrr|gggggggggggggggg        (uvec2 components)
// ccccccccccccssss|oooooooooooooooo        (system, complementary, object)
// llllllltttttssss|tttttttiiiiiiiii        (contents)
// layer_id        | object_id
//
// s: system ID, 8 bits
// l: local layer ID, 14 bits
// t: tile ID, 24 bits
// i: feature index, 18 bits (262,144 values)
//
// When the complementary ID is passed to one of the functions below, it must
// have been shifted to the right by the size of the system ID.
// (i.e. hrz::picking::extract_complementary_id() must have been used.)

static constexpr uint32_t MAX_FEATURE_INDEX = (1 << 18) - 1;

inline uint32_t extract_feature_index(const picking::ObjectReference& ref)
{
    return ref.object_id & ((1 << 18) - 1);
}

inline uint32_t extract_tile_id(const picking::ObjectReference& ref)
{
    return ((ref.complementary_id & ((1 << 10) - 1)) << 14) | (ref.object_id >> 18);
}

inline uint32_t extract_local_layer_id(const picking::ObjectReference& ref)
{
    return (ref.complementary_id >> 10) & ((1 << 14) - 1);
}

inline uint32_t make_complementary_id_for_layer_local_id(uint32_t layer_local_id)
{
    assert(layer_local_id <= ((1 << 14) - 1));
    return layer_local_id << 10;
}

inline uint32_t make_system_layer_id_partial(uint8_t system_id, uint32_t layer_local_id)
{
    return picking::make_layer_id(
        system_id, make_complementary_id_for_layer_local_id(layer_local_id));
}

inline picking::ObjectReference make_object_reference(
    uint32_t system_layer_id_partial,
    uint32_t tile_id)
{
    picking::ObjectReference obj_ref{
        picking::extract_system_id_from_layer_id(system_layer_id_partial),
        picking::extract_complementary_id_from_layer_id(system_layer_id_partial),
        0,
    };

    assert(tile_id <= ((1 << 24) - 1));
    obj_ref.complementary_id |= (tile_id >> 14);
    obj_ref.object_id = (tile_id & ((1 << 14) - 1)) << 18;

    return obj_ref;
}

inline picking::FeatureReference make_feature_reference(uint32_t system_layer_id_partial)
{
    picking::FeatureReference feature_ref;
    feature_ref.system_id = picking::extract_system_id_from_layer_id(system_layer_id_partial);
    feature_ref.complementary_id =
        picking::extract_complementary_id_from_layer_id(system_layer_id_partial);
    return feature_ref;
}

} // namespace hrz::vt
