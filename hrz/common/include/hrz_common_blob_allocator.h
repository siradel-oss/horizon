#pragma once

#include "hrz_common_metadata.h"
#include "hrz_common_monitoring_defs.h"

#include <hrz_fnd_unsafe.h>
#include <hrz_monitoring.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace hrz
{
struct BlobAllocator;
struct LayersInfo;

namespace blobs
{
using BlobId = uint64_t;

static constexpr BlobId NO_BLOB = 0;
static constexpr size_t BLOB_ALIGNMENT = alignof(std::max_align_t);

enum BlobState
{
    NotAllocated,
    Allocated,
    InUse,
    Error,
};

struct AllocationTicket;
struct BlobHandle;

/**
 * An instance of this structure gives read and write access to
 * the actual memory region of an allocated blob.
 *
 * It keeps the location of this region valid as long as it is
 * alive.
 *
 * Do not hold the instance for longer than strictly necessary, as
 * it prevents the blob from taking part in memory compaction.
 */
struct MutableBlobData
{
private:
    MutableBlobData(BlobAllocator* allocator, BlobId blob_id, std::span<std::byte> data_span) :
        allocator(allocator), blob_id(blob_id), data_span(data_span)
    {
    }

public:
    ~MutableBlobData();

    MutableBlobData(const MutableBlobData&) = delete;
    MutableBlobData& operator=(const MutableBlobData&) = delete;

    MutableBlobData(MutableBlobData&& other) noexcept;
    MutableBlobData& operator=(MutableBlobData&& other) noexcept;

    /**
     * Release this instance before it goes out of scope.
     */
    void release();

    bool is_valid() const { return blob_id != NO_BLOB; }

    size_t size() const { return data_span.size_bytes(); }

    bool empty() const { return data_span.empty(); }

    std::byte* data() & { return data_span.data(); }

    const std::byte* data() && = delete;

    const std::byte* data() const& { return data_span.data(); }

    std::span<const std::byte> as_span() && = delete;

    std::span<const std::byte> as_span() const& { return data_span; }

    std::span<std::byte> as_span() & { return data_span; }

    std::span<const std::byte> subspan(size_t offset, size_t count) && = delete;

    std::span<const std::byte> subspan(size_t offset, size_t count) const&
    {
        return data_span.subspan(offset, count);
    }

    std::span<std::byte> subspan(size_t offset, size_t count) &
    {
        return data_span.subspan(offset, count);
    }

    std::span<const std::byte> as_bytes() && = delete;

    std::span<const std::byte> as_bytes() const& { return std::as_bytes(data_span); }

    std::span<std::byte> as_writable_bytes() & { return std::as_writable_bytes(data_span); }

    std::span<std::byte>::iterator begin() & { return data_span.begin(); }

    std::span<std::byte>::iterator end() & { return data_span.end(); }

private:
    BlobAllocator* allocator;
    BlobId blob_id;
    std::span<std::byte> data_span;

    friend BlobHandle;
};

/**
 * An instance of this structure gives read-only access to the
 * actual memory region of an allocated blob
 *
 * It keeps the location of this region valid as long as it is
 * alive.
 *
 * Do not hold the instance for longer than strictly necessary, as
 * it prevents the blob from taking part in memory compaction.
 */
struct BlobData
{
private:
    BlobData(BlobAllocator* allocator, BlobId blob_id, std::span<const std::byte> data_span) :
        allocator(allocator), blob_id(blob_id), data_span(data_span)
    {
    }

public:
    ~BlobData();

    BlobData(const BlobData&) = delete;
    BlobData& operator=(const BlobData&) = delete;

    BlobData(BlobData&& other) noexcept;
    BlobData& operator=(BlobData&& other) noexcept;

    /**
     * Release this instance before it goes out of scope.
     */
    void release();

    bool is_valid() const { return blob_id != NO_BLOB; }

    size_t size() const { return data_span.size_bytes(); }

    bool empty() const { return data_span.empty(); }

    const std::byte* data() && = delete;

    const std::byte* data() const& { return data_span.data(); }

    std::span<const std::byte> as_span() && = delete;

    std::span<const std::byte> as_span() const& { return data_span; }

    std::span<const std::byte> subspan(size_t offset, size_t count) && = delete;

    std::span<const std::byte> subspan(size_t offset, size_t count) const&
    {
        return data_span.subspan(offset, count);
    }

    std::span<const std::byte> subspan(size_t offset) && = delete;

    std::span<const std::byte> subspan(size_t offset) const& { return data_span.subspan(offset); }

    std::span<const std::byte> as_bytes() && = delete;

    std::span<const std::byte> as_bytes() const& { return std::as_bytes(data_span); }

    std::span<const std::byte>::iterator begin() && = delete;

    std::span<const std::byte>::iterator begin() const& { return data_span.begin(); }

    std::span<const std::byte>::iterator end() && = delete;

    std::span<const std::byte>::iterator end() const& { return data_span.end(); }

    std::span<const std::byte>::iterator cbegin() && = delete;

    std::span<const std::byte>::iterator cbegin() const& { return data_span.begin(); }

    std::span<const std::byte>::iterator cend() && = delete;

    std::span<const std::byte>::iterator cend() const& { return data_span.end(); }

private:
    BlobAllocator* allocator;
    BlobId blob_id;
    std::span<const std::byte> data_span;

    friend BlobHandle;
};

/**
 * An instance of this structure gives access, and represents
 * ownership, to a blob.
 *
 * The ownership can be shared between multiple handles. The
 * blob is freed when the last handle to the blob is destroyed.
 * References are counted automatically when the handle is copied
 * or destroyed.
 *
 * Call `get_data()` or `get_mutable_data()` to gain access to
 * the actual memory of the blob. A mutable access is exclusive:
 * no other simultaneous accesses are permitted, including read-
 * only access. On the other hand multiple concurrent read-only
 * accesses are allowed.
 */
struct BlobHandle
{
private:
    BlobHandle(BlobAllocator* allocator, BlobId id) : allocator(allocator), blob_id(id) {}

public:
    BlobHandle() : allocator(nullptr), blob_id(NO_BLOB) {}

    ~BlobHandle();

    BlobHandle(const BlobHandle&);
    BlobHandle& operator=(const BlobHandle&);

    BlobHandle(BlobHandle&& other) noexcept;
    BlobHandle& operator=(BlobHandle&& other) noexcept;

    /**
     * Decrement the use count of the blob before this instance is
     * destroyed.
     */
    void release();

    bool is_valid() const { return blob_id != NO_BLOB; }

    BlobData get_data() const;
    MutableBlobData get_mutable_data() const;
    size_t data_size() const;
    size_t data_alignment() const;

    bool check_integrity() const;

    BlobHandle make_sub_blob(unsafe, size_t offset) const;
    BlobHandle make_sub_blob(unsafe, size_t offset, size_t size) const;

    std::optional<BlobHandle> make_sub_blob(size_t offset) const;
    std::optional<BlobHandle> make_sub_blob(size_t offset, size_t size) const;

private:
    void acquire();
    std::span<std::byte> get_data(bool mutable_data) const;

    BlobAllocator* allocator;
    BlobId blob_id;

    friend std::optional<BlobHandle> allocate_blob_sync(BlobAllocator* allocator, size_t size);
    friend BlobHandle to_blob(BlobAllocator*, AllocationTicket&);
    friend BlobHandle make_sub_blob(unsafe, BlobAllocator*, const BlobHandle&, size_t);
    friend BlobHandle make_sub_blob(unsafe, BlobAllocator*, const BlobHandle&, size_t, size_t);
    friend std::optional<BlobHandle> make_sub_blob(BlobAllocator*, const BlobHandle&, size_t);
    friend std::optional<BlobHandle> make_sub_blob(
        BlobAllocator*,
        const BlobHandle&,
        size_t,
        size_t);
    friend void shrink_blob(BlobAllocator*, const BlobHandle&, size_t);
    friend void register_metadata(
        BlobAllocator*,
        const BlobHandle&,
        MetadataString,
        MetadataString);
    friend void copy_metadata(BlobAllocator*, const BlobHandle&, const BlobHandle&);
    friend void register_owner(
        BlobAllocator*,
        const BlobHandle&,
        const hrz::monitoring::ResourceOwner&);
    friend void copy_owner(BlobAllocator*, const BlobHandle&, const BlobHandle&);
};

struct RawBlobHandle
{
    BlobId blob_id;
};

/**
 * An instance of this structure gives access to a raw blob data.
 * A raw blob can always be written to.
 * A raw blob is not reference counted, and is not automatically
 * destroyed.
 * A raw blob is not movable, so a pointer to its data is always
 * valid as long as the blob hasn't been deallocated.
 * Use raw blobs only when strictly necessary, and for as little
 * time as necessary (as they prevent good memory compaction).
 */
struct RawBlob
{
    RawBlobHandle handle;
    std::span<std::byte> data;
};

/**
 * This structure represents a request for a blob allocation.
 *
 * An allocation isn't always performed at the moment the
 * request is made, but it can take some time.
 * An allocation can fail, if the allocation is full or the
 * requested blob size is too large.
 *
 * An allocation ticket cannot be copied, but can be moved.
 *
 * The allocation is automatically cancelled when the ticket
 * is destroyed.
 */
struct AllocationTicket
{
private:
    AllocationTicket(BlobAllocator* allocator, BlobId blob_id) :
        allocator(allocator), blob_id(blob_id)
    {
    }

public:
    AllocationTicket() : allocator(nullptr), blob_id(NO_BLOB) {}

    ~AllocationTicket();

    /**
     * Cancel this allocation before it goes out of scope.
     */
    void cancel();

    AllocationTicket(const AllocationTicket&) = delete;
    AllocationTicket& operator=(const AllocationTicket&) = delete;

    AllocationTicket(AllocationTicket&& other) noexcept;
    AllocationTicket& operator=(AllocationTicket&& other) noexcept;

    bool is_valid() const { return blob_id != NO_BLOB; }

private:
    BlobAllocator* allocator;
    BlobId blob_id;

    friend AllocationTicket allocate_blob(BlobAllocator*, size_t, bool);
    friend std::optional<BlobHandle> allocate_blob_sync(BlobAllocator*, size_t);
    friend BlobState get_state(BlobAllocator*, const AllocationTicket&);
    friend void cancel(BlobAllocator*, AllocationTicket&);
    friend BlobHandle to_blob(BlobAllocator*, AllocationTicket&);
    friend std::optional<RawBlob> allocate_raw_blob_sync(BlobAllocator*, size_t);
    friend void register_metadata(
        BlobAllocator*,
        const AllocationTicket&,
        MetadataString,
        MetadataString);
    friend void register_owner(
        BlobAllocator*,
        const AllocationTicket&,
        const hrz::monitoring::ResourceOwner&);
};

/**
 * Create a blob allocator.
 */
BlobAllocator* create_allocator(size_t capacity, bool malloc_passthrough);

/**
 * Destroy a blob allocator.
 */
void destroy_allocator(BlobAllocator*);

/**
 * Request a blob allocation.
 *
 * If `block` is `true`, and the blob cannot be allocated right away
 * (because the allocator memory is too full), the call blocks the
 * thread until either the allocator has been able to allocate the
 * blob, or too much time has passed.
 */
AllocationTicket allocate_blob(BlobAllocator*, size_t size, bool block = false);

/**
 * Get the state of an allocation request.
 */
BlobState get_state(BlobAllocator*, const AllocationTicket&);

/**
 * Cancel an allocation request.
 *
 * Calling this function consumes the allocation ticket.
 */
void cancel(BlobAllocator*, AllocationTicket&);

/**
 * Cancel all allocation requests for blobs that haven't
 * been allocated yet.
 */
void cancel_all_pending_allocations(BlobAllocator*);

/**
 * Turn a allocation request into a blob handle.
 *
 * The request must be in the allocated state.
 *
 * Calling this function consumes the allocation ticket.
 */
BlobHandle to_blob(BlobAllocator*, AllocationTicket&);

/**
 * Try to allocate a blob.
 *
 * If the blob cannot be allocated right away (because the allocator
 * memory is too full), the call blocks the thread until either the
 * allocator has been able to allocate the blob, or too much time has
 * passed.
 *
 * If the allocation has ultimately failed, an empty optional is
 * returned.
 */
std::optional<BlobHandle> allocate_blob_sync(BlobAllocator*, size_t size);

/**
 * Create a handle to a blob whose data is contained in an existing
 * blob, at the given offset, up to the end of the existing blob.
 * @Safety Only call this function if you can guarantee that the offset
 * is not larger than the size of the blob.
 */
BlobHandle make_sub_blob(unsafe, BlobAllocator*, const BlobHandle&, size_t offset);

/**
 * Create a handle to a blob whose data is contained in an existing
 * blob, at the given offset, with the given size.
 * @Safety Only call this function if you can guarantee that offset +
 * size is not larger than the size of the blob.
 */
BlobHandle make_sub_blob(unsafe, BlobAllocator*, const BlobHandle&, size_t offset, size_t size);

/**
 * Create a handle to a blob whose data is contained in an existing
 * blob, at the given offset, up to the end of the existing blob.
 * Call this function is you cannot guarantee that the offset is less
 * than the size of the blob.
 */
std::optional<BlobHandle> make_sub_blob(BlobAllocator*, const BlobHandle&, size_t offset);

/**
 * Create a handle to a blob whose data is contained in an existing
 * blob, at the given offset, with the given size.
 * Call this function is you cannot guarantee that offset + size
 * is less than the size of the blob.
 */
std::optional<BlobHandle> make_sub_blob(
    BlobAllocator*,
    const BlobHandle&,
    size_t offset,
    size_t size);

/**
 * Shrink a blob.
 *
 * The new size must be less or equal to the blob's current size.
 * The blob's data must not be currently accessed.
 */
void shrink_blob(BlobAllocator*, const BlobHandle&, size_t new_size);

/**
 * Try to allocate a raw blob.
 *
 * If the blob cannot be allocated right away (because the allocator
 * memory is too full), the call blocks the thread until either the
 * allocator has been able to allocate the blob, or too much time has
 * passed.
 *
 * If the allocation has ultimately failed, an empty optional is
 * returned.
 */
std::optional<RawBlob> allocate_raw_blob_sync(BlobAllocator*, size_t size);

/**
 * Get the size of a raw blob, in bytes.
 */
size_t get_size(BlobAllocator*, RawBlobHandle);

/**
 * Shrink a raw blob.
 *
 * The new size must be less or equal to the blob's current size.
 */
void shrink_raw_blob(BlobAllocator*, RawBlobHandle, size_t new_size);

/**
 * Deallocate a raw blob.
 */
void dealloc_raw_blob(BlobAllocator*, RawBlobHandle);

void register_metadata(
    BlobAllocator*,
    const AllocationTicket&,
    MetadataString key,
    MetadataString value);
void register_metadata(BlobAllocator*, const BlobHandle&, MetadataString key, MetadataString value);
void copy_metadata(BlobAllocator*, const BlobHandle& from, const BlobHandle& to);

void register_owner(
    BlobAllocator*,
    const AllocationTicket&,
    const monitoring::ResourceOwner& owner);
void register_owner(BlobAllocator*, const BlobHandle&, const monitoring::ResourceOwner& owner);
void copy_owner(BlobAllocator*, const BlobHandle& from, const BlobHandle& to);

void dump_blobs(const BlobAllocator*, const LayersInfo*, hrz_monitoring::MessageBuffer*);

/**
 * This function should be called once per frame.
 */
void work(BlobAllocator*);

inline BlobHandle BlobHandle::make_sub_blob(::hrz::unsafe unsafe, size_t offset) const
{
    if (!allocator) return {};
    return ::hrz::blobs::make_sub_blob(unsafe, allocator, *this, offset);
}

inline BlobHandle BlobHandle::make_sub_blob(::hrz::unsafe unsafe, size_t offset, size_t size) const
{
    if (!allocator) return {};
    return ::hrz::blobs::make_sub_blob(unsafe, allocator, *this, offset, size);
}

inline std::optional<BlobHandle> BlobHandle::make_sub_blob(size_t offset) const
{
    if (!allocator) return std::nullopt;
    return ::hrz::blobs::make_sub_blob(allocator, *this, offset);
}

inline std::optional<BlobHandle> BlobHandle::make_sub_blob(size_t offset, size_t size) const
{
    if (!allocator) return std::nullopt;
    return ::hrz::blobs::make_sub_blob(allocator, *this, offset, size);
}
} // namespace blobs
} // namespace hrz
