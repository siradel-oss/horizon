#include "hrz/core/client_messages.h"
#include "hrz/core/vector/data_loader/impl.h"
#include "hrz/fnd/time.h"
#include "hrz/fnd/variant.h"

#include <queue>

namespace hrz
{
VectorDataLoader::TaskRef VectorDataLoader::get_or_create_request_client_data_task(
    const LayerModelRef& layer_model,
    uint32_t data_source,
    const FeatureSelection& feature_selection)
{
    uint64_t hash = hrz::index_of_variant<decltype(Task::data), Task::RequestClientData>();
    hash = hrz::hash_mix<uint64_t>(hash, layer_model.get_handle().hash());
    hash = hrz::hash_mix<uint64_t>(hash, hrz::hash_value(data_source));
    hash = hrz::hash_mix<uint64_t>(hash, feature_selection.hash());

    auto it = tasks_by_hash.find(hash);
    if (it != tasks_by_hash.end() && it->second.is_valid())
    {
        const auto& task = it->second.value();
        if (task.is_request_client_data())
        {
            const auto& task_data = task.request_client_data();
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
    Task::RequestClientData task_data;
    task_data.layer_model = layer_model;
    task_data.data_source = data_source;
    task_data.feature_selection = feature_selection;
    task_data.load_feature_ids_task = {};
    task_data.client_ticket = NO_CLIENT_TICKET;
    task_data.geometry = TileGeometryRef{};
    task_data.attribution = {};
    task_data.feature_ids = FeatureIdListRef{};
    new_task.data = std::move(task_data);
    new_task.hash = hash;

    tasks_by_hash.insert({new_task.hash, new_task_ref.make_weak_ref()});

    activate_task(new_task_ref.make_weak_ref());

    return new_task_ref;
}

template<>
void VectorDataLoader::clear_task<VectorDataLoader::Task::RequestClientData>(
    Task& task,
    Task::RequestClientData& task_data,
    JobScheduler* js)
{
    // No-op
}

template<>
void VectorDataLoader::cancel_task_jobs<VectorDataLoader::Task::RequestClientData>(
    Task& task,
    Task::RequestClientData& task_data,
    JobScheduler* js)
{
    if (task_data.client_ticket != NO_CLIENT_TICKET)
    {
        auto& history_entry = client_request_history.entries[task_data.request_history_index];
        if (history_entry.ticket == task_data.client_ticket)
        {
            history_entry.status = ClientRequestHistory::RequestStatus::Canceled;
        }

        client_tickets_to_tasks.erase(task_data.client_ticket);
        task_data.client_ticket = NO_CLIENT_TICKET;
    }
    hrz_jobs::cancel_job(js, task_data.move_to_blobs_ticket);
}

template<>
void VectorDataLoader::unload_task_data<VectorDataLoader::Task::RequestClientData>(
    Task& task,
    Task::RequestClientData& task_data,
    bool release_dependent_task_data,
    JobScheduler* js)
{
    if (task_data.client_ticket != NO_CLIENT_TICKET)
    {
        auto& history_entry = client_request_history.entries[task_data.request_history_index];
        if (history_entry.ticket == task_data.client_ticket)
        {
            history_entry.status = ClientRequestHistory::RequestStatus::Canceled;
        }

        client_tickets_to_tasks.erase(task_data.client_ticket);
        task_data.client_ticket = NO_CLIENT_TICKET;
    }

    task_data.feature_ids.release();
    task_data.geometry.release();

    for (auto& it : task_data.attribute_ids_to_values)
    {
        it.second.release();
    }
    task_data.attribute_ids_to_values.clear();

    task_data.client_response = std::nullopt;

    if (hrz_jobs::is_job_valid(js, task_data.move_to_blobs_ticket))
    {
        hrz_jobs::cancel_job(js, task_data.move_to_blobs_ticket);
    }

    // Do not release features IDs from the load feature IDs task.
    // Otherwise, client invalidations by feature IDs cannot be
    // checked, and validating values accessed by tile when this
    // source isn't the geometry source, after they have been
    // invalidated, must obtain feature IDs from the geometry
    // source and likely download vector data again.
}

template<>
void VectorDataLoader::work_new_task<VectorDataLoader::Task::RequestClientData>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::RequestClientData& task_data,
    SceneModel* scene_model,
    AttributionRegistry* attributions)
{
    const auto& layer_model = task_data.layer_model.value();
    const auto& data_source = layer_model.data_sources.at(task_data.data_source);
    const auto access = data_source.client_data_provider().access;

    if (access == hrz_proto::VectorDataSourceAccess::ACCESS_BY_TILE
        && !task_data.feature_selection.has_tile_coords())
    {
        HRZ_LOG_ERROR(
            "Cannot request client data using feature IDs when the client data "
            "source uses an access by tile");
        set_task_status(task_ref, task, TaskStatus::ModelError);
    }
    else if (
        access == hrz_proto::VectorDataSourceAccess::ACCESS_BY_FEATURE_ID
        || task_data.data_source != LayerModel::PRIMARY_SOURCE)
    {
        // When for a secondary source, even if the provider's access has
        // been set to an access by tile and not by feature IDs, we still
        // require the feature IDs so that we match the client values with
        // the features of the tile.
        task_data.load_feature_ids_task = TaskDependency::between_tasks(
            get_or_create_load_feature_ids_task(task_data.layer_model, task_data.feature_selection),
            task_ref);

        set_task_status(task_ref, task, TaskStatus::Unloaded);
    }
    else
    {
        set_task_status(task_ref, task, TaskStatus::Unloaded);
    }
}

template<>
void VectorDataLoader::work_unloaded_task<VectorDataLoader::Task::RequestClientData>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::RequestClientData& task_data)
{
    task_data.load_feature_ids_task.retain_data();
}

template<>
void VectorDataLoader::work_loading_task<VectorDataLoader::Task::RequestClientData>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::RequestClientData& task_data,
    JobScheduler* js,
    BlobAllocator* ba,
    ClientMessageQueue* mq,
    AttributionRegistry* attributions)
{
    const auto& layer_model = task_data.layer_model.value();

    if (hrz_jobs::is_job_valid(js, task_data.move_to_blobs_ticket))
    {
        if (hrz_jobs::is_job_finished(js, task_data.move_to_blobs_ticket))
        {
            if (hrz_jobs::get_job_status(js, task_data.move_to_blobs_ticket)
                == hrz::job_scheduler::JobStatus::Finished_Success)
            {
                vector_data::DecodedVectorTile tile_data;
                hrz_jobs::get_job_response(js, task_data.move_to_blobs_ticket, tile_data);

                task_data.geometry = tile_geometries.alloc();
                task_data.geometry.value() = std::move(tile_data.geometry);

                task_data.feature_ids = feature_id_lists.alloc();
                task_data.feature_ids.value() = std::move(tile_data.feature_ids);

                for (auto& values : tile_data.attributes)
                {
                    auto attribute_id = values.attribute_id;

                    auto ref = attribute_values.alloc();
                    ref.value() = std::move(values);
                    task_data.attribute_ids_to_values.insert({attribute_id, ref});
                }

                set_task_status(task_ref, task, TaskStatus::Loaded);
            }
            else
            {
                HRZ_LOG_ERROR(
                    "Could not allocate memory for the client-provided vector tile data of "
                    "layer {}.",
                    layer_model.id);
                set_task_status(task_ref, task, TaskStatus::DataError);
            }
        }
    }
    else if (task_data.client_response.has_value())
    {
        vector_data::RawClientVectorData params;
        params.attributes = std::move(task_data.attributes);
        params.expects_geometry = layer_model.data_sources.at(task_data.data_source).has_geometry;
        params.client_data = std::move(task_data.client_response.value());

        task_data.attribution =
            attribution::register_attribution(attributions, {params.client_data.attribution(), {}});
        task_data.move_to_blobs_ticket = hrz_jobs::add_job_move_client_vector_data_to_blobs(
            js, params, {monitoring::systems::VectorDataLoader, layer_model.layer_handle});
    }
    else if (
        task_data.client_ticket == NO_CLIENT_TICKET
        && (!task_data.load_feature_ids_task.has_task()
            || (task_data.load_feature_ids_task.has_task()
                && task_data.load_feature_ids_task.get_task().status == TaskStatus::Loaded)))
    {
        auto make_request_client_message = [&]() -> hrz_proto::VectorDataRequestMessage
        {
            const auto& data_source = layer_model.data_sources.at(task_data.data_source);

            task_data.client_ticket = client_ticket_generator.generate();

            hrz_proto::VectorDataRequestMessage message;
            message.set_ticket(task_data.client_ticket);
            message.set_vector_data_layer_id(task_data.layer_model->id);
            message.set_vector_data_source_index(task_data.data_source);
            message.set_expects_geometry(layer_model.geometry_source == task_data.data_source);

            for (const auto& pair : data_source.attributes)
            {
                message.add_attribute_ids(pair.first);
                task_data.attributes.push_back(
                    data_source.attributes.at(pair.first).to_attribute_model());
            }

            return message;
        };

        auto enqueue_request_client_message = [&](hrz_proto::VectorDataRequestMessage&& message)
        { hrz::client_message_queue::enqueue_vector_data_request_message(mq, std::move(message)); };

        const auto& data_source =
            layer_model.data_sources.at(task_data.data_source).client_data_provider();

        auto start_request = [&]()
        {
            client_tickets_to_tasks.insert({task_data.client_ticket, task_ref});

            if (data_source.timeout_duration.has_value())
            {
                client_tickets_timeouts.push(
                    {task_data.client_ticket,
                     hrz::now_frame_ms() + data_source.timeout_duration.value()});
            }

            set_task_status(task_ref, task, TaskStatus::Blocked);
        };

        if (data_source.access == hrz_proto::VectorDataSourceAccess::ACCESS_BY_TILE)
        {
            assert(task_data.feature_selection.has_tile_coords());

            auto message = make_request_client_message();
            message.mutable_tile_selection()->CopyFrom(
                hrz::to_proto(task_data.feature_selection.tile_coords()));

            start_request();

            std::optional<size_t> feature_count = std::nullopt;
            if (task_data.load_feature_ids_task.has_task())
            {
                const auto& feature_ids =
                    task_data.load_feature_ids_task.get_task().load_feature_ids().feature_ids;
                if (feature_ids.has_value())
                {
                    feature_count = feature_ids.value().size();
                }
            }

            ClientRequestHistory::Entry entry = {};
            entry.ticket = task_data.client_ticket;
            entry.layer_id = message.vector_data_layer_id();
            entry.expects_geometry = message.expects_geometry();
            entry.tile_coords = task_data.feature_selection.tile_coords();
            entry.feature_count = feature_count;
            for (auto attribute_id : message.attribute_ids())
            {
                entry.attribute_ids.push_back(attribute_id);
            }
            entry.status = ClientRequestHistory::RequestStatus::Loading;
            task_data.request_history_index = client_request_history.append(std::move(entry));

            enqueue_request_client_message(std::move(message));
        }
        else
        {
            assert(task_data.load_feature_ids_task.has_task());
            const auto& feature_ids =
                task_data.load_feature_ids_task.get_task().load_feature_ids().feature_ids.value();

            if (!feature_ids.has_any_attribute())
            {
                HRZ_LOG_ERROR(
                    "Cannot request values by feature ID without features IDs, for source "
                    "{} of layer {}.",
                    task_data.data_source, layer_model.id);
                set_task_status(task_ref, task, TaskStatus::ModelError);
            }
            else if (!feature_ids.empty())
            {
                auto message = make_request_client_message();
                feature_ids.to_proto(message.mutable_feature_id_selection()->mutable_feature_ids());

                start_request();

                ClientRequestHistory::Entry entry = {};
                entry.ticket = task_data.client_ticket;
                entry.layer_id = message.vector_data_layer_id();
                entry.expects_geometry = message.expects_geometry();
                entry.feature_count = feature_ids.size();
                for (auto attribute_id : message.attribute_ids())
                {
                    entry.attribute_ids.push_back(attribute_id);
                }
                entry.status = ClientRequestHistory::RequestStatus::Loading;
                task_data.request_history_index = client_request_history.append(std::move(entry));

                enqueue_request_client_message(std::move(message));
            }
            else
            {
                // No need to make an empty request to the client.

                task_data.feature_ids = feature_id_lists.alloc();
                task_data.feature_ids.value() = vector_data::FeatureIds{};

                if (layer_model.geometry_source == task_data.data_source)
                {
                    task_data.geometry = tile_geometries.alloc();
                    task_data.geometry.value() = vector_data::VectorTileGeometry{};
                }

                for (const auto& pair : layer_model.attributes)
                {
                    const auto& data_source = pair.second;
                    if (data_source != task_data.data_source)
                    {
                        continue;
                    }

                    auto values_ref = attribute_values.alloc();
                    auto& values = values_ref.value();
                    values.attribute_id = pair.first;

                    task_data.attribute_ids_to_values.insert({pair.first, values_ref});
                }

                set_task_status(task_ref, task, TaskStatus::Loaded);
            }
        }
    }
    else if (task_data.load_feature_ids_task.has_task())
    {
        auto& load_feature_ids_task = task_data.load_feature_ids_task.get_task();

        if (load_feature_ids_task.status != TaskStatus::Loaded)
        {
            if (is_error(load_feature_ids_task.status))
            {
                task_data.load_feature_ids_task.release_data();
                set_task_status(task_ref, task, load_feature_ids_task.status);
            }
            else
            {
                set_task_status(task_ref, task, TaskStatus::Blocked);
            }
        }
    }
}

