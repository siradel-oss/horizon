#include "hrz/core/planet/elevation_query.h"

#include "hrz/core/jobs/points_query_jobs_params.h"
#include "hrz/core/planet/raster.h"
#include "hrz/core/planet/raster_collection.h"
#include "hrz/fnd/format.h"
#include "hrz/fnd/hash.h"
#include "hrz/fnd/meta.h"

void hrz::planet::ElevationQuery::queue_cancel(Ticket ticket)
{
    _to_cancel.insert(ticket);
}

hrz::planet::ElevationQuery::Ticket hrz::planet::ElevationQuery::start_query(
    hrz::BlobArray<lm::dvec2> points,
    monitoring::ResourceOwner resource_owner)
{
    Ticket ticket = _batchs.alloc();

    Batch* batch = _batchs.get_object(ticket);
    batch->status = Batch::Queued;
    batch->point_count = points.size();
    batch->points = hrz::BlobArrayView<lm::dvec2>(std::move(points));
    batch->resource_owner = resource_owner;

    _queued.push_back(ticket);

    return ticket;
}

hrz::planet::ElevationQuery::Ticket hrz::planet::ElevationQuery::start_query(
    hrz::BlobArrayView<lm::dvec2> points,
    monitoring::ResourceOwner resource_owner)
{
    return start_query(std::move(points), resource_owner, std::nullopt);
}

hrz::planet::ElevationQuery::Ticket hrz::planet::ElevationQuery::start_query(
    hrz::BlobArrayView<lm::dvec2> points,
    monitoring::ResourceOwner resource_owner,
    std::optional<ElevationQueryId> query_id)
{
    Ticket ticket = _batchs.alloc();

    Batch* batch = _batchs.get_object(ticket);
    batch->query_id = query_id;
    batch->status = Batch::Queued;
    batch->point_count = points.size();
    batch->points = std::move(points);
    batch->resource_owner = resource_owner;

    _queued.push_back(ticket);

    return ticket;
}

hrz::planet::ElevationQuery::Ticket hrz::planet::ElevationQuery::start_query(
    const lm::dvec2& point,
    monitoring::ResourceOwner resource_owner)
{
    Ticket ticket = _batchs.alloc();

    Batch* batch = _batchs.get_object(ticket);
    batch->status = Batch::Queued;
    batch->point_count = 1;
    batch->points = point;
    batch->resource_owner = resource_owner;

    _queued.push_back(ticket);

    return ticket;
}

bool hrz::planet::ElevationQuery::is_ready(Ticket ticket) const
{
    const Batch* batch = _batchs.get_object(ticket);
    if (batch)
    {
        return batch->status == Batch::Finished;
    }
    else
    {
        return false;
    }
}

std::optional<hrz::BlobArray<float>> hrz::planet::ElevationQuery::retrieve(Ticket ticket)
{
    auto batch = _batchs.get_object(ticket);
    if (!batch || batch->status != Batch::Finished) return std::nullopt;

    auto elevations = std::move(batch->elevations);
    batch->elevations = std::nullopt;

    auto it = std::ranges::find(_finished, ticket);
    assert(it != _finished.end());
    _finished.erase(it);
    _batchs.release(ticket);

    return elevations;
}

void hrz::planet::ElevationQuery::_restart_all_batchs(
    JobScheduler* js,
    BlobAllocator* ba,
    IRasterCollection* collection)
{
    // We re-queue in reverse step order to try to keep the original
    // batchs order.
    _restart_batchs(js, ba, collection, _finished);
    _restart_batchs(js, ba, collection, _sampling);
    _restart_batchs(js, ba, collection, _ready_to_sample);
    _restart_batchs(js, ba, collection, _waiting_for_tiles);
    _restart_batchs(js, ba, collection, _culling);
}

