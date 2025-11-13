#include "hrz/core/vector/data_loader/impl.h"
#include "hrz/fnd/variant.h"

namespace hrz
{
VectorDataLoader::TaskRef VectorDataLoader::get_or_create_load_all_attributes_task(
    const LayerModelRef& layer_model,
    const FeatureSelection& feature_selection)
{
    uint64_t hash = hrz::index_of_variant<decltype(Task::data), Task::LoadAllAttributeValues>();
    hash = hrz::hash_mix<uint64_t>(hash, layer_model.get_handle().hash());
    hash = hrz::hash_mix<uint64_t>(hash, feature_selection.hash());

    auto it = tasks_by_hash.find(hash);
    if (it != tasks_by_hash.end() && it->second.is_valid())
    {
        const auto& task = it->second.value();
        if (task.is_load_all_attribute_values())
        {
            const auto& task_data = task.load_all_attribute_values();
            if (task_data.layer_model == layer_model
                && task_data.feature_selection == feature_selection)
            {
                return it->second;
            }
        }
    }

    auto new_task_ref = tasks.alloc();
    Task& new_task = new_task_ref.value();
    new_task.loader = this;
    new_task.status = TaskStatus::New;
    new_task.version = 0;
    new_task.data_use_count = 0;
    new_task.is_active = false;
    Task::LoadAllAttributeValues task_data;
    task_data.layer_model = layer_model;
    task_data.feature_selection = feature_selection;
    new_task.data = std::move(task_data);
    new_task.hash = hash;

    tasks_by_hash.insert({new_task.hash, new_task_ref.make_weak_ref()});

    activate_task(new_task_ref.make_weak_ref());

    return new_task_ref;
}

template<>
void VectorDataLoader::clear_task<VectorDataLoader::Task::LoadAllAttributeValues>(
    Task& task,
    Task::LoadAllAttributeValues& task_data,
    JobScheduler* js)
{
    // No-op
}

template<>
void VectorDataLoader::cancel_task_jobs<VectorDataLoader::Task::LoadAllAttributeValues>(
    Task& task,
    Task::LoadAllAttributeValues& task_data,
    JobScheduler* js)
{
    // No-op
}

template<>
void VectorDataLoader::unload_task_data<VectorDataLoader::Task::LoadAllAttributeValues>(
    Task& task,
    Task::LoadAllAttributeValues& task_data,
    bool release_dependent_task_data,
    JobScheduler* js)
{
    if (release_dependent_task_data)
    {
        for (auto& attribute_task_ref : task_data.attribute_tasks)
        {
            attribute_task_ref.release_data();
        }
    }
}

template<>
void VectorDataLoader::work_new_task<VectorDataLoader::Task::LoadAllAttributeValues>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::LoadAllAttributeValues& task_data,
    SceneModel* scene_model,
    AttributionRegistry* attributions)
{
    const auto& layer_model = task_data.layer_model.value();
    for (auto& it : layer_model.attributes)
    {
        const auto& attribute_id = it.first;
        auto sub_task = TaskDependency::between_tasks(
            get_or_create_load_attribute_values_task(
                task_data.layer_model, attribute_id, task_data.feature_selection),
            task_ref);
        task_data.attribute_tasks.push_back(std::move(sub_task));
    }

    set_task_status(task_ref, task, TaskStatus::Unloaded);
}

template<>
void VectorDataLoader::work_unloaded_task<VectorDataLoader::Task::LoadAllAttributeValues>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::LoadAllAttributeValues& task_data)
{
    for (auto& attribute_task_ref : task_data.attribute_tasks)
    {
        attribute_task_ref.retain_data();
    }
}

template<>
void VectorDataLoader::work_loading_task<VectorDataLoader::Task::LoadAllAttributeValues>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::LoadAllAttributeValues& task_data,
    JobScheduler* js,
    BlobAllocator* ba,
    ClientMessageQueue* mq,
    AttributionRegistry* attributions)
{
    bool all_attributes_loaded = true;
    std::optional<TaskStatus> error = std::nullopt;

    for (auto& attribute_task_ref : task_data.attribute_tasks)
    {
        auto& attribute_task = attribute_task_ref.get_task();

        if (is_error(attribute_task.status))
        {
            attribute_task_ref.release_data();
            error = attribute_task.status;
            break;
        }
        if (attribute_task.status != TaskStatus::Loaded)
        {
            all_attributes_loaded = false;
            break;
        }
    }

    if (error.has_value())
    {
        set_task_status(task_ref, task, error.value());

        auto iterpair = tasks_to_data_request_ids.equal_range(task_ref);
        for (auto it = iterpair.first; it != iterpair.second; ++it)
        {
            auto& request = request_ids_to_data_requests.at(it->second);
            send_data_error_message(request);
            request.task.release_data();
        }
    }
    else if (all_attributes_loaded)
    {
        hrz::InlinedVector<AttributionHandle, 16> inner_attributions;
        for (const auto& attribute_task_ref : task_data.attribute_tasks)
        {
            auto& attribute_task = attribute_task_ref.get_task();
            if (attribute_task.load_attribute_values().attribution)
            {
                inner_attributions.push_back(attribute_task.load_attribute_values().attribution);
            }
        }

        if (!inner_attributions.empty())
        {
            task_data.attribution =
                attribution::register_attribution_group(attributions, inner_attributions);
        }
        else
        {
            task_data.attribution = {};
        }

        set_task_status(task_ref, task, TaskStatus::Loaded);
        send_attribute_values_message(task_ref, task);
    }
    else
    {
        set_task_status(task_ref, task, TaskStatus::Blocked);
    }
}

template<>
bool VectorDataLoader::unload_task_data_if_not_needed<
    VectorDataLoader::Task::LoadAllAttributeValues>()
{
    return true;
}

template<>
void VectorDataLoader::check_for_invalidated_data_for_task<
    VectorDataLoader::Task::LoadAllAttributeValues>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::LoadAllAttributeValues& task_data,
    JobScheduler* js)
{
    // No-op
}
} // namespace hrz
