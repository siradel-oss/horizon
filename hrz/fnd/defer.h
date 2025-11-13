#pragma once

#include "hrz/fnd/defines.h"

#include <utility>

namespace hrz
{

template<typename Finalizer>
class DeferFinalizer
{
    HRZ_NO_UNIQUE_ADDRESS Finalizer _finalizer;
    bool _moved = false;

public:
    template<typename T>
    explicit DeferFinalizer(T&& finalizer) : _finalizer(std::forward<T>(finalizer))
    {
    }

    DeferFinalizer(const DeferFinalizer&) = delete;
    DeferFinalizer& operator=(const DeferFinalizer&) = delete;

    DeferFinalizer(DeferFinalizer&& other) :
        _finalizer(std::move(other._finalizer)), _moved(std::exchange(other._moved, true))
    {
    }

    DeferFinalizer& operator=(DeferFinalizer&&) = delete;

    ~DeferFinalizer()
    {
        if (!_moved) _finalizer();
    }
};

struct DeferInitializer
{
    template<typename Finalizer>
    DeferFinalizer<Finalizer> operator<<(Finalizer&& finalizer) const
    {
        return DeferFinalizer<Finalizer>(std::forward<Finalizer>(finalizer));
    }
};

} // namespace hrz

// Use it like this HRZ_DEFER [...]() { something(); };
// Be careful about the lifetime of the captured variables.
// This is why it is kept explicit.
#define HRZ_DEFER auto HRZ_CONCAT(_finalizer_, __COUNTER__) = hrz::DeferInitializer() <<
