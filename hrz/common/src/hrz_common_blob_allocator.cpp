#include "hrz_common_blob_allocator.h"

#include "absl/container/btree_map.h"

#include <hrz_common_layers.h>
#include <hrz_common_metrics.h>
#include <hrz_common_monitoring_defs.h>
#include <hrz_common_monitoring_resource_sorter.h>
#include <hrz_common_profiling.h>
#include <hrz_common_ui_utils.h>
#include <hrz_fnd_double_buffered.h>
#include <hrz_fnd_format.h>
#include <hrz_fnd_gen_object_pool.h>
#include <hrz_fnd_inlined_vector.h>
#include <hrz_fnd_linked_list.h>
#include <hrz_fnd_log.h>
#include <hrz_fnd_object_pool.h>
#include <hrz_fnd_time.h>
#include <hrz_fnd_variant.h>
#include <hrz_monitoring.h>

#include <tlsf/tlsf.h>

#include <algorithm>
#include <cstdlib>
#include <limits>

extern "C"
{
#include <microui/microui.h>
}

#include <hrz_fnd_thread.h>

#include <array>
#include <cassert>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <utility>

// When blobs are allocated in the common memory pool, or when the engine
// runs in the WASM VM, there is no memory protection system that can
// trigger segmentation faults to alert of an out-of-bounds read or write.
//
// There isn't much we can do about reads, but we can detect most ouf-of-
// bounds writes by writing a few bytes of memory with a known pattern at
// the end of each blob. If a write then goes too far, that pattern gets
// overwritten by other data. We can check if it happened by reading the
// memory and comparing it to the expected value.
//
// Writing the terminators and checking them is disabled in release as it
// is more of a development feature, and has a non-zero impact on memory
// usage and CPU time.
#define WRITE_TERMINATORS HRZ_DEBUG

#define CHECK_TLSF_INTEGRITY 0

namespace hrz
{
using namespace hrz::blobs;

static constexpr double MAX_WAIT_DURATION = 0.2; // seconds

static constexpr size_t MAX_CAPACITY = 2ull * 1024 * 1024 * 1024;
static constexpr size_t MIN_CAPACITY = 64ull * 1024 * 1024;
static constexpr size_t BLOCK_SIZE = 4ull * 1024 * 1024;

static constexpr float FRAGMENTATION_THRESHOLD = 0.25f;

static constexpr mu_Color PURPLE{172, 117, 239, 255};
static constexpr mu_Color RED{229, 26, 26, 255};

// Check that malloc will return suitably-aligned memory.
static_assert(BLOB_ALIGNMENT <= sizeof(max_align_t), "Insuficient platform max alignment");

namespace
{
struct Blob
{
    BlobState state = BlobState::NotAllocated;
    double request_date = 0; // seconds
    std::condition_variable_any condition;
    double allocation_date = 0; // seconds

    size_t size = 0;
    BlobId parent = NO_BLOB;
    size_t use_count = 0;
    std::mutex mutex;

    union
    {
        ptrdiff_t offset_in_parent = 0; // in parent for child blobs
        std::byte* data_ptr;            // for root blobs
    };

    bool mutably_accessed = false;
    uint32_t read_only_accesses = 0;

    hrz::InlinedVector<std::pair<MetadataString, MetadataString>, 3> metadata;
    hrz::monitoring::ResourceOwner owner;
};

#if WRITE_TERMINATORS
using Terminator = std::array<std::byte, 4>;
std::optional<Terminator> check_terminator_in_root_allocation(const Blob* blob);

static constexpr Terminator BLOB_ALLOCATION_TERMINATOR = {
    std::byte{0xb1}, std::byte{0x0b}, std::byte{0xb1}, std::byte{0x0b}};
#endif

constexpr size_t get_allocation_size(const Blob* blob)
{
    if (blob->size == 0) return 0;
#if WRITE_TERMINATORS
    return blob->size + BLOB_ALLOCATION_TERMINATOR.size();
#else
    return blob->size;
#endif
}

} // namespace

// This first level splits the whole allocatable region in BLOCK_SIZE blocks.
// A doubly linked list of free blocks is kept.
// Contiguous blocks can be allocated for allocations above the block size.
struct LargeBlockAllocator
{
    struct Block
    {
        uint32_t base{};
        uint32_t count{};

        constexpr size_t size() const { return BLOCK_SIZE * count; }

        constexpr size_t offset() const { return BLOCK_SIZE * base; }
    };

    struct FreeBlock
    {
        FreeBlock* prev{};
        FreeBlock* next{};

        // Index of first block
        uint32_t first_block{};

        // Number of blocks free and contiguous
        uint32_t block_count{};
    };

    hrz::LinkedList<FreeBlock> free_blocks_list{};
    hrz::ObjectPool<FreeBlock> free_blocks_pool{};

    FreeBlock* detach_free_block(FreeBlock* block)
    {
        free_blocks_list.detach(block);
        return block;
    }

    // Returns the "real" capacity
    size_t init(size_t capacity)
    {
        capacity = BLOCK_SIZE
            * std::max(std::min(capacity / BLOCK_SIZE, MAX_CAPACITY / BLOCK_SIZE),
                       MIN_CAPACITY / BLOCK_SIZE);

        HRZ_LOG_INFO("Actual blob allocator capacity: {}", bytes_to_string(capacity).data());

        uint32_t block_count = capacity / BLOCK_SIZE;

        FreeBlock* block = free_blocks_pool.acquire();
        block->first_block = 0;
        block->block_count = block_count;
        free_blocks_list.insert_head(block);

        return capacity;
    }

    FreeBlock* extract_block(FreeBlock* block, uint32_t count)
    {
        if (block->block_count > count)
        {
            FreeBlock* remaining_block = free_blocks_pool.acquire();
            remaining_block->first_block = block->first_block + count;
            remaining_block->block_count = block->block_count - count;
            free_blocks_list.insert_after(block, remaining_block);
            block->block_count = count;
        }

        assert(block->block_count == count);
        return detach_free_block(block);
    }

    void try_merge_with_next_free(FreeBlock* block)
    {
        if (!free_blocks_list.is_valid(block) || free_blocks_list.is_tail(block)) return;
        if (block->first_block + block->block_count != block->next->first_block) return;

        block->block_count += block->next->block_count;
        free_blocks_pool.release(detach_free_block(block->next));
    }

    // After calling this, the block might not be valid anymore
    void try_merge_neighboring_free_blocks(FreeBlock* block)
    {
        try_merge_with_next_free(block);
        try_merge_with_next_free(block->prev);
    }

    Block acquire_block(size_t capacity, size_t below_offset = std::numeric_limits<size_t>::max())
    {
        uint32_t below_base = below_offset / BLOCK_SIZE;
        uint32_t block_count = (std::min(MAX_CAPACITY, capacity) + BLOCK_SIZE - 1) / BLOCK_SIZE;

        for (auto* block = free_blocks_list.head(); free_blocks_list.is_valid(block);
             block = block->next)
        {
            if (block->first_block >= below_base)
            {
                return Block{};
            }

            if (block->block_count >= block_count)
            {
                block = extract_block(block, block_count);
                auto returned_block = Block{block->first_block, block->block_count};
                free_blocks_pool.release(block);
                return returned_block;
            }
        }
        return Block{};
    }

    void release_block(Block block)
    {
        if (block.count == 0) return;

        // Find the first block that should be before the one we're freeing
        FreeBlock* next_block = free_blocks_list.head();
        while (free_blocks_list.is_valid(next_block))
        {
            if (next_block->first_block > block.base)
            {
                break;
            }
            next_block = next_block->next;
        }
        FreeBlock* prev_free_block = next_block->prev;

#if HRZ_DEBUG
        if (free_blocks_list.is_valid(prev_free_block))
        {
            assert(prev_free_block->first_block < block.base);
        }
        if (free_blocks_list.is_valid(prev_free_block->next))
        {
            assert(prev_free_block->next->first_block >= block.base + block.count);
        }
#endif

        FreeBlock* freed_block = free_blocks_pool.acquire();
        freed_block->block_count = block.count;
        freed_block->first_block = block.base;
        free_blocks_list.insert_after(prev_free_block, freed_block);
        try_merge_neighboring_free_blocks(freed_block);
    }
};

// A block arena is essentially a TLSF allocator inside a block allocated by the block allocator.
// It doesn't do anything too smart except dealing with alignment when we're on a 32-bit platform.
class BlockArena
{
    LargeBlockAllocator::Block _block;
    tlsf_t _tlsf;
    size_t _alloc_count = 0;

    static std::byte* align_data_ptr_plus_one(std::byte* alloc_ptr)
    {
        return (std::byte*)hrz::align_up_po2((uintptr_t)alloc_ptr + 1, BLOB_ALIGNMENT);
    }

    static std::byte* retrieve_allocation_ptr(std::byte* data_ptr)
    {
        int8_t offset = ((int8_t*)data_ptr)[-1];
        return data_ptr - offset;
    }

    static std::byte* make_data_ptr(std::byte* alloc_ptr)
    {
        // Align the raw ptr + 1 to BLOB ALIGNMENT. The +1 makes sure we always have at least one
        // byte to write the offset to the real pointer returned by tlsf_malloc so that "free"
        // can retrieve it.
        std::byte* data_ptr = align_data_ptr_plus_one(alloc_ptr);
        static_assert(
            sizeof(std::byte) == sizeof(int8_t),
            "Byte and int8_t must be same size for offset to point to the alignment");
        new (data_ptr - 1) int8_t{(int8_t)(data_ptr - alloc_ptr)};
        return data_ptr;
    }

public:
    static inline size_t extra_space_for_alignment()
    {
        static size_t extra_space_for_alignment =
            tlsf_align_size() < BLOB_ALIGNMENT ? BLOB_ALIGNMENT : 0;
        return extra_space_for_alignment;
    }

    static inline size_t alloc_overhead()
    {
        static size_t alloc_overhead = extra_space_for_alignment() + tlsf_alloc_overhead();
        return alloc_overhead;
    }

    struct Stats
    {
        size_t largest_allocation_available;
        float fragmentation;
    };

    BlockArena(LargeBlockAllocator::Block block, std::byte* full_buffer) :
        _block(block), _tlsf(tlsf_create_with_pool(full_buffer + _block.offset(), _block.size()))
    {
    }

    void destroy() { tlsf_destroy(_tlsf); }

    constexpr const LargeBlockAllocator::Block& block() const { return _block; }

    inline size_t free_space() const { return tlsf_free_space(_tlsf); }

    inline size_t used_space() const { return _block.size() - free_space(); }

    constexpr size_t alloc_count() const { return _alloc_count; }

    Stats stats() const
    {
        return Stats{
            tlsf_largest_allocation_available(_tlsf),
            tlsf_fragmentation(_tlsf),
        };
    }

    constexpr bool is_empty() const { return _alloc_count == 0; }

    inline bool check_integrity()
    {
#if CHECK_TLSF_INTEGRITY
        return tlsf_check(_tlsf) == 0 && tlsf_check_pool(tlsf_get_pool(_tlsf)) == 0;
#else
        return true;
#endif
    }

    std::byte* alloc(size_t size)
    {
        assert(size > 0);
        std::byte* ptr = nullptr;
        if (extra_space_for_alignment() == 0)
        {
            ptr = (std::byte*)tlsf_malloc(_tlsf, size);
            assert(check_integrity());
        }
        else
        {
            ptr = (std::byte*)tlsf_malloc(_tlsf, size + extra_space_for_alignment());
            assert(check_integrity());
            if (!ptr) return nullptr;

            ptr = make_data_ptr(ptr);
        }

        if (ptr) _alloc_count++;
        return ptr;
    }

