#include "hrz/core/vector/data_loader/impl.h"
#include "hrz/fnd/log.h"
#include "hrz/fnd/variant.h"

namespace hrz
{

VectorDataLoader::TaskRef VectorDataLoader::get_or_create_load_untiled_vector_data_task(
    const LayerModelRef& layer_model,
    uint32_t data_source)
{
    uint64_t hash =
        hrz::hash_value(hrz::index_of_variant<decltype(Task::data), Task::LoadUntiledVectorData>());
    hash = hrz::hash_mix<uint64_t>(hash, hrz::hash_value(layer_model.get_handle()));
    hash = hrz::hash_mix<uint64_t>(hash, hrz::hash_value(data_source));

    auto it = tasks_by_hash.find(hash);
    if (it != tasks_by_hash.end() && it->second.is_valid())
    {
        const auto& task = it->second.value();
        if (task.is_load_untiled_vector_data())
        {
            const auto& task_data = task.load_untiled_vector_data();
            if (task_data.layer_model == layer_model && task_data.data_source == data_source)
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
    Task::LoadUntiledVectorData task_data;
    task_data.layer_model = layer_model;
    task_data.data_source = data_source;
    task_data.load_vector_tile_data_task = {};
    new_task.data = std::move(task_data);
    new_task.hash = hash;

    tasks_by_hash.insert({new_task.hash, new_task_ref.make_weak_ref()});

    activate_task(new_task_ref.make_weak_ref());

    return new_task_ref;
}

template<>
void VectorDataLoader::clear_task<VectorDataLoader::Task::LoadUntiledVectorData>(
    Task& task,
    Task::LoadUntiledVectorData& task_data,
    JobScheduler* js)
{
    // No-op
}

template<>
void VectorDataLoader::cancel_task_jobs<VectorDataLoader::Task::LoadUntiledVectorData>(
    Task& task,
    Task::LoadUntiledVectorData& task_data,
    JobScheduler* js)
{
    hrz_jobs::cancel_job(js, task_data.build_aabb_tree_ticket);
}

template<>
void VectorDataLoader::unload_task_data<VectorDataLoader::Task::LoadUntiledVectorData>(
    Task& task,
    Task::LoadUntiledVectorData& task_data,
    bool release_dependent_task_data,
    JobScheduler* js)
{
    if (hrz_jobs::is_job_valid(js, task_data.build_aabb_tree_ticket))
    {
        hrz_jobs::cancel_job(js, task_data.build_aabb_tree_ticket);
    }

    task_data.aabb_tree = {};
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
    }
}

template<>
void VectorDataLoader::work_new_task<VectorDataLoader::Task::LoadUntiledVectorData>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::LoadUntiledVectorData& task_data,
    SceneModel* scene_model,
    AttributionRegistry* attributions)
{
    const auto& layer_model = task_data.layer_model.value();
    const auto& data_source = layer_model.data_sources.at(task_data.data_source);
    const auto& provider = data_source.untiled_data_provider();

    if (provider.format != hrz_proto::GEOJSON_VECTOR_DATA
        && provider.format != hrz_proto::GEOBUF_VECTOR_DATA)
    {
        HRZ_LOG_ERROR("Untiled vector data providers only support the GeoJSON and Geobuf formats.");
        set_task_status(task_ref, task, TaskStatus::ModelError);
    }
    else
    {
        task_data.load_vector_tile_data_task = TaskDependency::between_tasks(
            get_or_create_load_vector_tile_data_task(
                task_data.layer_model, task_data.data_source, FeatureSelection{TileCoords{}}),
            task_ref);

        set_task_status(task_ref, task, TaskStatus::Unloaded);
    }
}

template<>
void VectorDataLoader::work_unloaded_task<VectorDataLoader::Task::LoadUntiledVectorData>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::LoadUntiledVectorData& task_data)
{
    task_data.load_vector_tile_data_task.retain_data();
}

template<>
void VectorDataLoader::work_loading_task<VectorDataLoader::Task::LoadUntiledVectorData>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::LoadUntiledVectorData& task_data,
    JobScheduler* js,
    BlobAllocator* ba,
    ClientMessageQueue* mq,
    AttributionRegistry* attributions)
{
    auto& layer_model = task_data.layer_model.value();
    auto& data_source = layer_model.data_sources.at(task_data.data_source);

    assert(data_source.has_untiled_data_provider());

    if (hrz_jobs::is_job_valid(js, task_data.build_aabb_tree_ticket))
    {
        if (hrz_jobs::is_job_finished(js, task_data.build_aabb_tree_ticket))
        {
            if (hrz_jobs::get_job_status(js, task_data.build_aabb_tree_ticket)
                == hrz::job_scheduler::JobStatus::Finished_Success)
            {
                auto aabb_tree = hrz_jobs::get_job_response(js, task_data.build_aabb_tree_ticket);

                task_data.aabb_tree = std::move(aabb_tree);
                task_data.attribution = data_source.untiled_data_provider().attribution;

                set_task_status(task_ref, task, TaskStatus::Loaded);
            }
            else
            {
                HRZ_LOG_ERROR("Could not load untiled vector data of layer {}", layer_model.id);
                hrz_jobs::cancel_job(js, task_data.build_aabb_tree_ticket);
                set_task_status(task_ref, task, TaskStatus::DataError);
            }
        }
    }
    else if (task_data.load_vector_tile_data_task.has_task())
    {
        auto& load_vector_tile_data_task = task_data.load_vector_tile_data_task.get_task();
        if (load_vector_tile_data_task.status == TaskStatus::Loaded)
        {
            const auto& load_data = load_vector_tile_data_task.load_vector_tile_data();

            task_data.feature_ids = load_data.feature_ids;
            task_data.geometry = load_data.geometry;
            task_data.attribute_ids_to_values = load_data.attribute_ids_to_values;
            task_data.attribution = load_data.attribution;

            vector_data::VectorTileGeometry geometry = task_data.geometry.value();
            task_data.build_aabb_tree_ticket = hrz_jobs::add_job_build_aabb_tree(
                js, std::move(geometry), {monitoring::systems::VectorDataLoader});

            task_data.load_vector_tile_data_task.release_data();
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
}

template<>
bool VectorDataLoader::
    unload_task_data_if_not_needed<VectorDataLoader::Task::LoadUntiledVectorData>()
{
    // We don't want to unload untiled vector data even when it's not used because
    // it could lead to the source data file being reloaded multiple times, and
    // the AABB tree recomputed as many times.
    // It should be unloaded when the provider disappears or is invalidated.
    return false;
}

template<>
void VectorDataLoader::check_for_invalidated_data_for_task<
    VectorDataLoader::Task::LoadUntiledVectorData
>(WeakTaskRef& task_ref, Task& task, Task::LoadUntiledVectorData& task_data, JobScheduler* js)
{
    const auto& layer_model = task_data.layer_model.value();

    for (const auto& invalidation : invalidations)
    {
        if (!invalidation.applies_to_source(layer_model.id, task_data.data_source))
        {
            continue;
        }

        if (invalidation.invalidates_everything())
        {
            restart_task(task_ref, task, js);
            return;
        }

        if (invalidation.proto.has_tile_coords())
        {
            restart_task(task_ref, task, js);
            return;
        }
    }
}

} // namespace hrz
