// SPDX-FileCopyrightText: Copyright 2019 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include <assert.h>
#include <string.h>

#include <stdint.h>
#include <stdlib.h>

namespace my
{

/**
 * Memory arena for POD data types.
 */
class Arena
{
    struct BlockHeader
    {
        BlockHeader* next;
        char* top;
        size_t free;
        size_t size;
    };

    size_t _block_size;
    BlockHeader* _first_block{};
    size_t _allocated = 0;

    void _alloc_new_block()
    {
        size_t size = _block_size + sizeof(BlockHeader);

        BlockHeader* block = (BlockHeader*)malloc(size);
        block->next = _first_block;
        block->size = _block_size;
        block->free = block->size;
        block->top = (char*)(block + 1);

        _first_block = block;
    }

    void* _alloc_dedicated_block(size_t size)
    {
        size += sizeof(BlockHeader);

        BlockHeader* block = (BlockHeader*)malloc(size);

        if (!_first_block)
        {
            // We have to have a first non-dedicated block for
            // the linked list of blocks to free otherwise we
            // may waste the space left in the first block.
            _alloc_new_block();
        }

        assert(_first_block);

        block->next = _first_block->next;
        _first_block->next = block;

        block->size = size - sizeof(BlockHeader);
        block->free = 0;
        block->top = ((char*)block) + size;

        return (void*)(block + 1);
    }

public:
    explicit Arena(size_t block_size) : _block_size(block_size) {}

    Arena(const Arena&) = delete;
    Arena& operator =(const Arena&) = delete;

    Arena(Arena&&) = delete;
    Arena& operator =(Arena&&) = delete;

    ~Arena() { free(); }

    void reset()
    {
        // Basically same as free, but we recycle one block.
        // This may not be ideal, maybe we want to recycle all blocks.
        // But for now this'll do.
        while (_first_block && _first_block->next)
        {
            BlockHeader* next = _first_block->next;
            ::free(_first_block);
            _first_block = next;
        }

        if (_first_block)
        {
            _first_block->free = _first_block->size;
            _first_block->top = ((char*)_first_block) + sizeof(BlockHeader);
        }

        _allocated = 0;
    }

    size_t allocated() const { return _allocated; }

    void free()
    {
        while (_first_block)
        {
            BlockHeader* next = _first_block->next;
            ::free(_first_block);
            _first_block = next;
        }

        _allocated = 0;
    }

    void* alloc(size_t size)
    {
        if (size == 0) return nullptr;

        // Align size up to 16 bytes
        size = (size + 15) & (~0xfULL);

        _allocated += size;

        // Alloc sizes that are too large get a dedicated block.
        // This prevents fragmentation.
        if (size > _block_size)
        {
            return _alloc_dedicated_block(size);
        }
        else
        {
            if (!_first_block || _first_block->free < size)
            {
                _alloc_new_block();
            }

            assert(_first_block->free >= size);

            void* ptr = (void*)_first_block->top;
            _first_block->top += size;
            _first_block->free -= size;

            return ptr;
        }
    }

    void* alloc_copy(const void* data, size_t size)
    {
        void* ptr = alloc(size);
        memcpy(ptr, data, size);
        return ptr;
    }

    template<typename T>
    T* alloc_place(const T& value)
    {
        return (T*)alloc_copy(&value, sizeof(T));
    }
};

} // namespace my
