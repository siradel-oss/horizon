#include "vector/data_loader/hrz_core_vector_data_loader_impl.h"

#include <hrz_fnd_variant.h>

namespace hrz
{
VectorDataLoader::TaskRef VectorDataLoader::get_or_create_load_in_memory_vector_data_task(
    const LayerModelRef& layer_model,
    uint32_t data_source,
    const FeatureSelection& feature_selection)
{
    uint64_t hash = hrz::hash_value(
        hrz::index_of_variant<decltype(Task::data), Task::LoadInMemoryVectorData>());
    hash = hrz::hash_mix<uint64_t>(hash, layer_model.get_handle().hash());
    hash = hrz::hash_mix<uint64_t>(hash, hrz::hash_value(data_source));
    hash = hrz::hash_mix<uint64_t>(hash, feature_selection.hash());

    auto it = tasks_by_hash.find(hash);
    if (it != tasks_by_hash.end() && it->second.is_valid())
    {
        const auto& task = it->second.value();
        if (task.is_load_in_memory_vector_data())
        {
            const auto& task_data = task.load_in_memory_vector_data();
            if (task_data.layer_model == layer_model && task_data.data_source == data_source
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
    Task::LoadInMemoryVectorData task_data;
    task_data.layer_model = layer_model;
    task_data.data_source = data_source;
    task_data.feature_selection = feature_selection;
    task_data.load_feature_ids_task = {};
    task_data.load_feature_ids_task_version = std::nullopt;
    task_data.in_memory_vector_data_request_id = std::nullopt;
    task_data.feature_ids = FeatureIdListRef{};
    task_data.geometry = TileGeometryRef{};
    task_data.attribution = {};
    new_task.data = std::move(task_data);
    new_task.hash = hash;

    tasks_by_hash.insert({new_task.hash, new_task_ref.make_weak_ref()});

    activate_task(new_task_ref.make_weak_ref());

    return new_task_ref;
}

template<>
void VectorDataLoader::clear_task<VectorDataLoader::Task::LoadInMemoryVectorData>(
    Task& task,
    Task::LoadInMemoryVectorData& task_data,
    JobScheduler* js)
{
    if (task_data.in_memory_vector_data_request_id.has_value())
    {
        in_memory_vector_data_channel.send(in_memory::messages::ReleaseDataRequest{
            task_data.in_memory_vector_data_request_id.value()});
        tasks_waiting_for_in_memory_vector_data_message.erase(
            task_data.in_memory_vector_data_request_id.value());
    }
}

template<>
void VectorDataLoader::cancel_task_jobs<VectorDataLoader::Task::LoadInMemoryVectorData>(
    Task& task,
    Task::LoadInMemoryVectorData& task_data,
    JobScheduler* js)
{
    // No-op
}

template<>
void VectorDataLoader::unload_task_data<VectorDataLoader::Task::LoadInMemoryVectorData>(
    Task& task,
    Task::LoadInMemoryVectorData& task_data,
    bool release_dependent_task_data,
    JobScheduler* js)
{
    task_data.feature_ids.release();
    task_data.geometry.release();

    for (auto& it : task_data.attribute_ids_to_values)
    {
        it.second.release();
    }
    task_data.attribute_ids_to_values.clear();

    if (release_dependent_task_data)
    {
        task_data.load_feature_ids_task.release_data();
    }
}

template<>
void VectorDataLoader::work_new_task<VectorDataLoader::Task::LoadInMemoryVectorData>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::LoadInMemoryVectorData& task_data,
    SceneModel* scene_model,
    AttributionRegistry* attributions)
{
    if (task_data.data_source != LayerModel::PRIMARY_SOURCE
        && !task_data.feature_selection.has_feature_ids())
    {
        task_data.load_feature_ids_task = TaskDependency::between_tasks(
            get_or_create_load_feature_ids_task(task_data.layer_model, task_data.feature_selection),
            task_ref);
    }

    set_task_status(task_ref, task, TaskStatus::Unloaded);
}

template<>
void VectorDataLoader::work_unloaded_task<VectorDataLoader::Task::LoadInMemoryVectorData>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::LoadInMemoryVectorData& task_data)
{
    task_data.load_feature_ids_task.retain_data();
}

template<>
void VectorDataLoader::work_loading_task<VectorDataLoader::Task::LoadInMemoryVectorData>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::LoadInMemoryVectorData& task_data,
    JobScheduler* js,
    BlobAllocator* ba,
    ClientMessageQueue* mq,
    AttributionRegistry* attributions)
{
    const auto& layer_model = task_data.layer_model.value();
    const auto& data_source = layer_model.data_sources.at(task_data.data_source);
    const auto& provider = data_source.in_memory_data_provider();

    if (task_data.load_feature_ids_task.has_task()
        && task_data.load_feature_ids_task_version != task_data.load_feature_ids_task.get_version()
        && task_data.in_memory_vector_data_request_id.has_value())
    {
        // Recreate the in-memory vector data request if the data is requested
        // by feature IDs and the task that provides them has been updated.
        in_memory_vector_data_channel.send(in_memory::messages::ReleaseDataRequest{
            task_data.in_memory_vector_data_request_id.value()});
        tasks_waiting_for_in_memory_vector_data_message.erase(
            task_data.in_memory_vector_data_request_id.value());
        task_data.in_memory_vector_data_request_id = std::nullopt;
    }

    if (!task_data.in_memory_vector_data_request_id.has_value())
    {
        auto set_request_id = [&]()
        {
            task_data.in_memory_vector_data_request_id = next_in_memory_vector_data_request_id;
            next_in_memory_vector_data_request_id += 1;
            return task_data.in_memory_vector_data_request_id.value();
        };

        if (task_data.load_feature_ids_task.has_task())
        {
            auto& load_feature_ids_task = task_data.load_feature_ids_task.get_task();
            if (load_feature_ids_task.status == TaskStatus::Loaded)
            {
                const auto& feature_ids =
                    load_feature_ids_task.load_feature_ids().feature_ids.value();
                task_data.load_feature_ids_task_version = {load_feature_ids_task.version};
                in_memory_vector_data_channel.send(in_memory::messages::FeatureDataRequest{
                    set_request_id(), provider.in_memory_layer_id, feature_ids, true});
                tasks_waiting_for_in_memory_vector_data_message.insert(
                    {task_data.in_memory_vector_data_request_id.value(), task_ref});
                set_task_status(task_ref, task, TaskStatus::Blocked);
            }
            else if (is_error(load_feature_ids_task.status))
            {
                task_data.load_feature_ids_task.release_data();
                set_task_status(task_ref, task, load_feature_ids_task.status);
            }
            else
            {
                set_task_status(task_ref, task, TaskStatus::Blocked);
            }
        }
        else
        {
            if (task_data.feature_selection.has_tile_coords())
            {
                in_memory_vector_data_channel.send(in_memory::messages::TileDataRequest{
                    set_request_id(), provider.in_memory_layer_id,
                    task_data.feature_selection.tile_coords(), true});
                tasks_waiting_for_in_memory_vector_data_message.insert(
                    {task_data.in_memory_vector_data_request_id.value(), task_ref});
                set_task_status(task_ref, task, TaskStatus::Blocked);
            }
            else if (task_data.feature_selection.has_feature_ids())
            {
                const auto& feature_ids = task_data.feature_selection.feature_ids().value();
                in_memory_vector_data_channel.send(in_memory::messages::FeatureDataRequest{
                    set_request_id(), provider.in_memory_layer_id, feature_ids, true});
                tasks_waiting_for_in_memory_vector_data_message.insert(
                    {task_data.in_memory_vector_data_request_id.value(), task_ref});
                set_task_status(task_ref, task, TaskStatus::Blocked);
            }
            else
            {
                assert(false && "Unhandled case");
            }
        }
    }
}

template<>
bool VectorDataLoader::unload_task_data_if_not_needed<
    VectorDataLoader::Task::LoadInMemoryVectorData>()
{
    // Do not release in-memory vector data, otherwise we would have to request
    // the data from the in-memory vector data system explicitly after an update
    // instead of being able to rely on receiving the data automatically when a
    // new version comes out.
    return false;
}

template<>
void VectorDataLoader::check_for_invalidated_data_for_task<
    VectorDataLoader::Task::LoadInMemoryVectorData>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::LoadInMemoryVectorData& task_data,
    JobScheduler* js)
{
    const auto& layer_model = task_data.layer_model.value();

    for (const auto& invalidation : invalidations)
    {
        if (!invalidation.applies_to_source(layer_model.id, task_data.data_source))
        {
            continue;
        }

        if (invalidation.invalidates_everything()
            || (task_data.feature_selection.has_tile_coords()
                && invalidation.invalidates_tile(task_data.feature_selection.tile_coords())))
        {
            restart_task(task_ref, task, js);
            return;
        }
    }
}
} // namespace hrz