    std::byte* shrink(void* ptr, size_t new_size)
    {
        assert(ptr && new_size > 0);
        if (extra_space_for_alignment() == 0)
        {
            auto* new_ptr = (std::byte*)tlsf_realloc(_tlsf, ptr, new_size);
            assert(check_integrity());
            return new_ptr;
        }
        else
        {
            std::byte* old_alloc_ptr = retrieve_allocation_ptr((std::byte*)ptr);
            std::byte* new_alloc_ptr = (std::byte*)tlsf_realloc(
                _tlsf, old_alloc_ptr, new_size + extra_space_for_alignment());
            assert(new_alloc_ptr == old_alloc_ptr);
            assert(check_integrity());
            return make_data_ptr(new_alloc_ptr);
        }
    }

    void free(void* ptr)
    {
        assert(ptr);
        if (extra_space_for_alignment() == 0)
        {
            tlsf_free(_tlsf, ptr);
        }
        else
        {
            std::byte* alloc_ptr = retrieve_allocation_ptr((std::byte*)ptr);
            tlsf_free(_tlsf, alloc_ptr);
        }
        assert(check_integrity());
        _alloc_count--;
    }

    struct WalkContext
    {
        BlockArena* block;
        tlsf_walker inner_walker;
        void* inner_user;

        static void walk_pool(void* ptr, size_t size, int used, void* user)
        {
            auto* ctx = (WalkContext*)user;
            if (extra_space_for_alignment() == 0)
            {
                ctx->inner_walker(ptr, size, used, ctx->inner_user);
            }
            else
            {
                ctx->inner_walker(
                    align_data_ptr_plus_one((std::byte*)ptr), size - extra_space_for_alignment(),
                    used, ctx->inner_user);
            }
        }
    };

    void walk_pool(tlsf_walker walker, void* user)
    {
        WalkContext ctx;
        ctx.block = this;
        ctx.inner_walker = walker;
        ctx.inner_user = user;
        tlsf_walk_pool(tlsf_get_pool(_tlsf), WalkContext::walk_pool, &ctx);
        assert(check_integrity());
    }
};

// This allocator divides the whole allocatable range into small blocks (See LargeBlockAllocator),
// and each block gets a TLSF allocator (See BlockArena). It manages the allocated blocks and
// defragments them automatically.
struct AwesomeAllocator
{
    // This is added to each allocation.
    struct AllocationHeader
    {
        BlockArena* owning_block;
        BlobId blob_id;
    };

    static constexpr size_t kHeaderSize = sizeof(AllocationHeader);
    static_assert(
        kHeaderSize % BLOB_ALIGNMENT == 0,
        "Allocation header needs to be aligned to 8 bytes so that adding it in front of a TLSF "
        "allocation keeps the offset pointer aligned to 8 bytes as well");

    // Once allocations have been defragmented, this replaces the header and is used to list the
    // allocations that need to be freed in case the whole block was not defragmented fully.
    struct ToFreeHeader
    {
        ToFreeHeader* next;
    };

    static_assert(
        sizeof(AllocationHeader) >= sizeof(ToFreeHeader),
        "Not enough room to store linked list of blocks to free");

    // This is the whole allocatable range.
    std::unique_ptr<std::byte[]> buffer;

    LargeBlockAllocator block_allocator{};
    hrz::ObjectPool<BlockArena> blocks_pool;

    // We have some "indexes" which accelerate lookups of allocated BlockArenas.
    // We want to keep those indexes up to date and this bitset helps us not update indexes that
    // don't need to by marking which onces need updating.
    using IndexBitset = uint32_t;
    static constexpr IndexBitset kLargestAllocationIndex = 0x01;
    static constexpr IndexBitset kFragmentationIndex = 0x02;
    static constexpr IndexBitset kOffsetIndex = 0x04;
    static constexpr IndexBitset kAllIndexes =
        kLargestAllocationIndex | kFragmentationIndex | kOffsetIndex;

    absl::btree_multimap<size_t, BlockArena*> blocks_by_largest_allocation;
    absl::btree_multimap<float, BlockArena*, std::greater<float>> blocks_by_fragmentation;
    absl::btree_map<size_t, BlockArena*> blocks_by_offset;

    // There are multiple types of defragmentations and we alternate between them at each frame.
    // Maybe in the future we'll want something smarter to orchestrate those different steps, but
    // for now it's fine.
    enum DefragmentationType
    {
        Defrag_HighestFragmentation = 0,
        Defrag_Rightmost,
        Defrag__Max,
    };

    DefragmentationType last_defragmentation_type = Defrag__Max;

    void insert_into_index(
        BlockArena* block,
        const BlockArena::Stats& stats,
        IndexBitset indexes = kAllIndexes)
    {
        if (indexes & kLargestAllocationIndex)
        {
            blocks_by_largest_allocation.insert(
                std::make_pair(stats.largest_allocation_available, block));
        }

        if (indexes & kFragmentationIndex)
        {
            blocks_by_fragmentation.insert(std::make_pair(stats.fragmentation, block));
        }

        if (indexes & kOffsetIndex)
        {
            blocks_by_offset.insert(std::make_pair(block->block().offset(), block));
        }
    }

    void insert_into_index(BlockArena* block) { insert_into_index(block, block->stats()); }

    void remove_from_index(
        BlockArena* block,
        const BlockArena::Stats& stats,
        IndexBitset indexes = kAllIndexes)
    {
        bool block_removed = false;
        (void)block_removed;

        if (indexes & kLargestAllocationIndex)
        {
            auto pair =
                blocks_by_largest_allocation.equal_range(stats.largest_allocation_available);
            for (auto it = pair.first; it != pair.second; ++it)
            {
                if (it->second == block)
                {
                    blocks_by_largest_allocation.erase(it);
                    block_removed = true;
                    break;
                }
            }
            assert(block_removed && "Block not in largest allocation index");
        }

        if (indexes & kFragmentationIndex)
        {
            block_removed = false;
            auto frag_begin = blocks_by_fragmentation.lower_bound(
                stats.fragmentation
                + stats.fragmentation * std::numeric_limits<float>::epsilon() * 10.0f);
            auto frag_end = blocks_by_fragmentation.upper_bound(
                stats.fragmentation
                - stats.fragmentation * std::numeric_limits<float>::epsilon() * 10.0f);
            for (auto it = frag_begin; it != frag_end; ++it)
            {
                if (it->second == block)
                {
                    blocks_by_fragmentation.erase(it);
                    block_removed = true;
                    break;
                }
            }
            assert(block_removed && "Block not in fragmentation index");
        }

        if (indexes & kOffsetIndex)
        {
            block_removed = false;
            auto it = blocks_by_offset.find(block->block().offset());
            if (it != blocks_by_offset.end())
            {
                assert(it->second == block);
                block_removed = true;
                blocks_by_offset.erase(it);
            }
            assert(block_removed && "Block not in offset index");
        }
    }

    void update_index(BlockArena* block, const BlockArena::Stats& old_stats)
    {
        auto new_stats = block->stats();
        IndexBitset indexes = 0;

        if (new_stats.largest_allocation_available != old_stats.largest_allocation_available)
        {
            indexes |= kLargestAllocationIndex;
        }

        if (new_stats.fragmentation != old_stats.fragmentation)
        {
            indexes |= kFragmentationIndex;
        }

        // Offset never changes!

        if (indexes != 0)
        {
            remove_from_index(block, old_stats, indexes);
            insert_into_index(block, new_stats, indexes);
        }
    }

    // Arena capacity is the total size of the block necessary for the given capacity and possibly
    // allocation count. This includes all overheads.
    static size_t estimate_arena_capacity(size_t capacity, size_t alloc_count)
    {
        // Because TLSF places free blocks in buckets that might represent smaller blocks than what
        // they actually are, we need to ask the implementation what would be the actual pool size
        // necessary to insert the pool in a bucket that can be reached for the required capacity.
        return tlsf_required_pool_capacity(
            capacity + alloc_count * (kHeaderSize + BlockArena::alloc_overhead()));
    }

    // Allocation capacity is the total number of byte for a single allocation.
    static size_t estimate_allocation_capacity(size_t size)
    {
        return size + kHeaderSize + BlockArena::alloc_overhead();
    }

    // Allocates a new block arena of the given arena capacity, but only if a block below the given
    // offset is found.
    BlockArena* new_block_arena_for_arena_capacity_below_offset(
        size_t capacity,
        size_t below_offset)
    {
        auto block = block_allocator.acquire_block(capacity, below_offset);
        if (block.size() < capacity) return nullptr;

        BlockArena* block_arena = blocks_pool.acquire(BlockArena(block, buffer.get()));
        insert_into_index(block_arena);
        return block_arena;
    }

    BlockArena* new_block_arena_for_arena_capacity(size_t capacity)
    {
        return new_block_arena_for_arena_capacity_below_offset(
            capacity, std::numeric_limits<size_t>::max());
    }

    void release_block_arena(BlockArena* block)
    {
        block->destroy();
        block_allocator.release_block(block->block());
        blocks_pool.release(block);
    }

    // We search for a block that can fit a single allocation of the given capacity, and optionally
    // can exclude a block from this search. This can be useful if we're looking for a block that
    // can fit the data of another block, but we don't want to find this first block, or that would
    // be useless.
    BlockArena* get_block_for_allocation_capacity(
        size_t capacity,
        BlockArena* which_is_not_this = nullptr)
    {
        auto it = blocks_by_largest_allocation.lower_bound(capacity);
        for (; it != blocks_by_largest_allocation.end(); ++it)
        {
            if (it->second != which_is_not_this)
            {
                return it->second;
            }
        }
        return nullptr;
    }

    // Same as get_block_for_allocation_capacity, but if no existing block is found, we allocate
    // one.
    BlockArena* get_or_create_block_for_allocation_capacity(
        size_t capacity,
        BlockArena* which_is_not_this = nullptr)
    {
        auto* block = get_block_for_allocation_capacity(capacity, which_is_not_this);
        return block ? block
                     : new_block_arena_for_arena_capacity(estimate_arena_capacity(capacity, 0));
    }

    size_t init(size_t capacity)
    {
        capacity = block_allocator.init(capacity);
        buffer.reset(new std::byte[capacity]);
        return capacity;
    }

    void* alloc(size_t size, BlobId blob_id)
    {
        assert(size > 0);

        BlockArena* block =
            get_or_create_block_for_allocation_capacity(estimate_allocation_capacity(size));
        if (!block) return nullptr;

        auto old_stats = block->stats();

        std::byte* ptr = block->alloc(size + kHeaderSize);
        if (!ptr)
        {
            // We might be out of memory, or at least out of contiguous space to allocate this blob.
            return nullptr;
        }

        auto* header = new (ptr) AllocationHeader;
        header->owning_block = block;
        header->blob_id = blob_id;

        update_index(block, old_stats);

        return ptr + kHeaderSize;
    }

    void* shrink(void* ptr, size_t new_size, BlobId blob_id)
    {
        assert(ptr && new_size > 0);

        auto* header = &((AllocationHeader*)ptr)[-1];
        assert(blob_id == header->blob_id);
        auto block = header->owning_block;
        auto old_stats = block->stats();
        void* new_header = block->shrink(header, new_size + kHeaderSize);
        (void)new_header;

        assert(new_header == header); // Should always be true because that's a property of the TLSF
                                      // allocator for shrinking

        assert(!block->is_empty());
        update_index(block, old_stats);

        return ptr;
    }

