// SPDX-FileCopyrightText: Copyright 2019 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/fnd/class.h"
#include "hrz/fnd/defines.h"
#include "hrz/fnd/gen_index_pool.h"

#include <stdlib.h>

// This checks that all allocated objects have been released when the stored
// objects don't have a trivial destructor. This check only happens in debug
// builds.
// For now this is disabled because I don't have the time to fix all instances
// of leaked resources but one day we should re-enable it and fix everything.
//      -slerouzic, 2021-12-08
#define HRZ_FND_GEN_OBJECT_POOL_CHECK_LEAK 0

#if HRZ_DEBUG && HRZ_FND_GEN_OBJECT_POOL_CHECK_LEAK
#    include <type_traits>
#endif

namespace hrz
{

/**
 * Object pool using generational indices for lookup.
 * It is recommended to always lookup objects by handle, except in
 * performance-critical parts, in which case a pointer can
 * be retained a bit longer.
 *
 * @Todo :Memory Recycle some chunks when not used at all.
 * @Todo @Leak :Memory Call destructors on still-used objects upon destruction.
 * Or maybe not because it's the caller's responsibility to release their stuff.
 * In which case we could have asserts or something like this to check that
 * everything has been released.
 */
template<
    typename T,
    typename IndexPool = GenIndexPool<uint32_t, 16, 16>,
    uint32_t ChunkSize = 128
> // Number of objects per allocated chunk
class GenObjectPool
{
    static_assert(ChunkSize > 0, "ChunkSize non-zero");

public:
    using Handle = typename IndexPool::Handle;

private:
    // This uses a raw pointer instead of a vector or unique_ptr or T[] because
    // this memory should be uninitialized before allocation and because we
    // call the destructor upon release, and we don't want that to happen again
    // when we destroy the pool.
    struct Chunk
    {
        std::byte* actual_ptr; // Memory allocation location
        T* view;               // Aligned version of the previous pointer

        constexpr Chunk(std::byte* actual_ptr, T* view) : actual_ptr{actual_ptr}, view{view} {}

        HRZ_DELETE_COPY(Chunk);

        Chunk(Chunk&& other) :
            actual_ptr{std::exchange(other.actual_ptr, nullptr)},
            view{std::exchange(other.view, nullptr)}
        {
        }

        Chunk& operator =(Chunk&& other)
        {
            if (&other != this)
            {
                actual_ptr = std::exchange(other.actual_ptr, nullptr);
                view = std::exchange(other.view, nullptr);
            }
            return *this;
        }

        ~Chunk()
        {
            if (actual_ptr)
            {
                free(actual_ptr);
            }
        }
    };

    IndexPool _index_pool;
    std::vector<Chunk> _chunks;
#if HRZ_DEBUG && HRZ_FND_GEN_OBJECT_POOL_CHECK_LEAK
    int64_t _allocated = 0;
#endif

    static inline void get_indices(Handle h, Handle* chunk_index, Handle* index_in_chunk)
    {
        assert(chunk_index && index_in_chunk);

        auto index = IndexPool::get_index(h);
        *chunk_index = index / ChunkSize;
        *index_in_chunk = index % ChunkSize;
    }

    Handle alloc_uninit()
    {
        Handle h = _index_pool.alloc();

        Handle chunk_index, index_in_chunk;
        get_indices(h, &chunk_index, &index_in_chunk);

        // This checks that we can push_back.
        // Should be guaranteed by the implementation of GenIndexPool.
        assert(chunk_index <= _chunks.size());

        if (chunk_index == _chunks.size())
        {
            auto* actual_ptr = (std::byte*)malloc(sizeof(T) * ChunkSize + alignof(T) - 1);

            // Alignment
            uintptr_t ptr_i = (uintptr_t)actual_ptr + alignof(T) - 1;
            ptr_i = (ptr_i / alignof(T)) * alignof(T);

            assert(ptr_i % alignof(T) == 0);

            _chunks.emplace_back(Chunk{actual_ptr, (T*)ptr_i});
        }

#if HRZ_DEBUG && HRZ_FND_GEN_OBJECT_POOL_CHECK_LEAK
        _allocated += 1;
#endif

        return h;
    }

public:
    GenObjectPool() = default;

    HRZ_DELETE_COPY(GenObjectPool);

#if HRZ_DEBUG && HRZ_FND_GEN_OBJECT_POOL_CHECK_LEAK
    GenObjectPool(GenObjectPool&& other) :
        _index_pool{std::move(other._index_pool)},
        _chunks{std::move(other._chunks)},
        _allocated{std::exchange(other._allocated, 0)}
    {
    }

    GenObjectPool& operator =(GenObjectPool&& other)
    {
        if (&other != this)
        {
            _index_pool = std::move(other._index_pool);
            _chunks = std::move(other._chunks);
            _allocated = std::exchange(other._allocated, 0);
        }
        return *this;
    }

    ~GenObjectPool()
    {
        if (!std::is_trivially_destructible_v<T>)
        {
            assert(_allocated == 0);
        }
    }
#else
    HRZ_DEFAULT_MOVE(GenObjectPool);
    ~GenObjectPool() = default;
#endif

    inline bool is_valid(Handle h) const { return _index_pool.is_valid(h); }

    const T* get_object(Handle h) const
    {
        if (!is_valid(h)) return nullptr;

        Handle chunk_index, index_in_chunk;
        get_indices(h, &chunk_index, &index_in_chunk);

        return _chunks[chunk_index].view + index_in_chunk;
    }

    T* get_object(Handle h)
    {
        if (!is_valid(h)) return nullptr;

        Handle chunk_index, index_in_chunk;
        get_indices(h, &chunk_index, &index_in_chunk);

        return _chunks[chunk_index].view + index_in_chunk;
    }

    template<typename... Args>
    Handle alloc(Args&&... args)
    {
        Handle h = alloc_uninit();

        T* obj = get_object(h);
        assert(obj);
        new (obj) T(std::forward<Args>(args)...);

        return h;
    }

    // Destructor is called when this is.
    void release(Handle h)
    {
        if (!is_valid(h)) return;

        T* obj = get_object(h);
        _index_pool.release(h);
        std::destroy_at(obj);

#if HRZ_DEBUG && HRZ_FND_GEN_OBJECT_POOL_CHECK_LEAK
        _allocated -= 1;
#endif
    }
};

} // namespace hrz
