#include "hrz_core_picking_id_allocator.h"

#include <hrz_common_profiling.h>
#include <hrz_fnd_gen_index_pool.h>

namespace hrz
{
struct PickingIdAllocator
{
    // This means we can only have 64 different system.
    // Honestly that seems fine to me. If that becomes an issue one
    // day, just switch to 16 bits.
    hrz::GenIndexPool<uint8_t, 2, 6> id_pool;
};

namespace picking
{
PickingIdAllocator* create_id_allocator()
{
    return new PickingIdAllocator();
}

void destroy_id_allocator(PickingIdAllocator* allocator)
{
    assert(allocator);
    delete allocator;
}

uint8_t allocate_system_id(PickingIdAllocator* allocator)
{
    assert(allocator);
    HRZ_SCOPED_SAMPLE("picking id allocator allocate system id");

    return allocator->id_pool.alloc();
}

void release_system_id(PickingIdAllocator* allocator, uint8_t id)
{
    assert(allocator);
    HRZ_SCOPED_SAMPLE("picking id allocator release system id");

    allocator->id_pool.release(id);
}

} // namespace picking
} // namespace hrz
