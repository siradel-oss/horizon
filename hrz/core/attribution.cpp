#include "hrz/core/attribution.h"

#include "hrz/fnd/arena.h"
#include "hrz/fnd/flat_hash_set.h"
#include "hrz/fnd/hash.h"
#include "hrz/fnd/inlined_vector.h"
#include "hrz/fnd/intern_string.h"
#include "hrz/fnd/thread.h"

#include <algorithm>
#include <bit>
#include <mutex>

namespace hrz
{

// When we want to allocate an AttributionGroup from the pool, when it's a simple attribution, we
// just allocate one normally. If it's a group, we allocate such that we can fit the header (size)
// and as many AttributionHandles as necessary. Since "group" is the last element this makes it
// essentially a variable length array.

struct AttributionGroup
{
    int size{}; // 0: simple attribution, > 0: group

    AttributionGroup() {}

    union
    {
        Attribution simple;

        struct
        {
            hrz::uint128 hash;
            AttributionHandle attributions[1]; // Actually has the size of "size".
                                               // Goes past this struct.
        } group;
    };

    // A prototype is a group that has just enough information for the == and hash methods to
    // operate on. Here just the "attributions" VLA is missing.
    static AttributionGroup make_simple_proto(const Attribution& attribution)
    {
        AttributionGroup group;
        group.size = 0;
        group.simple = attribution;
        return group;
    }

    static AttributionGroup make_group_proto(int size, hrz::uint128 hash)
    {
        AttributionGroup group;
        group.size = size;
        group.group.hash = hash;
        return group;
    }

    // Those are the methods to actually allocate and initialize a full attribution group in the
    // arena.
    static AttributionGroup* alloc_simple(Arena* arena, const Attribution& attribution)
    {
        AttributionGroup* group = arena->alloc<AttributionGroup>();
        group->size = 0;
        group->simple = attribution;
        return group;
    }

    static AttributionGroup* alloc_group(
        Arena* arena,
        std::span<const AttributionHandle> handles,
        hrz::uint128 hash)
    {
        assert(handles.size() > 0);

        // Allocate some extra space for the attributions VLA at the end of the structure.
        // We remove 1 because 1 is already allocated in the struct.
        // Maybe this could be optimized because there might be padding at the end. But I don't
        // care.
        size_t alloc_size =
            sizeof(AttributionGroup) + sizeof(AttributionHandle) * (handles.size() - 1);

        void* alloc_raw = arena->alloc_raw(alloc_size, alignof(AttributionGroup));
        AttributionGroup* group = new (alloc_raw) AttributionGroup;
        group->size = (int)handles.size();
        group->group.hash = hash;
        memcpy(&group->group.attributions[0], handles.data(), handles.size_bytes());

        return group;
    }
};

// It is very important that we don't access the attributions field in the methods below (hash and
// eq) because when we're searching for interned attribution groups, we'll construct an
// AttributionGroup that has correct field except for "attributions"!

template<typename H>
H AbslHashValue(H h, const AttributionGroup* a)
{
    if (a->size == 0)
    {
        return H::combine(
            std::move(h), a->size, std::bit_cast<uintptr_t>(a->simple.title.data()),
            std::bit_cast<uintptr_t>(a->simple.logo.data()));
    }
    else
    {
        return H::combine(std::move(h), a->size, a->group.hash);
    }
}

struct AttributionGroupHash
{
    inline size_t operator()(const AttributionGroup* a) const
    {
        return absl::Hash<const AttributionGroup*>{}(a);
    }
};

struct AttributionGroupEq
{
    constexpr bool operator()(const AttributionGroup* a, const AttributionGroup* b) const
    {
        if (a->size == 0 && b->size == 0)
        {
            return a->simple.title.data() == b->simple.title.data()
                && a->simple.logo.data() == b->simple.logo.data();
        }
        else if (a->size > 0 && b->size > 0)
        {
            return a->size == b->size && a->group.hash == b->group.hash;
        }
        else
        {
            return false;
        }
    }
};

struct AttributionRegistry
{
    hrz::InternString interner;
    hrz::Arena arena;
    hrz::flat_hash_set<AttributionGroup*, AttributionGroupHash, AttributionGroupEq>
        attribution_groups;
    mutable std::mutex registration_mutex;

    std::vector<Attribution> used_this_frame_in_order;

