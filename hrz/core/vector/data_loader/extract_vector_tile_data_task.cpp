#include "hrz/core/vector/data_loader/impl.h"
#include "hrz/fnd/log.h"
#include "hrz/fnd/variant.h"

namespace hrz
{

VectorDataLoader::TaskRef VectorDataLoader::get_or_create_extract_vector_tile_data_task(
    const LayerModelRef& layer_model,
    uint32_t data_source,
    TileCoords tile_coords)
{
    uint64_t hash =
        hrz::hash_value(hrz::index_of_variant<decltype(Task::data), Task::ExtractVectorTileData>());
    hash = hrz::hash_mix<uint64_t>(hash, hrz::hash_value(layer_model.get_handle()));
    hash = hrz::hash_mix<uint64_t>(hash, hrz::hash_value(data_source));
    hash = hrz::hash_mix<uint64_t>(hash, hrz::hash_value(tile_coords));

    auto it = tasks_by_hash.find(hash);
    if (it != tasks_by_hash.end() && it->second.is_valid())
    {
        const auto& task = it->second.value();
        if (task.is_extract_vector_tile_data())
        {
            const auto& task_data = task.extract_vector_tile_data();
            if (task_data.layer_model == layer_model && task_data.data_source == data_source
                && task_data.tile_coords == tile_coords)
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
    Task::ExtractVectorTileData task_data;
    task_data.layer_model = layer_model;
    task_data.data_source = data_source;
    task_data.tile_coords = tile_coords;
    task_data.load_untiled_vector_tile_data_task = {};
    new_task.data = std::move(task_data);
    new_task.hash = hash;

    tasks_by_hash.insert({new_task.hash, new_task_ref.make_weak_ref()});

    activate_task(new_task_ref.make_weak_ref());

    return new_task_ref;
}

template<>
void VectorDataLoader::clear_task<VectorDataLoader::Task::ExtractVectorTileData>(
    Task& task,
    Task::ExtractVectorTileData& task_data,
    JobScheduler* js)
{
    // No-op
}

template<>
void VectorDataLoader::cancel_task_jobs<VectorDataLoader::Task::ExtractVectorTileData>(
    Task& task,
    Task::ExtractVectorTileData& task_data,
    JobScheduler* js)
{
    hrz_jobs::cancel_job(js, task_data.extract_ticket);
}

template<>
void VectorDataLoader::unload_task_data<VectorDataLoader::Task::ExtractVectorTileData>(
    Task& task,
    Task::ExtractVectorTileData& task_data,
    bool release_dependent_task_data,
    JobScheduler* js)
{
    if (hrz_jobs::is_job_valid(js, task_data.extract_ticket))
    {
        hrz_jobs::cancel_job(js, task_data.extract_ticket);
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
        task_data.load_untiled_vector_tile_data_task.release_data();
    }
}

template<>
void VectorDataLoader::work_new_task<VectorDataLoader::Task::ExtractVectorTileData>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::ExtractVectorTileData& task_data,
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
        task_data.load_untiled_vector_tile_data_task = TaskDependency::between_tasks(
            get_or_create_load_untiled_vector_data_task(
                task_data.layer_model, task_data.data_source),
            task_ref);

        set_task_status(task_ref, task, TaskStatus::Unloaded);
    }
}

template<>
void VectorDataLoader::work_unloaded_task<VectorDataLoader::Task::ExtractVectorTileData>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::ExtractVectorTileData& task_data)
{
    task_data.load_untiled_vector_tile_data_task.retain_data();
}

template<>
void VectorDataLoader::work_loading_task<VectorDataLoader::Task::ExtractVectorTileData>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::ExtractVectorTileData& task_data,
    JobScheduler* js,
    BlobAllocator* ba,
    ClientMessageQueue* mq,
    AttributionRegistry* attributions)
{
    auto& layer_model = task_data.layer_model.value();

    if (hrz_jobs::is_job_valid(js, task_data.extract_ticket))
    {
        if (hrz_jobs::is_job_finished(js, task_data.extract_ticket))
        {
            if (hrz_jobs::get_job_status(js, task_data.extract_ticket)
                == hrz::job_scheduler::JobStatus::Finished_Success)
            {
                const auto& data_source = layer_model.data_sources.at(task_data.data_source);

                auto extracted_tile = hrz_jobs::get_job_response(js, task_data.extract_ticket);

                assert(
                    extracted_tile.feature_ids.size() == extracted_tile.geometry.features.size());
                task_data.feature_ids = feature_id_lists.alloc();
                task_data.feature_ids.value() = std::move(extracted_tile.feature_ids);

                task_data.geometry = tile_geometries.alloc();
                task_data.geometry.value() = std::move(extracted_tile.geometry);

                load_attribute_data_into_map(
                    extracted_tile.attributes, task_data.attribute_ids_to_values, data_source,
                    extracted_tile.geometry.features.size());

                set_task_status(task_ref, task, TaskStatus::Loaded);
            }
            else
            {
                HRZ_LOG_ERROR("Could not extract vector tile data of layer {}", layer_model.id);
                hrz_jobs::cancel_job(js, task_data.extract_ticket);
                set_task_status(task_ref, task, TaskStatus::DataError);
            }
        }
    }
    else if (task_data.load_untiled_vector_tile_data_task.has_task())
    {
        auto& load_untiled_vector_tile_data_task =
            task_data.load_untiled_vector_tile_data_task.get_task();
        if (load_untiled_vector_tile_data_task.status == TaskStatus::Loaded)
        {
            const auto& data_source = layer_model.data_sources.at(task_data.data_source);
            const auto& provider = data_source.untiled_data_provider();

            const auto& load_data = load_untiled_vector_tile_data_task.load_untiled_vector_data();

            hrz_jobs::VectorTileExtractionParams params{};
            params.source_data.coords = TileCoords{0, 0, 0};
            params.source_data.geometry = load_data.geometry.value();
            params.source_data.feature_ids = load_data.feature_ids.value();
            for (const auto& pair : load_data.attribute_ids_to_values)
            {
                params.source_data.attributes.push_back(pair.second.value());
            }
            params.aabb_tree = load_data.aabb_tree;
            params.coords = task_data.tile_coords;
            params.tolerance = data_source.untiled_data_provider().tolerance;
            params.include_clip_margin = data_source.untiled_data_provider().clip_margin;
            params.bounds = provider.bounds;

            task_data.extract_ticket = hrz_jobs::add_job_extract_vector_tile(
                js, std::move(params),
                hrz::monitoring::ResourceOwner(
                    hrz::monitoring::systems::VectorDataLoader, layer_model.layer_handle));

            task_data.attribution = load_data.attribution;

            task_data.load_untiled_vector_tile_data_task.release_data();
        }
        else if (is_error(load_untiled_vector_tile_data_task.status))
        {
            task_data.load_untiled_vector_tile_data_task.release_data();
            set_task_status(task_ref, task, load_untiled_vector_tile_data_task.status);
        }
        else
        {
            set_task_status(task_ref, task, TaskStatus::Blocked);
        }
    }
}

template<>
bool VectorDataLoader::
    unload_task_data_if_not_needed<VectorDataLoader::Task::ExtractVectorTileData>()
{
    return true;
}

template<>
void VectorDataLoader::check_for_invalidated_data_for_task<
    VectorDataLoader::Task::ExtractVectorTileData
>(WeakTaskRef& task_ref, Task& task, Task::ExtractVectorTileData& task_data, JobScheduler* js)
{
    // No-op
}

} // namespace hrz
