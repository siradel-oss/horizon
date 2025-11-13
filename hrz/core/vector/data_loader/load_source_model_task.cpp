#include "hrz/core/loading_priorities.h"
#include "hrz/core/vector/data_loader/impl.h"
#include "hrz/fnd/variant.h"

namespace hrz
{
VectorDataLoader::TaskRef VectorDataLoader::get_or_create_load_source_model_task(
    const LayerModelRef& layer_model,
    uint32_t data_source)
{
    uint64_t hash = hrz::index_of_variant<decltype(Task::data), Task::LoadSourceModel>();
    hash = hrz::hash_mix<uint64_t>(hash, layer_model.get_handle().hash());
    hash = hrz::hash_mix<uint64_t>(hash, hrz::hash_value(data_source));

    auto it = tasks_by_hash.find(hash);
    if (it != tasks_by_hash.end() && it->second.is_valid())
    {
        const auto& task = it->second.value();
        if (task.is_load_source_model())
        {
            const auto& task_data = task.load_source_model();
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
    Task::LoadSourceModel task_data;
    task_data.layer_model = layer_model;
    task_data.data_source = data_source;
    task_data.load_tilejson_task = {};
    task_data.load_pmtiles_task = {};
    task_data.load_untiled_vector_data_task = {};
    new_task.data = std::move(task_data);
    new_task.hash = hash;

    tasks_by_hash.insert({new_task.hash, new_task_ref.make_weak_ref()});

    activate_task(new_task_ref.make_weak_ref());

    return new_task_ref;
}

template<>
void VectorDataLoader::clear_task<VectorDataLoader::Task::LoadSourceModel>(
    Task& task,
    Task::LoadSourceModel& task_data,
    JobScheduler* js)
{
    // No-op
}

template<>
void VectorDataLoader::cancel_task_jobs<VectorDataLoader::Task::LoadSourceModel>(
    Task& task,
    Task::LoadSourceModel& task_data,
    JobScheduler* js)
{
    // No-op
}

template<>
void VectorDataLoader::unload_task_data<VectorDataLoader::Task::LoadSourceModel>(
    Task& task,
    Task::LoadSourceModel& task_data,
    bool release_dependent_task_data,
    JobScheduler* js)
{
    if (release_dependent_task_data)
    {
        task_data.load_tilejson_task.release_data();
        task_data.load_pmtiles_task.release_data();
        task_data.load_untiled_vector_data_task.release_data();
    }
}

template<>
void VectorDataLoader::work_new_task<VectorDataLoader::Task::LoadSourceModel>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::LoadSourceModel& task_data,
    SceneModel* scene_model,
    AttributionRegistry* attributions)
{
    auto& layer_model = task_data.layer_model.value();
    auto& data_source = layer_model.data_sources.at(task_data.data_source);

    if (data_source.has_tiled_data_provider())
    {
        const auto& provider = data_source.tiled_data_provider();

        data_source.min_lod = {provider.min_lod};
        data_source.max_lod = {provider.max_lod};
        data_source.bounds = {provider.bounds};

        set_task_status(task_ref, task, TaskStatus::Loaded);
    }
    else if (data_source.has_client_data_provider())
    {
        const auto& provider = data_source.client_data_provider();

        if (provider.access == hrz_proto::VectorDataSourceAccess::ACCESS_BY_TILE)
        {
            data_source.min_lod = {provider.min_lod};
            data_source.max_lod = {provider.max_lod};
            data_source.bounds = {provider.bounds};
        }

        set_task_status(task_ref, task, TaskStatus::Loaded);
    }
    else if (data_source.has_in_memory_data_provider())
    {
        data_source.min_lod = {0};
        data_source.max_lod = {24};
        data_source.bounds = hrz::GeoBounds::full();

        set_task_status(task_ref, task, TaskStatus::Loaded);
    }
    else if (data_source.has_tilejson_data_provider())
    {
        const auto& provider = data_source.tilejson_data_provider();

        task_data.load_tilejson_task = TaskDependency::between_tasks(
            get_or_create_load_tilejson_task(
                provider.url, provider.headers, get_load_queue(layer_model),
                hrz::combine_loading_priorities(
                    layer_model.loading_priority, std::numeric_limits<uint16_t>::max()),
                provider.preserve_query_parameters,
                hrz::monitoring::ResourceOwner(
                    hrz::monitoring::systems::VectorDataLoader, layer_model.layer_handle),
                data_source.request_count_metric),
            task_ref);
        set_task_status(task_ref, task, TaskStatus::Unloaded);
    }
    else if (data_source.has_pmtiles_data_provider())
    {
        const auto& provider = data_source.pmtiles_data_provider();

        task_data.load_pmtiles_task = TaskDependency::between_tasks(
            get_or_create_load_pmtiles_task(
                provider.url, provider.headers, get_load_queue(layer_model),
                hrz::combine_loading_priorities(
                    layer_model.loading_priority, std::numeric_limits<uint16_t>::max()),
                hrz::monitoring::ResourceOwner(
                    hrz::monitoring::systems::VectorDataLoader, layer_model.layer_handle),
                data_source.request_count_metric),
            task_ref);
        set_task_status(task_ref, task, TaskStatus::Unloaded);
    }
    else if (data_source.has_untiled_data_provider())
    {
        const auto& provider = data_source.untiled_data_provider();

        data_source.min_lod = {provider.min_lod};
        data_source.max_lod = {provider.max_lod};
        data_source.bounds = {provider.bounds};

        task_data.load_untiled_vector_data_task = TaskDependency::between_tasks(
            get_or_create_load_untiled_vector_data_task(
                task_data.layer_model, task_data.data_source),
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
void VectorDataLoader::work_unloaded_task<VectorDataLoader::Task::LoadSourceModel>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::LoadSourceModel& task_data)
{
    task_data.load_tilejson_task.retain_data();
    task_data.load_pmtiles_task.retain_data();
    task_data.load_untiled_vector_data_task.retain_data();
}

template<>
void VectorDataLoader::work_loading_task<VectorDataLoader::Task::LoadSourceModel>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::LoadSourceModel& task_data,
    JobScheduler* js,
    BlobAllocator* ba,
    ClientMessageQueue* mq,
    AttributionRegistry* attributions)
{
    if (task_data.load_tilejson_task.has_task())
    {
        auto& load_tilejson_task = task_data.load_tilejson_task.get_task();
        if (load_tilejson_task.status == TaskStatus::Loaded)
        {
            const auto& load_tilejson_task_data = load_tilejson_task.load_tilejson();

            auto& layer_model = task_data.layer_model.value();
            auto& data_source = layer_model.data_sources.at(task_data.data_source);

            assert(data_source.has_tilejson_data_provider());
            auto& provider = data_source.tilejson_data_provider();

            data_source.min_lod = {load_tilejson_task_data.min_level};
            data_source.max_lod = {load_tilejson_task_data.max_level};
            data_source.bounds = {load_tilejson_task_data.bounds};

            std::array<AttributionHandle, 2> attribution_group = {
                provider.attribution, load_tilejson_task_data.attribution};
            provider.attribution =
                attribution::register_attribution_group(attributions, attribution_group);

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
        else if (is_error(load_tilejson_task.status))
        {
            task_data.load_tilejson_task.release_data();
            set_task_status(task_ref, task, load_tilejson_task.status);
        }
        else
        {
            set_task_status(task_ref, task, TaskStatus::Blocked);
        }
    }
    else if (task_data.load_pmtiles_task.has_task())
    {
        auto& load_pmtiles_task = task_data.load_pmtiles_task.get_task();
        if (load_pmtiles_task.status == TaskStatus::Loaded)
        {
            const auto& load_pmtiles_task_data = load_pmtiles_task.load_pmtiles();

            auto& layer_model = task_data.layer_model.value();
            auto& data_source = layer_model.data_sources.at(task_data.data_source);

            assert(data_source.has_pmtiles_data_provider());
            auto& provider = data_source.pmtiles_data_provider();

            const auto& geometry = load_pmtiles_task_data.pmtiles->get_geometry();

            data_source.min_lod = geometry.tiling_scheme.global_tiling().min_level();
            data_source.max_lod = geometry.tiling_scheme.global_tiling().max_level();

            data_source.bounds = hrz::web_mercator_bounds_to_geo(geometry.bounds);

            auto new_attribution = attribution::register_attribution(
                attributions, {load_pmtiles_task_data.pmtiles->get_attribution(), ""});
            std::array<AttributionHandle, 2> attribution_group = {
                provider.attribution, new_attribution};
            provider.attribution =
                attribution::register_attribution_group(attributions, attribution_group);

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
        else if (is_error(load_pmtiles_task.status))
        {
            task_data.load_pmtiles_task.release_data();
            set_task_status(task_ref, task, load_pmtiles_task.status);
        }
        else
        {
            set_task_status(task_ref, task, TaskStatus::Blocked);
        }
    }
    else if (task_data.load_untiled_vector_data_task.has_task())
    {
        auto& load_untiled_vector_data_task = task_data.load_untiled_vector_data_task.get_task();
        if (load_untiled_vector_data_task.status == TaskStatus::Loaded)
        {
            const auto& aabb_tree =
                load_untiled_vector_data_task.load_untiled_vector_data().aabb_tree;

            auto& layer_model = task_data.layer_model.value();
            auto& data_source = layer_model.data_sources.at(task_data.data_source);

            assert(data_source.has_untiled_data_provider());
            auto& provider = data_source.untiled_data_provider();

            data_source.bounds = {hrz::intersection(provider.bounds, aabb_tree.bounds)};
            if (data_source.bounds != provider.bounds)
            {
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
            }

            task_data.load_untiled_vector_data_task.release_data();
            set_task_status(task_ref, task, TaskStatus::Loaded);
        }
        else if (is_error(load_untiled_vector_data_task.status))
        {
            task_data.load_untiled_vector_data_task.release_data();
            set_task_status(task_ref, task, load_untiled_vector_data_task.status);
        }
        else
        {
            set_task_status(task_ref, task, TaskStatus::Blocked);
        }
    }
    else
    {
        set_task_status(task_ref, task, TaskStatus::Loaded);
    }
}

template<>
bool VectorDataLoader::unload_task_data_if_not_needed<VectorDataLoader::Task::LoadSourceModel>()
{
    return true;
}

template<>
void VectorDataLoader::check_for_invalidated_data_for_task<VectorDataLoader::Task::LoadSourceModel>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::LoadSourceModel& task_data,
    JobScheduler* js)
{
    // No-op
}
} // namespace hrz
