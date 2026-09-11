// SPDX-FileCopyrightText: Copyright 2025 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/core/base_url.h"
#include "hrz/core/loading_priorities.h"
#include "hrz/core/tilejson.h"
#include "hrz/core/vector/data_loader/impl.h"
#include "hrz/fnd/log.h"
#include "hrz/fnd/variant.h"

namespace hrz
{

VectorDataLoader::TaskRef VectorDataLoader::get_or_create_load_tilejson_task(
    const std::string_view& url,
    const hrz::HttpHeaders& headers,
    hrz::assets_loader::Queue queue,
    uint32_t priority,
    bool preserve_query_parameters,
    const monitoring::ResourceOwner& resource_owner,
    const metrics::MetricDesc& request_count_metric)
{
    // Don’t check on the queue and priority. We want to reuse
    // the task even if only the URL is the same.
    // If a task is reused, the metrics and resource ownership information
    // may become incomplete, as they would only mention one data source
    // on one vector data layer, but at least the actual work isn't
    // duplicated.

    uint64_t hash = hrz::index_of_variant<decltype(Task::data), Task::LoadTileJson>();
    hash = hrz::hash_mix<uint64_t>(hash, hrz::murmur3_x64_64(url));
    hash = hrz::hash_mix<uint64_t>(hash, headers.hash_content());
    hash = hrz::hash_mix<uint64_t>(hash, preserve_query_parameters);

    auto it = tasks_by_hash.find(hash);
    if (it != tasks_by_hash.end() && it->second.is_valid())
    {
        auto& task = it->second.value();

        if (task.is_load_tilejson())
        {
            auto& task_data = task.load_tilejson();
            if (task_data.url == url && task_data.headers.hash_content() == headers.hash_content()
                && task_data.preserve_query_parameters == preserve_query_parameters)
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
    Task::LoadTileJson task_data;
    task_data.url = {url.data(), url.size()};
    task_data.headers = headers;
    task_data.queue = queue;
    task_data.priority = priority;
    task_data.preserve_query_parameters = preserve_query_parameters;
    task_data.load_url_data_task = {};
    task_data.resource_owner = resource_owner;
    task_data.request_count_metric = request_count_metric;
    new_task.data = std::move(task_data);
    new_task.hash = hash;

    tasks_by_hash.insert({new_task.hash, new_task_ref.make_weak_ref()});

    activate_task(new_task_ref.make_weak_ref());

    return new_task_ref;
}

template<>
void VectorDataLoader::clear_task<VectorDataLoader::Task::LoadTileJson>(
    Task& task,
    Task::LoadTileJson& task_data,
    JobScheduler* js)
{
    // No-op
}

template<>
void VectorDataLoader::cancel_task_jobs<VectorDataLoader::Task::LoadTileJson>(
    Task& task,
    Task::LoadTileJson& task_data,
    JobScheduler* js)
{
    // No-op
}

template<>
void VectorDataLoader::unload_task_data<VectorDataLoader::Task::LoadTileJson>(
    Task& task,
    Task::LoadTileJson& task_data,
    bool release_dependent_task_data,
    JobScheduler* js)
{
    if (release_dependent_task_data)
    {
        task_data.load_url_data_task.release_data();
    }
}

template<>
void VectorDataLoader::work_new_task<VectorDataLoader::Task::LoadTileJson>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::LoadTileJson& task_data,
    SceneModel* scene_model,
    AttributionRegistry* attributions)
{
    task_data.load_url_data_task = TaskDependency::between_tasks(
        get_or_create_load_url_data_task(
            task_data.url, task_data.headers, task_data.queue,
            hrz::combine_loading_priorities(
                task_data.priority, std::numeric_limits<uint16_t>::max()),
            task_data.resource_owner, task_data.request_count_metric),
        task_ref);

    set_task_status(task_ref, task, TaskStatus::Unloaded);
}

template<>
void VectorDataLoader::work_unloaded_task<VectorDataLoader::Task::LoadTileJson>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::LoadTileJson& task_data)
{
    task_data.load_url_data_task.retain_data();
}

template<>
void VectorDataLoader::work_loading_task<VectorDataLoader::Task::LoadTileJson>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::LoadTileJson& task_data,
    JobScheduler* js,
    BlobAllocator* ba,
    ClientMessageQueue* mq,
    AttributionRegistry* attributions)
{
    auto& load_url_data_task = task_data.load_url_data_task.get_task();
    if (load_url_data_task.status == TaskStatus::Loaded)
    {
        auto raw_data = load_url_data_task.load_url_data().blob.get_data();
        auto base_url = BaseUrl(task_data.url, task_data.preserve_query_parameters);
        auto tilejson = tilejson::parse_tilejson(raw_data, base_url);

        if (tilejson.has_value())
        {
            task_data.tile_url_generator =
                hrz::MultiPatternTileUrlGenerator({tilejson->url_patterns}, 1);
            task_data.min_level = tilejson->min_level;
            task_data.max_level = tilejson->max_level;
            task_data.bounds = tilejson->bounds;
            task_data.attribution =
                attribution::register_attribution(attributions, {tilejson->attribution, {}});
            set_task_status(task_ref, task, TaskStatus::Loaded);
        }
        else
        {
            HRZ_LOG_ERROR("Could not decode TileJSON file at {}", task_data.url);
            set_task_status(task_ref, task, TaskStatus::DataError);
        }

        task_data.load_url_data_task.release_data();
    }
    else if (is_error(load_url_data_task.status))
    {
        task_data.load_url_data_task.release_data();
        set_task_status(task_ref, task, load_url_data_task.status);
    }
    else
    {
        set_task_status(task_ref, task, TaskStatus::Blocked);
    }
}

template<>
bool VectorDataLoader::unload_task_data_if_not_needed<VectorDataLoader::Task::LoadTileJson>()
{
    return true;
}

template<>
void VectorDataLoader::check_for_invalidated_data_for_task<VectorDataLoader::Task::LoadTileJson>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::LoadTileJson& task_data,
    JobScheduler* js)
{
    // No-op
}

} // namespace hrz
