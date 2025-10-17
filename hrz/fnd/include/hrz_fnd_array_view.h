#pragma once

#include <cassert>
#include <cstddef>
#include <span>

namespace hrz
{
template<typename T>
class ArrayView
{
    template<typename U>
    struct BackingTypeT
    {
        using Type = std::byte;
    };

    template<typename U>
    struct BackingTypeT<const U>
    {
        using Type = const std::byte;
    };

    using BackingType = typename BackingTypeT<T>::Type;

public:
    ArrayView() = default;

    ArrayView(T* data, size_t element_count, size_t byte_stride) :
        _data((BackingType*)data), _length(element_count), _stride(byte_stride)
    {
        assert(((size_t)data) % alignof(T) == 0);
        assert(byte_stride >= sizeof(T));
        assert(byte_stride % alignof(T) == 0);
    }

    ArrayView(T* data, size_t length) :
        _data((BackingType*)data), _length(length), _stride(sizeof(T))
    {
    }

    ArrayView(std::span<T> span) :
        _data((BackingType*)span.data()), _length(span.size()), _stride(sizeof(T))
    {
    }

    constexpr size_t size() const { return _length; }

    inline T& at(size_t index) { return *_at(index); }

    inline const T& at(size_t index) const { return *_at(index); }

    inline T& operator[](size_t index) { return *_at(index); }

    inline const T& operator[](size_t index) const { return *_at(index); }

    inline T* data() { return (T*)_data; }

    inline const T* data() const { return (const T*)_data; }

private:
    inline T* _at(size_t index)
    {
        assert(index < _length);
        return (T*)(_data + index * _stride);
    }

    inline const T* _at(size_t index) const
    {
        assert(index < _length);
        return (const T*)(_data + index * _stride);
    }

    BackingType* _data;
    size_t _length;
    size_t _stride;
};
} // namespace hrz
