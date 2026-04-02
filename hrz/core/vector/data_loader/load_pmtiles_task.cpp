#include "hrz/core/vector/data_loader/impl.h"
#include "hrz/fnd/variant.h"

namespace hrz
{

VectorDataLoader::TaskRef VectorDataLoader::get_or_create_load_pmtiles_task(
    const std::string_view& url,
    const hrz::HttpHeaders& headers,
    hrz::assets_loader::Queue queue,
    uint32_t priority,
    const monitoring::ResourceOwner& resource_owner,
    const metrics::MetricDesc& request_count_metric)
{
    // Don’t check on the queue and priority. We want to reuse
    // the task even if only the URL is the same.
    // If a task is reused, the metrics and resource ownership information
    // may become incomplete, as they would only mention one data source
    // on one vector data layer, but at least the actual work isn't
    // duplicated.

    uint64_t hash = hrz::index_of_variant<decltype(Task::data), Task::LoadPmTiles>();
    hash = hrz::hash_mix<uint64_t>(hash, hrz::murmur3_x64_64(url));
    hash = hrz::hash_mix<uint64_t>(hash, headers.hash_content());

    auto it = tasks_by_hash.find(hash);
    if (it != tasks_by_hash.end() && it->second.is_valid())
    {
        auto& task = it->second.value();

        if (task.is_load_pmtiles())
        {
            auto& task_data = task.load_pmtiles();
            if (task_data.url == url && task_data.headers.hash_content() == headers.hash_content())
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
    Task::LoadPmTiles task_data;
    task_data.url = {url.data(), url.size()};
    task_data.headers = headers;
    task_data.queue = queue;
    task_data.priority = priority;
    task_data.resource_owner = resource_owner;
    task_data.request_count_metric = request_count_metric;
    task_data.asset_loader_channel_request_id = std::nullopt;
    task_data.pmtiles = nullptr;
    new_task.data = std::move(task_data);
    new_task.hash = hash;

    tasks_by_hash.insert({new_task.hash, new_task_ref.make_weak_ref()});

    activate_task(new_task_ref.make_weak_ref());

    return new_task_ref;
}

template<>
void VectorDataLoader::clear_task<VectorDataLoader::Task::LoadPmTiles>(
    Task& task,
    Task::LoadPmTiles& task_data,
    JobScheduler* js)
{
    if (task_data.pmtiles)
    {
        task_data.pmtiles->destroy();
        task_data.pmtiles.reset();
    }
}

template<>
void VectorDataLoader::cancel_task_jobs<VectorDataLoader::Task::LoadPmTiles>(
    Task& task,
    Task::LoadPmTiles& task_data,
    JobScheduler* js)
{
    if (task_data.pmtiles)
    {
        task_data.pmtiles->destroy();
        task_data.pmtiles.reset();
    }
}

template<>
void VectorDataLoader::unload_task_data<VectorDataLoader::Task::LoadPmTiles>(
    Task& task,
    Task::LoadPmTiles& task_data,
    bool release_dependent_task_data,
    JobScheduler* js)
{
    if (task_data.pmtiles)
    {
        task_data.pmtiles->destroy();
        task_data.pmtiles.reset();
    }
}

template<>
void VectorDataLoader::work_new_task<VectorDataLoader::Task::LoadPmTiles>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::LoadPmTiles& task_data,
    SceneModel* scene_model,
    AttributionRegistry* attributions)
{
    set_task_status(task_ref, task, TaskStatus::Unloaded);
}

template<>
void VectorDataLoader::work_unloaded_task<VectorDataLoader::Task::LoadPmTiles>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::LoadPmTiles& task_data)
{
    // No-op
}

template<>
void VectorDataLoader::work_loading_task<VectorDataLoader::Task::LoadPmTiles>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::LoadPmTiles& task_data,
    JobScheduler* js,
    BlobAllocator* ba,
    ClientMessageQueue* mq,
    AttributionRegistry* attributions)
{
    if (!task_data.asset_loader_channel_request_id.has_value())
    {
        task_data.asset_loader_channel_request_id = next_download_request_id;
        next_download_request_id += 1;
        asset_loader_channel.send(
            assets_loader::messages::CreateChannel{
                task_data.asset_loader_channel_request_id.value()
            });
        tasks_waiting_for_asset_loader_message.insert(
            {task_data.asset_loader_channel_request_id.value(), task_ref});

        set_task_status(task_ref, task, TaskStatus::Blocked);
    }
    else if (task_data.pmtiles)
    {
        task_data.pmtiles->work(js, ba);

        if (task_data.pmtiles->get_status() == PmTiles::kReady)
        {
            set_task_status(task_ref, task, TaskStatus::Loaded);
        }
        else if (task_data.pmtiles->get_status() == PmTiles::kError)
        {
            set_task_status(task_ref, task, TaskStatus::DataError);
        }
    }
}

template<>
bool VectorDataLoader::unload_task_data_if_not_needed<VectorDataLoader::Task::LoadPmTiles>()
{
    return true;
}

template<>
void VectorDataLoader::check_for_invalidated_data_for_task<VectorDataLoader::Task::LoadPmTiles>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::LoadPmTiles& task_data,
    JobScheduler* js)
{
    // No-op
}

} // namespace hrz
