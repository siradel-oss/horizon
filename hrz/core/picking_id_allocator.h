// SPDX-FileCopyrightText: Copyright 2019 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>

namespace hrz
{

/**
 * This allocator is responsible for giving out picking ids.
 * Ids from this allocator are guaranteed to be unique, as long
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

} // namespace picking

} // namespace hrz
