#pragma once

#include "hrz/fnd/flat_hash_set.h"
#include "hrz/fnd/inlined_vector.h"

#include <vector>

namespace hrz
{

// A unique vector is a push-only vector that deduplicates its elements using a set.
// In contrast to a set, it preserves insertion order.
// The Container must be std::vector-like (have push_back and clear).
template<typename T, typename Container = std::vector<T>>
class UniqueVector
{
    flat_hash_set<T> _set;
    Container _container;

public:
    using const_iterator = typename Container::const_iterator;

    void push_back(const T& value)
    {
        if (_set.insert(value).second)
        {
            _container.push_back(value);
        }
    }

    void clear()
    {
        _set.clear();
        _container.clear();
    }

    constexpr const_iterator begin() const { return _container.begin(); }

    constexpr const_iterator end() const { return _container.end(); }

    std::span<const T> as_span() const { return {_container.data(), _container.size()}; }
};

template<typename T, size_t N>
using InlinedUniqueVector = UniqueVector<T, InlinedVector<T, N>>;

} // namespace hrz