    void free(void* ptr, BlobId blob_id)
    {
        assert(ptr);
        auto* header = &((AllocationHeader*)ptr)[-1];
        assert(blob_id == header->blob_id);
        auto block = header->owning_block;
        auto old_stats = block->stats();
        block->free(header);

        if (!block->is_empty())
        {
            update_index(block, old_stats);
        }
        else
        {
            remove_from_index(block, old_stats);
            release_block_arena(block);
        }
    }

    struct DefragmentationReport
    {
        bool has_immovable_blocks = false;
        size_t allocation_capacity_of_large_blocks_that_did_not_fit = 0;
        size_t allocation_capacity_of_small_blocks_that_did_not_fit = 0;

        constexpr bool all_moved() const
        {
            return !has_immovable_blocks
                && allocation_capacity_of_large_blocks_that_did_not_fit == 0
                && allocation_capacity_of_small_blocks_that_did_not_fit == 0;
        }

        constexpr size_t allocation_capacity_that_did_not_fit() const
        {
            return allocation_capacity_of_large_blocks_that_did_not_fit
                + allocation_capacity_of_small_blocks_that_did_not_fit;
        }
    };

    struct DefragmentationContext
    {
        DefragmentationReport report;
        BlockArena* new_block;
        std::function<Blob*(BlobId)> get_blob_fn;

        // Linked list of blocks to free once we're done walking.
        // The pointer to next is stored first in the allocated block.
        ToFreeHeader* to_free_head = nullptr;

        DefragmentationContext(
            BlockArena* new_block_,
            const std::function<Blob*(BlobId)>& get_blob_fn_) :
            new_block{new_block_}, get_blob_fn(get_blob_fn_)
        {
        }

        static void walk(void* ptr, size_t tlsf_alloc_size, int used, void* user)
        {
            if (!used) return;

            auto* ctx = (DefragmentationContext*)user;
            auto* old_header = (AllocationHeader*)ptr;
            auto* blob = ctx->get_blob_fn(old_header->blob_id);

            if (!blob)
            {
                assert(!"Blob in allocator cannot be found in pool");
                HRZ_LOG_ERROR("Blob in allocator cannot be found in pool");
                return;
            }

            if (blob->mutably_accessed || blob->read_only_accesses > 0)
            {
                ctx->report.has_immovable_blocks = true;
                return;
            }

            size_t alloc_size = get_allocation_size(blob);
            auto* new_ptr = (std::byte*)ctx->new_block->alloc(alloc_size + kHeaderSize);
            if (!new_ptr)
            {
                if (alloc_size > BLOCK_SIZE / 2)
                {
                    ctx->report.allocation_capacity_of_large_blocks_that_did_not_fit +=
                        estimate_allocation_capacity(alloc_size);
                }
                else
                {
                    ctx->report.allocation_capacity_of_small_blocks_that_did_not_fit +=
                        estimate_allocation_capacity(alloc_size);
                }
                return;
            }

            assert(blob->parent == NO_BLOB);
            assert((uintptr_t)ptr + kHeaderSize == (uintptr_t)blob->data_ptr);
            assert(alloc_size + kHeaderSize <= tlsf_alloc_size);

            std::byte* new_data_ptr = new_ptr + kHeaderSize;
            memcpy(new_data_ptr, blob->data_ptr, alloc_size);

            auto* new_header = new (new_ptr) AllocationHeader;
            new_header->owning_block = ctx->new_block;
            new_header->blob_id = old_header->blob_id;
            blob->data_ptr = new_data_ptr;

            assert((uintptr_t)blob->data_ptr % BLOB_ALIGNMENT == 0);

            ToFreeHeader* to_free = new (ptr) ToFreeHeader;
            to_free->next = std::exchange(ctx->to_free_head, to_free);
        }
    };

    BlockArena* get_highest_fragmentation_block(float fragmentation_threshold)
    {
        // Only do blocks with fragmentation > threshold.
        auto it = blocks_by_fragmentation.begin();
        if (it == blocks_by_fragmentation.end() || it->first < fragmentation_threshold)
        {
            return nullptr;
        }
        else
        {
            return blocks_by_fragmentation.begin()->second;
        }
    }

    // After calling this, the to_block might not be valid anymore.
    // Also the from_block might not be valid anymore, unless the report shows that some blocks were
    // not moved.
    DefragmentationReport do_defragmentation(
        BlockArena* from_block,
        BlockArena* to_block,
        const std::function<Blob*(BlobId)>& get_blob_fn)
    {
        auto to_block_old_stats = to_block->stats();
        auto from_block_old_stats = from_block->stats();

        DefragmentationContext ctx(to_block, get_blob_fn);
        from_block->walk_pool(DefragmentationContext::walk, &ctx);

        if (to_block->is_empty())
        {
            remove_from_index(to_block, to_block_old_stats);
            release_block_arena(to_block);
        }
        else
        {
            update_index(to_block, to_block_old_stats);
        }

        if (ctx.report.all_moved())
        {
            // If everything was moved, we don't bother freeing individual allocations, just yeet
            // everything.
            remove_from_index(from_block, from_block_old_stats);
            release_block_arena(from_block);
        }
        else
        {
            // If we couldn't free all blocks, free the ones we were able to manually, because we
            // can't just throw the block into the trash.
            ToFreeHeader* to_free = ctx.to_free_head;
            while (to_free)
            {
                from_block->free(std::exchange(to_free, to_free->next));
            }

            assert(!from_block->is_empty());
            update_index(from_block, from_block_old_stats);
        }

        return ctx.report;
    }

    // After calling the, the old_block might not be valid anymore.
    void defragment_block(
        BlockArena* old_block,
        bool allow_large_blocks,
        const std::function<Blob*(BlobId)>& get_blob_fn,
        const std::function<BlockArena*(size_t)>& allocate_block_fn,
        std::optional<DefragmentationReport> old_report)
    {
        if (old_block->is_empty())
        {
            remove_from_index(old_block, old_block->stats());
            release_block_arena(old_block);
            return;
        }

        size_t used_capacity = old_block->used_space();
        auto* new_block = get_block_for_allocation_capacity(used_capacity, old_block);
        if (!new_block)
        {
            // If we constantly create blocks large enough to contain the entire old block, we'll
            // eventually end up with one giant block because of large allocations.
            // So instead we do a first pass where we try to fit as much allocations in a normal
            // size block, and if there are allocations remaining, do another pass with a block of
            // the necessary space to contain the large allocations. Of course it's possible that
            // after the small allocations have been defragmented, the large allocations fit in
            // another already allocated block, in which case we fall in the case above, which is
            // the best possible case.
            size_t used_arena_capacity = estimate_arena_capacity(used_capacity, 0);
            size_t new_arena_capacity = allow_large_blocks ? used_arena_capacity : BLOCK_SIZE;
            new_block = allocate_block_fn(new_arena_capacity);
        }

        if (new_block)
        {
            auto report = do_defragmentation(old_block, new_block, get_blob_fn);
            if (report.allocation_capacity_that_did_not_fit() > 0)
            {
                // Prevent recursing too much in case we can't properly estimate the necessary
                // capacity. Do to that we check that we made some progress.
                if (!old_report
                    || (old_report
                        && report.allocation_capacity_that_did_not_fit()
                            < old_report->allocation_capacity_that_did_not_fit()))
                {
                    defragment_block(
                        old_block, report.allocation_capacity_of_small_blocks_that_did_not_fit == 0,
                        get_blob_fn, allocate_block_fn, report);
                }
            }
        }
    }

    BlockArena* get_rightmost_block()
    {
        if (blocks_by_offset.empty())
            return nullptr;
        else
            return blocks_by_offset.rbegin()->second;
    }

    void defragment_one_block(const std::function<Blob*(BlobId)>& get_blob_fn)
    {
        // We alternate between different defragmentation modes
        last_defragmentation_type = (DefragmentationType)((int)last_defragmentation_type + 1);
        if (last_defragmentation_type >= Defrag__Max)
        {
            last_defragmentation_type = (DefragmentationType)0;
        }
        assert(last_defragmentation_type < Defrag__Max);

        switch (last_defragmentation_type)
        {
            case Defrag_HighestFragmentation:
            {
                auto* block = get_highest_fragmentation_block(FRAGMENTATION_THRESHOLD);
                if (block)
                {
                    defragment_block(
                        block, false, get_blob_fn,
                        [this](size_t capacity)
                        { return new_block_arena_for_arena_capacity(capacity); },
                        std::nullopt);
                }
                break;
            }
            case Defrag_Rightmost:
            {
                auto* block = get_rightmost_block();
                if (block)
                {
                    defragment_block(
                        block, false, get_blob_fn,
                        [this, block](size_t capacity) {
                            return new_block_arena_for_arena_capacity_below_offset(
                                capacity, block->block().offset());
                        },
                        std::nullopt);
                }
                break;
            }
            default: break;
        }
    }
};

struct BlobAllocator
{
    using BlobIdPool = GenIndexPool<BlobId, 32, 32>;
    using BlobPool = GenObjectPool<Blob, BlobIdPool, 128>;

    size_t capacity;
    bool is_malloc_passthrough;
    BlobPool blob_pool;
    AwesomeAllocator awesome_allocator;
    hrz::flat_hash_set<BlobId> blobs;
    std::deque<BlobId> unallocated_root_blobs;
    std::shared_mutex mutex;

    size_t unallocated_blob_count;
    size_t allocated_memory_size;
    uint32_t allocated_blob_count;
    uint32_t zero_size_allocated_blob_count;
    size_t unallocated_memory_size;

    hrz::DoubleBuffered<std::deque<BlobId>> potentially_unused_blobs;
    std::mutex potentially_unused_blobs_mutex;

    struct Ui
    {
        monitoring::ResourceSorter<BlobId, Blob> allocated_blob_sorter;
        monitoring::ResourceSorter<BlobId, Blob> unallocated_blob_sorter;
        ui::TooltipContext tooltip_ctx;
        hrz::flat_hash_set<mu_Id> expanded_nodes;
    };

