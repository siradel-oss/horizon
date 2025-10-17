#pragma once

#include "hrz_fnd_maths.h"

#include <assert.h>
#include <string.h>

#include <algorithm>
#include <memory>
#include <span>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string_view>

namespace hrz
{
class Arena
{
    struct BlockHeader
    {
        BlockHeader* next_block;
        size_t usable_size;

        constexpr void* data() { return this + 1; }
    };

    size_t _block_size;
    char* _current_end_ptr = nullptr;
    char* _current_bump_ptr = nullptr;
    BlockHeader* _blockchain = nullptr; // :)

    BlockHeader* alloc_block(size_t size)
    {
        BlockHeader* new_block = (BlockHeader*)malloc(size + sizeof(BlockHeader));
        new_block->usable_size = size;
        insert_in_blockchain(new_block);
        return new_block;
    }

    void insert_in_blockchain(BlockHeader* block)
    {
        block->next_block = _blockchain;
        _blockchain = block;
    }

    void make_current(BlockHeader* block)
    {
        _current_bump_ptr = (char*)block->data();
        _current_end_ptr = _current_bump_ptr + block->usable_size;
    }

    template<typename T>
    inline static T* align_ptr_up(T* ptr, size_t align)
    {
        return (T*)align_up_po2((uintptr_t)ptr, align);
    }

public:
    template<typename T>
    using Ptr = T*;

    template<typename T>
    struct Span
    {
        T* ptr = nullptr;
        size_t count = 0;

        constexpr size_t size() const { return count; }

        constexpr const T* data() const { return ptr; }

        constexpr const T* begin() const { return ptr; }

        constexpr const T* end() const { return ptr + count; }
    };

    template<typename T>
    struct Vec
    {
        T* ptr = nullptr;
        size_t count = 0;
        size_t cap = 0;

        constexpr size_t size() const { return count; }

        constexpr const T* data() const { return ptr; }

        constexpr const T* begin() const { return ptr; }

        constexpr const T* end() const { return ptr + count; }

        const T& operator[](unsigned int i) const { return ptr[i]; }

        void reset()
        {
            ptr = nullptr;
            count = 0;
            cap = 0;
        }
    };

    explicit Arena(size_t block_size = 1024 * 1024) : _block_size(block_size) {}

    ~Arena() { reset(true); }

    Arena(Arena&& other) : Arena(other._block_size) { *this = std::move(other); }

    Arena& operator=(Arena&& other)
    {
        if (&other == this) return *this;

        reset(true);

        _block_size = other._block_size;
        _current_end_ptr = std::exchange(other._current_end_ptr, nullptr);
        _current_bump_ptr = std::exchange(other._current_bump_ptr, nullptr);
        _blockchain = std::exchange(other._blockchain, nullptr);

        return *this;
    }

    void reset(bool free_all = false)
    {
        BlockHeader* block_to_keep = nullptr;

        while (_blockchain)
        {
            BlockHeader* block = _blockchain;
            _blockchain = block->next_block;

            if (!free_all && block->usable_size >= _block_size)
            {
                block_to_keep = block;
            }
            else
            {
                free(block);
            }
        }

        _current_bump_ptr = nullptr;
        _current_end_ptr = nullptr;
        _blockchain = nullptr;

        if (block_to_keep)
        {
            insert_in_blockchain(block_to_keep);
            make_current(block_to_keep);
        }
    }

    void* alloc_raw(size_t size, size_t alignment = alignof(max_align_t))
    {
        assert(alignment <= alignof(max_align_t) && "Please stop.");

        if (size > _block_size / 2)
        {
            // Allocate a dedicated block for large allocations.
            // We don't make it the current block or we'd create fragmentation
            // when the current block still has a lot of room.
            BlockHeader* block = alloc_block(size + alignment - 1);
            return align_ptr_up(block->data(), alignment);
        }

        char* ptr = align_ptr_up(_current_bump_ptr, alignment);
        if (ptr + size > _current_end_ptr)
        {
            make_current(alloc_block(_block_size));
            ptr = align_ptr_up(_current_bump_ptr, alignment);
        }

        assert(ptr + size <= _current_end_ptr);
        _current_bump_ptr = ptr + size;

        return ptr;
    }

    template<typename T, typename... Args>
    Ptr<T> alloc(Args&&... args)
    {
        static_assert(std::is_trivially_destructible_v<T>, "T should be trivially destructible");

        void* raw_ptr = alloc_raw(sizeof(T), alignof(T));
        T* ptr = new (raw_ptr) T(std::forward<Args>(args)...);
        return ptr;
    }

    template<typename T>
    Span<T> alloc_array(size_t count)
    {
        static_assert(std::is_trivially_destructible_v<T>, "T should be trivially destructible");

        void* raw_ptr = alloc_raw(sizeof(T) * count, alignof(T));
        T* ptr = new (raw_ptr) T[count];
        Span<T> span;
        span.ptr = ptr;
        span.count = count;
        return span;
    }

    template<typename T>
    void push(Vec<T>& vec, T value)
    {
        static_assert(std::is_trivially_destructible_v<T>, "T should be trivially destructible");

        if (vec.count >= vec.cap)
        {
            size_t new_cap = std::max(vec.cap * 2, (size_t)4);
            Span<T> new_span = alloc_array<T>(new_cap);
            std::copy_n(vec.ptr, vec.count, new_span.ptr);
            vec.ptr = new_span.ptr;
            vec.cap = new_span.count;
        }

        assert(vec.count < vec.cap);
        vec.ptr[vec.count++] = value;
    }

    std::string_view str(std::string_view str)
    {
        auto* copy = (char*)alloc_raw(str.size(), 1);
        memcpy(copy, str.data(), str.size());
        return {copy, str.size()};
    }

    std::string_view zstr(std::string_view str)
    {
        auto* copy = (char*)alloc_raw(str.size() + 1, 1);
        memcpy(copy, str.data(), str.size());
        copy[str.size()] = '\0';
        return {copy, str.size()};
    }

    // The span does not contain the terminating null character, but it's there.
    std::span<char> zstr_span(std::string_view str)
    {
        auto* copy = (char*)alloc_raw(str.size() + 1, 1);
        memcpy(copy, str.data(), str.size());
        copy[str.size()] = '\0';
        return {copy, str.size()};
    }
};

} // namespace hrz
