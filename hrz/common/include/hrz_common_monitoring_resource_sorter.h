#pragma once

#include "hrz_common_monitoring_defs.h"
#include "hrz_common_profiling.h"

#include <hrz_fnd_flat_hash_set.h>

#include <functional>
#include <span>
#include <vector>

namespace hrz::monitoring
{
template<typename Handle, typename Resource>
class ResourceSorter
{
    std::vector<Handle> handles_sorted_by_system;
    std::vector<Handle> handles_sorted_by_layer;

    hrz::flat_hash_set<Handle> newly_registered_handles;
    hrz::flat_hash_set<Handle> newly_unregistered_handles;

    bool dirty = false;

    void sort(
        const std::function<const Resource&(Handle)>& get_resource,
        const std::function<const monitoring::ResourceOwner&(const Resource&)>& get_owner,
        const std::function<bool(const Resource& a, const Resource& b)>& backup_sort)
    {
        HRZ_SCOPED_SAMPLE("monitoring resource sorter sort");

        std::sort(
            handles_sorted_by_system.begin(), handles_sorted_by_system.end(),
            [&](Handle handle_a, Handle handle_b)
            {
                const Resource& res_a = get_resource(handle_a);
                const ResourceOwner& owner_a = get_owner(res_a);

                const Resource& res_b = get_resource(handle_b);
                const ResourceOwner& owner_b = get_owner(res_b);

                if (owner_a.system != owner_b.system)
                {
                    return owner_a.system < owner_b.system;
                }
                if (owner_a.layer_id != owner_b.layer_id)
                {
                    return owner_a.layer_id < owner_b.layer_id;
                }

                return backup_sort(res_a, res_b);
            });

        std::sort(
            handles_sorted_by_layer.begin(), handles_sorted_by_layer.end(),
            [&](Handle handle_a, Handle handle_b)
            {
                const Resource& res_a = get_resource(handle_a);
                const ResourceOwner& owner_a = get_owner(res_a);

                const Resource& res_b = get_resource(handle_b);
                const ResourceOwner& owner_b = get_owner(res_b);

                if (owner_a.layer_id != owner_b.layer_id)
                {
                    return owner_a.layer_id < owner_b.layer_id;
                }
                if (owner_a.system != owner_b.system)
                {
                    return owner_a.system < owner_b.system;
                }

                return backup_sort(res_a, res_b);
            });
    }

public:
    void register_handle(Handle handle)
    {
        newly_registered_handles.insert(handle);
        newly_unregistered_handles.erase(handle);
        dirty = true;
    }

    void unregister_handle(Handle handle)
    {
        newly_unregistered_handles.insert(handle);
        newly_registered_handles.erase(handle);
        dirty = true;
    }

    void schedule_sort() { dirty = true; }

    std::span<const Handle> get_handles_sorted_by_system() { return handles_sorted_by_system; }

    std::span<const Handle> get_handles_sorted_by_layer() { return handles_sorted_by_layer; }

    size_t size() const { return handles_sorted_by_layer.size(); }

    void work(
        const std::function<const Resource&(Handle)>& get_resource,
        const std::function<const monitoring::ResourceOwner&(const Resource&)>& get_owner,
        const std::function<bool(const Resource& a, const Resource& b)>& backup_sort)
    {
        HRZ_SCOPED_SAMPLE("monitoring resource sorter work");

        if (!dirty) return;

        auto remove_unregistered_handles = [&](std::vector<Handle>& handles)
        {
            handles.erase(
                std::remove_if(
                    handles.begin(), handles.end(),
                    [&](uint64_t handle) { return newly_unregistered_handles.count(handle) > 0; }),
                handles.end());
        };

        remove_unregistered_handles(handles_sorted_by_system);
        remove_unregistered_handles(handles_sorted_by_layer);

        for (auto handle : newly_registered_handles)
        {
            handles_sorted_by_system.push_back(handle);
            handles_sorted_by_layer.push_back(handle);
        }

        newly_unregistered_handles.clear();
        newly_registered_handles.clear();

        assert(handles_sorted_by_layer.size() == handles_sorted_by_system.size());

        sort(get_resource, get_owner, backup_sort);
        dirty = false;
    }
};
} // namespace hrz::monitoring
