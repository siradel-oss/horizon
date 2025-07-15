#include "vector/data_loader/hrz_core_vector_data_loader_impl.h"

#include <hrz_fnd_variant.h>

namespace hrz
{
VectorDataLoader::TaskRef VectorDataLoader::get_or_create_load_attribute_values_task(
    const LayerModelRef& layer_model,
    uint32_t attribute_id,
    const FeatureSelection& feature_selection)
{
    uint64_t hash =
        hrz::hash_value(hrz::index_of_variant<decltype(Task::data), Task::LoadAttributeValues>());
    hash = hrz::hash_mix<uint64_t>(hash, layer_model.get_handle().hash());
    hash = hrz::hash_mix<uint64_t>(hash, hrz::hash_value(attribute_id));
    hash = hrz::hash_mix<uint64_t>(hash, feature_selection.hash());

    auto it = tasks_by_hash.find(hash);
    if (it != tasks_by_hash.end() && it->second.is_valid())
    {
        const auto& task = it->second.value();
        if (task.is_load_attribute_values())
        {
            const auto& task_data = task.load_attribute_values();
            if (task_data.layer_model == layer_model && task_data.attribute_id == attribute_id
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
    Task::LoadAttributeValues task_data;
    task_data.layer_model = layer_model;
    task_data.attribute_id = attribute_id;
    task_data.feature_selection = feature_selection;
    task_data.load_vector_data_task = {};
    task_data.attribute_values = AttributeValueListRef{};
    new_task.data = std::move(task_data);
    new_task.hash = hash;

    tasks_by_hash.insert({new_task.hash, new_task_ref.make_weak_ref()});

    activate_task(new_task_ref.make_weak_ref());

    return new_task_ref;
}

template<>
void VectorDataLoader::clear_task<VectorDataLoader::Task::LoadAttributeValues>(
    Task& task,
    Task::LoadAttributeValues& task_data,
    JobScheduler* js)
{
    // No-op
}

template<>
void VectorDataLoader::cancel_task_jobs<VectorDataLoader::Task::LoadAttributeValues>(
    Task& task,
    Task::LoadAttributeValues& task_data,
    JobScheduler* js)
{
    // No-op
}

template<>
void VectorDataLoader::unload_task_data<VectorDataLoader::Task::LoadAttributeValues>(
    Task& task,
    Task::LoadAttributeValues& task_data,
    bool release_dependent_task_data,
    JobScheduler* js)
{
    task_data.attribute_values.release();
    if (release_dependent_task_data)
    {
        task_data.load_vector_data_task.release_data();
    }
}

template<>
void VectorDataLoader::work_new_task<VectorDataLoader::Task::LoadAttributeValues>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::LoadAttributeValues& task_data,
    SceneModel* scene_model,
    AttributionRegistry* attributions)
{
    const auto& layer_model = task_data.layer_model.value();
    const auto& attribute_source = layer_model.attributes.at(task_data.attribute_id);

    task_data.load_vector_data_task = TaskDependency::between_tasks(
        get_or_create_load_vector_data_task(
            task_data.layer_model, attribute_source, task_data.feature_selection),
        task_ref);
    set_task_status(task_ref, task, TaskStatus::Unloaded);
}

template<>
void VectorDataLoader::work_unloaded_task<VectorDataLoader::Task::LoadAttributeValues>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::LoadAttributeValues& task_data)
{
    task_data.load_vector_data_task.retain_data();
}

template<>
void VectorDataLoader::work_loading_task<VectorDataLoader::Task::LoadAttributeValues>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::LoadAttributeValues& task_data,
    JobScheduler* js,
    BlobAllocator* ba,
    ClientMessageQueue* mq,
    AttributionRegistry* attributions)
{
    if (task_data.load_vector_data_task.has_task())
    {
        auto& load_vector_data_task = task_data.load_vector_data_task.get_task();
        if (load_vector_data_task.status == TaskStatus::Loaded)
        {
            auto it = load_vector_data_task.load_vector_data().attribute_ids_to_values.find(
                task_data.attribute_id);
            if (it != load_vector_data_task.load_vector_data().attribute_ids_to_values.end())
            {
                // Attribute values have been retrieved.
                const auto& values = it->second;
                task_data.attribute_values = values;
                task_data.attribution = load_vector_data_task.load_vector_data().attribution;
                set_task_status(task_ref, task, TaskStatus::Loaded);
            }
            else
            {
                HRZ_LOG_ERROR("Could not load values for attribute {}", task_data.attribute_id);
                set_task_status(task_ref, task, TaskStatus::DataError);
            }

            task_data.load_vector_data_task.release_data();
        }
        else if (is_error(load_vector_data_task.status))
        {
            task_data.load_vector_data_task.release_data();
            set_task_status(task_ref, task, load_vector_data_task.status);
        }
        else
        {
            set_task_status(task_ref, task, TaskStatus::Blocked);
        }
    }
}

template<>
bool VectorDataLoader::unload_task_data_if_not_needed<VectorDataLoader::Task::LoadAttributeValues>()
{
    return true;
}

template<>
void VectorDataLoader::check_for_invalidated_data_for_task<
    VectorDataLoader::Task::LoadAttributeValues>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::LoadAttributeValues& task_data,
    JobScheduler* js)
{
    // No-op
}
} // namespace hrz