    hrz::flat_hash_set<uintptr_t> used_this_frame;
    static_assert(
        std::is_same_v<uintptr_t, decltype(std::declval<AttributionHandle>().o)>,
        "The opaque handle type should be the same in the set above");

    Attribution intern_attribution(const Attribution& attrib)
    {
        Attribution intern_attrib;
        intern_attrib.title = interner.intern_view(attrib.title);
        intern_attrib.logo = interner.intern_view(attrib.logo);
        return intern_attrib;
    }
};

namespace attribution
{

AttributionRegistry* create_registry()
{
    return new AttributionRegistry();
}

void destroy(AttributionRegistry* registry)
{
    delete registry;
}

AttributionHandle register_attribution(AttributionRegistry* registry, const Attribution& attrib)
{
    if (attrib.title.empty() && attrib.logo.empty()) return {};

    HRZ_SCOPED_LOCK(registry->registration_mutex);

    auto group_proto = AttributionGroup::make_simple_proto(registry->intern_attribution(attrib));
    auto it = registry->attribution_groups.find(&group_proto);
    if (it != registry->attribution_groups.end())
    {
        return AttributionHandle{std::bit_cast<uintptr_t>(*it)};
    }
    else
    {
        AttributionGroup* attrib_to_insert =
            AttributionGroup::alloc_simple(&registry->arena, group_proto.simple);
        registry->attribution_groups.insert(attrib_to_insert);
        return AttributionHandle{std::bit_cast<uintptr_t>(attrib_to_insert)};
    }
}

AttributionHandle register_attribution_group(
    AttributionRegistry* registry,
    std::span<const AttributionHandle> source_group)
{
    HRZ_SCOPED_LOCK(registry->registration_mutex);

    // First make a copy of the handles, preserving their order, and removing 0 and duplicates.
    hrz::InlinedVector<AttributionHandle, 8> deduplicated_in_source_order;
    deduplicated_in_source_order.reserve(source_group.size());

    // std::unique only works on consecutive elements...
    for (auto handle : source_group)
    {
        if (handle.o == 0) continue;

        const bool is_duplicate = std::ranges::any_of(
            deduplicated_in_source_order,
            [handle](AttributionHandle other) { return other == handle; });
        if (!is_duplicate)
        {
            deduplicated_in_source_order.push_back(handle);
        }
    }

    if (deduplicated_in_source_order.size() == 0) return {};

    // Then sort them to compute the group hash
    hrz::InlinedVector<AttributionHandle, 8> sorted = deduplicated_in_source_order;
    std::ranges::sort(sorted);

    std::span<const AttributionHandle> sorted_span = sorted;
    hrz::uint128 hash = hrz::murmur3_x64_128(std::as_bytes(sorted_span));

    auto group_proto = AttributionGroup::make_group_proto(sorted.size(), hash);
    auto it = registry->attribution_groups.find(&group_proto);
    if (it != registry->attribution_groups.end())
    {
        return AttributionHandle{std::bit_cast<uintptr_t>(*it)};
    }
    else
    {
        AttributionGroup* attrib_to_insert =
            AttributionGroup::alloc_group(&registry->arena, deduplicated_in_source_order, hash);
        registry->attribution_groups.insert(attrib_to_insert);
        return AttributionHandle{std::bit_cast<uintptr_t>(attrib_to_insert)};
    }
}

void use_this_frame(AttributionRegistry* registry, AttributionHandle handle)
{
    if (handle.o == 0) return;

    if (registry->used_this_frame.insert(handle.o).second)
    {
        const auto* ptr = std::bit_cast<const AttributionGroup*>(handle.o);
        if (ptr->size == 0)
        {
            registry->used_this_frame_in_order.push_back(ptr->simple);
        }
        else
        {
            for (int i = 0; i < ptr->size; ++i)
            {
                use_this_frame(registry, ptr->group.attributions[i]);
            }
        }
    }
}

void use_this_frame(AttributionRegistry* registry, std::span<const AttributionHandle> handles)
{
    for (auto attrib : handles)
    {
        use_this_frame(registry, attrib);
    }
}

void reset_used_attributions(AttributionRegistry* registry)
{
    registry->used_this_frame_in_order.clear();
    registry->used_this_frame.clear();
}

std::span<const Attribution> get_frame_attributions(const AttributionRegistry* registry)
{
    return registry->used_this_frame_in_order;
}

} // namespace attribution
} // namespace hrz
