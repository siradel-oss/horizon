#include "hrz/common/blob_malloc_adapter.h"

namespace
{
static constexpr size_t HEADER_SIZE = alignof(std::max_align_t);

hrz::blobs::RawBlobHandle get_blob_handle(const void* ptr)
{
    hrz::blobs::RawBlobHandle blob_handle;
    auto* header_ptr = (const hrz::blobs::RawBlobHandle*)((const std::byte*)ptr - HEADER_SIZE);
    // @Safety If ptr points to a blob, this is how we constructed the header.
    std::memcpy(&blob_handle, header_ptr, sizeof(hrz::blobs::RawBlobHandle));
    return blob_handle;
}
} // namespace

namespace hrz::blobs
{
MallocAdapter::MallocAdapter(hrz::BlobAllocator* allocator) : allocator(allocator)
{
    static_assert(
        sizeof(hrz::blobs::RawBlobHandle) <= HEADER_SIZE, "Not enough room for raw blob handle");
}

void* MallocAdapter::malloc(size_t size)
{
    if (size == 0) return nullptr;

    // Allocate a blob that's large enough to contain a raw blob
    // handle, on top of the requested memory space.
    // Write the blob's own andle in its memory, in a header
    // section at the beginning of the blob data.
    // The remaining space becomes the allocation requested by
    // the caller.
    // This allows for an easy retrieval of the blob handle when
    // deallocating, and enables the adapter to be stateless.

    auto blob = hrz::blobs::allocate_raw_blob_sync(allocator, size + HEADER_SIZE);
    if (!blob.has_value()) return nullptr;

    std::memcpy(
        (hrz::blobs::RawBlobHandle*)blob->data.data(), &blob->handle,
        sizeof(hrz::blobs::RawBlobHandle));

    return blob->data.data() + HEADER_SIZE;
}

void* MallocAdapter::calloc(size_t num, size_t size)
{
    if (num == 0 || size == 0) return nullptr;

    auto ptr = this->malloc(num * size);

    if (ptr != nullptr)
    {
        std::memset(ptr, 0, num * size);
    }

    return ptr;
}

void* MallocAdapter::realloc(void* ptr, size_t new_size)
{
    if (ptr == nullptr)
    {
        return this->malloc(new_size);
    }

    auto blob_handle = get_blob_handle(ptr);
    auto blob_size = hrz::blobs::get_size(allocator, blob_handle);
    assert(blob_size > HEADER_SIZE);
    auto previous_size = blob_size - HEADER_SIZE;

    if (new_size == 0)
    {
        hrz::blobs::dealloc_raw_blob(allocator, blob_handle);
        return nullptr;
    }
    else if (new_size < previous_size)
    {
        // Update size in place
        hrz::blobs::shrink_raw_blob(allocator, blob_handle, new_size);
        return ptr;
    }
    else if (new_size > previous_size)
    {
        // Alloc new and copy
        auto new_ptr = this->malloc(new_size);

        if (new_ptr == nullptr) return nullptr;

        std::memcpy(new_ptr, ptr, previous_size);
        this->free(ptr);
        return new_ptr;
    }
    else
    {
        // Size is unchanged.
        return ptr;
    }
}

void MallocAdapter::free(void* ptr)
{
    if (ptr == nullptr) return;

    auto blob_handle = get_blob_handle(ptr);
    hrz::blobs::dealloc_raw_blob(allocator, blob_handle);
}
} // namespace hrz::blobs