// Cancels a batch. This does not release it from the pool.
// The batch is left unassigned to any status.
void hrz::planet::ElevationQuery::_cancel(
    JobScheduler* js,
    BlobAllocator* ba,
    Ticket ticket,
    IRasterCollection* collection,
    bool remove_from_collection)
{
    Batch* batch = _batchs.get_object(ticket);
    if (!batch) return;

    bool should_release_tiles = false;
    switch (batch->status)
    {
        case Batch::Queued: break;
        case Batch::Culling: hrz_jobs::cancel_job(js, batch->cull_ticket); break;
        case Batch::WaitingForTiles: should_release_tiles = true; break;
        case Batch::ReadyToSample: should_release_tiles = true; break;
        case Batch::Sampling:
            hrz_jobs::cancel_job(js, batch->sample_ticket);
            should_release_tiles = true;
            break;
        case Batch::Finished: break;
        default: assert(!"Unknown elevation query batch status.");
    }

    if (remove_from_collection)
    {
        switch (batch->status)
        {
            case Batch::Queued:
            {
                auto it = std::ranges::find(_queued, ticket);
                assert(it != _queued.end());
                _queued.erase(it);
                break;
            }
            case Batch::Culling:
            {
                auto it = std::ranges::find(_culling, ticket);
                assert(it != _culling.end());
                _culling.erase(it);
                break;
            }
            case Batch::WaitingForTiles:
            {
                auto it = std::ranges::find(_waiting_for_tiles, ticket);
                assert(it != _waiting_for_tiles.end());
                _waiting_for_tiles.erase(it);
                break;
            }
            case Batch::ReadyToSample:
            {
                auto it = std::ranges::find(_ready_to_sample, ticket);
                assert(it != _ready_to_sample.end());
                _ready_to_sample.erase(it);
                break;
            }
            case Batch::Sampling:
            {
                auto it = std::ranges::find(_sampling, ticket);
                assert(it != _sampling.end());
                _sampling.erase(it);
                break;
            }
            case Batch::Finished:
            {
                auto it = std::ranges::find(_finished, ticket);
                assert(it != _finished.end());
                _finished.erase(it);
                break;
            }
            default: assert(!"Unknown elevation query batch status.");
        }
    }

    if (should_release_tiles)
    {
        for (const auto& raster_tile : batch->tiles)
        {
            auto* raster = collection->get_raster_by_id(raster_tile.raster_id);
            if (raster)
            {
                raster->provider->release_tile(raster_tile.lock_ticket);
            }
        }

        batch->tiles.clear();
    }
}

void hrz::planet::ElevationQuery::_start_culling(
    JobScheduler* js,
    uint64_t raster_ids_hash,
    std::span<const Raster*> rasters,
    std::span<const hrz::planet::TiledRasterGeometry> raster_geometries,
    Ticket ticket)
{
    Batch* batch = _batchs.get_object(ticket);
    assert(batch && batch->status == Batch::Queued);

    hrz_jobs::CullPointsQueryParams job_data;
    job_data.points = batch->points;

    assert(rasters.size() == raster_geometries.size());
    for (unsigned int i = 0; i < rasters.size(); ++i)
    {
        job_data.rasters.push_back({raster_geometries[i], rasters[i]->display_bounds});
    }

    batch->cull_ticket = hrz_jobs::add_job_cull_points_query(js, job_data, batch->resource_owner);
    batch->raster_count = rasters.size();
    batch->raster_ids_hash = raster_ids_hash;
    batch->status = Batch::Culling;
    _culling.push_back(ticket);
}

void hrz::planet::ElevationQuery::_start_waiting_for_tiles(
    JobScheduler* js,
    AssetsLoader* al,
    Ticket ticket,
    std::span<const Raster*> rasters)
{
    Batch* batch = _batchs.get_object(ticket);
    assert(batch && batch->status == Batch::Culling);

    hrz_jobs::CulledPointsQuery results;
    hrz_jobs::get_job_response(js, batch->cull_ticket, results);

    batch->tiles.clear();

    for (const auto& tile : results.tiles)
    {
        auto lock_ticket =
            rasters[tile.raster_index]->provider->request_and_lock_tile(tile.coords, al);

        batch->tiles.push_back(
            {tile.raster_index, rasters[tile.raster_index]->id, tile.coords, lock_ticket});
    }

    batch->status = Batch::WaitingForTiles;
    _waiting_for_tiles.push_back(ticket);
}

bool hrz::planet::ElevationQuery::_all_tiles_available(
    const Batch* batch,
    std::span<const Raster*> rasters)
{
    for (const auto& tile : batch->tiles)
    {
        assert(tile.raster_index < rasters.size());
        auto status = rasters[tile.raster_index]->provider->get_tile_status(tile.lock_ticket);

        if (status == hrz::planet::RasterProvider::TileStatus::Loading) return false;
    }

    return true;
}

