#pragma once

#include "hrz_common_blob_allocator.h"

namespace hrz::blobs
{
/**
 * This structure allows using the blob allocator to emulate
 * the API of the standard memory allocator.
 *
 * Use it only when the blob allocator cannot be used directly,
 * for example when dealing with third-party code.
 */
struct MallocAdapter
{
    explicit MallocAdapter(hrz::BlobAllocator* allocator);

    void* malloc(size_t size);
    void* calloc(size_t num, size_t size);
    void* realloc(void* ptr, size_t new_size);
    void free(void* ptr);

private:
    hrz::BlobAllocator* allocator;
};
} // namespace hrz::blobs
