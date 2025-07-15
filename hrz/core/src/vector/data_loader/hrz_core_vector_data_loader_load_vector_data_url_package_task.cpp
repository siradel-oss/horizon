#include "vector/data_loader/hrz_core_vector_data_loader_impl.h"

#include <hrz_fnd_variant.h>

namespace hrz
{
VectorDataLoader::TaskRef VectorDataLoader::get_or_create_load_vector_data_url_package_task(
    const std::string_view& url,
    const hrz::HttpHeaders& headers,
    hrz::assets_loader::Queue queue,
    uint32_t priority,
    hrz_proto::VectorDataFormat format,
    const monitoring::ResourceOwner& resource_owner,
    const metrics::MetricDesc& request_count_metric)
{
    // Don’t check on the queue and priority. We want to reuse
    // the task even if only the URL is the same.
    // If a task is reused, the metrics and resource ownership information
    // may become incomplete, as they would only mention one data source
    // on one vector data layer, but at least the actual work isn't
    // duplicated.

    uint64_t hash = hrz::hash_value(
        hrz::index_of_variant<decltype(Task::data), Task::LoadVectorDataUrlPackage>());
    hash = hrz::hash_mix<uint64_t>(hash, hrz::murmur3_x64_64(url));
    hash = hrz::hash_mix<uint64_t>(hash, headers.hash_content());
    hash = hrz::hash_mix<uint64_t>(hash, hrz::hash_value(format));

    auto it = tasks_by_hash.find(hash);
    if (it != tasks_by_hash.end() && it->second.is_valid())
    {
        auto& task = it->second.value();

        if (task.is_load_vector_data_url_package())
        {
            auto& task_data = task.load_vector_data_url_package();
            if (task_data.url == url && task_data.headers.hash_content() == headers.hash_content()
                && task_data.format == format)
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
    Task::LoadVectorDataUrlPackage task_data;
    task_data.url = {url.data(), url.size()};
    task_data.format = format;
    task_data.headers = headers;
    task_data.queue = queue;
    task_data.priority = priority;
    task_data.resource_owner = resource_owner;
    task_data.request_count_metric = request_count_metric;
    task_data.package = std::nullopt;
    new_task.data = std::move(task_data);
    new_task.hash = hash;

    tasks_by_hash.insert({new_task.hash, new_task_ref.make_weak_ref()});

    activate_task(new_task_ref.make_weak_ref());

    return new_task_ref;
}

template<>
void VectorDataLoader::clear_task<VectorDataLoader::Task::LoadVectorDataUrlPackage>(
    Task& task,
    Task::LoadVectorDataUrlPackage& task_data,
    JobScheduler* js)
{
    // No-op
}

template<>
void VectorDataLoader::cancel_task_jobs<VectorDataLoader::Task::LoadVectorDataUrlPackage>(
    Task& task,
    Task::LoadVectorDataUrlPackage& task_data,
    JobScheduler* js)
{
    hrz_jobs::cancel_job(js, task_data.parse_ticket);
}

template<>
void VectorDataLoader::unload_task_data<VectorDataLoader::Task::LoadVectorDataUrlPackage>(
    Task& task,
    Task::LoadVectorDataUrlPackage& task_data,
    bool release_dependent_task_data,
    JobScheduler* js)
{
    if (hrz_jobs::is_job_valid(js, task_data.parse_ticket))
    {
        hrz_jobs::cancel_job(js, task_data.parse_ticket);
    }

    task_data.package = std::nullopt;

    if (release_dependent_task_data)
    {
        task_data.load_url_data_task.release_data();
    }
}

template<>
void VectorDataLoader::work_new_task<VectorDataLoader::Task::LoadVectorDataUrlPackage>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::LoadVectorDataUrlPackage& task_data,
    SceneModel* scene_model,
    AttributionRegistry* attributions)
{
    task_data.load_url_data_task = TaskDependency::between_tasks(
        get_or_create_load_url_data_task(
            task_data.url, task_data.headers, task_data.queue, task_data.priority,
            task_data.resource_owner, task_data.request_count_metric),
        task_ref);

    set_task_status(task_ref, task, TaskStatus::Unloaded);
}

template<>
void VectorDataLoader::work_unloaded_task<VectorDataLoader::Task::LoadVectorDataUrlPackage>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::LoadVectorDataUrlPackage& task_data)
{
    task_data.load_url_data_task.retain_data();
}

template<>
void VectorDataLoader::work_loading_task<VectorDataLoader::Task::LoadVectorDataUrlPackage>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::LoadVectorDataUrlPackage& task_data,
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
                hrz::vector_data::ParsedMvt parsed_mvt;
                hrz_jobs::get_job_response(js, task_data.parse_ticket, parsed_mvt);

                task_data.package = {
                    {std::move(parsed_mvt)},
                    hrz_proto::VectorDataFormat::MVT_VECTOR_DATA};

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
        const auto& load_url_data_task = task_data.load_url_data_task.get_task();
        if (load_url_data_task.status == TaskStatus::Loaded)
        {
            if (task_data.format == hrz_proto::VectorDataFormat::MVT_VECTOR_DATA)
            {
                // A copy of the blob is made, because `add_job_*` functions
                // move out their parameters, and we don't want to modify
                // the load URL data task.
                auto mvt_blob = load_url_data_task.load_url_data().blob;
                task_data.parse_ticket = hrz_jobs::add_job_parse_mvt(
                    js, mvt_blob, {monitoring::systems::VectorDataLoader});
            }
            else
            {
                task_data.package = {{load_url_data_task.load_url_data().blob, task_data.format}};

                set_task_status(task_ref, task, TaskStatus::Loaded);
            }
        }
        else if (is_error(load_url_data_task.status))
        {
            set_task_status(task_ref, task, load_url_data_task.status);
        }
        else
        {
            set_task_status(task_ref, task, TaskStatus::Blocked);
        }
    }
}

template<>
bool VectorDataLoader::unload_task_data_if_not_needed<
    VectorDataLoader::Task::LoadVectorDataUrlPackage>()
{
    return true;
}

template<>
void VectorDataLoader::check_for_invalidated_data_for_task<
    VectorDataLoader::Task::LoadVectorDataUrlPackage>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::LoadVectorDataUrlPackage& task_data,
    JobScheduler* js)
{
    // No-op
}
} // namespace hrz