    Ui ui;
};

namespace
{
#if WRITE_TERMINATORS
constexpr std::byte* get_terminator_ptr(const Blob* blob)
{
    return blob->data_ptr + blob->size;
}

void write_terminator_in_root_allocation(Blob* blob)
{
    assert(blob->parent == NO_BLOB && blob->data_ptr != nullptr);

    std::memcpy(
        get_terminator_ptr(blob), BLOB_ALLOCATION_TERMINATOR.data(),
        BLOB_ALLOCATION_TERMINATOR.size());
}

// Returns the observed terminator if it doesn't conform with what is expected.
std::optional<Terminator> check_terminator_in_root_allocation(const Blob* blob)
{
    if (blob->data_ptr == nullptr) return std::nullopt;
    assert(blob->parent == NO_BLOB && blob->data_ptr != nullptr);
    if (blob->size == 0) return std::nullopt;

    const std::byte* terminator_ptr = get_terminator_ptr(blob);

    if (std::memcmp(
            terminator_ptr, BLOB_ALLOCATION_TERMINATOR.data(), BLOB_ALLOCATION_TERMINATOR.size())
        != 0)
    {
        Terminator terminator;
        std::memcpy(terminator.data(), terminator_ptr, terminator.size());
        return {terminator};
    }
    else
    {
        return std::nullopt;
    }
}
#endif

void root_blob_prepare_allocation(Blob* blob, std::byte* allocation_ptr)
{
    if (allocation_ptr)
    {
        blob->data_ptr = allocation_ptr;
#if WRITE_TERMINATORS
        write_terminator_in_root_allocation(blob);
        assert(!check_terminator_in_root_allocation(blob).has_value());
#endif
    }
}

void root_blob_malloc(BlobAllocator* allocator, BlobId blob_id, Blob* blob)
{
    auto blob_allocation_size = get_allocation_size(blob);
    std::byte* allocation_ptr = nullptr;
    if (allocator->is_malloc_passthrough)
    {
        if (blob_allocation_size > 0)
        {
            allocation_ptr = (std::byte*)std::malloc(blob_allocation_size);
        }
    }
    else
    {
        allocation_ptr =
            (std::byte*)allocator->awesome_allocator.alloc(blob_allocation_size, blob_id);
    }
    assert((uintptr_t)allocation_ptr % BLOB_ALIGNMENT == 0);
    root_blob_prepare_allocation(blob, allocation_ptr);
    assert(!check_terminator_in_root_allocation(blob).has_value());
}

void root_blob_free(BlobAllocator* allocator, BlobId blob_id, Blob* blob)
{
    if (blob->size > 0)
    {
#if WRITE_TERMINATORS
        auto terminator = check_terminator_in_root_allocation(blob);
        if (terminator.has_value())
        {
            HRZ_LOG_ERROR(
                "Terminator has been overwritten for blob: {:02x} {:02x} {:02x} {:02x}",
                (uint8_t)terminator->at(0), (uint8_t)terminator->at(1), (uint8_t)terminator->at(2),
                (uint8_t)terminator->at(3));
            for (auto& it : blob->metadata)
            {
                HRZ_LOG_ERROR("    {}: {}", it.first.data(), it.second.data());
            }
            assert(!"Terminator overwritten");
        }
#endif

        if (allocator->is_malloc_passthrough)
        {
            std::free(blob->data_ptr);
        }
        else
        {
            allocator->awesome_allocator.free(blob->data_ptr, blob_id);
        }
        blob->size = 0;
        blob->data_ptr = nullptr;
    }
}

void root_blob_shrink(BlobAllocator* allocator, BlobId blob_id, Blob* blob, size_t new_size)
{
    assert(new_size > 0 && new_size < blob->size);

    blob->size = new_size;
    auto new_allocation_size = get_allocation_size(blob);

    std::byte* old_ptr = blob->data_ptr;
    std::byte* new_ptr = nullptr;
    if (allocator->is_malloc_passthrough)
    {
        new_ptr = (std::byte*)std::realloc(old_ptr, new_allocation_size);
    }
    else
    {
        new_ptr =
            (std::byte*)allocator->awesome_allocator.shrink(old_ptr, new_allocation_size, blob_id);
        // This should be a nice property of the TLSF allocator, and is very important so that we
        // don't have to deal with alignment when shrinking, since the TLSF allocator only aligns to
        // 4 bytes on wasm.
        assert(new_ptr == old_ptr);
    }
    assert((uintptr_t)new_ptr % BLOB_ALIGNMENT == 0);
    root_blob_prepare_allocation(blob, new_ptr);
    assert(!check_terminator_in_root_allocation(blob).has_value());
}

bool try_allocate_root_blob(BlobAllocator* allocator, BlobId blob_id, Blob* blob)
{
    assert(blob->state == BlobState::NotAllocated);
    assert(allocator->unallocated_blob_count >= 1);

    auto do_alloc_success = [&]()
    {
        blob->allocation_date = hrz::now_frame_s();
        blob->state = BlobState::Allocated;

        // Wake the thread that may be waiting on this blob's allocation.
        blob->condition.notify_all();

        allocator->unallocated_blob_count -= 1;
        allocator->allocated_blob_count += 1;
        allocator->allocated_memory_size += get_allocation_size(blob);
        allocator->ui.allocated_blob_sorter.register_handle(blob_id);
    };

    if (blob->size == 0)
    {
        blob->data_ptr = nullptr;
        do_alloc_success();
        allocator->zero_size_allocated_blob_count += 1;
        return true;
    }

    auto blob_allocation_size = get_allocation_size(blob);
    if (allocator->capacity - allocator->allocated_memory_size < blob_allocation_size)
    {
        return false;
    }

    root_blob_malloc(allocator, blob_id, blob);
    if (allocator->is_malloc_passthrough && !blob->data_ptr)
    {
        // System malloc shouldn't fail
        blob->state = BlobState::Error;
        return false;
    }

    if (blob->data_ptr != nullptr)
    {
        do_alloc_success();
        return true;
    }
    else
    {
        return false;
    }
}

void try_allocate_next_unallocated_root_blobs(BlobAllocator* allocator)
{
    if (allocator->unallocated_root_blobs.empty()) return;

    // Allocate unallocated blobs while they fit.

    for (auto it = allocator->unallocated_root_blobs.begin();
         it != allocator->unallocated_root_blobs.end();)
    {
        auto blob_id = *it;
        auto blob = allocator->blob_pool.get_object(blob_id);

        if (blob == nullptr)
        {
            // The allocation has been cancelled.
            it = allocator->unallocated_root_blobs.erase(it);
            allocator->ui.unallocated_blob_sorter.unregister_handle(blob_id);
            continue;
        }

        if (try_allocate_root_blob(allocator, blob_id, blob))
        {
            assert(allocator->unallocated_memory_size >= blob->size);
            allocator->unallocated_memory_size -= blob->size;
            it = allocator->unallocated_root_blobs.erase(it);
            allocator->ui.unallocated_blob_sorter.unregister_handle(blob_id);
        }
        else
        {
            break;
        }
    }
}

void deallocate_root_blob(BlobAllocator* allocator, BlobId blob_id, Blob* blob)
{
    assert(blob->state == BlobState::Allocated || blob->state == BlobState::InUse);
    assert(blob->use_count == 0);
    assert(blob->parent == NO_BLOB);

    auto blob_allocation_size = get_allocation_size(blob);
    root_blob_free(allocator, blob_id, blob);
    blob->state = BlobState::NotAllocated;

    assert(
        allocator->allocated_memory_size >= blob_allocation_size
        && allocator->allocated_blob_count >= 1);
    allocator->allocated_memory_size -= blob_allocation_size;
    allocator->allocated_blob_count -= 1;

    if (blob_allocation_size == 0)
    {
        assert(allocator->zero_size_allocated_blob_count >= 1);
        allocator->zero_size_allocated_blob_count -= 1;
    }

    allocator->ui.allocated_blob_sorter.unregister_handle(blob_id);
}

void try_deallocate_blob(BlobAllocator* allocator, BlobId blob_id, Blob* blob)
{
    bool release_blob = false;

    if ((blob->state == BlobState::Allocated || blob->state == BlobState::InUse)
        && blob->use_count == 0 && !blob->mutably_accessed && blob->read_only_accesses == 0)
    {
        if (blob->parent == NO_BLOB)
        {
            deallocate_root_blob(allocator, blob_id, blob);
            try_allocate_next_unallocated_root_blobs(allocator);
        }
        else
        {
            auto parent_blob = allocator->blob_pool.get_object(blob->parent);

            assert(parent_blob->use_count >= 1);
            parent_blob->use_count -= 1;

            if (parent_blob->use_count == 0)
            {
                try_deallocate_blob(allocator, blob->parent, parent_blob);
            }
        }

        release_blob = true;
    }
    else if (blob->state == BlobState::NotAllocated)
    {
        // If the blob isn't allocated yet, its ID remains in the unallocated blob
        // list. It will be removed next time try_allocate_next_unallocated_blobs()
        // stumbles on it.

        assert(allocator->unallocated_blob_count >= 1);
        allocator->unallocated_blob_count -= 1;

        release_blob = true;
    }
    else if (blob->state == BlobState::Error)
    {
        release_blob = true;
    }

    if (release_blob)
    {
        allocator->blob_pool.release(blob_id);
        allocator->blobs.erase(blob_id);
        allocator->ui.unallocated_blob_sorter.unregister_handle(blob_id);
    }
}

void do_cancel_all_pending_allocations(BlobAllocator* allocator)
{
    for (auto blob_id : allocator->unallocated_root_blobs)
    {
        auto blob = allocator->blob_pool.get_object(blob_id);

        if (blob == nullptr)
        {
            // The allocation has been cancelled.
            continue;
        }

        assert(allocator->unallocated_memory_size >= blob->size);
        allocator->unallocated_memory_size -= blob->size;

        blob->state = BlobState::Error;

        // Wake the thread that may be waiting on this blob's allocation.
        // (Though the allocation failed.)
        blob->condition.notify_all();
    }

    allocator->unallocated_root_blobs.clear();
    allocator->unallocated_blob_count = 0;
}
} // namespace

namespace blobs
{
BlobAllocator* create_allocator(size_t capacity, bool is_malloc_passthrough)
{
    capacity = std::min<size_t>(capacity, 0x8000'0000);

    HRZ_LOG_INFO(
        "Requested blob allocator capacity: {}{}", bytes_to_string(capacity).data(),
        is_malloc_passthrough ? " (malloc passthrough)" : "");

    auto allocator = new BlobAllocator();
    allocator->capacity = capacity;
    allocator->is_malloc_passthrough = is_malloc_passthrough;
    if (!is_malloc_passthrough)
    {
        allocator->capacity = allocator->awesome_allocator.init(capacity);
    }
    allocator->unallocated_blob_count = 0;
    allocator->allocated_memory_size = 0;
    allocator->allocated_blob_count = 0;
    allocator->zero_size_allocated_blob_count = 0;
    allocator->unallocated_memory_size = 0;
    return allocator;
}

void destroy_allocator(BlobAllocator* allocator)
{
    assert(allocator);

    {
        HRZ_SCOPED_EXCLUSIVE_LOCK(allocator->mutex);
        do_cancel_all_pending_allocations(allocator);
    }

    delete allocator;
}

AllocationTicket allocate_blob(BlobAllocator* allocator, size_t size, bool block)
{
    assert(allocator);

    std::unique_lock lock(allocator->mutex);

    auto id = allocator->blob_pool.alloc();
    allocator->blobs.erase(id);
    auto blob = allocator->blob_pool.get_object(id);
    blob->state = BlobState::NotAllocated;
    blob->size = size;
    blob->request_date = hrz::now_frame_s();
    blob->allocation_date = 0;
    blob->use_count = 1;

    allocator->unallocated_blob_count += 1;

    try_allocate_root_blob(allocator, id, blob);

    if (blob->state == BlobState::NotAllocated)
    {
        allocator->unallocated_root_blobs.push_back(id);
        allocator->unallocated_memory_size += blob->size;
        allocator->ui.unallocated_blob_sorter.register_handle(id);

        if (block)
        {
            blob->condition.wait(lock, [blob] { return blob->state != BlobState::NotAllocated; });
        }
    }

    return AllocationTicket(allocator, id);
}

std::optional<BlobHandle> allocate_blob_sync(BlobAllocator* allocator, size_t size)
{
    assert(allocator);

    auto ticket = allocate_blob(allocator, size, true);

    HRZ_SCOPED_EXCLUSIVE_LOCK(allocator->mutex);

    auto blob = allocator->blob_pool.get_object(ticket.blob_id);

    if (blob->state == BlobState::Allocated)
    {
        blob->state = BlobState::InUse;

        auto blob_id = ticket.blob_id;
        ticket.blob_id = NO_BLOB;

        return {BlobHandle(allocator, blob_id)};
    }
    else
    {
        assert(blob->use_count == 1);
        blob->use_count -= 1;

        auto blob_id = ticket.blob_id;
        ticket.blob_id = NO_BLOB;

        try_deallocate_blob(allocator, blob_id, blob);

        return std::nullopt;
    }
}

std::optional<RawBlob> allocate_raw_blob_sync(BlobAllocator* allocator, size_t size)
{
    assert(allocator);

    auto ticket = allocate_blob(allocator, size, true);

    HRZ_SCOPED_EXCLUSIVE_LOCK(allocator->mutex);

    auto blob = allocator->blob_pool.get_object(ticket.blob_id);

    if (blob->state == BlobState::Allocated)
    {
        auto blob_id = ticket.blob_id;
        ticket.blob_id = NO_BLOB;

        blob->state = BlobState::InUse;

        // This prevents the blob from being moved.
        blob->mutably_accessed = true;

        return {{{blob_id}, {blob->data_ptr, blob->size}}};
    }
    else
    {
        assert(blob->use_count == 1);
        blob->use_count -= 1;

        auto blob_id = ticket.blob_id;
        ticket.blob_id = NO_BLOB;

        try_deallocate_blob(allocator, blob_id, blob);

        return std::nullopt;
    }
}

size_t get_size(BlobAllocator* allocator, RawBlobHandle raw_blob_handle)
{
    assert(allocator);
    HRZ_SCOPED_EXCLUSIVE_LOCK(allocator->mutex);

    auto blob_id = raw_blob_handle.blob_id;

    if (blob_id == NO_BLOB)
    {
        return 0;
    }

    auto blob = allocator->blob_pool.get_object(blob_id);

    if (blob == nullptr)
    {
        HRZ_LOG_ERROR("Blob {} not found", blob_id);
        return 0;
    }

    return blob->size;
}

namespace
{
void shrink_root_blob(
    BlobAllocator* allocator,
    BlobId blob_id,
    size_t new_size,
    bool ignore_accesses)
{
    assert(allocator);
    HRZ_SCOPED_EXCLUSIVE_LOCK(allocator->mutex);

    auto blob = allocator->blob_pool.get_object(blob_id);

    if (blob == nullptr)
    {
        HRZ_LOG_ERROR("Blob {} not found", blob_id);
        return;
    }

    assert(blob->parent == NO_BLOB);
    assert(!check_terminator_in_root_allocation(blob).has_value());

    if (blob->use_count > 1)
    {
        HRZ_LOG_ERROR("Cannot shrink blob {} while it has multiple uses", blob_id);
        return;
    }

    assert(ignore_accesses || (blob->read_only_accesses == 0 && !blob->mutably_accessed));
    if (!ignore_accesses && (blob->read_only_accesses != 0 || blob->mutably_accessed))
    {
        HRZ_LOG_ERROR("Cannot shrink blob {} while its data is being accessed", blob_id);
        return;
    }

    if (new_size > blob->size)
    {
        HRZ_LOG_ERROR(
            "Cannot shrink blob {}: new size ({}) is greater than current size ({})", blob_id,
            new_size, blob->size);
        return;
    }

    if (new_size < blob->size)
    {
        allocator->allocated_memory_size -= get_allocation_size(blob);
        if (new_size > 0)
        {
            root_blob_shrink(allocator, blob_id, blob, new_size);
            assert(!check_terminator_in_root_allocation(blob).has_value());
        }
        else
        {
            root_blob_free(allocator, blob_id, blob);
            allocator->zero_size_allocated_blob_count += 1;
        }
        allocator->allocated_memory_size += get_allocation_size(blob);

        // Enough room to allow allocating more blobs may have
        // been made.
        try_allocate_next_unallocated_root_blobs(allocator);
    }
}
} // namespace

void shrink_raw_blob(BlobAllocator* allocator, RawBlobHandle raw_blob_handle, size_t new_size)
{
    shrink_root_blob(allocator, raw_blob_handle.blob_id, new_size, true);
}

void dealloc_raw_blob(BlobAllocator* allocator, RawBlobHandle raw_blob_handle)
{
    assert(allocator);
    HRZ_SCOPED_EXCLUSIVE_LOCK(allocator->mutex);

    auto blob_id = raw_blob_handle.blob_id;
    auto blob = allocator->blob_pool.get_object(blob_id);

    if (blob == nullptr)
    {
        HRZ_LOG_ERROR("Blob {} not found", blob_id);
        return;
    }

    assert(blob->mutably_accessed);
    blob->mutably_accessed = false;

    if (blob->parent == NO_BLOB)
    {
        assert(!check_terminator_in_root_allocation(blob));
    }
    else
    {
        assert(!check_terminator_in_root_allocation(allocator->blob_pool.get_object(blob->parent)));
    }

    assert(blob->use_count == 1);
    blob->use_count -= 1;

    if (blob->use_count == 0)
    {
        HRZ_SCOPED_LOCK(allocator->potentially_unused_blobs_mutex);
        allocator->potentially_unused_blobs.front().push_back(blob_id);
    }
}

BlobState get_state(BlobAllocator* allocator, const AllocationTicket& ticket)
{
    assert(allocator);
    HRZ_SCOPED_EXCLUSIVE_LOCK(allocator->mutex);

    if (ticket.blob_id == NO_BLOB)
    {
        return BlobState::Error;
    }

    auto blob = allocator->blob_pool.get_object(ticket.blob_id);

    if (blob == nullptr)
    {
        HRZ_LOG_ERROR("Blob {} not found", ticket.blob_id);
        return BlobState::Error;
    }

    return blob->state;
}

void cancel(BlobAllocator* allocator, AllocationTicket& ticket)
{
    assert(allocator);
    HRZ_SCOPED_EXCLUSIVE_LOCK(allocator->mutex);

    if (ticket.blob_id == NO_BLOB) return;

    auto blob_id = ticket.blob_id;
    auto blob = allocator->blob_pool.get_object(blob_id);

    assert(
        blob->state == BlobState::NotAllocated || blob->state == BlobState::Allocated
        || blob->state == BlobState::Error);

    assert(blob->use_count == 1);
    blob->use_count -= 1;

    if (blob->use_count == 0)
    {
        HRZ_SCOPED_LOCK(allocator->potentially_unused_blobs_mutex);
        allocator->potentially_unused_blobs.front().push_back(blob_id);
    }

    ticket.blob_id = NO_BLOB;
    ticket.allocator = nullptr;
}

void cancel_all_pending_allocations(BlobAllocator* allocator)
{
    assert(allocator);
    HRZ_SCOPED_EXCLUSIVE_LOCK(allocator->mutex);

    do_cancel_all_pending_allocations(allocator);
}

BlobHandle to_blob(BlobAllocator* allocator, AllocationTicket& ticket)
{
    assert(allocator);
    assert(ticket.blob_id != NO_BLOB);
    HRZ_SCOPED_EXCLUSIVE_LOCK(allocator->mutex);

    auto blob_id = ticket.blob_id;
    auto blob = allocator->blob_pool.get_object(blob_id);

    assert(blob->state == BlobState::Allocated);
    blob->state = BlobState::InUse;

    ticket.blob_id = NO_BLOB;

    return BlobHandle(allocator, blob_id);
}

namespace
{
BlobId make_sub_blob(
    BlobAllocator* allocator,
    BlobId parent_blob_id,
    Blob* parent_blob,
    size_t offset_in_parent,
    size_t size)
{
    assert(offset_in_parent <= parent_blob->size);
    assert(offset_in_parent + size <= parent_blob->size);

    if (parent_blob->parent != NO_BLOB)
    {
        // Prevent the creation of deep hierarchies.
        // Use the grand-parent as parent if it exists.

        offset_in_parent += parent_blob->offset_in_parent;

        auto grand_parent_blob_id = parent_blob->parent;
        auto grand_parent_blob = allocator->blob_pool.get_object(grand_parent_blob_id);

        assert(grand_parent_blob->parent == NO_BLOB);

        parent_blob_id = grand_parent_blob_id;
        parent_blob = grand_parent_blob;
    }

    auto blob_id = allocator->blob_pool.alloc();
    allocator->blobs.insert(blob_id);
    auto blob = allocator->blob_pool.get_object(blob_id);
    blob->parent = parent_blob_id;
    blob->state = BlobState::Allocated;
    blob->size = size;
    blob->offset_in_parent = offset_in_parent;
    blob->use_count = 1;

    parent_blob->use_count += 1;

    return blob_id;
}
} // namespace

BlobHandle make_sub_blob(
    BlobAllocator* allocator,
    const BlobHandle& parent_handle,
    size_t offset_in_parent)
{
    assert(allocator);
    assert(parent_handle.blob_id != NO_BLOB);
    HRZ_SCOPED_EXCLUSIVE_LOCK(allocator->mutex);

    auto parent_blob = allocator->blob_pool.get_object(parent_handle.blob_id);

    assert(offset_in_parent <= parent_blob->size);
    size_t size = parent_blob->size - offset_in_parent;

    auto sub_blob_id =
        make_sub_blob(allocator, parent_handle.blob_id, parent_blob, offset_in_parent, size);
    return BlobHandle(allocator, sub_blob_id);
}

BlobHandle make_sub_blob(
    BlobAllocator* allocator,
    const BlobHandle& parent_handle,
    size_t offset_in_parent,
    size_t size)
{
    assert(allocator);
    assert(parent_handle.blob_id != NO_BLOB);
    HRZ_SCOPED_EXCLUSIVE_LOCK(allocator->mutex);

    auto parent_blob = allocator->blob_pool.get_object(parent_handle.blob_id);

    assert(offset_in_parent <= parent_blob->size);
    assert(offset_in_parent + size <= parent_blob->size);

    auto sub_blob_id =
        make_sub_blob(allocator, parent_handle.blob_id, parent_blob, offset_in_parent, size);
    return BlobHandle(allocator, sub_blob_id);
}

void shrink_blob(BlobAllocator* allocator, const BlobHandle& handle, size_t new_size)
{
    shrink_root_blob(allocator, handle.blob_id, new_size, false);
}

namespace
{
void register_metadata(
    BlobAllocator* allocator,
    BlobId blob_id,
    MetadataString key,
    MetadataString value)
{
    assert(allocator);
    HRZ_SCOPED_EXCLUSIVE_LOCK(allocator->mutex);

    auto blob = allocator->blob_pool.get_object(blob_id);

    if (blob == nullptr)
    {
        HRZ_LOG_ERROR("Blob {} not found", blob_id);
        return;
    }

    for (auto& it : blob->metadata)
    {
        if (it.first == key)
        {
            it.second = std::move(value);
            return;
        }
    }

    blob->metadata.push_back({std::move(key), std::move(value)});
}

void register_owner(
    BlobAllocator* allocator,
    BlobId blob_id,
    const hrz::monitoring::ResourceOwner& owner)
{
    assert(allocator);
    HRZ_SCOPED_EXCLUSIVE_LOCK(allocator->mutex);

    auto blob = allocator->blob_pool.get_object(blob_id);

    if (blob == nullptr)
    {
        HRZ_LOG_ERROR("Blob {} not found", blob_id);
        return;
    }

    blob->owner = owner;

    if (blob->state == BlobState::NotAllocated)
    {
        allocator->ui.unallocated_blob_sorter.schedule_sort();
    }
    else
    {
        allocator->ui.allocated_blob_sorter.schedule_sort();
    }
}
} // namespace

void register_metadata(
    BlobAllocator* allocator,
    const AllocationTicket& ticket,
    MetadataString key,
    MetadataString value)
{
    register_metadata(allocator, ticket.blob_id, std::move(key), std::move(value));
}

void register_metadata(
    BlobAllocator* allocator,
    const BlobHandle& handle,
    MetadataString key,
    MetadataString value)
{
    register_metadata(allocator, handle.blob_id, std::move(key), std::move(value));
}

void copy_metadata(
    BlobAllocator* allocator,
    const BlobHandle& from_handle,
    const BlobHandle& to_handle)
{
    assert(allocator);
    HRZ_SCOPED_EXCLUSIVE_LOCK(allocator->mutex);

    auto from_blob_id = from_handle.blob_id;
    auto from_blob = allocator->blob_pool.get_object(from_blob_id);

    if (from_blob == nullptr)
    {
        HRZ_LOG_ERROR("Blob {} not found", from_blob_id);
        return;
    }

    auto to_blob_id = to_handle.blob_id;
    auto to_blob = allocator->blob_pool.get_object(to_blob_id);

    if (to_blob == nullptr)
    {
        HRZ_LOG_ERROR("Blob {} not found", to_blob_id);
        return;
    }

    for (const auto& from_it : from_blob->metadata)
    {
        bool found = false;

        for (auto& to_it : to_blob->metadata)
        {
            if (to_it.first == from_it.first)
            {
                to_it.second = from_it.second;
                found = true;
            }
        }

        if (!found)
        {
            to_blob->metadata.push_back(from_it);
        }
    }
}

void register_owner(
    BlobAllocator* allocator,
    const AllocationTicket& ticket,
    const monitoring::ResourceOwner& owner)
{
    register_owner(allocator, ticket.blob_id, owner);
}

void register_owner(
    BlobAllocator* allocator,
    const BlobHandle& handle,
    const monitoring::ResourceOwner& owner)
{
    register_owner(allocator, handle.blob_id, owner);
}

void copy_owner(
    BlobAllocator* allocator,
    const BlobHandle& from_handle,
    const BlobHandle& to_handle)
{
    assert(allocator);
    HRZ_SCOPED_EXCLUSIVE_LOCK(allocator->mutex);

    auto from_blob_id = from_handle.blob_id;
    auto from_blob = allocator->blob_pool.get_object(from_blob_id);

    if (from_blob == nullptr)
    {
        HRZ_LOG_ERROR("Blob {} not found", from_blob_id);
        return;
    }

    auto to_blob_id = to_handle.blob_id;
    auto to_blob = allocator->blob_pool.get_object(to_blob_id);

    if (to_blob == nullptr)
    {
        HRZ_LOG_ERROR("Blob {} not found", to_blob_id);
        return;
    }

    to_blob->owner = from_blob->owner;

    if (to_blob->state == BlobState::NotAllocated)
    {
        allocator->ui.unallocated_blob_sorter.schedule_sort();
    }
    else
    {
        allocator->ui.allocated_blob_sorter.schedule_sort();
    }
}

void dump_blobs(
    const BlobAllocator* allocator,
    const LayersInfo* layers_info,
    hrz_monitoring::MessageBuffer* buffer)
{
    HRZ_SCOPED_SAMPLE("blob allocator dump blobs");

    google::protobuf::Arena arena;
    auto* msgs = google::protobuf::Arena::Create<hrz_monitoring::MonitoringMessages>(&arena);
    auto* msg = msgs->add_messages();
    auto* snapshot = msg->mutable_blobs();
    snapshot->set_timestamp(hrz::now_frame_us_s64());
    snapshot->set_malloc_passthrough(allocator->is_malloc_passthrough);
    snapshot->set_capacity(allocator->capacity);

    hrz::flat_hash_map<monitoring::systems::Name, size_t> used_systems;
    hrz::flat_hash_map<uint64_t, size_t> used_layers;

    std::vector<monitoring::systems::Name> ordered_systems;
    std::vector<uint64_t> ordered_layers;

    auto get_index = [&](auto& map, auto& vector, const auto& key)
    {
        auto it = map.find(key);
        if (it != map.end())
        {
            return it->second;
        }

        auto index = map.size();
        map.insert({key, index});
        vector.push_back(key);
        return index;
    };

    auto get_offset = [&](const Blob* blob) -> size_t
    {
        if (allocator->is_malloc_passthrough || blob->size == 0
            || (blob->state != BlobState::Allocated && blob->state != BlobState::InUse))
            return 0;

        size_t offset = 0;
        if (blob->parent != NO_BLOB)
        {
            offset = blob->offset_in_parent;
            blob = allocator->blob_pool.get_object(blob->parent);
        }

        assert(blob && blob->parent == NO_BLOB);
        return offset + (blob->data_ptr - allocator->awesome_allocator.buffer.get());
    };

    auto write_blob_message =
        [&](const Blob* blob, BlobId blob_id,
            google::protobuf::RepeatedPtrField<hrz_monitoring_proto::Blob>* messages)
    {
        auto message = messages->Add();
        message->set_id(blob_id);
        message->set_size(blob->size);
        message->set_use_count(blob->use_count);
        message->set_offset(get_offset(blob));
        message->set_system(get_index(used_systems, ordered_systems, blob->owner.system));
        message->set_layer(get_index(used_layers, ordered_layers, blob->owner.layer_id));

        for (const auto& pair : blob->metadata)
        {
            auto metadata = message->add_metadata();
            metadata->set_name(pair.first.data());
            metadata->set_value(pair.second.data());
        }
    };

    for (auto blob_id : allocator->blobs)
    {
        const auto* blob = allocator->blob_pool.get_object(blob_id);
        if (blob)
        {
            switch (blob->state)
            {
                case BlobState::Allocated:
                case BlobState::InUse:
                {
                    write_blob_message(
                        blob, blob_id,
                        blob->size > 0 ? snapshot->mutable_allocated_blobs()
                                       : snapshot->mutable_allocated_empty_blobs());
                }
                case BlobState::NotAllocated:
                {
                    write_blob_message(blob, blob_id, snapshot->mutable_unallocated_blobs());
                }
                default: break;
            }
        }
    }

    for (auto system : ordered_systems)
    {
        snapshot->add_systems(monitoring::systems::to_string(system));
    }

    for (auto layer : ordered_layers)
    {
        auto it = layers_info->layers.find(layer);
        const char* name =
            (it != layers_info->layers.end()) ? it->second.name.c_str() : "(no layer)";

        snapshot->add_layers(name);
    }

    hrz_monitoring::push_messages(buffer, *msgs);
}

void work(BlobAllocator* allocator)
{
    HRZ_SCOPED_SAMPLE("blob allocator work");

    assert(allocator);

    {
        HRZ_SCOPED_LOCK(allocator->potentially_unused_blobs_mutex);
        allocator->potentially_unused_blobs.swap();
    }

    for (auto blob_id : allocator->potentially_unused_blobs.back())
    {
        HRZ_SCOPED_EXCLUSIVE_LOCK(allocator->mutex);

        auto blob = allocator->blob_pool.get_object(blob_id);
        if (blob != nullptr)
        {
            try_deallocate_blob(allocator, blob_id, blob);
        }
    }

    allocator->potentially_unused_blobs.back().clear();

    // Consider blob allocation requests that have been waiting for
    // too long as failed.
    bool allocations_have_timed_out = false;

    while (true)
    {
        HRZ_SCOPED_EXCLUSIVE_LOCK(allocator->mutex);

        if (allocator->unallocated_root_blobs.empty())
        {
            break;
        }

        auto blob_id = allocator->unallocated_root_blobs.front();
        auto blob = allocator->blob_pool.get_object(blob_id);

        if (blob != nullptr)
        {
            if (hrz::now_frame_s() - blob->request_date > MAX_WAIT_DURATION)
            {
                HRZ_LOG_WARNING(
                    "Could not allocate blob {} of size {} after {} s", blob_id, blob->size,
                    MAX_WAIT_DURATION);

                blob->state = BlobState::Error;
                blob->condition.notify_all();
                allocations_have_timed_out = true;

                assert(allocator->unallocated_memory_size >= blob->size);
                allocator->unallocated_memory_size -= blob->size;

                allocator->unallocated_root_blobs.pop_front();

                assert(allocator->unallocated_blob_count >= 1);
                allocator->unallocated_blob_count -= 1;
                allocator->ui.unallocated_blob_sorter.unregister_handle(blob_id);
            }
            else
            {
                break;
            }
        }
        else
        {
            allocator->unallocated_root_blobs.pop_front();
        }
    }

    {
        HRZ_SCOPED_EXCLUSIVE_LOCK(allocator->mutex);

        allocator->awesome_allocator.defragment_one_block(
            [&](BlobId id) -> Blob* { return allocator->blob_pool.get_object(id); });
    }

    {
        HRZ_SCOPED_EXCLUSIVE_LOCK(allocator->mutex);

        if (allocations_have_timed_out)
        {
            // At least one allocation has failed because it has been
            // waiting too much, but maybe the next allocations are
            // smaller and can fit.
            try_allocate_next_unallocated_root_blobs(allocator);
        }
    }

    HRZ_SET_GAUGE("Allocated blobs", allocator->allocated_blob_count, {});
    HRZ_SET_GAUGE("Unallocated blobs", allocator->unallocated_blob_count, {});
    HRZ_SET_GAUGE("Allocated blob memory (B)", allocator->allocated_memory_size, {});
    HRZ_SET_GAUGE("Unallocated blob memory (B)", allocator->unallocated_memory_size, {});
}

class TlsfDevUiDrawer
{
    mu_Context* _ctx;
    hrz::ui::TooltipContext _tooltip_ctx;
    fmt::memory_buffer _buffer;
    bool _has_tooltip = false;

    mu_Rect _rect;

    size_t _capacity;

    struct TlsfWalkContext
    {
        TlsfDevUiDrawer* drawer;
        mu_Rect rect;
        size_t bucket_used_bytes = 0;
        size_t bucket_index = 0;
        size_t bucket_size;
        bool bucket_has_large_blob = false;
        size_t next_bucket_memory_position;
        size_t memory_position = 0;
        uintptr_t base;

        TlsfWalkContext(TlsfDevUiDrawer* d, mu_Rect r, void* base, size_t capacity) :
            drawer(d),
            rect(r),
            bucket_size((size_t)std::ceil((float)capacity / rect.w)),
            next_bucket_memory_position(bucket_size),
            base((uintptr_t)base)
        {
        }

        void finish() { draw_bucket(); }

        void draw_bucket()
        {
            if (bucket_used_bytes > 0)
            {
                mu_Rect bucket_rect{rect.x + (int)bucket_index, rect.y, 1, rect.h};
                float fill_ratio = bucket_used_bytes / (float)bucket_size;
                auto color = bucket_has_large_blob ? RED : PURPLE;
                color.a = 127 + 128 * fill_ratio;
                mu_draw_rect(drawer->_ctx, bucket_rect, color);
            }

            bucket_used_bytes = 0;
            bucket_index += 1;
            next_bucket_memory_position += bucket_size;
            bucket_has_large_blob = false;
        };

        void add_used_range(void* ptr, size_t size, bool large_blob)
        {
            size_t range_end = (uintptr_t)ptr - base + size;
            while (range_end >= next_bucket_memory_position)
            {
                bucket_used_bytes += next_bucket_memory_position - memory_position;
                memory_position = next_bucket_memory_position;
                bucket_has_large_blob |= large_blob;
                draw_bucket();
            }

            bucket_used_bytes += range_end - memory_position;
            memory_position = range_end;
        };

        void add_free_range(void* ptr, size_t size, bool large_blob)
        {
            size_t range_end = (uintptr_t)ptr - base + size;
            while (range_end >= next_bucket_memory_position)
            {
                memory_position = next_bucket_memory_position;
                bucket_has_large_blob |= large_blob;
                draw_bucket();
            }

            memory_position = range_end;
        }
    };

public:
    TlsfDevUiDrawer(void* base, size_t capacity, mu_Context* ctx) :
        _ctx(ctx), _rect(mu_layout_next(ctx)), _capacity(capacity)
    {
        mu_draw_rect(_ctx, _rect, mu_Color{20, 20, 20, 255});
    }

    void finish()
    {
        mu_draw_box(_ctx, _rect, mu_Color{0, 0, 0, 255});

        hrz::ui::draw_tooltips(_ctx, &_tooltip_ctx);
        _tooltip_ctx.tooltips.clear();
    }

    static void tlsf_walk_callback(void* ptr, size_t size, int used, void* user)
    {
        bool is_large = size >= 1024 * 1024; // 1 MiB
        auto* ctx = (TlsfWalkContext*)user;
        if (used)
        {
            ctx->add_used_range(ptr, size, is_large);
        }
        else
        {
            ctx->add_free_range(ptr, size, is_large);
        }
    }

    void draw_arena(std::byte* base, BlockArena* arena)
    {
        size_t offset = arena->block().offset();
        size_t size = arena->block().size();

        // Avoid overflow by premultiplying this, because size_t is 32-bit on wasm.
        float size_to_width = (float)_rect.w / _capacity;
        int x0 = offset * size_to_width;
        int x1 = (offset + size) * size_to_width;
        mu_Rect rect{_rect.x + x0, _rect.y, x1 - x0 + 1, _rect.h};

        bool hover = false;
        if (mu_mouse_over(_ctx, rect) && !_has_tooltip)
        {
            size_t free_space = arena->free_space();
            auto stats = arena->stats();
            const auto& str = hrz::format_to_buffer(
                _buffer,
                "Block size: {}\nFree: {} ({:.4}%)\nLargest available: {}\nFragmentation: {:.4}%",
                hrz::bytes_to_string(arena->block().size()).data(),
                hrz::bytes_to_string(free_space).data(),
                (float)free_space * 100.0f / arena->block().size(),
                hrz::bytes_to_string(stats.largest_allocation_available).data(),
                stats.fragmentation * 100.0f);
            hrz::ui::add_tooltip(_ctx, &_tooltip_ctx, str);
            hover = true;
            _has_tooltip = true;
        }

        mu_draw_rect(_ctx, rect, mu_Color{30, 60, 30, 255});

        TlsfWalkContext walk_ctx(this, rect, base + arena->block().offset(), arena->block().size());
        arena->walk_pool(tlsf_walk_callback, &walk_ctx);
        walk_ctx.finish();

        if (hover)
        {
            mu_draw_rect(_ctx, rect, mu_Color{255, 255, 255, 100});
        }
    }
};

void dev_ui(
    BlobAllocator* allocator,
    const LayersInfo* layers_info,
    mu_Context* ctx,
    const char* window_name)
{
    HRZ_SCOPED_SAMPLE("blob allocator dev ui");
    assert(allocator);

    using TreenodeId = std::array<uint64_t, 3>;

    fmt::memory_buffer buffer;

    if (mu_begin_window_ex(ctx, window_name, mu_rect(300, 100, 400, 300), MU_OPT_CLOSED))
    {
        int window_width = mu_get_current_container(ctx)->body.w - 16;

        mu_layout_row(ctx, 1, &window_width, 0);

        mu_text(
            ctx,
            hrz::format_to_buffer(buffer, "{} blobs allocated", allocator->allocated_blob_count));

        mu_text(
            ctx,
            hrz::format_to_buffer(
                buffer, "{} non-0-sized blobs allocated ({:.2f}%)",
                allocator->allocated_blob_count - allocator->zero_size_allocated_blob_count,
                100.0f
                    * (float)(allocator->allocated_blob_count
                              - allocator->zero_size_allocated_blob_count)
                    / allocator->allocated_blob_count));

        mu_text(
            ctx,
            hrz::format_to_buffer(
                buffer, "{} 0-sized blobs allocated ({:.2f}%)",
                allocator->zero_size_allocated_blob_count,
                100.0f * (float)allocator->zero_size_allocated_blob_count
                    / allocator->allocated_blob_count));

        mu_text(
            ctx,
            hrz::format_to_buffer(
                buffer, "{} occupied ({:.2f}%)",
                bytes_to_string(allocator->allocated_memory_size).data(),
                100.0f * (float)allocator->allocated_memory_size / allocator->capacity));

        mu_text(
            ctx,
            hrz::format_to_buffer(
                buffer, "{} free ({:.2f}%)",
                bytes_to_string(allocator->capacity - allocator->allocated_memory_size).data(),
                100.0f * (1.0f - (float)allocator->allocated_memory_size / allocator->capacity)));

        mu_text(
            ctx,
            hrz::format_to_buffer(
                buffer, "{} allocations pending ({})", allocator->unallocated_blob_count,
                bytes_to_string(allocator->unallocated_memory_size).data()));

        if (!allocator->is_malloc_passthrough)
        {
            HRZ_SCOPED_EXCLUSIVE_LOCK(allocator->mutex);
            TlsfDevUiDrawer drawer(nullptr, allocator->capacity, ctx);
            for (const auto& it : allocator->awesome_allocator.blocks_by_largest_allocation)
            {
                drawer.draw_arena(allocator->awesome_allocator.buffer.get(), it.second);
            }
            drawer.finish();
        }
        else
        {
            mu_text(ctx, "Malloc passthrough");
        }

        mu_text(ctx, ""); // A bit of vertical spacing

        auto draw_blob_ui = [&](BlobId blob_id, const Blob* blob)
        {
            buffer.clear();

            static int width = -1;
            mu_layout_row(ctx, 1, &width, 0);

            mu_Rect tooltip_rect = mu_layout_next(ctx);
            mu_layout_set_next(ctx, tooltip_rect, 0);
            if (mu_mouse_over(ctx, tooltip_rect))
            {
                fmt::format_to(
                    std::back_inserter(buffer), "{} use{}", blob->use_count,
                    blob->use_count != 1 ? "s" : "");

                if (blob->mutably_accessed)
                {
                    fmt::format_to(std::back_inserter(buffer), ", mutably accessed");
                }
                if (blob->read_only_accesses > 0)
                {
                    fmt::format_to(
                        std::back_inserter(buffer), ", {} read-only access{}",
                        blob->read_only_accesses, blob->read_only_accesses != 1 ? "es" : "");
                }

                buffer.push_back('\n');
                fmt::format_to(
                    std::back_inserter(buffer), "{:.0f} seconds old",
                    hrz::now_frame_s() - blob->allocation_date);

                hrz::ui::add_tooltip(
                    ctx, &allocator->ui.tooltip_ctx, {buffer.data(), buffer.size()});
            }

            hrz::format_to_buffer(
                buffer, "Blob {}, {}", blob_id, bytes_to_string(blob->size).data());
            if (mu_begin_treenode(ctx, buffer.data()))
            {
                int layout[] = {110, -1};
                mu_layout_row(ctx, 2, layout, 0);

                for (auto& it : blob->metadata)
                {
                    mu_text(ctx, it.first.data());
                    mu_text(ctx, it.second.data());
                }

                mu_end_treenode(ctx);
            }
            buffer.clear();
        };

        auto get_blob = [&](BlobId id) -> const Blob&
        {
            const auto* blob = allocator->blob_pool.get_object(id);
            assert(blob);
            return *blob;
        };

        HRZ_SCOPED_EXCLUSIVE_LOCK(allocator->mutex);

        allocator->ui.allocated_blob_sorter.work(
            get_blob,
            [](const Blob& blob) -> const monitoring::ResourceOwner& { return blob.owner; },
            [](const Blob& a, const Blob& b) { return a.size > b.size; });
        allocator->ui.unallocated_blob_sorter.work(
            get_blob,
            [](const Blob& blob) -> const monitoring::ResourceOwner& { return blob.owner; },
            [](const Blob& a, const Blob& b) { return a.size > b.size; });

        auto draw_resource_tree =
            [&](uint64_t tree_id, gsl::span<const BlobId> ids,
                const std::function<uint64_t(const Blob&)>& get_first_value,
                const std::function<const char*(uint64_t)>& print_first_value,
                const mu_Color& first_bar_color,
                const std::function<uint64_t(const Blob&)>& get_second_value,
                const std::function<const char*(uint64_t)>& print_second_value,
                const mu_Color& second_bar_color)
        {
            size_t next_first_start = 0;

            while (next_first_start < ids.size())
            {
                auto current_first_start = next_first_start;
                auto current_first = get_first_value(get_blob(ids[current_first_start]));
                size_t current_first_res_size = 0;
                size_t current_first_res_count = 0;

                size_t i = current_first_start;
                while (i < ids.size())
                {
                    const auto& resource = get_blob(ids[i]);

                    if (get_first_value(resource) != current_first) break;

                    current_first_res_size += resource.size;
                    current_first_res_count += 1;
                    i += 1;
                }

                next_first_start = i;

                TreenodeId system_row_id = {tree_id << 1, current_first, 0};
                bool first_expanded = hrz::ui::begin_layout_treenode(
                    ctx, &system_row_id, sizeof(system_row_id), allocator->ui.expanded_nodes);
                static int first_layout[] = {100, 60, 64, -1};
                mu_layout_row(ctx, 4, first_layout, 0);
                mu_text(ctx, print_first_value(current_first));
                mu_Rect tooltip_rect = mu_layout_next(ctx);
                mu_layout_set_next(ctx, tooltip_rect, 0);
                mu_text(ctx, bytes_to_string(current_first_res_size, buffer));
                mu_text(ctx, hrz::format_to_buffer(buffer, "{} blobs", current_first_res_count));
                hrz::ui::draw_progress_bar(
                    ctx, (float)current_first_res_size / allocator->capacity, first_bar_color,
                    {80, 80, 80, 255}, &allocator->ui.tooltip_ctx);
                hrz::ui::end_layout_treenode_header(ctx, first_expanded);
                if (first_expanded)
                {
                    size_t next_second_start = current_first_start;

                    while (next_second_start < next_first_start)
                    {
                        auto current_second_start = next_second_start;
                        auto current_second = get_second_value(get_blob(ids[current_second_start]));
                        size_t current_second_res_size = 0;
                        size_t current_second_res_count = 0;

                        size_t j = current_second_start;
                        while (j < next_first_start)
                        {
                            const auto& resource = get_blob(ids[j]);

                            if (get_second_value(resource) != current_second) break;

                            current_second_res_size += resource.size;
                            current_second_res_count += 1;
                            j += 1;
                        }

                        next_second_start = j;

                        TreenodeId layer_row_id = {
                            (tree_id << 1) + 1, current_first, current_second};
                        bool second_expanded = hrz::ui::begin_layout_treenode(
                            ctx, &layer_row_id, sizeof(layer_row_id), allocator->ui.expanded_nodes);
                        static int second_layout[] = {100, 60, 64, -1};
                        mu_layout_row(ctx, 4, second_layout, 0);
                        mu_text(ctx, print_second_value(current_second));
                        mu_Rect tooltip_rect = mu_layout_next(ctx);
                        mu_layout_set_next(ctx, tooltip_rect, 0);
                        mu_text(ctx, bytes_to_string(current_second_res_size, buffer));
                        mu_text(
                            ctx,
                            hrz::format_to_buffer(buffer, "{} blobs", current_second_res_count));
                        hrz::ui::draw_progress_bar(
                            ctx, (float)current_second_res_size / current_first_res_size,
                            second_bar_color, {80, 80, 80, 255}, &allocator->ui.tooltip_ctx);
                        hrz::ui::end_layout_treenode_header(ctx, second_expanded);
                        if (second_expanded)
                        {
                            for (size_t k = current_second_start; k < next_second_start; ++k)
                            {
                                auto blob_id = ids[k];
                                const auto& blob = get_blob(blob_id);

                                draw_blob_ui(blob_id, &blob);
                            }
                        }
                        hrz::ui::end_layout_treenode(ctx, second_expanded);
                    }
                }
                hrz::ui::end_layout_treenode(ctx, first_expanded);
            }
        };

        auto get_system = [&](const Blob& blob) { return (uint64_t)blob.owner.system; };
        auto system_to_string = [&](uint64_t system_as_uint)
        { return monitoring::systems::to_string((monitoring::systems::Name)system_as_uint); };
        auto get_layer_id = [&](const Blob& blob) { return blob.owner.layer_id; };
        auto layer_id_to_string = [&](uint64_t layer_id)
        {
            if (layer_id == monitoring::NoLayer) return "No layer";
            auto it = layers_info->layers.find(layer_id);
            return it != layers_info->layers.end()
                ? it->second.name.c_str()
                : hrz::format_to_buffer(buffer, "Layer {}", layer_id);
        };

        static const mu_Color purple{172, 117, 239, 255};
        static const mu_Color cyan{83, 178, 181, 255};

        if (mu_header(ctx, "Allocated blobs"))
        {
            if (allocator->ui.allocated_blob_sorter.size() == 0)
            {
                mu_label(ctx, "(No blobs)");
            }
            else
            {
                if (mu_begin_treenode(ctx, "By system"))
                {
                    draw_resource_tree(
                        0, allocator->ui.allocated_blob_sorter.get_handles_sorted_by_system(),
                        get_system, system_to_string, purple, get_layer_id, layer_id_to_string,
                        cyan);
                    mu_end_treenode(ctx);
                }

                if (mu_begin_treenode(ctx, "By layer"))
                {
                    draw_resource_tree(
                        1, allocator->ui.allocated_blob_sorter.get_handles_sorted_by_layer(),
                        get_layer_id, layer_id_to_string, cyan, get_system, system_to_string,
                        purple);
                    mu_end_treenode(ctx);
                }
            }
        }

        if (mu_header(ctx, "Pending blob allocations"))
        {
            if (allocator->unallocated_root_blobs.empty())
            {
                mu_label(ctx, "(No blobs)");
            }
            else
            {
                if (mu_begin_treenode(ctx, "By system"))
                {
                    draw_resource_tree(
                        0, allocator->ui.unallocated_blob_sorter.get_handles_sorted_by_system(),
                        get_system, system_to_string, purple, get_layer_id, layer_id_to_string,
                        cyan);
                    mu_end_treenode(ctx);
                }

                if (mu_begin_treenode(ctx, "By layer"))
                {
                    draw_resource_tree(
                        1, allocator->ui.unallocated_blob_sorter.get_handles_sorted_by_layer(),
                        get_layer_id, layer_id_to_string, cyan, get_system, system_to_string,
                        purple);
                    mu_end_treenode(ctx);
                }
            }
        }

        hrz::ui::draw_tooltips(ctx, &allocator->ui.tooltip_ctx);
        allocator->ui.tooltip_ctx.tooltips.clear();

        mu_end_window(ctx);
    }
}

AllocationTicket::~AllocationTicket()
{
    cancel();
}

AllocationTicket::AllocationTicket(AllocationTicket&& other) noexcept :
    allocator{std::exchange(other.allocator, nullptr)},
    blob_id{std::exchange(other.blob_id, NO_BLOB)}
{
}

AllocationTicket& AllocationTicket::operator=(AllocationTicket&& other) noexcept
{
    if (&other != this)
    {
        cancel();

        allocator = std::exchange(other.allocator, nullptr);
        blob_id = std::exchange(other.blob_id, NO_BLOB);
    }
    return *this;
}

void AllocationTicket::cancel()
{
    if (blob_id != NO_BLOB)
    {
        blobs::cancel(allocator, *this);
    }
}

MutableBlobData::~MutableBlobData()
{
    release();
}

MutableBlobData::MutableBlobData(MutableBlobData&& other) noexcept :
    allocator{std::exchange(other.allocator, nullptr)},
    blob_id{std::exchange(other.blob_id, NO_BLOB)},
    data_span{std::exchange(other.data_span, {})}
{
}

MutableBlobData& MutableBlobData::operator=(MutableBlobData&& other) noexcept
{
    if (&other != this)
    {
        release();

        allocator = std::exchange(other.allocator, nullptr);
        blob_id = std::exchange(other.blob_id, NO_BLOB);
        data_span = std::exchange(other.data_span, {});
    }
    return *this;
}

void MutableBlobData::release()
{
    if (blob_id != NO_BLOB)
    {
        HRZ_SCOPED_SHARED_LOCK(allocator->mutex);

        auto blob = allocator->blob_pool.get_object(blob_id);

        HRZ_SCOPED_LOCK(blob->mutex);

        if (blob->parent == NO_BLOB)
        {
#if WRITE_TERMINATORS
            assert(!check_terminator_in_root_allocation(blob));
#endif
            assert(blob->mutably_accessed);
            blob->mutably_accessed = false;
        }
        else
        {
            auto parent_blob = allocator->blob_pool.get_object(blob->parent);

            HRZ_SCOPED_LOCK(parent_blob->mutex);

            assert(parent_blob->mutably_accessed);
            parent_blob->mutably_accessed = false;
        }

        if (blob->use_count == 0)
        {
            HRZ_SCOPED_LOCK(allocator->potentially_unused_blobs_mutex);
            allocator->potentially_unused_blobs.front().push_back(blob_id);
        }

        allocator = nullptr;
        blob_id = NO_BLOB;
    }
}

BlobHandle::~BlobHandle()
{
    release();
}

BlobHandle::BlobHandle(const BlobHandle& other) : allocator{other.allocator}, blob_id{other.blob_id}
{
    acquire();
}

BlobHandle& BlobHandle::operator=(const BlobHandle& other)
{
    if (&other != this)
    {
        release();

        allocator = other.allocator;
        blob_id = other.blob_id;

        acquire();
    }

    return *this;
}

BlobHandle::BlobHandle(BlobHandle&& other) noexcept :
    allocator{std::exchange(other.allocator, nullptr)},
    blob_id{std::exchange(other.blob_id, NO_BLOB)}
{
}

BlobHandle& BlobHandle::operator=(BlobHandle&& other) noexcept
{
    if (&other != this)
    {
        release();

        blob_id = std::exchange(other.blob_id, NO_BLOB);
        allocator = std::exchange(other.allocator, nullptr);
    }
    return *this;
}

void BlobHandle::acquire()
{
    if (blob_id != NO_BLOB)
    {
        HRZ_SCOPED_SHARED_LOCK(allocator->mutex);

        auto blob = allocator->blob_pool.get_object(blob_id);
        assert(blob);

        HRZ_SCOPED_LOCK(blob->mutex);

        assert(!blob->mutably_accessed);
        blob->use_count += 1;
    }
}

void BlobHandle::release()
{
    if (blob_id != NO_BLOB)
    {
        assert(allocator);

        HRZ_SCOPED_SHARED_LOCK(allocator->mutex);

        auto blob = allocator->blob_pool.get_object(blob_id);
        assert(blob);

        HRZ_SCOPED_LOCK(blob->mutex);

        assert(blob->use_count >= 1);
        blob->use_count -= 1;

        if (blob->use_count == 0)
        {
            HRZ_SCOPED_LOCK(allocator->potentially_unused_blobs_mutex);
            allocator->potentially_unused_blobs.front().push_back(blob_id);
        }

        allocator = nullptr;
        blob_id = NO_BLOB;
    }
}

gsl::span<std::byte> BlobHandle::get_data(bool mutable_data) const
{
    auto blob = allocator->blob_pool.get_object(blob_id);

    HRZ_SCOPED_LOCK(blob->mutex);

    std::byte* data_ptr = nullptr;

    if (blob->parent == NO_BLOB)
    {
        assert(
            !blob->mutably_accessed && (blob->read_only_accesses == 0 || !mutable_data)
            && "R^W access error");
        if (mutable_data)
        {
            blob->mutably_accessed = true;
        }
        else
        {
            blob->read_only_accesses += 1;
        }

        data_ptr = blob->data_ptr;
    }
    else
    {
        auto parent_blob = allocator->blob_pool.get_object(blob->parent);

        HRZ_SCOPED_LOCK(parent_blob->mutex);

        assert(
            !parent_blob->mutably_accessed
            && (parent_blob->read_only_accesses == 0 || !mutable_data) && "R^W access error");
        if (mutable_data)
        {
            parent_blob->mutably_accessed = true;
        }
        else
        {
            parent_blob->read_only_accesses += 1;
        }

        data_ptr = parent_blob->data_ptr + blob->offset_in_parent;
    }

    return {data_ptr, blob->size};
}

BlobData BlobHandle::get_data() const
{
    assert(allocator);
    assert(blob_id != NO_BLOB);
    HRZ_SCOPED_SHARED_LOCK(allocator->mutex);

    return BlobData(allocator, blob_id, get_data(false));
}

MutableBlobData BlobHandle::get_mutable_data() const
{
    assert(allocator);
    assert(blob_id != NO_BLOB);
    HRZ_SCOPED_SHARED_LOCK(allocator->mutex);

    return MutableBlobData(allocator, blob_id, get_data(true));
}

size_t BlobHandle::data_size() const
{
    if (allocator == nullptr || blob_id == NO_BLOB)
    {
        return 0;
    }

    assert(allocator);
    assert(blob_id != NO_BLOB);
    HRZ_SCOPED_SHARED_LOCK(allocator->mutex);

    auto blob = allocator->blob_pool.get_object(blob_id);

    return blob->size;
}

bool BlobHandle::check_integrity() const
{
#if WRITE_TERMINATORS
    if (allocator == nullptr || blob_id == NO_BLOB)
    {
        return true;
    }

    assert(allocator);
    assert(blob_id != NO_BLOB);
    HRZ_SCOPED_SHARED_LOCK(allocator->mutex);

    auto blob = allocator->blob_pool.get_object(blob_id);

    if (blob->size == 0 || blob->parent != NO_BLOB)
    {
        return true;
    }

    return !check_terminator_in_root_allocation(blob).has_value();
#else
    return true;
#endif
}

BlobData::~BlobData()
{
    release();
}

BlobData::BlobData(BlobData&& other) noexcept :
    allocator{std::exchange(other.allocator, nullptr)},
    blob_id{std::exchange(other.blob_id, NO_BLOB)},
    data_span{std::exchange(other.data_span, {})}
{
}

BlobData& BlobData::operator=(BlobData&& other) noexcept
{
    if (&other != this)
    {
        release();

        assert(allocator == nullptr && blob_id == NO_BLOB);

        allocator = std::exchange(other.allocator, nullptr);
        blob_id = std::exchange(other.blob_id, NO_BLOB);
        data_span = std::exchange(other.data_span, {});
    }
    return *this;
}

void BlobData::release()
{
    if (blob_id != NO_BLOB)
    {
        assert(allocator);

        HRZ_SCOPED_SHARED_LOCK(allocator->mutex);

        auto blob = allocator->blob_pool.get_object(blob_id);

        HRZ_SCOPED_LOCK(blob->mutex);

        if (blob->parent == NO_BLOB)
        {
            assert(blob->read_only_accesses >= 1);
            blob->read_only_accesses -= 1;
        }
        else
        {
            auto parent_blob = allocator->blob_pool.get_object(blob->parent);

            HRZ_SCOPED_LOCK(parent_blob->mutex);

            assert(parent_blob->read_only_accesses >= 1);
            parent_blob->read_only_accesses -= 1;
        }

        if (blob->use_count == 0)
        {
            HRZ_SCOPED_LOCK(allocator->potentially_unused_blobs_mutex);
            allocator->potentially_unused_blobs.front().push_back(blob_id);
        }

        allocator = nullptr;
        blob_id = NO_BLOB;
    }
}
} // namespace blobs
} // namespace hrz
