// SPDX-FileCopyrightText: Copyright 2019 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include <cassert>
#include <cstdint>
#include <vector>

namespace my
{

/**
 * Generational object pool for POD data types.
 */
template<typename T, uint64_t GEN_BITS, uint64_t INDEX_BITS>
struct GenPool
{
    static_assert(GEN_BITS + INDEX_BITS <= 64, "Pool handle size");

    static const uint64_t MAX_INDEX = (1ULL << INDEX_BITS) - 1;
    static const uint64_t MAX_GEN = (1ULL << GEN_BITS) - 1;
    static const uint64_t INDEX_MASK = (1ULL << INDEX_BITS) - 1;
    static const uint64_t GEN_MASK = ((1ULL << GEN_BITS) - 1) << INDEX_BITS;

    GenPool() = default;

    ~GenPool() { free(); }

    using Handle = uint64_t;

    GenPool(const GenPool&) = delete;
    GenPool& operator =(const GenPool&) = delete;

    void free()
    {
        _obj.clear();
        _gen.clear();
        _free.clear();
    }

    inline Handle _make_handle(uint64_t index) const
    {
        uint64_t gen = _gen[index];
        return (((uint64_t)gen) << INDEX_BITS) | index;
    }

    static inline uint64_t _get_index(Handle handle) { return (uint64_t)(handle & INDEX_MASK); }

    bool is_valid(Handle handle) const
    {
        uint64_t gen = (handle & GEN_MASK) >> INDEX_BITS;
        uint64_t index = _get_index(handle);

        return index < _gen.size() && _gen[index] == gen;
    }

    Handle acquire(const T& value)
    {
        if (_free.empty())
        {
            _obj.push_back(T{});
            _gen.push_back(1);
            _free.push_back(_obj.size() - 1);

            assert(_obj.size() <= INDEX_MASK);
        }

        uint64_t index = _free.back();
        _free.pop_back();

        _obj[index] = value;
        return _make_handle(index);
    }

    void release(Handle handle)
    {
        if (!is_valid(handle)) return;

        uint64_t index = _get_index(handle);
        std::destroy_at(&_obj[index]);
        _gen[index] = (_gen[index] + 1) & MAX_GEN;
        _free.push_back(index);
    }

    T* operator [](Handle handle)
    {
        if (!is_valid(handle)) return nullptr;
        return &_obj[_get_index(handle)];
    }

    const T* operator [](Handle handle) const
    {
        if (!is_valid(handle)) return nullptr;
        return &_obj[_get_index(handle)];
    }

    std::vector<T> _obj;
    std::vector<uint64_t> _gen;
    std::vector<uint64_t> _free;
};

} // namespace my