void hrz::planet::ElevationQuery::_start_sampling(
    JobScheduler* js,
    BlobAllocator* ba,
    std::span<const Raster*> rasters,
    std::span<const hrz::planet::TiledRasterGeometry> raster_geometries,
    Ticket ticket,
    Batch* batch)
{
    assert(rasters.size() == raster_geometries.size());

    hrz_jobs::SamplePointsQueryParams job_data;
    job_data.points = batch->points;

    for (unsigned int i = 0; i < rasters.size(); ++i)
    {
        hrz_jobs::SamplePointsQueryParams::Raster job_raster;
        job_raster.image_format = rasters[i]->provider->get_image_format();
        job_raster.geometry = raster_geometries[i];
        job_raster.nodata.CopyFrom(rasters[i]->provider->get_nodata());
        job_raster.sampling.CopyFrom(rasters[i]->sampling);
        job_raster.blending.CopyFrom(rasters[i]->blending);
        job_raster.display_bounds = rasters[i]->display_bounds;
        job_data.rasters.push_back(job_raster);
    }

    for (const auto& tile : batch->tiles)
    {
        const auto& raster = rasters[tile.raster_index];
        auto coords_and_image = raster->provider->get_tile_image(tile.lock_ticket);

        if (coords_and_image.image.valid())
        {
            hrz_jobs::SamplePointsQueryParams::TileWithImage pb_tile;
            pb_tile.raster_index = tile.raster_index;
            pb_tile.tile_coords = coords_and_image.coords;

            assert(coords_and_image.image.proto_format() == raster->provider->get_image_format());
            pb_tile.image = std::move(coords_and_image.image);

            job_data.tiles.push_back(pb_tile);
        }
    }

    batch->sample_ticket =
        hrz_jobs::add_job_sample_points_query(js, job_data, batch->resource_owner);
    batch->status = Batch::Sampling;
    _sampling.push_back(ticket);
}

void hrz::planet::ElevationQuery::_handle_finished_batch(Ticket ticket, Batch* batch)
{
    if (batch->query_id.has_value())
    {
        auto it = _channels.find(batch->query_id->channel_id);
        if (it != _channels.end())
        {
            auto& channel = it->second;
            channel.send(elevation_query::messages::ElevationQueryResult{
                batch->query_id->query_id, std::move(batch->elevations)});

            if (batch->query_id.has_value())
            {
                auto it = _query_ids_to_tickets.find(batch->query_id.value());
                if (it != _query_ids_to_tickets.end())
                {
                    _query_ids_to_tickets.erase(it);
                }
            }

            _batchs.release(ticket);
        }
    }
    else
    {
        _finished.push_back(ticket);
    }
}

