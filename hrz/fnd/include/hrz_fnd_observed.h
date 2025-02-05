#pragma once

#include "absl/utility/utility.h"

#include <functional>
#include <utility>

namespace hrz
{
/**
 * Stores some data and observes modifications applied to it.
 * It's basically an automatic dirty flag.
 * Use "reset" to reset the dirty flag.
 */
template<typename T>
class Observed
{
    T _data{};
    bool _dirty = false;

public:
    Observed() = default;

    template<typename... Args>
    Observed(std::in_place_t, Args&&... args) : _data(std::forward<Args>(args)...), _dirty(true)
    {
    }

    constexpr const T* operator->() const { return &_data; }

    void mutate(std::function<void(T&)> fn)
    {
        T old = _data;
        fn(_data);
        if (old != _data) _dirty = true;
    }

    void set(const T& value)
    {
        if (value != _data)
        {
            _data = value;
            _dirty = true;
        }
    }

    void set(T&& value)
    {
        if (value != _data)
        {
            _data = value;
            _dirty = true;
        }
    }

    const T& get() const { return _data; }

    constexpr bool is_dirty() const { return _dirty; }

    void reset() { _dirty = false; }
};

} // namespace hrz
