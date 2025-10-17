#pragma once

#include <hrz_common_geo.h>
#include <hrz_fnd_flat_hash_set.h>

#include <mycelium_backend.h>

#include <functional>

namespace hrz::model
{
static hrz::BSphere<double> compute_bsphere_from_bbox(
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
    inline void iterate_waiting_on(std::function<bool(const T&)> fn)
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

    inline void iterate_all(std::function<void(const T&)> fn)
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
};

} // namespace hrz::model