void hrz::planet::ElevationQuery::work(
    JobScheduler* js,
    AssetsLoader* al,
    BlobAllocator* ba,
    IRasterCollection* collection,
    RasterMergeGroup* rg)
{
    _channels.work();

    for (auto& it : _channels)
    {
        auto channel_id = it.first;
        auto& channel = it.second;

        for (auto& generic_message : channel.receive())
        {
            std::visit(
                hrz::overload{
                    [&](elevation_query::messages::QueryElevation& message)
                    {
                        ElevationQueryId query_id{channel_id, message.query_id};
                        auto ticket = start_query(
                            std::move(message.points), message.resource_owner, {query_id});
                        if (!_query_ids_to_tickets.contains(query_id))
                        {
                            _query_ids_to_tickets.insert({query_id, ticket});
                        }
                        else
                        {
                            HRZ_LOG_ERROR(
                                "Duplicated elevation query ID: {}-{}", query_id.channel_id,
                                query_id.query_id);
                        }
                    },
                    [&](elevation_query::messages::CancelElevationQuery& message)
                    {
                        ElevationQueryId query_id{channel_id, message.query_id};
                        auto it = _query_ids_to_tickets.find(query_id);
                        if (it != _query_ids_to_tickets.end())
                        {
                            queue_cancel(it->second);
                            _query_ids_to_tickets.erase(it);
                        }
                    },
                },
                generic_message);
        }
    }

    // We want to lazy-initialize those two arrays because we won't need them
    // 99% of the time.
    std::vector<hrz::planet::TiledRasterGeometry> raster_geometries_lazy(0);
    std::vector<const Raster*> rasters_lazy(0);
    uint64_t raster_ids_hash_lazy = 0;
    bool all_rasters_ready_lazy;
    bool initialized_rasters_data = false;

    auto initialize_rasters_data = [&]()
    {
        initialized_rasters_data = true;
        all_rasters_ready_lazy = true;

        auto raster_pairs = rg->get_raster();
        raster_geometries_lazy.reserve(raster_pairs.size());
        rasters_lazy.reserve(raster_pairs.size());

        for (const auto& raster_pair : raster_pairs)
        {
            const auto* raster = collection->get_raster_by_index(raster_pair.index);

            if (raster->is_visible)
            {
                if (raster->provider->get_status() == RasterProvider::Status::Ready)
                {
                    raster_geometries_lazy.push_back(raster->provider->get_geometry());
                }
                else
                {
                    all_rasters_ready_lazy = false;
                }

                rasters_lazy.push_back(raster);
                raster_ids_hash_lazy =
                    hrz::hash_mix<uint64_t>(raster_ids_hash_lazy, raster->unique_id);
            }
        }
    };

    auto get_raster_geometries = [&]() -> const std::vector<hrz::planet::TiledRasterGeometry>&
    {
        if (!initialized_rasters_data)
        {
            initialize_rasters_data();
        }
        return raster_geometries_lazy;
    };

    auto get_rasters = [&]() -> std::span<const Raster*>
    {
        if (!initialized_rasters_data)
        {
            initialize_rasters_data();
        }
        return rasters_lazy;
    };

    auto get_raster_ids_hash = [&]() -> uint64_t
    {
        if (!initialized_rasters_data)
        {
            initialize_rasters_data();
        }
        return raster_ids_hash_lazy;
    };

    auto get_all_rasters_ready = [&]() -> bool
    {
        if (!initialized_rasters_data)
        {
            initialize_rasters_data();
        }
        return all_rasters_ready_lazy;
    };

    auto has_up_to_date_rasters = [&](const Batch* batch) -> bool
    {
        return batch->raster_count == get_rasters().size()
            && batch->raster_ids_hash == get_raster_ids_hash();
    };

    auto clear_and_release_batch_tiles = [&](Batch* batch)
    {
        auto rasters = get_rasters();
        for (const auto& tile : batch->tiles)
        {
            for (auto& raster : rasters)
            {
                if (raster->id == tile.raster_id)
                {
                    raster->provider->release_tile(tile.lock_ticket);
                    break;
                }
            }
        }
        batch->tiles.clear();
    };

    for (Ticket ticket : _to_cancel)
    {
        _cancel(js, ba, ticket, collection);
        _batchs.release(ticket);
    }
    _to_cancel.clear();

    if (rg->get_revision() != _raster_group_revision)
    {
        _restart_all_batchs(js, ba, collection);
        _raster_group_revision = rg->get_revision();
    }

    if (!_queued.empty())
    {
        if (rg->is_empty())
        {
            // No need to go through the whole process, as there is no data to retrieve.
            // We can mark the requests as ready.
            while (!_queued.empty())
            {
                Ticket ticket = _queued.front();
                _queued.pop_front();

                Batch* batch = _batchs.get_object(ticket);
                assert(batch && batch->status == Batch::Queued);

                batch->status = Batch::Finished;
                _handle_finished_batch(ticket, batch);
            }
        }
        else
        {
            // Send queued batches to culling when available
            while (!_queued.empty() && _culling.size() < MaxCulling && get_all_rasters_ready())
            {
                Ticket ticket = _queued.front();
                _queued.pop_front();
                _start_culling(
                    js, get_raster_ids_hash(), get_rasters(), get_raster_geometries(), ticket);
            }
        }
    }

    // When culling is done, query tiles and wait
    for (auto it = _culling.begin(); it != _culling.end();)
    {
        Ticket ticket = *it;
        Batch* batch = _batchs.get_object(ticket);

        assert(batch && batch->status == Batch::Culling);

        if (has_up_to_date_rasters(batch))
        {
            if (hrz_jobs::is_job_finished(js, batch->cull_ticket))
            {
                if (hrz_jobs::get_job_status(js, batch->cull_ticket)
                    == hrz::job_scheduler::JobStatus::Finished_Success)
                {
                    it = _culling.erase(it);
                    _start_waiting_for_tiles(js, al, ticket, get_rasters());
                }
                else
                {
                    hrz_jobs::cancel_job(js, batch->cull_ticket);

                    batch->status = Batch::Finished;
                    _handle_finished_batch(ticket, batch);

                    it = _culling.erase(it);
                }
            }
            else
            {
                ++it;
            }
        }
        else
        {
            hrz_jobs::cancel_job(js, batch->cull_ticket);

            _queued.push_back(ticket);

            batch->status = Batch::Queued;
            it = _culling.erase(it);
        }
    }

    // @Todo Should we limit the number of batchs we check per work?
    // When all tiles are available, queue to sample
    for (auto it = _waiting_for_tiles.begin(); it != _waiting_for_tiles.end();)
    {
        Ticket ticket = *it;
        Batch* batch = _batchs.get_object(ticket);

        assert(batch && batch->status == Batch::WaitingForTiles);

        if (has_up_to_date_rasters(batch))
        {
            if (_all_tiles_available(batch, get_rasters()))
            {
                it = _waiting_for_tiles.erase(it);
                _ready_to_sample.push_back(ticket);
                batch->status = Batch::ReadyToSample;
            }
            else
            {
                ++it;
            }
        }
        else
        {
            clear_and_release_batch_tiles(batch);

            it = _waiting_for_tiles.erase(it);
            _queued.push_back(ticket);
            batch->status = Batch::Queued;
        }
    }

    // Sample a few points at every frame, and when finished set to finished
    while (!_ready_to_sample.empty() && _sampling.size() < MaxSampling && get_all_rasters_ready())
    {
        Ticket ticket = _ready_to_sample.front();
        _ready_to_sample.pop_front();

        Batch* batch = _batchs.get_object(ticket);
        assert(batch && batch->status == Batch::ReadyToSample);

        if (has_up_to_date_rasters(batch))
        {
            _start_sampling(js, ba, get_rasters(), get_raster_geometries(), ticket, batch);
        }
        else
        {
            clear_and_release_batch_tiles(batch);

            _queued.push_back(ticket);
            batch->status = Batch::Queued;
        }
    }

    // When finished, get results and release tiles.
    for (auto it = _sampling.begin(); it != _sampling.end();)
    {
        Ticket ticket = *it;
        Batch* batch = _batchs.get_object(ticket);
        assert(batch && batch->status == Batch::Sampling);

        if (has_up_to_date_rasters(batch))
        {
            if (hrz_jobs::is_job_finished(js, batch->sample_ticket))
            {
                it = _sampling.erase(it);

                if (hrz_jobs::get_job_status(js, batch->sample_ticket)
                    == hrz::job_scheduler::JobStatus::Finished_Success)
                {
                    hrz_jobs::SampledPointsQuery response;
                    hrz_jobs::get_job_response(js, batch->sample_ticket, response);

                    batch->elevations = std::move(response.values);
                }
                else
                {
                    hrz_jobs::cancel_job(js, batch->sample_ticket);
                }

                auto rasters = get_rasters();
                for (const auto& tile : batch->tiles)
                {
                    rasters[tile.raster_index]->provider->release_tile(tile.lock_ticket);
                }
                batch->tiles.clear();

                batch->status = Batch::Finished;
                _handle_finished_batch(ticket, batch);
            }
            else
            {
                ++it;
            }
        }
        else
        {
            hrz_jobs::cancel_job(js, batch->sample_ticket);
            clear_and_release_batch_tiles(batch);
            _queued.push_back(ticket);
            batch->status = Batch::Queued;
            it = _sampling.erase(it);
        }
    }
}

