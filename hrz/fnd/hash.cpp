#include "hrz/fnd/hash.h"

#include "hrz/fnd/inlined_vector.h"

namespace hrz
{

uint64_t hash_kv(std::span<const std::pair<std::string_view, std::string_view>> kvs)
{
    if (kvs.empty())
    {
        return 0;
    }

    hrz::InlinedVector<std::pair<uint64_t, uint64_t>, 16> ordered;
    ordered.reserve(kvs.size());

    for (const auto& kv : kvs)
    {
        const uint64_t k = murmur3_x64_64(kv.first);
        const uint64_t v = murmur3_x64_64(kv.second);
        ordered.emplace_back(k, hash_mix(k, v));
    }

    std::ranges::sort(
        ordered,
        [](const std::pair<uint64_t, uint64_t>& a, const std::pair<uint64_t, uint64_t>& b) -> bool
        { return a.first < b.first; });

    hrz::InlinedVector<uint64_t, 16> hashes;
    hashes.reserve(ordered.size());

    for (const auto& pair : ordered)
    {
        hashes.push_back(pair.second);
    }

    return hash_value(hashes);
}

} // namespace hrz
