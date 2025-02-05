#pragma once

#include <assert.h>

#include <cstdint>

namespace hrz
{
/**
 * This allocator is responsible for giving out picking ids.
 * Ids from this allocator are garanteed to be unique, as long
 * as they are properly allocated and released by calling the
 * appropriate functions.
 */
struct PickingIdAllocator;

namespace picking
{
/**
 * Create an id allocator.
 */
PickingIdAllocator* create_id_allocator();

/**
 * Destroy an id allocator.
 */
void destroy_id_allocator(PickingIdAllocator*);

/**
 * Returns a new picking id, to be used by a layer subsystem.
 * 255 different values are available.
 */
uint8_t allocate_system_id(PickingIdAllocator*);

/**
 * Release a layer subsystem picking id.
 * It can be reallocated to a different subsystem later on. So
 * make sure not to use it after calling this function.
 */
void release_system_id(PickingIdAllocator*, uint8_t);

/**
 * Combine a layer subsystem picking id with another arbritrary id.
 * This is useful for adding additional information in the picking
 * framebuffer channel that holds subsystem picking ids.
 * The complementary id value must be less than 2^24.
 */
inline uint32_t combine_picking_ids(uint8_t system_id, uint32_t complementary_id)
{
    assert(complementary_id <= 0xffffff);
    return system_id + (complementary_id << 8);
}

/**
 * Returns the layer subsystem picking id inside the combined picking id
 * passed as parameter.
 */
inline uint8_t extract_system_id(uint32_t layer_id)
{
    return layer_id & 0xff;
}

/**
 * Returns the complementary picking id inside the combined picking id
 * passed as parameter.
 */
inline uint32_t extract_complementary_id(uint32_t layer_id)
{
    return layer_id >> 8;
}

/**
 * Combines the layer id (which is the system & complementary ids) and
 * a layer-specific object id into a full picking id that designates an
 * object uniquely in the scene.
 */
inline uint64_t combine_layer_object_ids(uint32_t layer_id, uint32_t object_id)
{
    return (((uint64_t)layer_id) << 32) | ((uint64_t)object_id);
}

/**
 * Extracts the layer id (which is the system & complementary ids) from
 * a scene-global full picking id.
 */
inline uint32_t extract_layer_id(uint64_t full_id)
{
    return (uint32_t)(full_id >> 32);
}

/**
 * Extract the layer-specific object id from a scene-global full picking id.
 */
inline uint32_t extract_object_id(uint64_t full_id)
{
    return (uint32_t)(full_id & 0xffffffff);
}

} // namespace picking

} // namespace hrz