hrz::planet::ElevationQuery::Channel hrz::planet::ElevationQuery::create_channel()
{
    return _channels.create_channel().second;
}

void hrz::planet::ElevationQuery::dev_ui(mu_Context* ctx, const char* window_name)
{
    if (mu_begin_window_ex(ctx, window_name, mu_rect(300, 100, 400, 300), MU_OPT_CLOSED))
    {
        fmt::memory_buffer buffer;

        static int layout[] = {120, -1};
        mu_layout_row(ctx, 2, layout, 0);

        mu_text(ctx, "Queued");
        mu_text(ctx, hrz::format_to_buffer(buffer, "{}", _queued.size()));
        mu_text(ctx, "Culling");
        mu_text(ctx, hrz::format_to_buffer(buffer, "{}", _culling.size()));
        mu_text(ctx, "Waiting for tiles");
        mu_text(ctx, hrz::format_to_buffer(buffer, "{}", _waiting_for_tiles.size()));
        mu_text(ctx, "Ready to sample");
        mu_text(ctx, hrz::format_to_buffer(buffer, "{}", _ready_to_sample.size()));
        mu_text(ctx, "Sampling");
        mu_text(ctx, hrz::format_to_buffer(buffer, "{}", _sampling.size()));
        mu_text(ctx, "Finished");
        mu_text(ctx, hrz::format_to_buffer(buffer, "{}", _finished.size()));
        mu_text(ctx, "To cancel");
        mu_text(ctx, hrz::format_to_buffer(buffer, "{}", _to_cancel.size()));

        mu_end_window(ctx);
    }
}
