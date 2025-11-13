#pragma once

#include "hrz/fnd/arena.h"
#include "hrz/fnd/flat_hash_set.h"

#include <string.h>

#include <string_view>

namespace hrz
{
class InternString
{
    Arena _arena;
    hrz::flat_hash_set<std::string_view> _index;

public:
    InternString() : _arena(1024) {}

    std::string_view intern_view(std::string_view str)
    {
        std::string_view interned = intern_view_or_null(str);
        if (interned.data())
        {
            return interned;
        }
        else
        {
            char* ptr = (char*)_arena.alloc_raw(str.size() + 1);
            memcpy(ptr, str.data(), str.size());
            ptr[str.size()] = 0;
            _index.insert(std::string_view(ptr, str.size()));
            return {ptr, str.size()};
        }
    }

    inline const char* intern(std::string_view str) { return intern_view(str).data(); }

    std::string_view intern_view_or_null(std::string_view str) const
    {
        auto it = _index.find(str);
        if (it != _index.end())
        {
            return *it;
        }
        else
        {
            return {};
        }
    }

    inline const char* intern_or_null(std::string_view str) const
    {
        return intern_view_or_null(str).data();
    }

    void reset()
    {
        _arena.reset();
        _index.clear();
    }
};
} // namespace hrz
