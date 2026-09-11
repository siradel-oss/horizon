// SPDX-FileCopyrightText: Copyright 2025 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/core/vector/data_loader/impl.h"
#include "hrz/fnd/variant.h"

namespace hrz
{

VectorDataLoader::TaskRef VectorDataLoader::get_or_create_load_vector_data_pmtiles_package_task(
    const std::string_view& url,
    const hrz::HttpHeaders& headers,
    const TileCoords& tile_coords,
    hrz::assets_loader::Queue queue,
    uint32_t priority,
    const monitoring::ResourceOwner& resource_owner,
    const metrics::MetricDesc& request_count_metric)
{
    uint64_t hash = hrz::hash_values(
        hrz::index_of_variant<decltype(Task::data), Task::LoadVectorDataPmTilesPackage>(),
        hrz::murmur3_x64_64(url), headers.hash_content(), tile_coords);

    auto it = tasks_by_hash.find(hash);
    if (it != tasks_by_hash.end() && it->second.is_valid())
    {
        auto& task = it->second.value();

        if (task.is_load_vector_data_pmtiles_package())
        {
            auto& task_data = task.load_vector_data_pmtiles_package();
            if (task_data.url == url && task_data.headers.hash_content() == headers.hash_content()
                && task_data.tile_coords == tile_coords)
            {
                bump_task_priority(task, task_data, queue, priority);

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
    Task::LoadVectorDataPmTilesPackage task_data;
    task_data.url = url;
    task_data.tile_coords = tile_coords;
    task_data.headers = headers;
    task_data.queue = queue;
    task_data.priority = priority;
    task_data.resource_owner = resource_owner;
    task_data.request_count_metric = request_count_metric;
    task_data.load_pmtiles_task = {};
    task_data.tile_query = std::nullopt;
    task_data.package = std::nullopt;
    new_task.data = std::move(task_data);
    new_task.hash = hash;

    tasks_by_hash.insert({new_task.hash, new_task_ref.make_weak_ref()});

    activate_task(new_task_ref.make_weak_ref());

    return new_task_ref;
}

template<>
void VectorDataLoader::clear_task<VectorDataLoader::Task::LoadVectorDataPmTilesPackage>(
    Task& task,
    Task::LoadVectorDataPmTilesPackage& task_data,
    JobScheduler* js)
{
    // No-op
}

template<>
void VectorDataLoader::cancel_task_jobs<VectorDataLoader::Task::LoadVectorDataPmTilesPackage>(
    Task& task,
    Task::LoadVectorDataPmTilesPackage& task_data,
    JobScheduler* js)
{
    if (task_data.load_pmtiles_task.has_task()
        && task_data.load_pmtiles_task.get_task().status == TaskStatus::Loaded)
    {
        auto& pmtiles_task = task_data.load_pmtiles_task.get_task().load_pmtiles();

        if (pmtiles_task.pmtiles && task_data.tile_query.has_value())
        {
            pmtiles_task.pmtiles->cancel(task_data.tile_query.value(), js);
            task_data.tile_query = std::nullopt;
        }
        else
        {
            assert(!task_data.tile_query.has_value());
        }
    }
    hrz_jobs::cancel_job(js, task_data.parse_ticket);
}

template<>
void VectorDataLoader::unload_task_data<VectorDataLoader::Task::LoadVectorDataPmTilesPackage>(
    Task& task,
    Task::LoadVectorDataPmTilesPackage& task_data,
    bool release_dependent_task_data,
    JobScheduler* js)
{
    if (task_data.load_pmtiles_task.has_task()
        && task_data.load_pmtiles_task.get_task().status == TaskStatus::Loaded)
    {
        auto& pmtiles_task = task_data.load_pmtiles_task.get_task().load_pmtiles();

        if (pmtiles_task.pmtiles && task_data.tile_query.has_value())
        {
            pmtiles_task.pmtiles->cancel(task_data.tile_query.value(), js);
            task_data.tile_query = std::nullopt;
        }
        else
        {
            assert(!task_data.tile_query.has_value());
        }
    }
    hrz_jobs::cancel_job(js, task_data.parse_ticket);

    task_data.package = std::nullopt;

    if (release_dependent_task_data)
    {
        task_data.load_pmtiles_task.release_data();
    }
}

template<>
void VectorDataLoader::work_new_task<VectorDataLoader::Task::LoadVectorDataPmTilesPackage>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::LoadVectorDataPmTilesPackage& task_data,
    SceneModel* scene_model,
    AttributionRegistry* attributions)
{
    task_data.load_pmtiles_task = TaskDependency::between_tasks(
        get_or_create_load_pmtiles_task(
            task_data.url, task_data.headers, task_data.queue, task_data.priority,
            task_data.resource_owner, task_data.request_count_metric),
        task_ref);

    set_task_status(task_ref, task, TaskStatus::Unloaded);
}

template<>
void VectorDataLoader::work_unloaded_task<VectorDataLoader::Task::LoadVectorDataPmTilesPackage>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::LoadVectorDataPmTilesPackage& task_data)
{
    task_data.load_pmtiles_task.retain_data();
}

template<>
void VectorDataLoader::work_loading_task<VectorDataLoader::Task::LoadVectorDataPmTilesPackage>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::LoadVectorDataPmTilesPackage& task_data,
    JobScheduler* js,
    BlobAllocator* ba,
    ClientMessageQueue* mq,
    AttributionRegistry* attributions)
{
    if (hrz_jobs::is_job_valid(js, task_data.parse_ticket))
    {
        if (hrz_jobs::is_job_finished(js, task_data.parse_ticket))
        {
            if (hrz_jobs::get_job_status(js, task_data.parse_ticket)
                == hrz::job_scheduler::JobStatus::Finished_Success)
            {
                auto parsed_mvt = hrz_jobs::get_job_response(js, task_data.parse_ticket);

                task_data.package = {
                    {std::move(parsed_mvt)},
                    hrz_proto::VectorDataFormat::MVT_VECTOR_DATA
                };

                set_task_status(task_ref, task, TaskStatus::Loaded);
            }
            else
            {
                HRZ_LOG_ERROR("Could not parse MVT data");
                hrz_jobs::cancel_job(js, task_data.parse_ticket);
                set_task_status(task_ref, task, TaskStatus::DataError);
            }
        }
    }
    else
    {
        const auto& load_pmtiles_task = task_data.load_pmtiles_task.get_task();
        if (load_pmtiles_task.status == TaskStatus::Loaded)
        {
            assert(load_pmtiles_task.load_pmtiles().pmtiles);
            auto& pmtiles = *load_pmtiles_task.load_pmtiles().pmtiles;

            pmtiles.work(js, ba);

            if (!task_data.tile_query.has_value())
            {
                task_data.tile_query = pmtiles.request_tile(
                    task_data.tile_coords, task_data.queue, task_data.priority,
                    task_data.resource_owner);
                metrics::increment_counter(&task_data.request_count_metric);
            }
            else if (pmtiles.is_finished(task_data.tile_query.value()))
            {
                if (pmtiles.is_success(task_data.tile_query.value()))
                {
                    auto mvt_blob = pmtiles.retrieve_blob(task_data.tile_query.value());
                    task_data.parse_ticket = hrz_jobs::add_job_parse_mvt(
                        js, std::move(mvt_blob), {monitoring::systems::VectorDataLoader});
                }
                else
                {
                    set_task_status(task_ref, task, TaskStatus::DataError);
                }

                pmtiles.cancel(task_data.tile_query.value(), js);
                task_data.tile_query = std::nullopt;
            }
        }
        else if (is_error(load_pmtiles_task.status))
        {
            set_task_status(task_ref, task, load_pmtiles_task.status);
        }
        else
        {
            set_task_status(task_ref, task, TaskStatus::Blocked);
        }
    }
}

template<>
bool VectorDataLoader::
    unload_task_data_if_not_needed<VectorDataLoader::Task::LoadVectorDataPmTilesPackage>()
{
    return true;
}

template<>
void VectorDataLoader::check_for_invalidated_data_for_task<
    VectorDataLoader::Task::LoadVectorDataPmTilesPackage
>(WeakTaskRef& task_ref,
  Task& task,
  Task::LoadVectorDataPmTilesPackage& task_data,
  JobScheduler* js)
{
    // No-op
}

} // namespace hrz
