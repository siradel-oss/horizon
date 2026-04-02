#pragma once

#include "absl/container/flat_hash_set.h"
#include "arena.h"

#include <string.h>

#include <string_view>

namespace my
{

class InternString
{
    Arena _arena{1024 * 1024};
    // Strings are stored zero-terminated so then can be printf-ed later.
    absl::flat_hash_set<std::string_view> _index;

public:
    const char* intern_as_str(std::string_view str)
    {
        const uintptr_t interned = intern_or_null(str);
        if (interned)
        {
            return reinterpret_cast<const char*>(interned);
        }
        else
        {
            auto* ptr = (char*)_arena.alloc(str.size() + 1);
            memcpy(ptr, str.data(), str.size());
            ptr[str.size()] = '\0';
            _index.insert(std::string_view(ptr, str.size()));
            return ptr;
        }
    }

    uintptr_t intern(std::string_view str) { return absl::bit_cast<uintptr_t>(intern_as_str(str)); }

    uintptr_t intern_or_null(std::string_view str) const
    {
        auto it = _index.find(str);
        if (it != _index.end())
        {
            return absl::bit_cast<uintptr_t>(it->data());
        }
        else
        {
            return 0;
        }
    }

    const char* get_string(uintptr_t interned) const
    {
        return absl::bit_cast<const char*>(interned);
    }
};

} // namespace my
