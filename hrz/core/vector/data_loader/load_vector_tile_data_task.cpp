#include "hrz/common/fmt.h"
#include "hrz/core/vector/data_loader/impl.h"
#include "hrz/fnd/variant.h"

namespace hrz
{
VectorDataLoader::TaskRef VectorDataLoader::get_or_create_load_vector_tile_data_task(
    const LayerModelRef& layer_model,
    uint32_t data_source,
    const FeatureSelection& feature_selection)
{
    uint64_t hash = hrz::index_of_variant<decltype(Task::data), Task::LoadVectorTileData>();
    hash = hrz::hash_mix<uint64_t>(hash, layer_model.get_handle().hash());
    hash = hrz::hash_mix<uint64_t>(hash, hrz::hash_value(data_source));
    hash = hrz::hash_mix<uint64_t>(hash, feature_selection.hash());

    auto it = tasks_by_hash.find(hash);
    if (it != tasks_by_hash.end() && it->second.is_valid())
    {
        const auto& task = it->second.value();
        if (task.is_load_vector_tile_data())
        {
            const auto& task_data = task.load_vector_tile_data();
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
    Task::LoadVectorTileData task_data;
    task_data.layer_model = layer_model;
    task_data.data_source = data_source;
    task_data.feature_selection = feature_selection;
    task_data.load_vector_data_url_package_task = {};
    task_data.load_vector_data_pmtiles_package_task = {};
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
void VectorDataLoader::clear_task<VectorDataLoader::Task::LoadVectorTileData>(
    Task& task,
    Task::LoadVectorTileData& task_data,
    JobScheduler* js)
{
    // No-op
}

template<>
void VectorDataLoader::cancel_task_jobs<VectorDataLoader::Task::LoadVectorTileData>(
    Task& task,
    Task::LoadVectorTileData& task_data,
    JobScheduler* js)
{
    hrz_jobs::cancel_job(js, task_data.decode_ticket);
}

template<>
void VectorDataLoader::unload_task_data<VectorDataLoader::Task::LoadVectorTileData>(
    Task& task,
    Task::LoadVectorTileData& task_data,
    bool release_dependent_task_data,
    JobScheduler* js)
{
    if (hrz_jobs::is_job_valid(js, task_data.decode_ticket))
    {
        hrz_jobs::cancel_job(js, task_data.decode_ticket);
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
        task_data.load_vector_data_url_package_task.release_data();
        task_data.load_vector_data_pmtiles_package_task.release_data();
    }
}

template<>
void VectorDataLoader::work_new_task<VectorDataLoader::Task::LoadVectorTileData>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::LoadVectorTileData& task_data,
    SceneModel* scene_model,
    AttributionRegistry* attributions)
{
    const auto& layer_model = task_data.layer_model.value();
    const auto& data_source = layer_model.data_sources.at(task_data.data_source);

    assert(task_data.feature_selection.has_tile_coords());

    if (data_source.has_tiled_data_provider())
    {
        const auto& provider = data_source.tiled_data_provider();

        task_data.load_vector_data_url_package_task = TaskDependency::between_tasks(
            get_or_create_load_vector_data_url_package_task(
                make_url(provider.tile_url_generator, task_data.feature_selection.tile_coords()),
                provider.headers, get_load_queue(layer_model),
                compute_tile_loading_priority(
                    layer_model, task_data.feature_selection.tile_coords()),
                provider.format,
                hrz::monitoring::ResourceOwner(
                    hrz::monitoring::systems::VectorDataLoader, layer_model.layer_handle),
                data_source.request_count_metric),
            task_ref);

        set_task_status(task_ref, task, TaskStatus::Unloaded);
    }
    else if (data_source.has_untiled_data_provider())
    {
        const auto& provider = data_source.untiled_data_provider();

        task_data.load_vector_data_url_package_task = TaskDependency::between_tasks(
            get_or_create_load_vector_data_url_package_task(
                provider.url, provider.headers, get_load_queue(layer_model),
                compute_tile_loading_priority(
                    layer_model, task_data.feature_selection.tile_coords()),
                provider.format,
                hrz::monitoring::ResourceOwner(
                    hrz::monitoring::systems::VectorDataLoader, layer_model.layer_handle),
                data_source.request_count_metric),
            task_ref);

        set_task_status(task_ref, task, TaskStatus::Unloaded);
    }
    else if (data_source.has_tilejson_data_provider())
    {
        const auto& provider = data_source.tilejson_data_provider();

        task_data.load_vector_data_url_package_task = TaskDependency::between_tasks(
            get_or_create_load_vector_data_url_package_task(
                make_url(
                    provider.load_tilejson_task.get_task().load_tilejson().tile_url_generator,
                    task_data.feature_selection.tile_coords()),
                provider.headers, get_load_queue(layer_model),
                compute_tile_loading_priority(
                    layer_model, task_data.feature_selection.tile_coords()),
                hrz_proto::VectorDataFormat::MVT_VECTOR_DATA,
                hrz::monitoring::ResourceOwner(
                    hrz::monitoring::systems::VectorDataLoader, layer_model.layer_handle),
                data_source.request_count_metric),
            task_ref);

        set_task_status(task_ref, task, TaskStatus::Unloaded);
    }
    else if (data_source.has_pmtiles_data_provider())
    {
        const auto& provider = data_source.pmtiles_data_provider();

        task_data.load_vector_data_pmtiles_package_task = TaskDependency::between_tasks(
            get_or_create_load_vector_data_pmtiles_package_task(
                provider.url, provider.headers, task_data.feature_selection.tile_coords(),
                get_load_queue(layer_model),
                compute_tile_loading_priority(
                    layer_model, task_data.feature_selection.tile_coords()),
                hrz::monitoring::ResourceOwner(
                    hrz::monitoring::systems::VectorDataLoader, layer_model.layer_handle),
                data_source.request_count_metric),
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
void VectorDataLoader::work_unloaded_task<VectorDataLoader::Task::LoadVectorTileData>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::LoadVectorTileData& task_data)
{
    task_data.load_vector_data_url_package_task.retain_data();
    task_data.load_vector_data_pmtiles_package_task.retain_data();
}

template<>
void VectorDataLoader::work_loading_task<VectorDataLoader::Task::LoadVectorTileData>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::LoadVectorTileData& task_data,
    JobScheduler* js,
    BlobAllocator* ba,
    ClientMessageQueue* mq,
    AttributionRegistry* attributions)
{
    const auto& layer_model = task_data.layer_model.value();
    const auto& data_source = layer_model.data_sources.at(task_data.data_source);

    auto decode_data = [&](TileCoords tile_coords, std::string_view layer_name,
                           AttributionHandle attribution,
                           const vector_data::VectorDataPackage& package)
    {
        vector_data::EncodedVectorTile job_params;
        job_params.coords = tile_coords;
        job_params.data = package;
        job_params.layer_name = layer_name;
        job_params.decode_geometry = layer_model.geometry_source == task_data.data_source;
        job_params.source_feature_id_attribute = std::nullopt;

        for (const auto& it : data_source.attributes)
        {
            auto attribute_id = it.first;
            const auto& attribute = it.second;

            if (attribute.data_source != task_data.data_source) continue;

            vector_data::AttributeModel param_attribute;
            param_attribute.id = attribute_id;
            param_attribute.name = attribute.name_in_source;
            param_attribute.is_feature_id = attribute.is_feature_id;
            param_attribute.transform = attribute.transform;

            job_params.attributes.push_back(param_attribute);

            if (attribute_id == data_source.source_feature_id_attribute)
            {
                job_params.source_feature_id_attribute = {job_params.attributes.size() - 1};
            }
        }

        task_data.attribution = attribution;

        task_data.decode_ticket = hrz_jobs::add_job_decode_vector_tile(
            js, job_params,
            {monitoring::systems::VectorDataLoader, task_data.layer_model->layer_handle});
    };

    auto decode_url_data_if_loaded =
        [&](TileCoords tile_coords, const std::string& layer_name, AttributionHandle attribution)
    {
        auto& load_vector_data_url_package_task =
            task_data.load_vector_data_url_package_task.get_task();
        if (load_vector_data_url_package_task.status == TaskStatus::Loaded)
        {
            const auto& load_vector_data_url_package_task_data =
                load_vector_data_url_package_task.load_vector_data_url_package();
            decode_data(
                tile_coords, layer_name, attribution,
                load_vector_data_url_package_task_data.package.value());
        }
        else if (is_error(load_vector_data_url_package_task.status))
        {
            task_data.load_vector_data_url_package_task.release_data();
            set_task_status(task_ref, task, load_vector_data_url_package_task.status);
        }
        else
        {
            set_task_status(task_ref, task, TaskStatus::Blocked);
        }
    };

    if (hrz_jobs::is_job_valid(js, task_data.decode_ticket))
    {
        if (hrz_jobs::is_job_finished(js, task_data.decode_ticket))
        {
            if (hrz_jobs::get_job_status(js, task_data.decode_ticket)
                == hrz::job_scheduler::JobStatus::Finished_Success)
            {
                vector_data::DecodedVectorTile data;
                hrz_jobs::get_job_response(js, task_data.decode_ticket, data);

                task_data.feature_ids = feature_id_lists.alloc();
                task_data.feature_ids.value() = std::move(data.feature_ids);

                if (layer_model.geometry_source == task_data.data_source)
                {
                    assert(data.feature_ids.size() == data.geometry.features.size());
                    task_data.geometry = tile_geometries.alloc();
                    task_data.geometry.value() = std::move(data.geometry);
                }

                load_attribute_data_into_map(
                    data.attributes, task_data.attribute_ids_to_values, data_source,
                    data.feature_ids.size());

                set_task_status(task_ref, task, TaskStatus::Loaded);
            }
            else
            {
                const auto tile_coords = task_data.feature_selection.tile_coords();
                HRZ_LOG_ERROR(
                    "Could not decode tile {} of vector data layer {}", tile_coords,
                    layer_model.id);
                hrz_jobs::cancel_job(js, task_data.decode_ticket);
                set_task_status(task_ref, task, TaskStatus::DataError);
            }
        }
    }
    else if (data_source.has_tiled_data_provider())
    {
        const auto& provider = data_source.tiled_data_provider();

        assert(task_data.feature_selection.has_tile_coords());
        decode_url_data_if_loaded(
            task_data.feature_selection.tile_coords(), provider.layer_name, provider.attribution);
    }
    else if (data_source.has_untiled_data_provider())
    {
        const auto& provider = data_source.untiled_data_provider();

        assert(task_data.feature_selection.has_tile_coords());
        decode_url_data_if_loaded(
            task_data.feature_selection.tile_coords(), "", provider.attribution);
    }
    else if (data_source.has_tilejson_data_provider())
    {
        const auto& provider = data_source.tilejson_data_provider();

        assert(task_data.feature_selection.has_tile_coords());
        decode_url_data_if_loaded(
            task_data.feature_selection.tile_coords(), provider.layer_name, provider.attribution);
    }
    else if (data_source.has_pmtiles_data_provider())
    {
        const auto& provider = data_source.pmtiles_data_provider();

        assert(task_data.feature_selection.has_tile_coords());

        auto& load_vector_data_pmtiles_package_task =
            task_data.load_vector_data_pmtiles_package_task.get_task();
        if (load_vector_data_pmtiles_package_task.status == TaskStatus::Loaded)
        {
            const auto& load_vector_data_pmtiles_package_task_data =
                load_vector_data_pmtiles_package_task.load_vector_data_pmtiles_package();
            decode_data(
                task_data.feature_selection.tile_coords(), provider.layer_name,
                provider.attribution, load_vector_data_pmtiles_package_task_data.package.value());
        }
        else if (is_error(load_vector_data_pmtiles_package_task.status))
        {
            task_data.load_vector_data_pmtiles_package_task.release_data();
            set_task_status(task_ref, task, load_vector_data_pmtiles_package_task.status);
        }
        else
        {
            set_task_status(task_ref, task, TaskStatus::Blocked);
        }
    }
    else
    {
        assert(false && "Unhandled case");
        set_task_status(task_ref, task, TaskStatus::ModelError);
    }
}

template<>
bool VectorDataLoader::unload_task_data_if_not_needed<VectorDataLoader::Task::LoadVectorTileData>()
{
    return true;
}

template<>
void VectorDataLoader::check_for_invalidated_data_for_task<
    VectorDataLoader::Task::LoadVectorTileData>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::LoadVectorTileData& task_data,
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
