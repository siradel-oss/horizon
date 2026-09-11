// SPDX-FileCopyrightText: Copyright 2025 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/core/vector/data_loader/impl.h"
#include "hrz/fnd/variant.h"
#include "hrz/protocol/path_builder/layer/vector_data_layer.h"

namespace hrz
{

VectorDataLoader::TaskRef VectorDataLoader::get_or_create_load_layer_model_task(uint32_t layer_id)
{
    uint64_t hash =
        hrz::hash_value(hrz::index_of_variant<decltype(Task::data), Task::LoadLayerModel>());
    hash = hrz::hash_mix<uint64_t>(hash, hrz::hash_value(layer_id));

    auto it = tasks_by_hash.find(hash);
    if (it != tasks_by_hash.end() && it->second.is_valid())
    {
        const auto& task = it->second.value();
        if (task.is_load_layer_model())
        {
            const auto& task_data = task.load_layer_model();
            if (task_data.layer_id == layer_id)
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
    Task::LoadLayerModel task_data;
    task_data.layer_id = layer_id;
    new_task.data = std::move(task_data);
    new_task.hash = hash;

    tasks_by_hash.insert({new_task.hash, new_task_ref.make_weak_ref()});

    activate_task(new_task_ref.make_weak_ref());

    return new_task_ref;
}

template<>
void VectorDataLoader::clear_task<VectorDataLoader::Task::LoadLayerModel>(
    Task& task,
    Task::LoadLayerModel& task_data,
    JobScheduler* js)
{
    // No-op
}

template<>
void VectorDataLoader::cancel_task_jobs<VectorDataLoader::Task::LoadLayerModel>(
    Task& task,
    Task::LoadLayerModel& task_data,
    JobScheduler* js)
{
    // No-op
}

template<>
void VectorDataLoader::unload_task_data<VectorDataLoader::Task::LoadLayerModel>(
    Task& task,
    Task::LoadLayerModel& task_data,
    bool release_dependent_task_data,
    JobScheduler* js)
{
    task_data.layer_model.release();

    if (release_dependent_task_data)
    {
        for (auto& task : task_data.load_source_model_tasks)
        {
            task.release_data();
        }
    }
}

template<>
void VectorDataLoader::work_new_task<VectorDataLoader::Task::LoadLayerModel>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::LoadLayerModel& task_data,
    SceneModel* scene_model,
    AttributionRegistry* attributions)
{
    auto layer_handle_it = layer_ids_to_active_layer_handles.find(task_data.layer_id);
    if (layer_handle_it != layer_ids_to_active_layer_handles.end())
    {
        uint64_t layer_handle = layer_handle_it->second;

        hrz_proto::LayerHandle handle;
        handle.set_opaque(layer_handle);

        auto source_model =
            hrz_proto::VectorDataLayerPathBuilder<hrz::SceneModelAccessor>(scene_model, handle)
                .get();

        task_data.layer_model = layer_models.alloc();
        auto& layer_model = task_data.layer_model.value();
        layer_model = make_model(attributions, source_model, layer_handle);

        for (uint32_t s = 0; s < layer_model.data_sources.size(); ++s)
        {
            task_data.load_source_model_tasks.push_back(
                TaskDependency::between_tasks(
                    get_or_create_load_source_model_task(task_data.layer_model, s), task_ref));
        }

        set_task_status(task_ref, task, TaskStatus::Unloaded);
    }
    else
    {
        set_task_status(task_ref, task, TaskStatus::ModelError);

        auto iterpair = layer_ids_to_request_ids.equal_range(task_data.layer_id);
        for (auto it = iterpair.first; it != iterpair.second; ++it)
        {
            auto request_id = it->second;
            auto loader_it = request_ids_to_layer_loaders.find(request_id);
            if (loader_it != request_ids_to_layer_loaders.end())
            {
                auto& loader = loader_it->second;
                loader.has_full_model_update = true;
                send_layer_model_error_message(loader);
            }
        }
    }
}

template<>
void VectorDataLoader::work_unloaded_task<VectorDataLoader::Task::LoadLayerModel>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::LoadLayerModel& task_data)
{
    for (auto& task : task_data.load_source_model_tasks)
    {
        task.retain_data();
    }
}

template<>
void VectorDataLoader::work_loading_task<VectorDataLoader::Task::LoadLayerModel>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::LoadLayerModel& task_data,
    JobScheduler* js,
    BlobAllocator* ba,
    ClientMessageQueue* mq,
    AttributionRegistry* attributions)
{
    bool all_loaded = true;
    for (auto& task : task_data.load_source_model_tasks)
    {
        if (task.get_task().status != TaskStatus::Loaded)
        {
            all_loaded = false;
            break;
        }
    }

    if (all_loaded)
    {
        auto& layer_model = task_data.layer_model.value();

        auto iterpair = layer_ids_to_request_ids.equal_range(layer_model.id);
        for (auto it = iterpair.first; it != iterpair.second; ++it)
        {
            auto request_id = it->second;
            auto loader_it = request_ids_to_layer_loaders.find(request_id);
            if (loader_it != request_ids_to_layer_loaders.end())
            {
                auto& loader = loader_it->second;
                loader.has_full_model_update = true;
                send_layer_model_message(loader, layer_model);
            }
        }

        set_task_status(task_ref, task, TaskStatus::Loaded);
    }
    else
    {
        set_task_status(task_ref, task, TaskStatus::Blocked);
    }
}

template<>
bool VectorDataLoader::unload_task_data_if_not_needed<VectorDataLoader::Task::LoadLayerModel>()
{
    return true;
}

template<>
void VectorDataLoader::check_for_invalidated_data_for_task<VectorDataLoader::Task::LoadLayerModel>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::LoadLayerModel& task_data,
    JobScheduler* js)
{
    // No-op
}

} // namespace hrz
