#include "hrz/core/selection.h"

#include "hrz/fnd/flat_hash_map.h"
#include "hrz/fnd/flat_hash_set.h"

namespace
{

struct FullObjectId
{
    uint64_t layer_id;
    uint64_t object_id;

    constexpr bool operator ==(const FullObjectId& id) const = default;

    template<typename H>
    friend H AbslHashValue(H h, const FullObjectId& id)
    {
        return H::combine(std::move(h), id.layer_id, id.object_id);
    }
};

} // namespace

namespace hrz
{

struct SelectionSystem
{
    bool has_changed_global = false;
    hrz::flat_hash_set<uint64_t> layers_changed;

    hrz::flat_hash_set<FullObjectId> selected;
    hrz::flat_hash_map<uint64_t, uint64_t> selected_count_per_layer;
};

namespace selection
{

SelectionSystem* create()
{
    return new SelectionSystem();
}

void destroy(SelectionSystem* sys)
{
    delete sys;
}

void select(SelectionSystem* sys, uint64_t layer_id, uint64_t object_id)
{
    assert(sys);
    if (sys->selected.insert({layer_id, object_id}).second)
    {
        sys->has_changed_global = true;
        sys->layers_changed.insert(layer_id);
        sys->selected_count_per_layer[layer_id] += 1;
    }
}

void deselect(SelectionSystem* sys, uint64_t layer_id, uint64_t object_id)
{
    assert(sys);
    auto it = sys->selected.find({layer_id, object_id});
    if (it != sys->selected.end())
    {
        sys->selected.erase(it);
        sys->has_changed_global = true;
        sys->layers_changed.insert(layer_id);

        auto it = sys->selected_count_per_layer.find(layer_id);
        assert(it != sys->selected_count_per_layer.end());
        assert(it->second > 0);

        it->second -= 1;
        if (it->second == 0)
        {
            sys->selected_count_per_layer.erase(it);
        }
    }
}

void deselect_all(SelectionSystem* sys)
{
    assert(sys);
    if (!sys->selected.empty())
    {
        sys->selected.clear();
        sys->has_changed_global = true;

        for (const auto& it : sys->selected_count_per_layer)
        {
            sys->layers_changed.insert(it.first);
        }
        sys->selected_count_per_layer.clear();
    }
}

bool is_selected(const SelectionSystem* sys, uint64_t layer_id, uint64_t object_id)
{
    assert(sys);
    return sys->selected.count({layer_id, object_id}) != 0;
}

void finish_frame(SelectionSystem* sys)
{
    assert(sys);
    sys->has_changed_global = false;
    sys->layers_changed.clear();
}

bool has_changed_since_last_frame(const SelectionSystem* sys)
{
    assert(sys);
    return sys->has_changed_global;
}

bool has_changed_since_last_frame(const SelectionSystem* sys, uint64_t layer_id)
{
    assert(sys);
    return sys->layers_changed.count(layer_id) != 0;
}

size_t selected_objects_count(const SelectionSystem* sys, uint64_t layer_id)
{
    assert(sys);
    auto it = sys->selected_count_per_layer.find(layer_id);
    if (it != sys->selected_count_per_layer.end())
    {
        assert(it->second > 0);
        return (size_t)it->second;
    }
    else
    {
        return 0;
    }
}

size_t selected_objects_count(const SelectionSystem* sys)
{
    assert(sys);
    return sys->selected.size();
}

size_t get_selected_objects(
    const SelectionSystem* sys,
    uint64_t layer_id,
    std::span<uint64_t> out_object_ids)
{
    assert(sys);
    auto it = sys->selected_count_per_layer.find(layer_id);
    if (it != sys->selected_count_per_layer.end())
    {
        assert(it->second <= out_object_ids.size());
        size_t cursor = 0;
        for (const auto& full_id : sys->selected)
        {
            if (full_id.layer_id == layer_id)
            {
                out_object_ids[cursor++] = full_id.object_id;
            }
        }

        return cursor;
    }
    else
    {
        return 0;
    }
}

} // namespace selection
} // namespace hrz