template<>
bool VectorDataLoader::unload_task_data_if_not_needed<VectorDataLoader::Task::RequestClientData>()
{
    return true;
}

template<>
void VectorDataLoader::check_for_invalidated_data_for_task<
    VectorDataLoader::Task::RequestClientData>(
    WeakTaskRef& task_ref,
    Task& task,
    Task::RequestClientData& task_data,
    JobScheduler* js)
{
    const auto& layer_model = task_data.layer_model.value();
    const auto& data_source = layer_model.data_sources.at(task_data.data_source);
    const auto access = data_source.client_data_provider().access;

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

        if (access == hrz_proto::VectorDataSourceAccess::ACCESS_BY_TILE
            && task_data.feature_selection.has_tile_coords()
            && invalidation.invalidates_tile(task_data.feature_selection.tile_coords()))
        {
            restart_task(task_ref, task, js);
            return;
        }

        if (access == hrz_proto::VectorDataSourceAccess::ACCESS_BY_FEATURE_ID)
        {
            const auto& feature_ids =
                task_data.load_feature_ids_task.get_task().load_feature_ids().feature_ids.value();
            if (invalidation.invalidates_feature_ids(feature_ids))
            {
                restart_task(task_ref, task, js);
                return;
            }
        }
    }
}
} // namespace hrz
