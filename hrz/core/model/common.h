#pragma once

#include "hrz/common/maths.h"
#include "hrz/fnd/flat_hash_set.h"
#include "hrz/fnd/function_ref.h"

#include <mycelium/backend.h>

namespace hrz::model
{

inline hrz::BSphere<double> compute_bsphere_from_bbox(
    const lm::dbbox3& prim_bbox,
    const lm::dmat4& transform)
{
    // Transform all the corners of the primitive's bounding box,
    // and compute a bounding sphere from the set of transformed points.
    lm::dvec3 corners[8];
    for (uint32_t i = 0; i < 8; ++i)
    {
        corners[i] = (transform * lm::dvec4(lm::corner(prim_bbox, i), 1.0)).xyz;
    }
    return hrz::compute_bounding_sphere(std::span<const lm::dvec3>(corners));
}

struct AdditionalVertexInputStream
{
    const char* name;
    int index;
    my::VertexInputStream fallback_stream;
};

// This structure stores a list of resources used by a mesh, and specifically
// what ones are still loading, and what ones are ready. It is useful for
// waiting for specific resources to be loaded, and being able to destroy them
// when the owner is destroyed.
template<typename T>
struct UsedResources
{
    hrz::flat_hash_set<T> waiting_on;
    hrz::flat_hash_set<T> in_use;

    inline void add(const T& key) { waiting_on.insert(key); }

    // The callback must return true if the waiting is done, false otherwise.
    inline void iterate_waiting_on(hrz::function_ref<bool(const T&)> fn)
    {
        for (auto it = waiting_on.begin(); it != waiting_on.end();)
        {
            if (fn(*it))
            {
                in_use.insert(*it);
                waiting_on.erase(it++);
            }
            else
            {
                ++it;
            }
        }
    }

    inline void iterate_all(hrz::function_ref<void(const T&)> fn) const
    {
        for (const T& key : waiting_on)
        {
            fn(key);
        }

        for (const T& key : in_use)
        {
            fn(key);
        }
    }

    constexpr bool all_ready() const { return waiting_on.empty(); }

    bool contains(const T& key) const { return waiting_on.contains(key) || in_use.contains(key); }

    void clear()
    {
        waiting_on.clear();
        in_use.clear();
    }
};

} // namespace hrz::model
