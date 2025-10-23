#include "planet/hrz_core_planet_tile_fetcher.h"

#include <hrz_common_fmt.h>
#include <hrz_common_profiling.h>
#include <hrz_fnd_format.h>
#include <hrz_fnd_time.h>

namespace hrz::planet
{
namespace
{
TileFetcher::LockTicket generate_lock_ticket()
{
    static std::atomic<uint64_t> lock_ticket_generator;
    return (TileFetcher::LockTicket)++lock_ticket_generator;
}
} // namespace

ImageTileDecoder::Ticket ImageTileDecoder::decode_tile(
    blobs::BlobHandle blob,
    std::string_view mime_type,
    JobScheduler* js)
{
    auto handle = tiles.alloc();
    auto tile = tiles.get_object(handle);
    tile->status = Status::Decoding;
    tile->decode_ticket = image_decoder::decode_async(
        js, blob, {monitoring::systems::PlanetSurface, raster_id}, image_format, mime_type);
    return handle;
}

ImageTileDecoder::Status ImageTileDecoder::get_tile_status(Ticket handle)
{
    auto tile = tiles.get_object(handle);
    if (tile != nullptr)
    {
        return tile->status;
    }
    return Status::Error;
}

BlobImage ImageTileDecoder::get_tile_image(Ticket handle)
{
    auto tile = tiles.get_object(handle);
    assert(tile != nullptr && tile->status == Status::Decoded);
    auto image = std::move(tile->image);
    tiles.release(handle);
    return image;
}

void ImageTileDecoder::cancel_tile(ImageTileDecoder::Ticket handle, JobScheduler* js)
{
    auto tile = tiles.get_object(handle);
    if (tile != nullptr)
    {
        if (tile->status == Status::Decoding)
        {
            hrz_jobs::cancel_job(js, tile->decode_ticket);
        }
        tiles.release(handle);
    }
}

void ImageTileDecoder::work_tile(ImageTileDecoder::Ticket handle, JobScheduler* js)
{
    auto tile = tiles.get_object(handle);
    if (tile != nullptr && tile->status == Status::Decoding)
    {
        if (hrz_jobs::is_job_valid(js, tile->decode_ticket)
            && hrz_jobs::is_job_finished(js, tile->decode_ticket))
        {
            auto status = hrz_jobs::get_job_status(js, tile->decode_ticket);
            if (status == job_scheduler::JobStatus::Finished_Success)
            {
                tile->image = image_decoder::job_to_image(js, tile->decode_ticket);
                tile->status = Status::Decoded;
            }
            else
            {
                hrz_jobs::cancel_job(js, tile->decode_ticket);
                tile->status = Status::Error;
            }
        }
    }
}

TileFetcher::TileFetcher(
    std::unique_ptr<TileRequester> tile_requester,
    uint32_t lod_min,
    bool try_lower_resolution,
    std::unique_ptr<TileDecoder> tile_decoder,
    std::unique_ptr<TileAttributionPolicy> tile_attribution,
    size_t cache_capacity,
    uint64_t raster_id,
    MetricInfo metric_info) :
    _tile_requester(std::move(tile_requester)),
    _try_lower_resolution(try_lower_resolution),
    _cache_capacity(cache_capacity),
    _lod_min(lod_min),
    _tile_decoder(std::move(tile_decoder)),
    _tile_attribution(std::move(tile_attribution)),
    _raster_id(raster_id)
{
    _request_count_metric =
        metrics::MetricDesc(metric_info.name, false, {{"url", metric_info.url}});
}

RasterProvider::TileStatus TileFetcher::get_tile_status(LockTicket lock_ticket) const
{
    auto it = _active_locks.find(lock_ticket);
    if (it == _active_locks.end())
    {
        // Missing tiles are always loaded.
        return RasterProvider::TileStatus::Loaded;
    }
    else
    {
        const Tile* tile = _tiles_pool.get_object(it->second);
        assert(tile);
        if (tile->status == Tile::Status::UseLowerRes)
        {
            return get_tile_status(std::get<Tile::ParentTicket>(tile->payload).ticket);
        }
        else
        {
            return convert_tile_status(tile->status);
        }
    }
}

RasterProvider::TileImage TileFetcher::get_tile_image(LockTicket lock_ticket)
{
    auto it = _active_locks.find(lock_ticket);
    if (it == _active_locks.end())
    {
        return {};
    }
    else
    {
        Tile* tile = _tiles_pool.get_object(it->second);
        assert(tile);

        tile->last_touch_time_ms = now_frame_ms();

        switch (tile->status)
        {
            case Tile::Status::Loaded:
            {
                auto attributions = _tile_attribution->get_tile_attribution(tile->coords);
                return {
                    tile->coords,
                    std::get<BlobImage>(tile->payload),
                    {attributions.begin(), attributions.end()}};
            }
            case Tile::Status::UseLowerRes:
                return get_tile_image(std::get<Tile::ParentTicket>(tile->payload).ticket);
            default: return {tile->coords, BlobImage{}, {}};
        }
    }
}

void TileFetcher::release_tile(LockTicket lock_ticket, bool nominate_for_eviction)
{
    auto it = _active_locks.find(lock_ticket);
    if (it != _active_locks.end())
    {
        Tile* tile = _tiles_pool.get_object(it->second);
        assert(tile);

        tile->lock_count -= 1;
        assert(tile->lock_count >= 0 && "Error: invalid tile 'lock_count' in provider.");

        if (tile->lock_count == 0 && nominate_for_eviction)
        {
            tile->last_touch_time_ms = 0;
        }

        _active_locks.erase(it);
    }
}

TileFetcher::LockTicket TileFetcher::request_and_lock_tile(
    TileCoords tile_coords,
    AssetsLoader* al,
    assets_loader::Queue queue,
    uint32_t priority)
{
    LockTicket lock_ticket = generate_lock_ticket();

    auto it = _coords_to_tile_handle.find(tile_coords);
    if (it == _coords_to_tile_handle.end())
    {
        Tile tile;
        tile.status = Tile::Status::Loading;
        tile.coords = tile_coords;
        tile.payload = Tile::LoadingTicket{
            _tile_requester->request_tile(
                tile_coords, al, queue, priority, {monitoring::systems::PlanetSurface, _raster_id}),
            queue, priority};
        tile.last_touch_time_ms = now_frame_ms();
        tile.lock_count = 1;

        TileHandle handle = _tiles_pool.alloc();
        Tile* t = _tiles_pool.get_object(handle);
        *t = std::move(tile);

        _coords_to_tile_handle[tile_coords] = handle;
        _loading_tiles.insert(handle);
        _ordered_tiles.push_back(handle);
        _active_locks[lock_ticket] = handle;

        metrics::increment_counter(&_request_count_metric);
        _download_count++;
    }
    else
    {
        Tile* tile = _tiles_pool.get_object(it->second);
        tile->last_touch_time_ms = now_frame_ms();
        tile->lock_count += 1;
        _active_locks[lock_ticket] = it->second;
    }

    return lock_ticket;
}

TileFetcher::LockTicket TileFetcher::lock_tile_if_ready(TileCoords tile_coords)
{
    auto it = _coords_to_tile_handle.find(tile_coords);
    if (it != _coords_to_tile_handle.end())
    {
        Tile* tile = _tiles_pool.get_object(it->second);

        if (tile->status == Tile::Status::Loaded)
        {
            tile->lock_count += 1;
            tile->last_touch_time_ms = now_frame_ms();

            auto lock_ticket = generate_lock_ticket();
            _active_locks[lock_ticket] = it->second;
            return lock_ticket;
        }
        else if (tile->status == Tile::Status::UseLowerRes && tile_coords.lod > _lod_min)
        {
            return lock_tile_if_ready(tile_coords.parent());
        }
    }

    return TileFetcher::LockTicket::Invalid;
}

void TileFetcher::cancel_jobs_and_release_tiles(
    AssetsLoader* al,
    BlobAllocator* ba,
    JobScheduler* js)
{
    for (auto handle : _loading_tiles)
    {
        Tile* tile = _tiles_pool.get_object(handle);
        if (!tile) continue;

        _tile_requester->cancel(std::get<Tile::LoadingTicket>(tile->payload).ticket, al, js);
        tile->status = Tile::Status::Canceled;
        tile->payload = Tile::Empty{};
    }
    _loading_tiles.clear();

    for (auto handle : _decoding_tiles)
    {
        Tile* tile = _tiles_pool.get_object(handle);
        if (!tile) continue;

        _tile_decoder->cancel_tile(std::get<Tile::DecodingTicket>(tile->payload).ticket, js);
        tile->status = Tile::Status::Canceled;
        tile->payload = Tile::Empty{};
    }
    _decoding_tiles.clear();

    for (const auto& it : _coords_to_tile_handle)
    {
        _tiles_pool.release(it.second);
    }

    _tile_requester->cancel_all(al, ba);
}

void TileFetcher::evict_tile(
    AssetsLoader* al,
    BlobAllocator* ba,
    JobScheduler* js,
    TileHandle handle)
{
    Tile* tile = _tiles_pool.get_object(handle);
    if (!tile) return;

    assert(tile->lock_count == 0);

    if (tile->status == Tile::Status::Loading)
    {
        _tile_requester->cancel(std::get<Tile::LoadingTicket>(tile->payload).ticket, al, js);
        auto it = _loading_tiles.find(handle);
        if (it != _loading_tiles.end())
        {
            _loading_tiles.erase(it);
        }
    }
    else if (tile->status == Tile::Status::Decoding)
    {
        _tile_decoder->cancel_tile(std::get<Tile::DecodingTicket>(tile->payload).ticket, js);
        auto it = _decoding_tiles.find(handle);
        if (it != _decoding_tiles.end())
        {
            _decoding_tiles.erase(it);
        }
    }
    else if (tile->status == Tile::Status::UseLowerRes)
    {
        release_tile(std::get<Tile::ParentTicket>(tile->payload).ticket);
    }

    auto it = _coords_to_tile_handle.find(tile->coords);
    assert(it != _coords_to_tile_handle.end());
    _coords_to_tile_handle.erase(it);

    _tiles_pool.release(handle);
}

void TileFetcher::work(
    AssetsLoader* al,
    BlobAllocator* ba,
    JobScheduler* js,
    AttributionRegistry* attributions)
{
    HRZ_SCOPED_SAMPLE("provider image fetcher work");

    _tile_attribution->work(attributions);
    _tile_requester->work(al, js, ba);

    // Sort tiles from most recently used to least recently used. This allows iterating the
    // array back to front and thus making the erase operation as simple as a pop_back().
    // @Note: `last_touch_time` is ms since epoch. So, higher values mean more recent tiles.
    std::ranges::sort(
        _ordered_tiles,
        [&](TileHandle h1, TileHandle h2)
        {
            const Tile* t1 = _tiles_pool.get_object(h1);
            const Tile* t2 = _tiles_pool.get_object(h2);
            return t1->last_touch_time_ms > t2->last_touch_time_ms;
        });

    // Evicting tiles that need to be.
    for (int i = _ordered_tiles.size() - 1; i >= 0; --i)
    {
        Tile* tile = _tiles_pool.get_object(_ordered_tiles[i]);
        assert(tile);

        if (tile->lock_count != 0) continue;

        // Evict tile when:
        // - it has been tagged as 'old' (last_touch_time == 0), with no regard to the cache
        // capacity.
        // - the cache capacity has been reached.
        if (tile->last_touch_time_ms == 0 || _coords_to_tile_handle.size() > _cache_capacity)
        {
            evict_tile(al, ba, js, _ordered_tiles[i]);
            std::swap(_ordered_tiles[i], _ordered_tiles.back());
            _ordered_tiles.pop_back();
        }
    }

    // Download tiles.
    for (auto it = std::begin(_loading_tiles); it != std::end(_loading_tiles);)
    {
        Tile* tile = _tiles_pool.get_object(*it);
        if (!tile)
        {
            _loading_tiles.erase(it++);
            continue;
        }

        assert(tile->status == Tile::Status::Loading);

        auto load_ticket = std::get<Tile::LoadingTicket>(tile->payload);

        if (_tile_requester->is_finished(load_ticket.ticket, al))
        {
            HRZ_SCOPED_SAMPLE("load job finished");

            if (_tile_requester->is_success(load_ticket.ticket, al))
            {
                auto [blob, mime_type] = _tile_requester->retrieve_blob(load_ticket.ticket, al, ba);
                if (blob.data_size() > 0)
                {
                    Tile::DecodingTicket decode_ticket{};
                    decode_ticket.ticket = _tile_decoder->decode_tile(blob, mime_type, js);
                    tile->status = Tile::Status::Decoding;
                    tile->payload = decode_ticket;
                    _decoding_tiles.insert(*it);
                }
            }

            _tile_requester->cancel(load_ticket.ticket, al, js);
            _loading_tiles.erase(it++);

            if (tile->status != Tile::Status::Decoding)
            {
                if (_try_lower_resolution && tile->coords.lod > _lod_min)
                {
                    auto parent_lock_ticket = request_and_lock_tile(
                        tile->coords.parent(), al, load_ticket.queue, load_ticket.priority);

                    if (parent_lock_ticket != TileFetcher::LockTicket::Invalid)
                    {
                        tile->status = Tile::Status::UseLowerRes;
                        tile->payload = Tile::ParentTicket{parent_lock_ticket};
                    }
                    else
                    {
                        tile->status = Tile::Status::Missing;
                        tile->payload = Tile::Empty{};
                    }
                }
                else
                {
                    HRZ_LOG_WARNING("Could not load tile {}", tile->coords);

                    tile->status = Tile::Status::Missing;
                    tile->payload = Tile::Empty{};
                }
            }
        }
        else
        {
            it++;
        }
    }

    // Decode tiles.
    for (auto it = std::begin(_decoding_tiles); it != std::end(_decoding_tiles);)
    {
        Tile* tile = _tiles_pool.get_object(*it);
        if (!tile)
        {
            _decoding_tiles.erase(it++);
            continue;
        }

        assert(tile->status == Tile::Status::Decoding);

        auto decode_ticket = std::get<Tile::DecodingTicket>(tile->payload);

        _tile_decoder->work_tile(decode_ticket.ticket, js);

        auto decode_status = _tile_decoder->get_tile_status(decode_ticket.ticket);

        if (decode_status == TileDecoder::Status::Decoded)
        {
            HRZ_SCOPED_SAMPLE("decode job finished");

            auto img = _tile_decoder->get_tile_image(decode_ticket.ticket);

            if (img.valid())
            {
                img.register_blob_metadata(ba, "image type"_ss, "planet tile"_ss);
                img.register_blob_metadata(
                    ba, "Origin"_ss, _tile_requester->make_description(tile->coords));
                img.register_blob_owner(ba, {monitoring::systems::PlanetSurface, _raster_id});

                tile->status = Tile::Status::Loaded;
                tile->payload = std::move(img);
            }
            else
            {
                HRZ_LOG_WARNING("Could not decode image for tile {}", tile->coords);
                tile->status = Tile::Status::Missing;
                tile->payload = Tile::Empty{};
            }

            _decoding_tiles.erase(it++);
        }
        else if (decode_status == TileDecoder::Status::Error)
        {
            HRZ_LOG_WARNING("Could not decode image for tile {}", tile->coords);
            tile->status = Tile::Status::Missing;
            tile->payload = Tile::Empty{};

            _tile_decoder->cancel_tile(decode_ticket.ticket, js);

            _decoding_tiles.erase(it++);
        }
        else
        {
            it++;
        }
    }
}

bool TileFetcher::is_working() const
{
    return _loading_tiles.size() != 0 || _decoding_tiles.size() != 0;
}

void TileFetcher::dev_ui(mu_Context* ctx)
{
    static int layout[] = {100, 100, -1};

    unsigned int images_count = 0;
    unsigned int bytes_count = 0;

    for (const auto& it : _coords_to_tile_handle)
    {
        const Tile* tile = _tiles_pool.get_object(it.second);
        if (tile->status == Tile::Status::Loaded && std::get<BlobImage>(tile->payload).valid())
        {
            ++images_count;
            bytes_count += std::get<BlobImage>(tile->payload).size_in_bytes();
        }
    }

    mu_layout_row(ctx, 3, layout, 0);

    fmt::memory_buffer buffer;

    buffer.clear();
    fmt::format_to(std::back_inserter(buffer), "images: {} / {}", images_count, _cache_capacity);
    buffer.push_back(0);
    mu_text(ctx, buffer.data());

    buffer.clear();
    fmt::format_to(std::back_inserter(buffer), "memory: ");
    bytes_to_string(bytes_count, buffer, false, true);
    mu_text(ctx, buffer.data());

    buffer.clear();
    fmt::format_to(std::back_inserter(buffer), "total download count: {}", _download_count);
    buffer.push_back(0);
    mu_text(ctx, buffer.data());
}

void WebMercatorZonesTileAttributionPolicy::add_zone(
    int lod_min,
    int lod_max,
    const hrz::GeoBounds& geo_bounds,
    AttributionHandle attribution)
{
    _zones.push_back({lod_min, lod_max, hrz::geo_to_web_mercator(geo_bounds), attribution});
    std::ranges::sort(
        _zones, [](const Zone& a, const Zone& b) -> bool { return a.lod_min < b.lod_min; });
}

hrz::InlinedVector<AttributionHandle, 4> WebMercatorZonesTileAttributionPolicy::
    get_tile_attribution(TileCoords tile) const
{
    auto last = std::ranges::upper_bound(
        _zones, (int)tile.lod, std::less<int>{},
        [](const Zone& element) { return element.lod_min; });

    auto bounds = hrz::mercator_tile_bbox_meters(tile);

    hrz::InlinedVector<AttributionHandle, 4> handles;
    for (auto it = _zones.begin(); it != last; ++it)
    {
        if (it->lod_max >= tile.lod && lm::intersect(bounds, it->webmercator_bounds))
        {
            handles.push_back(it->attribution);
        }
    }

    return handles;
}

} // namespace hrz::planet
