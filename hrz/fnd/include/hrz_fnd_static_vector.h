#pragma once

#include <assert.h>

#include <array>
#include <span>

namespace hrz
{
template<typename T, size_t CAPACITY>
class StaticVector
{
    static_assert(
        std::is_trivially_destructible_v<T>,
        "StaticVector only works with trivially destructible data types");

    std::array<T, CAPACITY> _data;
    size_t _size = 0;

    using iterator = typename std::array<T, CAPACITY>::iterator;
    using const_iterator = typename std::array<T, CAPACITY>::const_iterator;

public:
    inline void push_back(const T& elmt)
    {
        assert(_size < CAPACITY);
        _data[_size++] = elmt;
    }

    inline void push_back(T&& elmt)
    {
        assert(_size < CAPACITY);
        _data[_size++] = std::move(elmt);
    }

    inline void pop_back()
    {
        assert(_size > 0);
        _size -= 1;
    }

    inline void clear() { _size = 0; }

    inline size_t empty() const { return _size == 0; }

    inline size_t size() const { return _size; }

    inline const T* data() const { return _data.data(); }

    inline T* data() { return _data.data(); }

    inline iterator begin() { return _data.begin(); }

    inline iterator end() { return _data.begin() + _size; }

    inline const_iterator begin() const { return _data.begin(); }

    inline const_iterator end() const { return _data.begin() + _size; }

    inline void set_size(size_t new_size)
    {
        assert(new_size <= CAPACITY);
        _size = new_size;
    }

    inline T& operator[](size_t i)
    {
        assert(i < _size);
        return _data[i];
    }

    inline const T& operator[](size_t i) const
    {
        assert(i < _size);
        return _data[i];
    }

    inline operator std::span<T>() { return std::span<T>(_data.data(), _size); }

    inline operator std::span<const T>() const { return std::span<const T>(_data.data(), _size); }

    inline bool operator==(const StaticVector& other) const
    {
        if (_size != other._size)
        {
            return false;
        }

        for (size_t i = 0; i < _size; ++i)
        {
            if (_data[i] != other._data[i])
            {
                return false;
            }
        }

        return true;
    }

    inline bool operator!=(const StaticVector& other) const { return !(*this == other); }
};

} // namespace hrz
