// SPDX-FileCopyrightText: Copyright 2025 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/core/vector/data_loader/impl.h"
#include "hrz/fnd/variant.h"

namespace hrz
{

VectorDataLoader::TaskRef VectorDataLoader::get_or_create_load_vector_data_task(
    const LayerModelRef& layer_model,
    uint32_t data_source,
    const FeatureSelection& feature_selection)
{
    uint64_t hash = hrz::index_of_variant<decltype(Task::data), Task::LoadVectorData>();
    hash = hrz::hash_mix<uint64_t>(hash, hrz::hash_value(layer_model.get_handle()));
    hash = hrz::hash_mix<uint64_t>(hash, hrz::hash_value(data_source));
    hash = hrz::hash_mix<uint64_t>(hash, feature_selection.hash());

    auto it = tasks_by_hash.find(hash);
    if (it != tasks_by_hash.end() && it->second.is_valid())
    {
        const auto& task = it->second.value();
        if (task.is_load_vector_data())
        {
            const auto& task_data = task.load_vector_data();
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
    Task::LoadVectorData task_data;
    task_data.layer_model = layer_model;
    task_data.data_source = data_source;
    task_data.feature_selection = feature_selection;
    task_data.load_vector_tile_data_task = {};
    task_data.load_in_memory_vector_data_task = {};
    task_data.request_client_data_task = {};
    task_data.extract_vector_tile_data_task = {};
    task_data.load_feature_ids_task = {};
    task_data.reference_feature_ids = FeatureIdListRef{};
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
void VectorDataLoader::clear_task<VectorDataLoader::Task::LoadVectorData>(
    Task& task,
    Task::LoadVectorData& task_data,
    JobScheduler* js)
{
    // No-op
}

template<>
void VectorDataLoader::cancel_task_jobs<VectorDataLoader::Task::LoadVectorData>(
    Task& task,
    Task::LoadVectorData& task_data,
    JobScheduler* js)
{
    hrz_jobs::cancel_job(js, task_data.join_ticket);
}

template<>
void VectorDataLoader::unload_task_data<VectorDataLoader::Task::LoadVectorData>(
    Task& task,
    Task::LoadVectorData& task_data,
    bool release_dependent_task_data,
    JobScheduler* js)
{
    if (hrz_jobs::is_job_valid(js, task_data.join_ticket))
    {
        hrz_jobs::cancel_job(js, task_data.join_ticket);
    }

    task_data.feature_ids.release();
    task_data.geometry.release();

    for (auto& it : task_data.attribute_ids_to_values)
    {
        it.second.release();
    }
    task_data.attribute_ids_to_values.clear();

    if (release_dependent_task_data)
    {
        task_data.load_vector_tile_data_task.release_data();
        task_data.load_in_memory_vector_data_task.release_data();
        task_data.request_client_data_task.release_data();
        task_data.extract_vector_tile_data_task.release_data();
        task_data.load_feature_ids_task.release_data();

        if (!task_data.feature_selection.has_feature_ids())
        {
            task_data.reference_feature_ids.release();
        }
    }
}

template<>
void VectorDataLoader::work_new_task<VectorDataLoader::Task::LoadVectorData>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::LoadVectorData& task_data,
    SceneModel* scene_model,
    AttributionRegistry* attributions)
{
    const auto& layer_model = task_data.layer_model.value();
    const auto& data_source = layer_model.data_sources.at(task_data.data_source);

    auto load_feature_ids_if_needed = [&]()
    {
        // If the data needs to be joined, we need reference feature IDs
        // for either joining by feature ID or just checking if there are
        // enough values. (Even when no feature ID attributes are defined,
        // reference feature IDs at least contain the number of features.)
        if (data_source.join_type != LayerModel::JoinType::None)
        {
            if (task_data.feature_selection.has_feature_ids())
            {
                assert(task_data.feature_selection.feature_ids().has_value());
                task_data.reference_feature_ids = task_data.feature_selection.feature_ids();
                assert(task_data.reference_feature_ids.has_value());
            }
            else
            {
                task_data.load_feature_ids_task = TaskDependency::between_tasks(
                    get_or_create_load_feature_ids_task(
                        task_data.layer_model, task_data.feature_selection),
                    task_ref);
            }
        }
    };

    if (data_source.has_tiled_data_provider() || data_source.has_tilejson_data_provider()
        || data_source.has_pmtiles_data_provider())
    {
        if (task_data.feature_selection.has_tile_coords())
        {
            task_data.load_vector_tile_data_task = TaskDependency::between_tasks(
                get_or_create_load_vector_tile_data_task(
                    task_data.layer_model, task_data.data_source, task_data.feature_selection),
                task_ref);

            load_feature_ids_if_needed();

            set_task_status(task_ref, task, TaskStatus::Unloaded);
        }
        else
        {
            HRZ_LOG_ERROR("No tile coords provided for tiled data access.");
            set_task_status(task_ref, task, TaskStatus::ModelError);
        }
    }
    else if (data_source.has_in_memory_data_provider())
    {
        task_data.load_in_memory_vector_data_task = TaskDependency::between_tasks(
            get_or_create_load_in_memory_vector_data_task(
                task_data.layer_model, task_data.data_source, task_data.feature_selection),
            task_ref);

        load_feature_ids_if_needed();

        set_task_status(task_ref, task, TaskStatus::Unloaded);
    }
    else if (data_source.has_client_data_provider())
    {
        task_data.request_client_data_task = TaskDependency::between_tasks(
            get_or_create_request_client_data_task(
                task_data.layer_model, task_data.data_source, task_data.feature_selection),
            task_ref);

        load_feature_ids_if_needed();

        set_task_status(task_ref, task, TaskStatus::Unloaded);
    }
    else if (data_source.has_untiled_data_provider())
    {
        if (task_data.feature_selection.has_tile_coords())
        {
            task_data.extract_vector_tile_data_task = TaskDependency::between_tasks(
                get_or_create_extract_vector_tile_data_task(
                    task_data.layer_model, task_data.data_source,
                    task_data.feature_selection.tile_coords()),
                task_ref);

            load_feature_ids_if_needed();

            set_task_status(task_ref, task, TaskStatus::Unloaded);
        }
        else
        {
            HRZ_LOG_ERROR("No tile coords provided for untiled data access.");
            set_task_status(task_ref, task, TaskStatus::ModelError);
        }
    }
    else
    {
        assert(false && "Unhandled case");
        set_task_status(task_ref, task, TaskStatus::ModelError);
    }
}

template<>
void VectorDataLoader::work_unloaded_task<VectorDataLoader::Task::LoadVectorData>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::LoadVectorData& task_data)
{
    task_data.load_vector_tile_data_task.retain_data();
    task_data.load_in_memory_vector_data_task.retain_data();
    task_data.request_client_data_task.retain_data();
    task_data.extract_vector_tile_data_task.retain_data();
    task_data.load_feature_ids_task.retain_data();
}

template<>
void VectorDataLoader::work_loading_task<VectorDataLoader::Task::LoadVectorData>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::LoadVectorData& task_data,
    JobScheduler* js,
    BlobAllocator* ba,
    ClientMessageQueue* mq,
    AttributionRegistry* attributions)
{
    const auto& layer_model = task_data.layer_model.value();
    const auto& data_source = layer_model.data_sources.at(task_data.data_source);

    // If the data comes from the primary source, then its values are in
    // the correct order, and the data can be considered to be loaded.
    // Otherwise, the data must be joined to match the feature order or
    // count of the primary source (i.e. the first source in the list).
    auto check_for_joined_data = [&]()
    {
        if (data_source.join_type == LayerModel::JoinType::None)
        {
            set_task_status(task_ref, task, TaskStatus::Loaded);
        }
        else if (task_data.reference_feature_ids.has_value())
        {
            hrz_jobs::UnjoinedVectorData params;
            params.join_by_feature_ids =
                data_source.join_type == LayerModel::JoinType::SortByFeatureIds;
            params.reference_feature_ids = task_data.reference_feature_ids.value();
            params.feature_ids = task_data.feature_ids.value();
            if (task_data.geometry.has_value())
            {
                params.geometry = {task_data.geometry.value()};
            }
            for (const auto& it : task_data.attribute_ids_to_values)
            {
                params.attributes.push_back(it.second.value());
            }

            task_data.join_ticket = hrz_jobs::add_job_join_vector_data(
                js, std::move(params),
                {monitoring::systems::VectorDataLoader, layer_model.layer_handle});
        }
        else
        {
            assert(task_data.load_feature_ids_task.has_task());
        }
    };

    auto copy_geometry_if_needed = [&](const TileGeometryRef& geometry)
    {
        if (layer_model.geometry_source == task_data.data_source)
        {
            task_data.geometry = geometry;
        }
    };

    auto copy_relevant_attributes =
        [&](const hrz::flat_hash_map<uint32_t, AttributeValueListRef>& attribute_ids_to_values)
    {
        for (const auto& it : attribute_ids_to_values)
        {
            if (data_source.attributes.find(it.first) != data_source.attributes.end())
            {
                task_data.attribute_ids_to_values.insert_or_assign(it.first, it.second);
            }
        }
    };

    if (hrz_jobs::is_job_valid(js, task_data.join_ticket))
    {
        if (hrz_jobs::is_job_finished(js, task_data.join_ticket))
        {
            if (hrz_jobs::get_job_status(js, task_data.join_ticket)
                == hrz::job_scheduler::JobStatus::Finished_Success)
            {
                auto data = hrz_jobs::get_job_response(js, task_data.join_ticket);
                size_t feature_count = task_data.reference_feature_ids->size();

                task_data.feature_ids = task_data.reference_feature_ids;

                task_data.geometry = tile_geometries.alloc();
                if (data.geometry.has_value())
                {
                    task_data.geometry.value() = std::move(data.geometry.value());
                }

                load_attribute_data_into_map(
                    data.attributes, task_data.attribute_ids_to_values, data_source, feature_count);

                set_task_status(task_ref, task, TaskStatus::Loaded);
            }
            else
            {
                HRZ_LOG_ERROR(
                    "Could not join data of tile of vector data layer {}", layer_model.id);
                hrz_jobs::cancel_job(js, task_data.join_ticket);
                set_task_status(task_ref, task, TaskStatus::DataError);
            }
        }
    }
    else if (
        task_data.load_feature_ids_task.has_task() && !task_data.reference_feature_ids.has_value())
    {
        auto& load_feature_ids_task = task_data.load_feature_ids_task.get_task();
        if (load_feature_ids_task.status == TaskStatus::Loaded)
        {
            task_data.reference_feature_ids = load_feature_ids_task.load_feature_ids().feature_ids;

            if (task_data.feature_ids.has_value())
            {
                // If this condition is true, it means that the data
                // has been loaded, and it can be joined.
                // (But most of the time reference feature IDs are
                // loaded first, before the actual data has been
                // loaded. In this case other calls to this function
                // trigger the joining.)
                check_for_joined_data();
            }
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
    else if (task_data.load_vector_tile_data_task.has_task())
    {
        auto& load_vector_tile_data_task = task_data.load_vector_tile_data_task.get_task();
        if (load_vector_tile_data_task.status == TaskStatus::Loaded)
        {
            const auto& data = load_vector_tile_data_task.load_vector_tile_data();
            task_data.feature_ids = data.feature_ids;
            copy_geometry_if_needed(data.geometry);
            task_data.attribution = data.attribution;
            copy_relevant_attributes(data.attribute_ids_to_values);

            task_data.load_vector_tile_data_task.release_data();

            check_for_joined_data();
        }
        else if (is_error(load_vector_tile_data_task.status))
        {
            task_data.load_vector_tile_data_task.release_data();
            set_task_status(task_ref, task, load_vector_tile_data_task.status);
        }
        else
        {
            set_task_status(task_ref, task, TaskStatus::Blocked);
        }
    }
    else if (task_data.load_in_memory_vector_data_task.has_task())
    {
        auto& load_in_memory_vector_data_task =
            task_data.load_in_memory_vector_data_task.get_task();
        if (load_in_memory_vector_data_task.status == TaskStatus::Loaded)
        {
            const auto& data = load_in_memory_vector_data_task.load_in_memory_vector_data();
            task_data.feature_ids = data.feature_ids;
            copy_geometry_if_needed(data.geometry);
            task_data.attribution = data.attribution;
            copy_relevant_attributes(data.attribute_ids_to_values);

            task_data.load_in_memory_vector_data_task.release_data();

            check_for_joined_data();
        }
        else if (is_error(load_in_memory_vector_data_task.status))
        {
            task_data.load_in_memory_vector_data_task.release_data();
            set_task_status(task_ref, task, load_in_memory_vector_data_task.status);
        }
        else
        {
            set_task_status(task_ref, task, TaskStatus::Blocked);
        }
    }
    else if (task_data.request_client_data_task.has_task())
    {
        auto& request_client_data_task = task_data.request_client_data_task.get_task();
        if (request_client_data_task.status == TaskStatus::Loaded)
        {
            const auto& data = request_client_data_task.request_client_data();
            task_data.feature_ids = data.feature_ids;
            copy_geometry_if_needed(data.geometry);
            task_data.attribution = data.attribution;
            copy_relevant_attributes(data.attribute_ids_to_values);

            task_data.request_client_data_task.release_data();

            check_for_joined_data();
        }
        else if (is_error(request_client_data_task.status))
        {
            task_data.request_client_data_task.release_data();
            set_task_status(task_ref, task, request_client_data_task.status);
        }
        else
        {
            set_task_status(task_ref, task, TaskStatus::Blocked);
        }
    }
    else if (task_data.extract_vector_tile_data_task.has_task())
    {
        auto& extract_vector_tile_data_task = task_data.extract_vector_tile_data_task.get_task();
        if (extract_vector_tile_data_task.status == TaskStatus::Loaded)
        {
            const auto& data = extract_vector_tile_data_task.extract_vector_tile_data();
            task_data.feature_ids = data.feature_ids;
            copy_geometry_if_needed(data.geometry);
            task_data.attribution = data.attribution;
            copy_relevant_attributes(data.attribute_ids_to_values);

            task_data.load_in_memory_vector_data_task.release_data();

            check_for_joined_data();
        }
        else if (is_error(extract_vector_tile_data_task.status))
        {
            task_data.extract_vector_tile_data_task.release_data();
            set_task_status(task_ref, task, extract_vector_tile_data_task.status);
        }
        else
        {
            set_task_status(task_ref, task, TaskStatus::Blocked);
        }
    }
}

template<>
bool VectorDataLoader::unload_task_data_if_not_needed<VectorDataLoader::Task::LoadVectorData>()
{
    return true;
}

template<>
void VectorDataLoader::check_for_invalidated_data_for_task<VectorDataLoader::Task::LoadVectorData>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::LoadVectorData& task_data,
    JobScheduler* js)
{
    // No-op
}

} // namespace hrz
