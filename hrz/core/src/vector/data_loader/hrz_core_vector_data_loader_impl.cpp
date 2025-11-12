#include "vector/data_loader/hrz_core_vector_data_loader_impl.h"

#include "hrz_core_loading_priorities.h"
#include "hrz_core_scene_model.h"

#include <hrz_common_blob_array.h>
#include <hrz_common_fmt.h>
#include <hrz_common_profiling.h>
#include <hrz_common_proto_geo.h>
#include <hrz_common_ui_utils.h>
#include <hrz_fnd_format.h>
#include <hrz_fnd_maths.h>
#include <hrz_fnd_meta.h>
#include <hrz_fnd_thread.h>
#include <hrz_fnd_time.h>
#include <hrz_protocol_path_builder.h>

#include <lin_maths.h>

#include <algorithm>
#include <type_traits>

namespace hrz
{
namespace in_memory = vector_data::in_memory;

std::string VectorDataLoader::make_url(
    const hrz::PatternTileUrlGenerator& tile_url_generator,
    hrz::TileCoords tile_coords)
{
    return tile_url_generator.make_url(tile_coords.x, tile_coords.y, tile_coords.lod);
}

std::string VectorDataLoader::make_url(
    const hrz::MultiPatternTileUrlGenerator& tile_url_generator,
    hrz::TileCoords tile_coords)
{
    return tile_url_generator.make_url(tile_coords.x, tile_coords.y, tile_coords.lod);
}

hrz::assets_loader::Queue VectorDataLoader::get_load_queue(const LayerModel& model)
{
    return hrz::get_request_queue(model.loading_priority, hrz::assets_loader::Queue::VectorData);
}

uint32_t VectorDataLoader::compute_tile_loading_priority(
    const LayerModel& model,
    hrz::TileCoords tile_coords)
{
    // Load high resolution tiles before low resolution ones.
    // High resolution tiles are normally closer to the camera, and roughly at the
    // bottom-centre of the screen (when the view is tilted), or at the centre (when
    // the view is straight down).
    // Low resolution tiles are more peripheral.
    return hrz::combine_loading_priorities(model.loading_priority, tile_coords.lod);
}

const char* VectorDataLoader::get_vector_data_provider_name(
    hrz_proto::VectorDataProviderType provider_type)
{
    switch (provider_type)
    {
        case hrz_proto::VectorDataProviderType::TILED_VECTOR_DATA_PROVIDER:
            return "Tiled vector data";
        case hrz_proto::VectorDataProviderType::CLIENT_VECTOR_DATA_PROVIDER:
            return "Client vector data";
        case hrz_proto::VectorDataProviderType::IN_MEMORY_VECTOR_DATA_PROVIDER:
            return "In-memory vector data";
        case hrz_proto::VectorDataProviderType::TILEJSON_VECTOR_DATA_PROVIDER:
            return "TileJSON vector data";
        case hrz_proto::VectorDataProviderType::UNTILED_VECTOR_DATA_PROVIDER:
            return "Untiled vector data";
        case hrz_proto::VectorDataProviderType::PMTILES_VECTOR_DATA_PROVIDER:
            return "PMTiles vector data";
        default: assert(false && "Unhandled case");
    }
    return "";
}

VectorDataLoader::LayerModel VectorDataLoader::make_model(
    AttributionRegistry* attributions,
    const hrz_proto::VectorDataLayer& layer,
    uint64_t layer_handle)
{
    LayerModel model;
    model.layer_handle = layer_handle;
    model.id = layer.id();
    model.data_version = 0;
    model.loading_priority = hrz::clamp_cast<int32_t, int8_t>(layer.loading_priority());
    model.geometry_source = LayerModel::NO_SOURCE;
    model.has_feature_ids = false;

    bool has_warned_about_multiple_sources_for_geometry = false;

    for (uint32_t s = 0; s < (uint32_t)layer.sources_size(); ++s)
    {
        const auto& source = layer.sources(s);

        LayerModel::Source model_source;

        if (source.provider_type() == hrz_proto::VectorDataProviderType::TILED_VECTOR_DATA_PROVIDER)
        {
            const auto& provider = source.tiled_data_provider();

            LayerModel::Source::TiledDataProvider model_provider;
            model_provider.tile_url_generator =
                hrz::PatternTileUrlGenerator(provider.url_pattern(), 1);
            model_provider.headers = hrz::assets_loader::from_proto(provider.http_headers());
            model_provider.format = provider.format();
            model_provider.layer_name = provider.layer_name();
            model_provider.min_lod = provider.min_level();
            model_provider.max_lod = provider.max_level();
            model_provider.bounds = hrz::from_proto(provider.bounds());
            model_provider.attribution =
                attribution::register_attribution(attributions, {provider.attribution(), ""});

            model_source.provider = {model_provider};

            model_source.request_count_metric.push_label("url_pattern", provider.url_pattern());
        }
        else if (
            source.provider_type()
            == hrz_proto::VectorDataProviderType::CLIENT_VECTOR_DATA_PROVIDER)
        {
            const auto& provider = source.client_data_provider();

            LayerModel::Source::ClientDataProvider model_provider;
            model_provider.access = provider.access();

            if (provider.timeout() != 0)
            {
                model_provider.timeout_duration = {provider.timeout() * 1000};
            }

            model_provider.min_lod = provider.min_level();
            model_provider.max_lod = provider.max_level();
            model_provider.bounds = hrz::from_proto(provider.bounds());

            model_source.provider = {model_provider};
        }
        else if (
            source.provider_type()
            == hrz_proto::VectorDataProviderType::IN_MEMORY_VECTOR_DATA_PROVIDER)
        {
            const auto& provider = source.in_memory_data_provider();

            LayerModel::Source::InMemoryDataProvider model_provider;
            model_provider.in_memory_layer_id = provider.in_memory_layer_id();

            model_source.provider = {model_provider};

            model_source.request_count_metric.push_label(
                "layer_id", fmt::format("{}", model_provider.in_memory_layer_id));
        }
        else if (
            source.provider_type()
            == hrz_proto::VectorDataProviderType::TILEJSON_VECTOR_DATA_PROVIDER)
        {
            const auto& provider = source.tilejson_data_provider();

            model_source.request_count_metric.push_label("url", provider.url());

            LayerModel::Source::TileJsonDataProvider model_provider;
            model_provider.url = provider.url();
            model_provider.headers = hrz::assets_loader::from_proto(provider.http_headers());
            model_provider.preserve_query_parameters = provider.preserve_query_parameters();
            model_provider.layer_name = provider.layer_name();
            model_provider.attribution =
                attribution::register_attribution(attributions, {provider.attribution(), ""});
            model_provider.load_tilejson_task =
                TaskDependency::on_task(get_or_create_load_tilejson_task(
                    provider.url(), model_provider.headers, get_load_queue(model),
                    hrz::combine_loading_priorities(
                        model.loading_priority, std::numeric_limits<uint16_t>::max()),
                    provider.preserve_query_parameters(),
                    hrz::monitoring::ResourceOwner(
                        hrz::monitoring::systems::VectorDataLoader, model.layer_handle),
                    model_source.request_count_metric));

            model_provider.load_tilejson_task.retain_data();

            model_source.provider = {std::move(model_provider)};
        }
        else if (
            source.provider_type()
            == hrz_proto::VectorDataProviderType::PMTILES_VECTOR_DATA_PROVIDER)
        {
            const auto& provider = source.pmtiles_data_provider();

            model_source.request_count_metric.push_label("url", provider.url());

            LayerModel::Source::PmTilesDataProvider model_provider;
            model_provider.url = provider.url();
            model_provider.headers = hrz::assets_loader::from_proto(provider.http_headers());
            model_provider.layer_name = provider.layer_name();
            model_provider.attribution =
                attribution::register_attribution(attributions, {provider.attribution(), ""});
            model_provider.load_pmtiles_task =
                TaskDependency::on_task(get_or_create_load_pmtiles_task(
                    provider.url(), model_provider.headers, get_load_queue(model),
                    hrz::combine_loading_priorities(
                        model.loading_priority, std::numeric_limits<uint16_t>::max()),
                    hrz::monitoring::ResourceOwner(
                        hrz::monitoring::systems::VectorDataLoader, model.layer_handle),
                    model_source.request_count_metric));

            model_provider.load_pmtiles_task.retain_data();

            model_source.provider = {std::move(model_provider)};
        }
        else if (
            source.provider_type()
            == hrz_proto::VectorDataProviderType::UNTILED_VECTOR_DATA_PROVIDER)
        {
            const auto& provider = source.untiled_data_provider();

            LayerModel::Source::UntiledDataProvider model_provider;
            model_provider.url = provider.url();
            model_provider.headers = hrz::assets_loader::from_proto(provider.http_headers());
            model_provider.format = provider.format();
            model_provider.tolerance = provider.tolerance();
            model_provider.clip_margin = provider.clip_margin();
            model_provider.min_lod = provider.min_level();
            model_provider.max_lod = provider.max_level();
            model_provider.bounds = hrz::from_proto(provider.bounds());
            model_provider.attribution =
                attribution::register_attribution(attributions, {provider.attribution(), ""});

            model_source.provider = {model_provider};

            model_source.request_count_metric.push_label("url", provider.url());
        }
        else
        {
            assert(false && "Unhandled case");
        }

        model_source.has_geometry = source.has_geometry();
        model_source.source_feature_id_attribute = std::nullopt;
        model_source.request_count_metric = metrics::MetricDesc(
            "Vector data (requests tally)", false,
            {{"layer", std::to_string(layer_handle)},
             {"source", std::to_string(s)},
             {"format", get_vector_data_provider_name(source.provider_type())}});

        if (model_source.has_geometry)
        {
            if (model.geometry_source == LayerModel::NO_SOURCE)
            {
                model.geometry_source = s;
            }
            else if (!has_warned_about_multiple_sources_for_geometry)
            {
                HRZ_LOG_WARNING(
                    "Multiple data sources with geometry. Only the first one will be used.");
                has_warned_about_multiple_sources_for_geometry = true;
            }
        }

        model_source.join_type = s == LayerModel::PRIMARY_SOURCE
            ? LayerModel::JoinType::None
            : LayerModel::JoinType::MatchFeatureCount;

        for (uint32_t a = 0; a < (uint32_t)source.attributes_size(); ++a)
        {
            const auto& attribute = source.attributes(a);

            LayerModel::Attribute model_attribute;
            model_attribute.id = attribute.id();
            model_attribute.transform = attribute.transform();
            model_attribute.is_source_feature_ids = attribute.is_source_feature_ids();
            model_attribute.name_in_source = attribute.source_name();
            model_attribute.is_feature_id = attribute.is_feature_id();
            model_attribute.data_source = s;

            if (model_attribute.is_source_feature_ids)
            {
                if (!model_source.source_feature_id_attribute.has_value())
                {
                    model_source.source_feature_id_attribute = {model_attribute.id};
                }
                else
                {
                    HRZ_LOG_WARNING(
                        "Multiple attributes declared as source feature ID in source {} of "
                        "layer {}: {} and {}",
                        s, model.id, model_source.source_feature_id_attribute.value(),
                        model_attribute.id);
                }
            }

            if (model_attribute.is_feature_id)
            {
                if (s == LayerModel::PRIMARY_SOURCE)
                {
                    model.has_feature_ids = true;
                }
                else
                {
                    bool found = false;
                    for (const auto& it : model.attributes)
                    {
                        if (it.first == model_attribute.id && model_attribute.is_feature_id)
                        {
                            found = true;
                            break;
                        }
                    }

                    if (!found)
                    {
                        HRZ_LOG_WARNING(
                            "Attribute {}, declared as feature ID in source {} of layer {}, "
                            "has no counterpart in the first source",
                            model_attribute.id, s, model.id);

                        model_attribute.is_feature_id = false;
                    }

                    model_source.join_type = LayerModel::JoinType::SortByFeatureIds;
                }
            }
            else if (model.attributes.find(model_attribute.id) != model.attributes.end())
            {
                HRZ_LOG_WARNING(
                    "Duplicate attribute ID {} in source {} of layer {}", model_attribute.id, s,
                    model.id);
            }

            model_source.attributes.insert({model_attribute.id, std::move(model_attribute)});
        }

        if (model_source.join_type == LayerModel::JoinType::SortByFeatureIds)
        {
            // If a secondary source is joined on feature IDs,
            // check that it has all feature ID attributes.
            for (const auto& it : model.attributes)
            {
                const auto& attribute_id = it.first;

                if (it.second == LayerModel::PRIMARY_SOURCE)
                {
                    const auto& attribute =
                        model.data_sources[LayerModel::PRIMARY_SOURCE].attributes[attribute_id];

                    if (attribute.is_feature_id)
                    {
                        const auto& attribute_it = model_source.attributes.find(attribute_id);

                        if (attribute_it == model_source.attributes.end())
                        {
                            HRZ_LOG_ERROR(
                                "Feature ID attribute {} is missing from source {} of layer {}",
                                attribute_id, s, model.id);
                        }
                        else if (!attribute_it->second.is_feature_id)
                        {
                            HRZ_LOG_ERROR(
                                "Attribute {} from source {} of layer {} is not marked as "
                                "feature ID, when it is in the first source",
                                attribute_id, s, model.id);
                        }
                    }
                }
            }

            if (!model.has_feature_ids)
            {
                model_source.join_type = LayerModel::JoinType::MatchFeatureCount;
            }
        }

        for (const auto& it : model_source.attributes)
        {
            // Source feature IDs can be duplicated among multiple sources
            // (this is necessary to perform joins by feature ID), but we
            // only want them to be retrieved from the primary source.
            if (s == LayerModel::PRIMARY_SOURCE || !it.second.is_feature_id)
            {
                model.attributes.insert({it.first, (uint32_t)model.data_sources.size()});
            }
        }

        assert(
            !(s == LayerModel::PRIMARY_SOURCE
              && model_source.join_type != LayerModel::JoinType::None));
        assert(
            !(s != LayerModel::PRIMARY_SOURCE
              && model_source.join_type == LayerModel::JoinType::None));

        model.data_sources.push_back(std::move(model_source));
    }

    return model;
}

void VectorDataLoader::load_attribute_data_into_map(
    std::vector<vector_data::AttributeValues>& attributes,
    hrz::flat_hash_map<uint32_t, AttributeValueListRef>& attribute_ids_to_values,
    const LayerModel::Source& data_source,
    size_t expected_value_count)
{
    for (size_t a = 0; a < attributes.size(); ++a)
    {
        auto& attribute = attributes[a];

        {
            const auto& it = data_source.attributes.find(attribute.attribute_id);

            if (it == data_source.attributes.end())
            {
                // Unknown attribute
                continue;
            }
        }

        assert(attribute.values.size() == expected_value_count);
        if (attribute.values.size() != expected_value_count)
        {
            continue;
        }

        auto values_ref = attribute_values.alloc();
        auto& values = values_ref.value();
        values.attribute_id = attribute.attribute_id;
        values.values = std::move(attribute.values);
        values.out_of_line_data = std::move(attribute.out_of_line_data);

        attribute_ids_to_values.insert_or_assign(attribute.attribute_id, values_ref);
    }
}

VectorDataLoader::DataRequest VectorDataLoader::create_data_request(
    RequestId request_id,
    RequestId layer_loader_request_id,
    FeatureSelection feature_selection,
    hrz::vector_data::DataKind data_kind)
{
    auto layer_model_ref = get_layer_model_for_loaded_layer(layer_loader_request_id);
    if (!layer_model_ref.has_value())
    {
        return make_data_request(
            request_id, layer_loader_request_id, std::numeric_limits<uint32_t>::max(),
            std::move(feature_selection), data_kind);
    }

    auto& layer_model = layer_model_ref.value();

    DataRequest request = make_data_request(
        request_id, layer_loader_request_id, layer_model.id, std::move(feature_selection),
        data_kind);

    if (request.feature_selection.has_tile_coords())
    {
        const auto& tile_coords = request.feature_selection.tile_coords();
        auto tile_bounds = hrz::mercator_tile_bounds(tile_coords);

        auto layer_bounds = get_bounds(layer_model);
        auto layer_min_lod = get_min_lod(layer_model);
        auto layer_max_lod = get_max_lod(layer_model);

        if (!hrz::intersect(tile_bounds, layer_bounds) || tile_coords.lod < layer_min_lod
            || tile_coords.lod > layer_max_lod)
        {
            HRZ_LOG_WARNING(
                "Out-of-bounds request for vector data layer {}: {}", layer_model.id, tile_coords);
            return request;
        }
    }

    TaskRef task_ref;

    switch (data_kind)
    {
        case hrz::vector_data::DataKind::AttributeValues:
            task_ref =
                get_or_create_load_all_attributes_task(layer_model_ref, request.feature_selection);
            break;
        case hrz::vector_data::DataKind::Geometry:
            task_ref = get_or_create_load_geometry_task(layer_model_ref, request.feature_selection);
            break;
        case hrz::vector_data::DataKind::FeatureIds:
            task_ref =
                get_or_create_load_feature_ids_task(layer_model_ref, request.feature_selection);
            break;
        default:
            assert(false && "Unhandled case");
            task_ref = TaskRef{};
            break;
    }

    request.task = TaskDependency::on_task(task_ref);
    request.task.retain_data();
    request.data_version = task_ref.value().version;

    return request;
}

VectorDataLoader* VectorDataLoader::create(
    AssetsLoader* al,
    InMemoryVectorDataBase* in_memory_database)
{
    auto loader = new VectorDataLoader();
    loader->asset_loader_channel = assets_loader::create_channel(al);
    loader->in_memory_vector_data_channel = in_memory::create_channel(in_memory_database);

    return loader;
}

void VectorDataLoader::destroy(JobScheduler* js)
{
    request_ids_to_layer_loaders.clear();
    request_ids_to_data_requests.clear();
    tasks_to_data_request_ids.clear();

    for (auto ref : tasks)
    {
        clear_task(ref.value(), js);
    }

    asset_loader_channel.close();
    in_memory_vector_data_channel.close();
}

void VectorDataLoader::register_layer(SceneModel* scene_model, uint64_t layer_handle)
{
    assert(scene_model);
    HRZ_SCOPED_LOCK(model_mutex);

    if (layer_handles_to_layer_ids.find(layer_handle) != layer_handles_to_layer_ids.end())
    {
        // Layer already registered.
        return;
    }

    hrz_proto::PathRoot root;
    root.mutable_vector_data_layer()->set_opaque(layer_handle);
    scene_model::register_element(scene_model, root);

    // Default data
    uint32_t layer_id = 0;
    hrz_proto::VectorDataLayer layer;
    layer.set_id(layer_id);

    hrz_proto::VectorDataLayerPathBuilder<hrz::SceneModelAccessor>(
        scene_model, root.vector_data_layer())
        .set(layer);

    layer_handles_to_layer_ids.insert({layer_handle, layer_id});
    created_model_layers.insert(layer_handle);
}

void VectorDataLoader::unregister_layer(uint64_t layer_handle)
{
    HRZ_SCOPED_LOCK(model_mutex);

    if (layer_handles_to_layer_ids.find(layer_handle) == layer_handles_to_layer_ids.end())
    {
        return;
    }

    destroyed_model_layers.insert(layer_handle);
    updated_model_layers.erase(layer_handle);
    updated_headers_model_layers.erase(layer_handle);
}

void VectorDataLoader::notify_update(
    uint64_t layer_handle,
    scene_model::UpdateType,
    const scene_model::VectorDataLayerPath& path)
{
    HRZ_SCOPED_LOCK(model_mutex);

    if (layer_handles_to_layer_ids.find(layer_handle) == layer_handles_to_layer_ids.end())
    {
        return;
    }

    bool must_update_all = true;

    if (path.is_sources() && path.has_sources_index())
    {
        auto source = path.clone().sources();
        if ((source.is_tiled_data_provider() && source.tiled_data_provider().is_http_headers())
            || (source.is_tilejson_data_provider()
                && source.tilejson_data_provider().is_http_headers()))
        {
            updated_headers_model_layers.insert({layer_handle, path.sources_index()});
            must_update_all = false;
        }
    }

    if (must_update_all)
    {
        updated_model_layers.insert(layer_handle);
        updated_headers_model_layers.erase(layer_handle);
    }
}

void VectorDataLoader::load_layer(RequestId request_id, uint32_t layer_id)
{
    auto it = request_ids_to_layer_loaders.find(request_id);
    if (it != request_ids_to_layer_loaders.end())
    {
        HRZ_LOG_ERROR("Duplicate request ID for load layer request");
        return;
    }

    LayerLoader loader;
    loader.request_id = request_id;
    loader.layer_id = layer_id;
    loader.load_model_task = {};
    loader.has_full_model_update = false;
    loader.has_max_lod_update = false;
    loader.data_version = 0;

    loader.load_model_task = TaskDependency::on_task(get_or_create_load_layer_model_task(layer_id));
    loader.load_model_task.retain_data();

    if (loader.load_model_task.get_task().status == TaskStatus::Loaded)
    {
        // The model is already loaded, signal this to the requester.
        loader.has_full_model_update = true;
        send_layer_model_message(
            loader, loader.load_model_task.get_task().load_layer_model().layer_model.value());
    }

    request_ids_to_layer_loaders.insert({request_id, std::move(loader)});
    layer_ids_to_request_ids.insert({layer_id, request_id});
}

void VectorDataLoader::release_layer_loader(RequestId request_id)
{
    auto it_loader = request_ids_to_layer_loaders.find(request_id);
    if (it_loader == request_ids_to_layer_loaders.end()) return;

    auto iterpair = layer_ids_to_request_ids.equal_range(it_loader->second.layer_id);
    for (auto it_request = iterpair.first; it_request != iterpair.second;)
    {
        if (it_request->second == request_id)
        {
            it_request = layer_ids_to_request_ids.erase(it_request);
        }
        else
        {
            ++it_request;
        }
    }

    request_ids_to_layer_loaders.erase(it_loader);
}

void VectorDataLoader::send_layer_model_message(LayerLoader& loader, const LayerModel& layer_model)
{
    auto it = channels.find(loader.request_id.channel_id);
    if (it != channels.end())
    {
        auto& channel = it->second;
        channel.send(vector_data::messages::LayerModelUpdate{
            loader.request_id.request_id, get_min_lod(layer_model), get_max_lod(layer_model),
            get_bounds(layer_model), get_attribute_ids(layer_model)});
    }
}

void VectorDataLoader::send_layer_model_error_message(LayerLoader& loader)
{
    auto it = channels.find(loader.request_id.channel_id);
    if (it != channels.end())
    {
        auto& channel = it->second;
        channel.send(vector_data::messages::LayerModelError{loader.request_id.request_id});
    }
}

void VectorDataLoader::send_layer_has_new_data_message(LayerLoader& loader)
{
    auto it = channels.find(loader.request_id.channel_id);
    if (it != channels.end())
    {
        auto& channel = it->second;
        channel.send(vector_data::messages::LayerNewData{loader.request_id.request_id});
    }
}

void VectorDataLoader::send_data_message(
    DataRequest& request,
    std::variant<
        TileGeometry,
        hrz::InlinedVector<vector_data::AttributeValues, 16>,
        vector_data::FeatureIds> data,
    AttributionHandle attribution)
{
    auto it = channels.find(request.request_id.channel_id);
    if (it != channels.end())
    {
        auto& channel = it->second;
        channel.send(vector_data::messages::DataUpdate{
            request.request_id.request_id,
            {std::move(data)},
            attribution});
    }
}

void VectorDataLoader::send_geometry_message(WeakTaskRef& task_ref, Task& task)
{
    assert(task.is_load_geometry());
    assert(task.status == TaskStatus::Loaded);

    auto& task_data = task.load_geometry();

    auto iterpair = tasks_to_data_request_ids.equal_range(task_ref);
    for (auto it = iterpair.first; it != iterpair.second; ++it)
    {
        auto& request = request_ids_to_data_requests.at(it->second);
        send_data_message(request, task_data.geometry.value(), task_data.attribution);
    }
}

void VectorDataLoader::send_attribute_values_message(WeakTaskRef& task_ref, Task& task)
{
    assert(task.is_load_all_attribute_values());
    assert(task.status == TaskStatus::Loaded);

    auto& task_data = task.load_all_attribute_values();

    auto iterpair = tasks_to_data_request_ids.equal_range(task_ref);
    for (auto it = iterpair.first; it != iterpair.second; ++it)
    {
        auto& request = request_ids_to_data_requests.at(it->second);

        hrz::InlinedVector<vector_data::AttributeValues, 16> attribute_values_to_send;
        attribute_values_to_send.reserve(task_data.attribute_tasks.size());

        for (auto& attribute_task_ref : task_data.attribute_tasks)
        {
            auto& attribute_task = attribute_task_ref.get_task();
            assert(attribute_task.status == TaskStatus::Loaded);
            attribute_values_to_send.push_back(
                attribute_task.load_attribute_values().attribute_values.value());
        }

        send_data_message(request, std::move(attribute_values_to_send), task_data.attribution);
    }
}

void VectorDataLoader::send_feature_ids_message(WeakTaskRef& task_ref, Task& task)
{
    assert(task.is_load_feature_ids());
    assert(task.status == TaskStatus::Loaded);

    auto& task_data = task.load_feature_ids();

    auto iterpair = tasks_to_data_request_ids.equal_range(task_ref);
    for (auto it = iterpair.first; it != iterpair.second; ++it)
    {
        auto& request = request_ids_to_data_requests.at(it->second);
        send_data_message(request, task_data.feature_ids.value(), {});
    }
}

void VectorDataLoader::send_data_error_message(DataRequest& request)
{
    auto it = channels.find(request.request_id.channel_id);
    if (it != channels.end())
    {
        auto& channel = it->second;
        channel.send(vector_data::messages::DataError{request.request_id.request_id});
    }
}

VectorDataLoader::LayerModelRef VectorDataLoader::get_layer_model_for_loaded_layer(
    LayerLoader& layer_loader)
{
    const auto& load_layer_model_task = layer_loader.load_model_task.get_task();

    if (load_layer_model_task.status != TaskStatus::Loaded)
    {
        HRZ_LOG_ERROR("Layer {} not loaded", layer_loader.layer_id);
        return {};
    }

    assert(load_layer_model_task.is_load_layer_model());
    const auto& task_data = load_layer_model_task.load_layer_model();

    if (!task_data.layer_model.has_value())
    {
        assert(false);
        HRZ_LOG_ERROR("Model for layer {} not found", layer_loader.layer_id);
        return {};
    }

    return task_data.layer_model;
}

VectorDataLoader::LayerModelRef VectorDataLoader::get_layer_model_for_loaded_layer(
    RequestId layer_loader_request_id)
{
    auto loader_it = request_ids_to_layer_loaders.find(layer_loader_request_id);
    if (loader_it == request_ids_to_layer_loaders.end())
    {
        HRZ_LOG_ERROR(
            "Invalid layer loader request ID: {}-{}", layer_loader_request_id.channel_id,
            layer_loader_request_id.request_id);
        return {};
    }

    return get_layer_model_for_loaded_layer(loader_it->second);
}

uint32_t VectorDataLoader::get_min_lod(const LayerModel& layer_model)
{
    uint32_t min_lod = 0;

    for (const auto& data_source : layer_model.data_sources)
    {
        if (data_source.min_lod.has_value())
        {
            min_lod = std::max(min_lod, data_source.min_lod.value());
        }
    }

    return min_lod;
}

uint32_t VectorDataLoader::get_max_lod(const LayerModel& layer_model)
{
    uint32_t max_lod = std::numeric_limits<uint8_t>::max();

    for (const auto& data_source : layer_model.data_sources)
    {
        if (data_source.max_lod.has_value())
        {
            max_lod = std::min(max_lod, data_source.max_lod.value());
        }
    }

    return max_lod;
}

hrz::InlinedVector<uint32_t, 16> VectorDataLoader::get_attribute_ids(const LayerModel& layer_model)
{
    hrz::InlinedVector<uint32_t, 16> ids;
    for (const auto& attrib : layer_model.attributes)
    {
        ids.push_back(attrib.first);
    }

    return ids;
}

GeoBounds VectorDataLoader::get_bounds(const LayerModel& layer_model)
{
    auto bounds = GeoBounds::empty();

    for (const auto& data_source : layer_model.data_sources)
    {
        if (data_source.bounds.has_value())
        {
            if (bounds.is_empty())
            {
                bounds = data_source.bounds.value();
            }
            else
            {
                bounds = hrz::intersection(bounds, data_source.bounds.value());
            }
        }
    }

    return bounds;
}

void VectorDataLoader::request_data_for_selection(
    RequestId request_id,
    RequestId layer_loader_request_id,
    FeatureSelection feature_selection,
    DataKind data_kind)
{
    auto request = create_data_request(
        request_id, layer_loader_request_id, std::move(feature_selection), data_kind);

    if (!request.task.has_task())
    {
        send_data_error_message(request);
        request_ids_to_data_requests.insert({request_id, std::move(request)});
        return;
    }

    auto task_ref = request.task.get_task_ref().make_weak_ref();
    auto& task = request.task.get_task();

    tasks_to_data_request_ids.insert({request.task.get_task_ref().make_weak_ref(), request_id});

    if (task.status == TaskStatus::DataError || task.status == TaskStatus::ModelError)
    {
        send_data_error_message(request);
    }

    request_ids_to_data_requests.insert({request_id, std::move(request)});

    if (task.status == TaskStatus::Loaded)
    {
        switch (data_kind)
        {
            case DataKind::Geometry:
            {
                send_geometry_message(task_ref, task);
                break;
            }
            case DataKind::AttributeValues:
            {
                send_attribute_values_message(task_ref, task);
                break;
            }
            case DataKind::FeatureIds:
            {
                send_feature_ids_message(task_ref, task);
                break;
            }
            default: assert(false && "Unhandled case"); break;
        }
    }
}

void VectorDataLoader::request_data(
    RequestId request_id,
    RequestId layer_loader_request_id,
    TileCoords tile_coords,
    DataKind data_kind)
{
    request_data_for_selection(
        request_id, layer_loader_request_id, FeatureSelection{tile_coords}, data_kind);
}

void VectorDataLoader::request_data(
    RequestId request_id,
    RequestId layer_loader_request_id,
    const hrz::vector_data::FeatureIds& feature_ids,
    DataKind data_kind)
{
    auto feature_id_list_ref = feature_id_lists.alloc();
    feature_id_list_ref.value() = feature_ids;

    request_data_for_selection(
        request_id, layer_loader_request_id, FeatureSelection{feature_id_list_ref}, data_kind);
}

void VectorDataLoader::release_data(RequestId request_id)
{
    auto request_it = request_ids_to_data_requests.find(request_id);
    if (request_it == request_ids_to_data_requests.end())
    {
        HRZ_LOG_ERROR(
            "Invalid data request ID: {}-{}", request_id.channel_id, request_id.request_id);
        return;
    }

    auto& request = request_it->second;

    if (!request.task.is_data_retained()) return;

    auto& task_ref = request.task;

    if (!task_ref.has_task())
    {
        HRZ_LOG_ERROR(
            "No data to release for request ID {}-{}", request_id.channel_id,
            request_id.request_id);
        return;
    }

    task_ref.release_data();
}

void VectorDataLoader::retain_data(RequestId request_id)
{
    auto request_it = request_ids_to_data_requests.find(request_id);
    if (request_it == request_ids_to_data_requests.end())
    {
        HRZ_LOG_ERROR(
            "Invalid data request ID: {}-{}", request_id.channel_id, request_id.request_id);
        return;
    }

    auto& request = request_it->second;

    if (request.task.is_data_retained()) return;

    auto& task_ref = request.task;

    if (!task_ref.has_task())
    {
        HRZ_LOG_ERROR(
            "No data to reload for request ID {}-{}", request_id.channel_id, request_id.request_id);
        return;
    }

    task_ref.retain_data();
}

void VectorDataLoader::release_request(RequestId request_id)
{
    auto request_it = request_ids_to_data_requests.find(request_id);
    if (request_it == request_ids_to_data_requests.end())
    {
        return;
    }

    auto& request = request_it->second;

    auto iterpair =
        tasks_to_data_request_ids.equal_range(request.task.get_task_ref().make_weak_ref());
    for (auto it = iterpair.first; it != iterpair.second;)
    {
        if (it->second == request_id)
        {
            it = tasks_to_data_request_ids.erase(it);
        }
        else
        {
            ++it;
        }
    }

    request_ids_to_data_requests.erase(request_it);
}

void VectorDataLoader::visit_load_layer_models_for_layer_id(
    uint32_t layer_id,
    const std::function<void(LayerModel&)>& callback)
{
    auto iterpair = layer_ids_to_request_ids.equal_range(layer_id);
    for (auto it = iterpair.first; it != iterpair.second; ++it)
    {
        auto request_id = it->second;
        auto loader_it = request_ids_to_layer_loaders.find(request_id);
        if (loader_it != request_ids_to_layer_loaders.end())
        {
            auto& loader = loader_it->second;
            auto& load_layer_task_data = loader.load_model_task.get_task().load_layer_model();
            callback(*load_layer_task_data.layer_model);
        }
    }
}

// Returns whether content negotiation headers have changed.
bool VectorDataLoader::update_source_headers(
    uint64_t layer_handle,
    uint32_t layer_id,
    uint32_t source_index,
    SceneModel* scene_model)
{
    bool content_negotiation_changed = false;
    auto iterpair = layer_ids_to_request_ids.equal_range(layer_id);
    for (auto it = iterpair.first; it != iterpair.second; ++it)
    {
        auto request_id = it->second;
        auto loader_it = request_ids_to_layer_loaders.find(request_id);
        if (loader_it == request_ids_to_layer_loaders.end()) continue;

        auto& loader = loader_it->second;

        auto& model_opt = loader.load_model_task.get_task().load_layer_model().layer_model;
        if (!model_opt.has_value()) continue;

        auto& model = model_opt.value();

        hrz_proto::LayerHandle handle;
        handle.set_opaque(layer_handle);
        hrz_proto::VectorDataLayerPathBuilder<hrz::SceneModelAccessor> builder(scene_model, handle);

        auto& data_source = model.data_sources.at(source_index);
        if (data_source.has_tiled_data_provider())
        {
            auto new_headers_proto =
                builder.clone().sources(source_index).tiled_data_provider().http_headers().get();
            auto new_headers = hrz::assets_loader::from_proto(new_headers_proto);
            content_negotiation_changed = data_source.tiled_data_provider().headers.hash_content()
                    != new_headers.hash_content()
                || content_negotiation_changed;
            data_source.tiled_data_provider().headers = new_headers;

            visit_load_layer_models_for_layer_id(
                model.id,
                [&](LayerModel& load_layer_model)
                {
                    auto& source = load_layer_model.data_sources.at(source_index);
                    if (source.has_tiled_data_provider())
                    {
                        source.tiled_data_provider().headers = new_headers;
                    }
                });
        }
        else if (data_source.has_tilejson_data_provider())
        {
            auto new_headers_proto =
                builder.clone().sources(source_index).tilejson_data_provider().http_headers().get();
            auto new_headers = hrz::assets_loader::from_proto(new_headers_proto);
            content_negotiation_changed =
                data_source.tilejson_data_provider().headers.hash_content()
                    != new_headers.hash_content()
                || content_negotiation_changed;
            data_source.tilejson_data_provider().headers = new_headers;

            visit_load_layer_models_for_layer_id(
                model.id,
                [&](LayerModel& load_layer_model)
                {
                    auto& source = load_layer_model.data_sources.at(source_index);
                    if (source.has_tilejson_data_provider())
                    {
                        source.tilejson_data_provider().headers = new_headers;
                    }
                });
        }
        else if (data_source.has_pmtiles_data_provider())
        {
            auto new_headers_proto =
                builder.clone().sources(source_index).pmtiles_data_provider().http_headers().get();
            auto& provider = data_source.pmtiles_data_provider();

            auto new_headers = hrz::assets_loader::from_proto(new_headers_proto);

            content_negotiation_changed =
                provider.headers.hash_content() != new_headers.hash_content()
                || content_negotiation_changed;
            if (provider.load_pmtiles_task.has_task())
            {
                auto& pmtiles_task = provider.load_pmtiles_task.get_task().load_pmtiles();
                pmtiles_task.headers = new_headers;
                if (pmtiles_task.pmtiles)
                {
                    content_negotiation_changed =
                        pmtiles_task.pmtiles->set_http_headers(new_headers)
                        || content_negotiation_changed;
                }
            }

            provider.headers = new_headers;

            visit_load_layer_models_for_layer_id(
                model.id,
                [&](LayerModel& load_layer_model)
                {
                    auto& source = load_layer_model.data_sources.at(source_index);
                    if (source.has_pmtiles_data_provider())
                    {
                        auto& provider = source.pmtiles_data_provider();
                        provider.headers = new_headers;
                        if (provider.load_pmtiles_task.has_task())
                        {
                            auto& pmtiles_task =
                                provider.load_pmtiles_task.get_task().load_pmtiles();
                            pmtiles_task.headers = new_headers;
                            if (pmtiles_task.pmtiles)
                            {
                                pmtiles_task.pmtiles->set_http_headers(new_headers);
                            }
                        }
                    }
                });
        }
    }
    return content_negotiation_changed;
}

void VectorDataLoader::update_layers_from_model(SceneModel* scene_model)
{
    HRZ_SCOPED_SAMPLE_A("update layers from model");

    HRZ_SCOPED_LOCK(model_mutex);

    for (uint64_t layer_handle : created_model_layers)
    {
        auto it = layer_handles_to_layer_ids.find(layer_handle);
        if (it == layer_handles_to_layer_ids.end()) continue;

        uint32_t layer_id = it->second;

        if (layer_ids_to_active_layer_handles.find(layer_id)
            == layer_ids_to_active_layer_handles.end())
        {
            // No layer with this id is active.
            layer_ids_to_active_layer_handles.insert({layer_id, layer_handle});
            updated_model_layers.insert(layer_handle);
        }
    }

    // Activates a new one if a another model with the same id exists.
    auto deactivate_model_layer_at_id = [&](uint64_t model_layer_handle, uint32_t layer_id)
    {
        if (layer_ids_to_active_layer_handles.at(layer_id) == model_layer_handle)
        {
            // The model layer was active, find another one.
            bool new_active_layer_found = false;

            for (auto& it : layer_handles_to_layer_ids)
            {
                uint64_t replacement_layer_handle = it.first;
                uint32_t replacement_layer_id = it.second;
                if (replacement_layer_id == layer_id)
                {
                    new_active_layer_found = true;
                    layer_ids_to_active_layer_handles[layer_id] = replacement_layer_handle;
                    break;
                }
            }

            if (!new_active_layer_found)
            {
                layer_ids_to_active_layer_handles.erase(layer_id);
            }

            updated_layers.insert(layer_id);
        }
    };

    for (uint64_t layer_handle : destroyed_model_layers)
    {
        auto it = layer_handles_to_layer_ids.find(layer_handle);
        if (it == layer_handles_to_layer_ids.end()) continue;

        uint32_t layer_id = it->second;

        layer_handles_to_layer_ids.erase(it);

        deactivate_model_layer_at_id(layer_handle, layer_id);
    }

    for (auto update_headers_it : updated_headers_model_layers)
    {
        uint64_t layer_handle = update_headers_it.first;

        auto layer_it = layer_handles_to_layer_ids.find(layer_handle);
        if (layer_it == layer_handles_to_layer_ids.end()) continue;

        uint32_t layer_id = layer_it->second;

        auto active_layer_it = layer_ids_to_active_layer_handles.find(layer_id);
        if (active_layer_it == layer_ids_to_active_layer_handles.end()) continue;

        uint32_t source_index = update_headers_it.second;

        if (update_source_headers(layer_handle, layer_id, source_index, scene_model))
        {
            updated_model_layers.insert(layer_handle);
        }
    }

    for (uint64_t layer_handle : updated_model_layers)
    {
        auto layer_it = layer_handles_to_layer_ids.find(layer_handle);
        if (layer_it == layer_handles_to_layer_ids.end()) continue;

        uint32_t previous_layer_id = layer_it->second;

        hrz_proto::LayerHandle handle;
        handle.set_opaque(layer_handle);

        uint32_t new_layer_id =
            hrz_proto::VectorDataLayerPathBuilder<hrz::SceneModelAccessor>(scene_model, handle)
                .id()
                .get();

        if (new_layer_id == previous_layer_id)
        {
            auto layer_id = previous_layer_id;

            if (layer_ids_to_active_layer_handles.at(layer_id) == layer_handle)
            {
                // The layer was, and it still is, active.
                updated_layers.insert(layer_id);
            }
        }
        else
        {
            layer_handles_to_layer_ids[layer_handle] = new_layer_id;

            deactivate_model_layer_at_id(layer_handle, previous_layer_id);

            if (layer_ids_to_active_layer_handles.find(new_layer_id)
                == layer_ids_to_active_layer_handles.end())
            {
                // No active layer for the new id, so activate the updated model layer.
                layer_ids_to_active_layer_handles.insert({new_layer_id, layer_handle});
                updated_layers.insert(new_layer_id);
            }
        }
    }

    created_model_layers.clear();
    destroyed_model_layers.clear();
    updated_model_layers.clear();
    updated_headers_model_layers.clear();

    for (uint32_t layer_id : updated_layers)
    {
        for (auto& it : request_ids_to_data_requests)
        {
            const auto& request_id = it.first;
            auto& request = it.second;

            if (request.layer_id == layer_id)
            {
                auto iterpair = tasks_to_data_request_ids.equal_range(
                    request.task.get_task_ref().make_weak_ref());
                for (auto it = iterpair.first; it != iterpair.second;)
                {
                    if (it->second == request_id)
                    {
                        it = tasks_to_data_request_ids.erase(it);
                    }
                    else
                    {
                        ++it;
                    }
                }

                request.task.release();

                request = create_data_request(
                    request.request_id, request.layer_loader_request_id,
                    std::move(request.feature_selection), request.data_kind);
                tasks_to_data_request_ids.insert(
                    {request.task.get_task_ref().make_weak_ref(), request_id});
            }
        }
    }

    if (!updated_layers.empty())
    {
        hrz::flat_hash_set<RequestId> updated_loaders;

        for (auto& it : request_ids_to_layer_loaders)
        {
            auto request_id = it.first;
            auto& loader = it.second;

            if (updated_layers.find(loader.layer_id) != updated_layers.end())
            {
                // Load layer model tasks must be removed from the hash-to-task
                // map, so that when new tasks are created, the old ones are not
                // reused, even though they are for the same layer IDs.

                tasks_by_hash.erase(loader.load_model_task.get_task().hash);

                updated_loaders.insert(request_id);
            }
        }

        for (auto request_id : updated_loaders)
        {
            auto& loader = request_ids_to_layer_loaders.at(request_id);

            // It isn't possible to simply restart the existing task,
            // because the model has changed, and the sub-tasks that
            // are needed may have changed.
            // On top of that, creating sub-tasks is only done when
            // the task is in the New state, and restarting a task
            // doesn't bring it back to that state.

            loader.load_model_task =
                TaskDependency::on_task(get_or_create_load_layer_model_task(loader.layer_id));
            loader.load_model_task.retain_data();

            loader.has_full_model_update = true;
        }
    }

    updated_layers.clear();
}

void VectorDataLoader::collect_garbage(JobScheduler* js)
{
    HRZ_SCOPED_SAMPLE_A("collect garbage");

    layer_models.collect_garbage();
    feature_id_lists.collect_garbage();
    attribute_values.collect_garbage();
    tile_geometries.collect_garbage();
    tasks.collect_garbage([this, js](Task& task) { clear_task(task, js); });
}

void VectorDataLoader::activate_task(WeakTaskRef task_ref)
{
    if (task_ref.is_valid())
    {
        auto& task = task_ref.value();
        if (!task.is_active)
        {
            active_tasks.push_back(task_ref);
            task.is_active = true;
        }
    }
}

void VectorDataLoader::set_task_status(WeakTaskRef& task_ref, Task& task, TaskStatus status)
{
    auto activate_if_needed = [this](WeakTaskRef& task_ref, Task& task, TaskStatus status_before)
    {
        if (task.status != status_before
            && (task.status == TaskStatus::New || task.status == TaskStatus::Loading))
        {
            activate_task(task_ref);
        }
    };

    auto status_before = task.status;

    task.status = status;

    if (status != TaskStatus::Blocked)
    {
        for (auto dependent_task_ref : task.dependents)
        {
            auto& dependent_task = dependent_task_ref.value();
            if (dependent_task.status == TaskStatus::Blocked)
            {
                // If A is blocked by B, and B is blocked by C, A is unblocked
                // when B is loaded. When C is finished loading (and changes
                // state, and we are in this function), B is unblocked, but
                // isn’t finished loading yet. By unlocking B, we allow it to
                // finish its work later, change state on its own, and then
                // unlock A.
                // All in all, we don't want this function to be recursive.
                auto dependent_task_status_before = dependent_task.status;
                dependent_task.status = TaskStatus::Loading;
                activate_if_needed(
                    dependent_task_ref, dependent_task, dependent_task_status_before);
            }
        }
    }

    activate_if_needed(task_ref, task, status_before);
}

void VectorDataLoader::clear_task(Task& task, JobScheduler* js)
{
    cancel_task_jobs(task, js);

    std::visit(
        [&]<typename TaskType>(TaskType& data) { clear_task<TaskType>(task, data, js); },
        task.data);

    tasks_by_hash.erase(task.hash);
}

void VectorDataLoader::cancel_task_jobs(Task& task, JobScheduler* js)
{
    if (!is_loading(task.status)) return;

    std::visit(
        [&]<typename TaskDataType>(TaskDataType& data)
        { cancel_task_jobs<TaskDataType>(task, data, js); },
        task.data);

    tasks_by_hash.erase(task.hash);
}

void VectorDataLoader::unload_task_data(
    Task& task,
    bool release_dependent_task_data,
    JobScheduler* js)
{
    std::visit(
        [&]<typename TaskDataType>(TaskDataType& data)
        { unload_task_data<TaskDataType>(task, data, release_dependent_task_data, js); },
        task.data);
}

void VectorDataLoader::restart_task(WeakTaskRef& task_ref, Task& task, hrz::JobScheduler* js)
{
    if (task.status == TaskStatus::New || task.status == TaskStatus::ModelError) return;

    if (is_loading(task.status) || task.status == TaskStatus::Loaded
        || task.status == TaskStatus::Unloaded || task.status == TaskStatus::DataError)
    {
        cancel_task_jobs(task, js);
        unload_task_data(task, false, js);

        task.version += 1;

        set_task_status(task_ref, task, TaskStatus::Unloaded);
        activate_task(task_ref);

        auto send_new_data_message = [&](const LayerModel& layer_model)
        {
            auto iterpair = layer_ids_to_request_ids.equal_range(layer_model.id);
            for (auto it = iterpair.first; it != iterpair.second; ++it)
            {
                auto request_id = it->second;
                auto loader_it = request_ids_to_layer_loaders.find(request_id);
                if (loader_it != request_ids_to_layer_loaders.end())
                {
                    send_layer_has_new_data_message(loader_it->second);
                }
            }
        };

        auto restart_tasks_from_requests = [&]()
        {
            auto iterpair = tasks_to_data_request_ids.equal_range(task_ref);
            for (auto it = iterpair.first; it != iterpair.second; ++it)
            {
                auto& request = request_ids_to_data_requests.at(it->second);
                request.task.retain_data();
            }
        };

        // These three task types are the ones that are created when requesting data.
        // There is no need to increment the layer model data version in the other
        // cases as they always have at least one task of one of these types among
        // their (transitive) dependents.
        // If a data request from a message depends on the task, we retain the data
        // so that it gets reloaded and then sent to the requester in a data message.
        std::visit(
            hrz::overload{
                [&](Task::LoadGeometry& data)
                {
                    auto& layer_model = data.layer_model.value();
                    layer_model.data_version += 1;
                    send_new_data_message(layer_model);
                    restart_tasks_from_requests();
                },
                [&](Task::LoadAllAttributeValues& data)
                {
                    auto& layer_model = data.layer_model.value();
                    layer_model.data_version += 1;
                    send_new_data_message(layer_model);
                    restart_tasks_from_requests();
                },
                [&](Task::LoadFeatureIds& data)
                {
                    auto& layer_model = data.layer_model.value();
                    layer_model.data_version += 1;
                    send_new_data_message(layer_model);
                    restart_tasks_from_requests();
                },
                [&](auto&) {},
            },
            task.data);
    }

    for (auto dependent_task_ref : task.dependents)
    {
        restart_task(dependent_task_ref, dependent_task_ref.value(), js);
    }
}

void VectorDataLoader::work_new_task(
    WeakTaskRef& task_ref,
    Task& task,
    SceneModel* scene_model,
    AttributionRegistry* attributions)
{
    HRZ_SCOPED_SAMPLE_A("work new task");

    assert(task.status == TaskStatus::New);

    std::visit(
        [&]<typename TaskDataType>(TaskDataType& data)
        { work_new_task<TaskDataType>(task_ref, task, data, scene_model, attributions); },
        task.data);
}

void VectorDataLoader::work_unloaded_task(WeakTaskRef& task_ref, Task& task)
{
    HRZ_SCOPED_SAMPLE_A("work unloaded task");

    assert(task.status == TaskStatus::Unloaded);

    if (task.data_use_count == 0) return;

    std::visit(
        [&]<typename TaskDataType>(TaskDataType& data)
        { work_unloaded_task<TaskDataType>(task_ref, task, data); },
        task.data);

    set_task_status(task_ref, task, TaskStatus::Loading);
}

void VectorDataLoader::work_loading_task(
    WeakTaskRef& task_ref,
    Task& task,
    JobScheduler* js,
    BlobAllocator* ba,
    ClientMessageQueue* mq,
    AttributionRegistry* attributions)
{
    HRZ_SCOPED_SAMPLE_A("work loading task");

    assert(task.status == TaskStatus::Loading);

    std::visit(
        [&]<typename TaskDataType>(TaskDataType& data)
        { work_loading_task<TaskDataType>(task_ref, task, data, js, ba, mq, attributions); },
        task.data);
}

void VectorDataLoader::unload_task_data_if_not_needed(
    WeakTaskRef& task_ref,
    Task& task,
    JobScheduler* js)
{
    HRZ_SCOPED_SAMPLE_A("unload task data if not needed");

    if (task.data_use_count > 0 || !(is_loading(task.status) || task.status == TaskStatus::Loaded))
    {
        return;
    }

    std::visit(
        [&]<typename TaskDataType>(TaskDataType&)
        {
            if (unload_task_data_if_not_needed<TaskDataType>())
            {
                cancel_task_jobs(task, js);
                unload_task_data(task, true, js);
            }

            set_task_status(task_ref, task, TaskStatus::Unloaded);
        },
        task.data);
}

void VectorDataLoader::check_for_invalidated_data_for_task(
    WeakTaskRef& task_ref,
    Task& task,
    JobScheduler* js)
{
    HRZ_SCOPED_SAMPLE_A("check for updated data for task");

    if (!(is_loading(task.status) || task.status == TaskStatus::Loaded
          || task.status == TaskStatus::Unloaded || task.status == TaskStatus::DataError))
    {
        return;
    }

    std::visit(
        [&]<typename TaskDataType>(TaskDataType& data)
        { check_for_invalidated_data_for_task<TaskDataType>(task_ref, task, data, js); },
        task.data);
}

void VectorDataLoader::work_messages(SceneModel* scene_model, JobScheduler* js)
{
    HRZ_SCOPED_SAMPLE("work messages");

    for (auto& generic_message : asset_loader_channel.receive())
    {
        std::visit(
            hrz::overload{
                [&](assets_loader::messages::LoadedData& message)
                {
                    auto it = tasks_waiting_for_asset_loader_message.find(message.request_id);
                    if (it != tasks_waiting_for_asset_loader_message.end()
                        && it->second.has_value())
                    {
                        auto& task_ref = it->second;
                        auto& task = task_ref.value();
                        assert(task.is_load_url_data());
                        auto& task_data = task.load_url_data();

                        if (task.status == TaskStatus::Blocked)
                        {
                            task_data.blob = std::move(message.data);
                            set_task_status(task_ref, task, TaskStatus::Loaded);
                        }
                    }
                },
                [&](assets_loader::messages::LoadFailure& message)
                {
                    auto it = tasks_waiting_for_asset_loader_message.find(message.request_id);
                    if (it != tasks_waiting_for_asset_loader_message.end()
                        && it->second.has_value())
                    {
                        auto& task_ref = it->second;
                        auto& task = task_ref.value();
                        assert(task.is_load_url_data());

                        if (task.status == TaskStatus::Blocked)
                        {
                            set_task_status(task_ref, task, TaskStatus::DataError);
                        }
                    }
                },
                [&](assets_loader::messages::NewChannel& message)
                {
                    auto it = tasks_waiting_for_asset_loader_message.find(message.request_id);
                    if (it != tasks_waiting_for_asset_loader_message.end()
                        && it->second.has_value())
                    {
                        auto& task_ref = it->second;
                        auto& task = task_ref.value();
                        assert(task.is_load_pmtiles());
                        auto& task_data = task.load_pmtiles();

                        if (task.status == TaskStatus::Blocked)
                        {
                            task_data.pmtiles = PmTiles::create(
                                task_data.url, task_data.headers, task_data.queue,
                                std::move(message.channel));
                            set_task_status(task_ref, task, TaskStatus::Loading);
                        }
                    }
                },
            },
            generic_message);
    }

    for (auto& generic_message : in_memory_vector_data_channel.receive())
    {
        std::visit(
            hrz::overload{
                [&](in_memory::messages::VectorData& message)
                {
                    auto it =
                        tasks_waiting_for_in_memory_vector_data_message.find(message.request_id);
                    if (it != tasks_waiting_for_in_memory_vector_data_message.end()
                        && it->second.has_value())
                    {
                        auto& task_ref = it->second;
                        auto& task = task_ref.value();
                        assert(task.is_load_in_memory_vector_data());

                        if (task.status == TaskStatus::Blocked || task.status == TaskStatus::Loaded
                            || task.status == TaskStatus::Unloaded)
                        {
                            auto& task_data = task.load_in_memory_vector_data();
                            const auto& layer_model = task_data.layer_model.value();
                            const auto& data_source =
                                layer_model.data_sources.at(task_data.data_source);

                            auto& data = message.data;

                            assert(data.feature_ids.size() == data.geometry.features.size());
                            task_data.feature_ids = feature_id_lists.alloc();
                            task_data.feature_ids.value() = std::move(data.feature_ids);

                            task_data.geometry = tile_geometries.alloc();
                            task_data.geometry.value() = std::move(data.geometry);

                            task_data.attribution = message.attribution;

                            load_attribute_data_into_map(
                                data.attributes, task_data.attribute_ids_to_values, data_source,
                                data.geometry.features.size());

                            set_task_status(task_ref, task, TaskStatus::Loaded);

                            if (task.status == TaskStatus::Loaded
                                || task.status == TaskStatus::Unloaded)
                            {
                                task.version += 1;

                                for (auto dependent_task_ref : task.dependents)
                                {
                                    restart_task(
                                        dependent_task_ref, dependent_task_ref.value(), js);
                                }
                            }
                        }
                    }
                },
                [&](in_memory::messages::Error& message)
                {
                    auto it =
                        tasks_waiting_for_in_memory_vector_data_message.find(message.request_id);
                    if (it != tasks_waiting_for_in_memory_vector_data_message.end()
                        && it->second.has_value())
                    {
                        auto& task_ref = it->second;
                        auto& task = task_ref.value();

                        if (task.status == TaskStatus::Blocked || task.status == TaskStatus::Loaded
                            || task.status == TaskStatus::Unloaded)
                        {
                            set_task_status(task_ref, task, TaskStatus::DataError);
                        }
                    }
                },
            },
            generic_message);
    }

    channels.work();

    for (auto& it : channels)
    {
        auto channel_id = it.first;
        auto& channel = it.second;

        for (auto& generic_message : channel.receive())
        {
            std::visit(
                hrz::overload{
                    [&](const vector_data::messages::LoadLayer& message)
                    {
                        RequestId request_id{channel_id, message.request_id};
                        load_layer(request_id, message.layer_id);
                    },
                    [&](const vector_data::messages::ReleaseLayerLoader& message)
                    {
                        RequestId request_id{channel_id, message.request_id};
                        release_layer_loader(request_id);
                    },
                    [&](const vector_data::messages::RequestData& message)
                    {
                        RequestId request_id{channel_id, message.request_id};
                        RequestId layer_request_id{channel_id, message.load_layer_request_id};
                        auto it_data_request = request_ids_to_data_requests.find(request_id);
                        if (it_data_request == request_ids_to_data_requests.end())
                        {
                            std::visit(
                                hrz::overload{
                                    [&](const TileCoords& tile_coords) {
                                        request_data(
                                            request_id, layer_request_id, tile_coords,
                                            message.data_kind);
                                    },
                                    [&](const vector_data::FeatureIds& feature_ids) {
                                        request_data(
                                            request_id, layer_request_id, feature_ids,
                                            message.data_kind);
                                    },
                                },
                                message.feature_selection);
                        }
                        else
                        {
                            retain_data(request_id);
                        }
                    },
                    [&](const vector_data::messages::ReleaseData& message)
                    {
                        RequestId request_id{channel_id, message.request_id};
                        release_data(request_id);
                    },
                    [&](const vector_data::messages::RetainData& message)
                    {
                        RequestId request_id{channel_id, message.request_id};
                        retain_data(request_id);
                    },
                    [&](const vector_data::messages::ReleaseDataRequest& message)
                    {
                        RequestId request_id{channel_id, message.request_id};
                        release_request(request_id);
                    },
                    [&](const hrz_proto::VectorDataRequestResponse& message)
                    { provide_client_data(message); },
                    [&](const hrz_proto::VectorDataInvalidation& message)
                    { invalidate_client_data(message); },
                },
                generic_message);
        }
    }
}

void VectorDataLoader::work_tasks(
    SceneModel* scene_model,
    JobScheduler* js,
    BlobAllocator* ba,
    ClientMessageQueue* mq,
    AttributionRegistry* attributions)
{
    HRZ_SCOPED_SAMPLE("work tasks");

    if (active_tasks.empty() && invalidations.empty()) return;

    // Here we go through the list of active tasks and work on them.
    // Some tasks may remain in an active state after having been worked
    // on. In this case, they remain in the active_tasks array.
    //
    // * We want tasks that are being activated during the loop to be
    //   worked on if they are activated.
    // * We do not want tasks that remain active to be worked on multiple
    //   times.
    // * The next time this function is called, we want tasks that have
    //   activated this time to be worked on before tasks that remained
    //   active. (Otherwise if there are too many tasks remaining active,
    //   they could take up all the slots and prevent other tasks from
    //   being worked on, potentially soft-locking the loader.)
    // * When we exit this function, we want tasks to be packed at the
    //   front of the active task array.
    //
    // The strategy is to go through as many unique active tasks as we
    // are allowed by looping through the array. Tasks that remain active
    // are packed at the front. Newly activated tasks are pushed at the
    // back. Then after the loop, tasks that remained active are in turn
    // pushed at the back, and finally all tasks are move to the front.

    static constexpr size_t max_tasks_to_update = std::numeric_limits<size_t>::max();
    size_t write_index = 0;

    for (size_t read_index = 0;
         read_index < active_tasks.size() && read_index < max_tasks_to_update; ++read_index)
    {
        // Cannot take a reference here, as `active_tasks` may be resized,
        // and its data moved, if new tasks are activated during this loop.
        auto task_ref = active_tasks[read_index];

        if (!task_ref.is_valid()) continue;

        auto& task = task_ref.value();

        if (task.status == TaskStatus::New)
        {
            work_new_task(task_ref, task, scene_model, attributions);
        }
        if (task.status == TaskStatus::Unloaded)
        {
            work_unloaded_task(task_ref, task);
        }
        if (task.status == TaskStatus::Loading)
        {
            work_loading_task(task_ref, task, js, ba, mq, attributions);
        }

        unload_task_data_if_not_needed(task_ref, task, js);

        if (task.status == TaskStatus::New || task.status == TaskStatus::Loading)
        {
            // Keep the task active.
            // Tasks that remain active are accumulated at the front of the array.

            if (write_index != read_index)
            {
                active_tasks[write_index] = task_ref;
            }

            write_index += 1;
        }
        else
        {
            task.is_active = false;
        }
    }

    if (active_tasks.size() > max_tasks_to_update)
    {
        // Move tasks that remained active from the front of the array to the back.
        for (size_t i = 0; i < write_index; ++i)
        {
            active_tasks.push_back(active_tasks[i]);
        }

        // Move all tasks to the front of the array.
        for (size_t i = max_tasks_to_update; i < active_tasks.size(); ++i)
        {
            active_tasks[i - max_tasks_to_update] = active_tasks[i];
        }

        // Tasks are now tidily packed at the front of the array, so the array
        // can be resized.
        active_tasks.resize(active_tasks.size() - max_tasks_to_update);
    }
    else
    {
        assert(active_tasks.size() >= write_index);
        if (active_tasks.size() > write_index)
        {
            // The end of the array is now unused, as references of tasks that
            // remained active have been pushed to the front, and tasks that have
            // been made active during the loop have themselves been worked on.
            active_tasks.resize(write_index);
        }
    }

    if (!invalidations.empty())
    {
        // We don't have a nice way to check if a given invalidation applies to a
        // given task without explicitly testing one against the other. So we have
        // to loop through all tasks. But invalidations are infrequent, so it should
        // be okay.

        for (auto task_ref : tasks)
        {
            auto& task = task_ref.value();
            check_for_invalidated_data_for_task(task_ref, task, js);
        }

        invalidations.clear();
    }
}

void VectorDataLoader::work_client_request_timeouts()
{
    while (!client_tickets_timeouts.empty())
    {
        auto& request = client_tickets_timeouts.top();
        if (hrz::now_frame_ms() >= request.timeout_date)
        {
            auto it = client_tickets_to_tasks.find(request.ticket);
            if (it != client_tickets_to_tasks.end())
            {
                auto& task_ref = it->second;
                auto& task = task_ref.value();
                auto& task_data = task.request_client_data();

                HRZ_LOG_WARNING(
                    "Client did not respond to vector data request {}", task_data.client_ticket);

                auto& history_entry =
                    client_request_history.entries[task_data.request_history_index];
                if (history_entry.ticket == task_data.client_ticket)
                {
                    history_entry.status = ClientRequestHistory::RequestStatus::Timeout;
                }

                client_tickets_to_tasks.erase(task_data.client_ticket);
                task_data.client_ticket = NO_CLIENT_TICKET;

                set_task_status(task_ref, task, TaskStatus::DataError);
            }

            client_tickets_timeouts.pop();
        }
        else
        {
            break;
        }
    }
}

void VectorDataLoader::work(
    SceneModel* scene_model,
    JobScheduler* js,
    BlobAllocator* ba,
    ClientMessageQueue* mq,
    AttributionRegistry* attributions)
{
    HRZ_SCOPED_SAMPLE_A("vector data loader work");

    assert(js && ba && mq);

    HRZ_SCOPED_LOCK(dev_ui_mutex);

    update_layers_from_model(scene_model);
    collect_garbage(js);
    work_messages(scene_model, js);
    work_tasks(scene_model, js, ba, mq, attributions);
    work_client_request_timeouts();
}

void VectorDataLoader::provide_client_data(const hrz_proto::VectorDataRequestResponse& response)
{
    HRZ_SCOPED_SAMPLE_A("vector data loader add client data");

    auto task_ref_it = client_tickets_to_tasks.find(response.ticket());
    if (task_ref_it == client_tickets_to_tasks.end())
    {
        // It is probably data for a tile that has been removed since the request
        // was made.
        return;
    }

    auto& task_ref = task_ref_it->second;
    auto& task = task_ref.value();

    if (!task.is_request_client_data() || !is_loading(task.status))
    {
        HRZ_LOG_WARNING("Unexpected client vector data for ticket {}", response.ticket());
        return;
    }

    auto& task_data = task.request_client_data();

    auto set_history_entry_status = [&](ClientRequestHistory::RequestStatus status)
    {
        auto& history_entry = client_request_history.entries[task_data.request_history_index];
        if (history_entry.ticket == task_data.client_ticket)
        {
            history_entry.status = status;
        }
    };

    if (response.error())
    {
        HRZ_LOG_WARNING("Received error from client for vector data request {}", response.ticket());
        set_history_entry_status(ClientRequestHistory::RequestStatus::Error);
        set_task_status(task_ref, task, TaskStatus::DataError);
    }
    else
    {
        std::optional<uint32_t> expected_feature_count = std::nullopt;

        if (task_data.load_feature_ids_task.has_task())
        {
            const auto& feature_ids =
                task_data.load_feature_ids_task.get_task().load_feature_ids().feature_ids;
            assert(feature_ids.has_value());
            if (feature_ids.has_value())
            {
                expected_feature_count = {(uint32_t)feature_ids.value().size()};
            }
        }

        if (expected_feature_count.has_value()
            && expected_feature_count.value() != (size_t)response.features_size())
        {
            HRZ_LOG_WARNING(
                "Incorrect amount of features in client vector data for ticket {}: got {}, "
                "expected {}",
                response.ticket(), response.features_size(), expected_feature_count.value());

            set_history_entry_status(ClientRequestHistory::RequestStatus::Error);
            set_task_status(task_ref, task, TaskStatus::DataError);
        }
        else
        {
            task_data.client_response = {std::move(response)};

            set_history_entry_status(ClientRequestHistory::RequestStatus::Received);
            set_task_status(task_ref, task, TaskStatus::Loading);
        }
    }

    client_tickets_to_tasks.erase(response.ticket());
    task_data.client_ticket = NO_CLIENT_TICKET;
}

void VectorDataLoader::invalidate_client_data(const hrz_proto::VectorDataInvalidation& invalidation)
{
    invalidations.push_back({invalidation});
}

vector_data::VectorDataLoaderChannel VectorDataLoader::create_channel()
{
    return channels.create_channel().second;
}

const char* VectorDataLoader::data_kind_str(DataKind data_kind)
{
    switch (data_kind)
    {
        case DataKind::Geometry: return "Geometry";
        case DataKind::AttributeValues: return "Attribute values";
        case DataKind::FeatureIds: return "Feature IDs";
        default: return "Unknown";
    }
}

const char* VectorDataLoader::task_type_str(const VectorDataLoader::Task& task)
{
    return std::visit(
        hrz::overload{
            [](const Task::LoadGeometry&) { return "Load geometry"; },
            [](const Task::LoadAllAttributeValues&) { return "Load all attribute values"; },
            [](const Task::LoadAttributeValues&) { return "Load single attribute values"; },
            [](const Task::LoadFeatureIds&) { return "Load feature IDs"; },
            [](const Task::LoadVectorData&) { return "Load vector data"; },
            [](const Task::LoadVectorTileData&) { return "Load vector tile data"; },
            [](const Task::LoadVectorDataUrlPackage&)
            { return "Load vector data package from URL"; },
            [](const Task::LoadVectorDataPmTilesPackage&)
            { return "Load vector data package from PMTiles"; },
            [](const Task::LoadUntiledVectorData&) { return "Load untiled vector data"; },
            [](const Task::ExtractVectorTileData&) { return "Extract vector tile data"; },
            [](const Task::LoadInMemoryVectorData&) { return "Load in-memory vector data"; },
            [](const Task::LoadUrlData&) { return "Load URL data"; },
            [](const Task::RequestClientData&) { return "Request client data"; },
            [](const Task::LoadTileJson&) { return "Load TileJSON"; },
            [](const Task::LoadPmTiles&) { return "Load PMTiles"; },
            [](const Task::LoadLayerModel&) { return "Load layer model"; },
            [](const Task::LoadSourceModel&) { return "Load source model"; },
        },
        task.data);
}

const char* VectorDataLoader::task_status_str(const VectorDataLoader::Task& task)
{
    switch (task.status)
    {
        case VectorDataLoader::TaskStatus::New: return "Initializing";
        case VectorDataLoader::TaskStatus::Loading: return "Loading";
        case VectorDataLoader::TaskStatus::Blocked: return "Blocked";
        case VectorDataLoader::TaskStatus::Loaded: return "Complete";
        case VectorDataLoader::TaskStatus::Unloaded: return "Unloaded";
        case VectorDataLoader::TaskStatus::DataError: return "Data error";
        case VectorDataLoader::TaskStatus::ModelError: return "Model error";
        default: return "Unknown";
    }
}

const char* VectorDataLoader::status_to_str(
    VectorDataLoader::ClientRequestHistory::RequestStatus status)
{
    using RequestStatus = VectorDataLoader::ClientRequestHistory::RequestStatus;

    switch (status)
    {
        case RequestStatus::Loading: return "(Loading)";
        case RequestStatus::Received: return "(Received)";
        case RequestStatus::Canceled: return "(Canceled)";
        case RequestStatus::Timeout: return "(Timeout)";
        case RequestStatus::Error: return "(Error)";
        default:
            assert(false && "Unhandled case");
            return "(???"
                   ")";
    }
}

mu_Color VectorDataLoader::status_to_text_color(
    VectorDataLoader::ClientRequestHistory::RequestStatus status)
{
    using RequestStatus = VectorDataLoader::ClientRequestHistory::RequestStatus;

    switch (status)
    {
        case RequestStatus::Loading:
        case RequestStatus::Canceled: return {160, 160, 160, 255};
        case RequestStatus::Received: return {97, 204, 38, 255};
        case RequestStatus::Timeout:
        case RequestStatus::Error: return {255, 32, 32, 255};
        default: assert(false && "Unhandled case"); return {255, 255, 255, 255};
    }
}

void VectorDataLoader::dev_ui(mu_Context* ctx)
{
    HRZ_SCOPED_LOCK(dev_ui_mutex);

    auto get_spinner_str = [&]() -> const char*
    {
        size_t frame = (((size_t)ctx->frame) / 30) % 4;
        switch (frame)
        {
            case 0: return "-";
            case 1: return "\\";
            case 2: return "|";
            case 3: return "/";
            default: return "";
        }
    };

    fmt::memory_buffer buffer;

    int window_width = mu_get_current_container(ctx)->body.w - 16;

    mu_layout_row(ctx, 1, &window_width, 0);

    if (mu_header_ex(
            ctx, "Requests",
            hrz::format_to_buffer(buffer, "{} requests", request_ids_to_data_requests.size()), 0))
    {
        std::vector<VectorDataLoader::RequestId> request_ids(request_ids_to_data_requests.size());
        {
            uint32_t index = 0;
            for (const auto& request_it : request_ids_to_data_requests)
            {
                request_ids[index] = request_it.first;
                index += 1;
            }
            std::ranges::sort(
                request_ids,
                [](const VectorDataLoader::RequestId& a, const VectorDataLoader::RequestId& b)
                {
                    if (a.channel_id == b.channel_id)
                    {
                        return a.request_id < b.request_id;
                    }
                    return a.channel_id < b.channel_id;
                });
        }

        static int layout[] = {120, 120, 80, 110, 40, -1};
        mu_layout_row(ctx, 6, layout, 0);

        mu_text(ctx, "#");
        mu_text(ctx, "Type");
        mu_text(ctx, "Layer ID");
        mu_text(ctx, "Feature selection");
        mu_text(ctx, "Task #");
        mu_text(ctx, "Status");

        for (auto request_id : request_ids)
        {
            const auto& request = request_ids_to_data_requests.at(request_id);

            mu_text(
                ctx,
                hrz::format_to_buffer(
                    buffer, "{}-{}", request_id.channel_id, request_id.request_id));

            mu_Rect tooltip_rect = mu_layout_next(ctx);
            mu_layout_set_next(ctx, tooltip_rect, 0);

            mu_text(ctx, data_kind_str(request.data_kind));

            mu_text(ctx, hrz::format_to_buffer(buffer, "{}", request.layer_id));

            if (request.feature_selection.has_tile_coords())
            {
                const auto& tile_coords = request.feature_selection.tile_coords();
                mu_text(ctx, hrz::format_to_buffer(buffer, "Tile {}", tile_coords));
            }
            else if (request.feature_selection.has_feature_ids())
            {
                const auto& feature_ids = request.feature_selection.feature_ids();
                mu_text(ctx, hrz::format_to_buffer(buffer, "{} feature IDs", feature_ids->size()));
            }

            if (request.task.has_task())
            {
                const auto& task_ref = request.task.get_task_ref();
                const auto& task = request.task.get_task();

                mu_text(ctx, hrz::format_to_buffer(buffer, "{}", task_ref.get_handle().to_int()));
                mu_text(ctx, task_status_str(task));
            }
            else
            {
                mu_text(ctx, "None");
                mu_text(ctx, "");
                mu_text(ctx, "");
            }
        }
    }

    if (mu_header_ex(
            ctx, "Tasks",
            hrz::format_to_buffer(
                buffer, "{} tasks ({} active)", tasks.size(), active_tasks.size()),
            0))
    {
        static int layout[] = {40, 160, 50, 50, 50, 50, 60, -1};
        mu_layout_row(ctx, 8, layout, 0);

        mu_text(ctx, "#");
        mu_text(ctx, "Type");
        mu_text(ctx, "Refs");
        mu_text(ctx, "Dep tasks");
        mu_text(ctx, "Data uses");
        mu_text(ctx, "Version");
        mu_text(ctx, "Status");
        mu_text(ctx, "");

        for (const auto& ref : tasks)
        {
            const auto& handle = ref.get_handle();
            const auto& task = ref.value();

            mu_text(ctx, hrz::format_to_buffer(buffer, "{}", handle.to_int()));
            mu_text(ctx, task_type_str(task));
            mu_text(ctx, hrz::format_to_buffer(buffer, "{}", ref.ref_count()));
            mu_text(ctx, hrz::format_to_buffer(buffer, "{}", task.dependents.size()));
            mu_text(ctx, hrz::format_to_buffer(buffer, "{}", task.data_use_count));
            mu_text(ctx, hrz::format_to_buffer(buffer, "{}", task.version));
            mu_text(ctx, task_status_str(task));

            if (task.is_active)
            {
                mu_text(ctx, get_spinner_str());
            }
            else
            {
                mu_text(ctx, "");
            }
        }
    }

    if (mu_header_ex(
            ctx, "Layer models",
            hrz::format_to_buffer(buffer, "{} layer models", layer_models.size()), 0))
    {
        static int layout[] = {40, 60, 60, -1};
        mu_layout_row(ctx, 4, layout, 0);

        mu_text(ctx, "#");
        mu_text(ctx, "Refs");
        mu_text(ctx, "Layer ID");
        mu_text(ctx, "Layer handle");

        for (const auto& ref : layer_models)
        {
            mu_text(ctx, hrz::format_to_buffer(buffer, "{}", ref.get_handle().to_int()));
            mu_text(ctx, hrz::format_to_buffer(buffer, "{}", (uint32_t)ref.ref_count()));

            if (ref.has_value())
            {
                const auto& layer_model = ref.value();

                mu_text(ctx, hrz::format_to_buffer(buffer, "{}", layer_model.id));
                mu_text(ctx, hrz::format_to_buffer(buffer, "{}", layer_model.layer_handle));
            }
            else
            {
                mu_text(ctx, "");
                mu_text(ctx, "");
            }
        }
    }

    if (mu_header_ex(
            ctx, "Feature ID lists",
            hrz::format_to_buffer(buffer, "{} feature ID lists", feature_id_lists.size()), 0))
    {
        static int layout[] = {40, 60, -1};
        mu_layout_row(ctx, 3, layout, 0);

        mu_text(ctx, "#");
        mu_text(ctx, "Refs");
        mu_text(ctx, "Features");

        for (const auto& ref : feature_id_lists)
        {
            mu_text(ctx, hrz::format_to_buffer(buffer, "{}", ref.get_handle().to_int()));
            mu_text(ctx, hrz::format_to_buffer(buffer, "{}", (uint32_t)ref.ref_count()));

            if (ref.has_value())
            {
                const auto& feature_id_list = ref.value();

                mu_text(ctx, hrz::format_to_buffer(buffer, "{}", feature_id_list.size()));
            }
            else
            {
                mu_text(ctx, "");
            }
        }
    }

    if (mu_header_ex(
            ctx, "Attribute value lists",
            hrz::format_to_buffer(buffer, "{} attribute value lists", attribute_values.size()), 0))
    {
        static int layout[] = {40, 60, 60, -1};
        mu_layout_row(ctx, 4, layout, 0);

        mu_text(ctx, "#");
        mu_text(ctx, "Refs");
        mu_text(ctx, "Attribute ID");
        mu_text(ctx, "Values");

        for (const auto& ref : attribute_values)
        {
            mu_text(ctx, hrz::format_to_buffer(buffer, "{}", ref.get_handle().to_int()));
            mu_text(ctx, hrz::format_to_buffer(buffer, "{}", (uint32_t)ref.ref_count()));

            if (ref.has_value())
            {
                const auto& attribute_values = ref.value();

                mu_text(ctx, hrz::format_to_buffer(buffer, "{}", attribute_values.attribute_id));
                mu_text(ctx, hrz::format_to_buffer(buffer, "{}", attribute_values.values.size()));
            }
            else
            {
                mu_text(ctx, "");
                mu_text(ctx, "");
            }
        }
    }

    if (mu_header_ex(
            ctx, "Tile geometries",
            hrz::format_to_buffer(buffer, "{} tile geometries", tile_geometries.size()), 0))
    {
        static int layout[] = {40, 60, 60, 60, -1};
        mu_layout_row(ctx, 5, layout, 0);

        mu_text(ctx, "#");
        mu_text(ctx, "Refs");
        mu_text(ctx, "Points");
        mu_text(ctx, "Features");
        mu_text(ctx, "Bounds");

        for (const auto& ref : tile_geometries)
        {
            mu_text(ctx, hrz::format_to_buffer(buffer, "{}", ref.get_handle().to_int()));
            mu_text(ctx, hrz::format_to_buffer(buffer, "{}", (uint32_t)ref.ref_count()));

            if (ref.has_value())
            {
                const auto& geometry = ref.value();

                mu_text(ctx, hrz::format_to_buffer(buffer, "{}", geometry.features.size()));
                mu_text(ctx, hrz::format_to_buffer(buffer, "{}", geometry.features.size()));
                mu_text(
                    ctx,
                    hrz::format_to_buffer(
                        buffer, "({:.0f}, {:.0f}), ({:.0f}, {:.0f})", geometry.bounds.min.x,
                        geometry.bounds.min.y, geometry.bounds.max.x, geometry.bounds.max.y));
            }
            else
            {
                mu_text(ctx, "");
                mu_text(ctx, "");
                mu_text(ctx, "");
            }
        }
    }

    if (mu_header(ctx, "Client request history"))
    {
        {
            static const int layout[] = {100, 50, 50, -1};
            mu_layout_row(ctx, 4, layout, 0);

            buffer.clear();
            fmt::format_to(std::back_inserter(buffer), "{} requests", client_request_history.count);
            buffer.push_back(0);
            mu_draw_control_text(
                ctx, buffer.data(), mu_layout_next(ctx), MU_COLOR_TEXT, MU_OPT_ALIGNRIGHT);

            mu_text(ctx, "");

            if (mu_button(ctx, "Clear"))
            {
                client_request_history.clear();
            }
        }

        static int row_layout[] = {40, 50, 60, 120, -70, 65};
        mu_layout_row(ctx, 6, row_layout, 0);

        mu_text(ctx, "#");
        mu_text(ctx, "Layer ID");
        mu_text(ctx, "Geometry");
        mu_text(ctx, "Feature selection");
        mu_text(ctx, "Attribute IDs");
        mu_text(ctx, "Status");

        static const int panel_layout = -1;
        mu_layout_row(ctx, 1, &panel_layout, -1);
        static ui::StickyPanelState sticky_panel_state;
        begin_sticky_panel(ctx, &sticky_panel_state, "Client request panel");

        for (uint32_t i = 0; i < client_request_history.count; i++)
        {
            uint32_t index = (client_request_history.head + i)
                % hrz::VectorDataLoader::ClientRequestHistory::CAPACITY;
            const auto& entry = client_request_history.entries[index];

            mu_layout_row(ctx, 6, row_layout, 0);

            mu_text(ctx, hrz::format_to_buffer(buffer, "{}", entry.ticket));
            mu_text(ctx, hrz::format_to_buffer(buffer, "{}", entry.layer_id));
            mu_text(ctx, entry.expects_geometry ? "Yes" : "No");

            if (entry.tile_coords.has_value())
            {
                buffer.clear();
                fmt::format_to(std::back_inserter(buffer), "Tile {}", entry.tile_coords.value());

                if (entry.feature_count.has_value())
                {
                    fmt::format_to(
                        std::back_inserter(buffer), " ({} features)", entry.feature_count.value());
                }

                buffer.push_back(0);
            }
            else if (entry.feature_count.has_value())
            {
                hrz::format_to_buffer(buffer, "{} features", entry.feature_count.value());
            }

            mu_text(ctx, buffer.data());

            buffer.clear();
            for (size_t i = 0; i < entry.attribute_ids.size(); i++)
            {
                fmt::format_to(std::back_inserter(buffer), "{}", entry.attribute_ids[i]);
                if (i < entry.attribute_ids.size() - 1)
                {
                    fmt::format_to(std::back_inserter(buffer), ", ");
                }
            }
            buffer.push_back(0);
            mu_text(ctx, buffer.data());

            mu_text_color(ctx, status_to_str(entry.status), status_to_text_color(entry.status));
        }

        end_sticky_panel(ctx, &sticky_panel_state);
    }

    if (mu_header(ctx, "Memory usage"))
    {
        size_t tasks_memory = tasks.size() * sizeof(VectorDataLoader::Task);

        size_t feature_id_lists_memory = 0;
        for (const auto& ref : feature_id_lists)
        {
            feature_id_lists_memory += ref->size_bytes();
        }

        size_t attribute_values_memory = 0;
        for (const auto& ref : attribute_values)
        {
            attribute_values_memory += sizeof(vector_data::AttributeValues)
                + ref->values.size_bytes() + ref->out_of_line_data.size_bytes();
        }

        size_t tile_geometries_memory = 0;
        for (const auto& ref : tile_geometries)
        {
            tile_geometries_memory += sizeof(vector_data::VectorTileGeometry)
                + ref->features.size_bytes() + ref->points.size_bytes()
                + ref->linestring_sizes.size_bytes();
        }

        size_t total_memory = tasks_memory + feature_id_lists_memory + attribute_values_memory
            + tile_geometries_memory;

        static int layout[] = {120, -1};
        mu_layout_row(ctx, 2, layout, 0);

        fmt::memory_buffer buffer;

        mu_text(ctx, "Total memory:");
        mu_text(ctx, bytes_to_string(total_memory, buffer));
        mu_text(ctx, "Tasks:");
        mu_text(ctx, bytes_to_string(tasks_memory, buffer));
        mu_text(ctx, "Feature ID lists:");
        mu_text(ctx, bytes_to_string(feature_id_lists_memory, buffer));
        mu_text(ctx, "Attribute values:");
        mu_text(ctx, bytes_to_string(attribute_values_memory, buffer));
        mu_text(ctx, "Tile geometries:");
        mu_text(ctx, bytes_to_string(tile_geometries_memory, buffer));
    }
}
} // namespace hrz
