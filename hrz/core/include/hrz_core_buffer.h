#pragma once

#include <cassert>
#include <memory>
#include <span>

namespace hrz
{
template<typename T>
class Buffer
{
    int _width;
    int _height;
    int _depth;
    std::unique_ptr<T[]> _data;

    int offset(int x, int y, int z) const
    {
        assert(x >= 0 && x < _width);
        assert(y >= 0 && y < _height);
        assert(z >= 0 && z < _depth);
        return x + (y + z * _height) * _width;
    }

public:
    Buffer() = default;
    ~Buffer() = default;

    Buffer(const Buffer&) = default;
    Buffer(Buffer&&) = default;

    Buffer& operator=(const Buffer&) = default;
    Buffer& operator=(Buffer&&) = default;

    Buffer(int w, int h, int d = 1) : _width(w), _height(h), _depth(d), _data(new T[w * h * d]) {}

    bool valid() const { return _data.get() != nullptr; }

    int width() const { return _width; }

    int height() const { return _height; }

    int depth() const { return _depth; }

    const T* data() const { return _data.get(); }

    T* data() { return _data.get(); }

    const T* data(int x, int y, int z = 0) const { return _data.get() + offset(x, y, z); }

    T* data(int x, int y, int z = 0) { return _data.get() + offset(x, y, z); }

    std::span<const T> as_span() const { return {data(), (size_t)(_width * _height * _depth)}; }

    std::span<T> as_span() { return {data(), (size_t)(_width * _height * _depth)}; }

    std::span<const T> as_span(int x, int y, int z = 0) const
    {
        return {data(x, y, z), (size_t)(_width * _height * _depth - offset(x, y, z))};
    }

    std::span<T> as_span(int x, int y, int z = 0)
    {
        return {data(x, y, z), (size_t)(_width * _height * _depth - offset(x, y, z))};
    }

    const T& operator()(int x, int y, int z = 0) const { return _data[offset(x, y, z)]; }

    T& operator()(int x, int y, int z = 0) { return _data[offset(x, y, z)]; }
};
} // namespace hrz
