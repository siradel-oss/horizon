#include "vector/data_loader/hrz_core_vector_data_loader_impl.h"

#include <hrz_fnd_variant.h>

namespace hrz
{
VectorDataLoader::TaskRef VectorDataLoader::get_or_create_load_feature_ids_task(
    const LayerModelRef& layer_model,
    const FeatureSelection& feature_selection)
{
    uint64_t hash =
        hrz::hash_value(hrz::index_of_variant<decltype(Task::data), Task::LoadFeatureIds>());
    hash = hrz::hash_mix<uint64_t>(hash, layer_model.get_handle().hash());
    hash = hrz::hash_mix<uint64_t>(hash, feature_selection.hash());

    auto it = tasks_by_hash.find(hash);
    if (it != tasks_by_hash.end() && it->second.is_valid())
    {
        const auto& task = it->second.value();
        if (task.is_load_feature_ids())
        {
            const auto& task_data = task.load_feature_ids();
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
    Task::LoadFeatureIds task_data;
    task_data.layer_model = layer_model;
    task_data.feature_selection = feature_selection;
    task_data.load_vector_data_task = {};
    task_data.feature_ids = FeatureIdListRef{};
    new_task.data = std::move(task_data);
    new_task.hash = hash;

    tasks_by_hash.insert({new_task.hash, new_task_ref.make_weak_ref()});

    activate_task(new_task_ref.make_weak_ref());

    return new_task_ref;
}

template<>
void VectorDataLoader::clear_task<VectorDataLoader::Task::LoadFeatureIds>(
    Task& task,
    Task::LoadFeatureIds& task_data,
    JobScheduler* js)
{
    // No-op
}

template<>
void VectorDataLoader::cancel_task_jobs<VectorDataLoader::Task::LoadFeatureIds>(
    Task& task,
    Task::LoadFeatureIds& task_data,
    JobScheduler* js)
{
    // No-op
}

template<>
void VectorDataLoader::unload_task_data<VectorDataLoader::Task::LoadFeatureIds>(
    Task& task,
    Task::LoadFeatureIds& task_data,
    bool release_dependent_task_data,
    JobScheduler* js)
{
    task_data.feature_ids.release();
    if (release_dependent_task_data)
    {
        task_data.load_vector_data_task.release_data();
    }
}

template<>
void VectorDataLoader::work_new_task<VectorDataLoader::Task::LoadFeatureIds>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::LoadFeatureIds& task_data,
    SceneModel* scene_model,
    AttributionRegistry* attributions)
{
    if (task_data.feature_selection.has_feature_ids())
    {
        task_data.feature_ids = task_data.feature_selection.feature_ids();
        set_task_status(task_ref, task, TaskStatus::Loaded);
    }
    else if (task_data.feature_selection.has_tile_coords())
    {
        // Whether or not the primary source has feature IDs, we load its
        // vector data. If it has feature IDs, they will be returned.
        // If not, it must have geometries, and a feature ID list with the
        // correct number of feature will be returned. (Though not containing
        // any actual feature ID.)
        task_data.load_vector_data_task = TaskDependency::between_tasks(
            get_or_create_load_vector_data_task(
                task_data.layer_model, LayerModel::PRIMARY_SOURCE, task_data.feature_selection),
            task_ref);
        set_task_status(task_ref, task, TaskStatus::Unloaded);
    }
    else
    {
        assert(false && "Unhandled case");
        set_task_status(task_ref, task, TaskStatus::ModelError);
    }
}

template<>
void VectorDataLoader::work_unloaded_task<VectorDataLoader::Task::LoadFeatureIds>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::LoadFeatureIds& task_data)
{
    task_data.load_vector_data_task.retain_data();
}

template<>
void VectorDataLoader::work_loading_task<VectorDataLoader::Task::LoadFeatureIds>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::LoadFeatureIds& task_data,
    JobScheduler* js,
    BlobAllocator* ba,
    ClientMessageQueue* mq,
    AttributionRegistry* attributions)
{
    if (task_data.feature_selection.has_feature_ids())
    {
        task_data.feature_ids = task_data.feature_selection.feature_ids();
        set_task_status(task_ref, task, TaskStatus::Loaded);
    }
    else if (task_data.load_vector_data_task.has_task())
    {
        auto& load_vector_data_task = task_data.load_vector_data_task.get_task();
        if (load_vector_data_task.status == TaskStatus::Loaded)
        {
            task_data.feature_ids = load_vector_data_task.load_vector_data().feature_ids;
            task_data.load_vector_data_task.release_data();
            set_task_status(task_ref, task, TaskStatus::Loaded);
            send_feature_ids_message(task_ref, task);
        }
        else if (is_error(load_vector_data_task.status))
        {
            task_data.load_vector_data_task.release_data();
            set_task_status(task_ref, task, load_vector_data_task.status);

            auto iterpair = tasks_to_data_request_ids.equal_range(task_ref);
            for (auto it = iterpair.first; it != iterpair.second; ++it)
            {
                auto& request = request_ids_to_data_requests.at(it->second);
                send_data_error_message(request);
            }
        }
        else
        {
            set_task_status(task_ref, task, TaskStatus::Blocked);
        }
    }
}

template<>
bool VectorDataLoader::unload_task_data_if_not_needed<VectorDataLoader::Task::LoadFeatureIds>()
{
    return true;
}

template<>
void VectorDataLoader::check_for_invalidated_data_for_task<VectorDataLoader::Task::LoadFeatureIds>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::LoadFeatureIds& task_data,
    JobScheduler* js)
{
    // No-op
}
} // namespace hrz
