// SPDX-FileCopyrightText: Copyright 2021 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/common/color.h"
#include "hrz/common/fmt.h" // IWYU pragma: keep
#include "hrz/common/image_processing.h"
#include "hrz/common/palette.h"
#include "hrz/core/jobs/jobs_tickets.h"
#include "hrz/core/jobs/palettize_image.h"
#include "hrz/core/planet/raster_provider.h"
#include "hrz/fnd/flat_hash_set.h"
#include "hrz/fnd/format.h"
#include "hrz/fnd/gen_object_pool.h"

#include <cassert>
#include <vector>

namespace hrz::planet
{
namespace
{

RasterProvider::LockTicket generate_lock_ticket()
{
    static std::atomic<uint64_t> lock_ticket_generator;
    return (RasterProvider::LockTicket)++lock_ticket_generator;
}

} // namespace

bool is_provider_model_complete(const hrz_proto::PalettizedRasterProviderParams& model)
{
    return model.has_provider() && is_provider_model_complete(model.provider())
        && hrz::is_scalar_image_format(get_image_format(model.provider()));
}

hrz_proto::ImageFormat get_image_format(const hrz_proto::PalettizedRasterProviderParams&)
{
    return hrz_proto::ImageFormat::SRGBA_8;
}

class PalettizedRasterProvider : public RasterProvider
{
    struct PalettizedTile
    {
        enum class Status
        {
            Loading,
            Palettizing,
            Loaded,
            Error
        };

        TileCoords coords;
        uint32_t lock_count;
        Status status;
        LockTicket lock_ticket;
        hrz_jobs::PalettizeImageTicket palettize_image_ticket;
        BlobImage image;
        hrz::InlinedVector<AttributionHandle, 8> attribution;
    };

    Status status;

    using TileHandle = uint32_t;
    using IndexPool = GenIndexPool<TileHandle, 16, 16>;
    using TilesPool = GenObjectPool<PalettizedTile, IndexPool, 128>;

    TilesPool tiles;
    hrz::flat_hash_map<TileCoords, TileHandle> coords_to_tile_handle;
    hrz::flat_hash_map<RasterProvider::LockTicket, TileHandle> active_locks;

    hrz::flat_hash_set<TileHandle> loading_tiles;

    std::unique_ptr<RasterProvider> child_raster_provider;
    hrz::Palette palette;
    lm::ubvec4 nodata_color_srgb;
    hrz_proto::RasterNodata nodata;

    std::vector<hrz_jobs::PalettizeImageTicket> jobs_to_cancel;
    uint64_t raster_id;

public:
    PalettizedRasterProvider(
        const hrz_proto::PalettizedRasterProviderParams& params,
        assets_loader::Queue queue,
        uint32_t default_tile_cache_size,
        uint64_t raster_id) :
        status(Status::Loading),
        child_raster_provider(
            create_provider(params.provider(), queue, default_tile_cache_size, raster_id)),
        palette(hrz::palette::from_proto(params.palette())),
        nodata_color_srgb(hrz::convert_proto_color_to_bytes(params.nodata_color())),
        nodata(params.nodata()),
        raster_id(raster_id)
    {
    }

    hrz_proto::RasterProvider::ProviderTypeCase get_raster_provider_type() const override
    {
        return hrz_proto::RasterProvider::ProviderTypeCase::kPalettized;
    }

    hrz_proto::ImageFormat get_image_format() const override
    {
        return hrz_proto::ImageFormat::SRGBA_8;
    }

    hrz_proto::ImageFormat get_source_image_format() const override
    {
        return child_raster_provider->get_source_image_format();
    }

    void set_load_queue(assets_loader::Queue queue) override
    {
        assert(status == Status::Ready);

        child_raster_provider->set_load_queue(queue);
    }

    Status get_status() const override { return status; }

    RasterProvider::TileImage get_tile_image(LockTicket lock_ticket) override
    {
        assert(status == Status::Ready);

        auto it = active_locks.find(lock_ticket);
        if (it == active_locks.end())
        {
            return {};
        }

        const auto tile = tiles.get_object(it->second);
        switch (tile->status)
        {
            case PalettizedTile::Status::Loaded:
                return {tile->coords, tile->image, tile->attribution};
            case PalettizedTile::Status::Error: return {tile->coords, BlobImage{}};
            default: assert(false); return {tile->coords, BlobImage{}};
        }
    }

    const hrz::planet::TiledRasterGeometry& get_geometry() const override
    {
        assert(status == Status::Ready);

        return child_raster_provider->get_geometry();
    }

    const hrz_proto::RasterNodata& get_nodata() const override { return nodata; }

    const hrz_proto::RasterNodata& get_source_nodata() const override
    {
        return child_raster_provider->get_source_nodata();
    }

    void cancel_jobs_and_release_tiles(AssetsLoader* al, BlobAllocator* ba, JobScheduler* js)
        override
    {
        for (auto job : jobs_to_cancel)
        {
            hrz_jobs::cancel_job(js, job);
        }
        jobs_to_cancel.clear();

        if (status == Status::Ready)
        {
            child_raster_provider->cancel_jobs_and_release_tiles(al, ba, js);

            for (auto it : coords_to_tile_handle)
            {
                auto handle = it.second;
                auto tile = tiles.get_object(handle);
                hrz_jobs::cancel_job(js, tile->palettize_image_ticket);
                tiles.release(handle);
            }

            loading_tiles.clear();
            active_locks.clear();
            coords_to_tile_handle.clear();
        }
    }

    LockTicket request_and_lock_tile(TileCoords tile_coords, AssetsLoader* al, uint32_t priority)
        override
    {
        if (status != Status::Ready) return LockTicket::Invalid;

        auto lock_ticket = generate_lock_ticket();

        auto it = coords_to_tile_handle.find(tile_coords);
        if (it != coords_to_tile_handle.end())
        {
            auto handle = it->second;
            auto tile = tiles.get_object(handle);
            tile->lock_count += 1;
            active_locks.insert({lock_ticket, handle});
        }
        else
        {
            auto handle = tiles.alloc();
            auto tile = tiles.get_object(handle);
            tile->coords = tile_coords;
            tile->lock_count = 1;
            tile->status = PalettizedTile::Status::Loading;
            tile->lock_ticket =
                child_raster_provider->request_and_lock_tile(tile_coords, al, priority);
            coords_to_tile_handle.insert({tile_coords, handle});
            active_locks.insert({lock_ticket, handle});
            loading_tiles.insert(handle);
        }

        return lock_ticket;
    }

    LockTicket lock_tile_if_ready(TileCoords tile_coords) override
    {
        if (status != Status::Ready) return LockTicket::Invalid;

        auto it = coords_to_tile_handle.find(tile_coords);
        if (it != coords_to_tile_handle.end())
        {
            auto handle = it->second;
            auto tile = tiles.get_object(handle);
            if (tile->status == PalettizedTile::Status::Loaded)
            {
                auto lock_ticket = generate_lock_ticket();

                tile->lock_count += 1;
                active_locks.insert({lock_ticket, handle});

                return lock_ticket;
            }
        }

        return LockTicket::Invalid;
    }

    void release_tile(LockTicket lock_ticket, bool nominate_for_eviction = false) override
    {
        auto it = active_locks.find(lock_ticket);
        if (it != active_locks.end())
        {
            auto handle = it->second;
            auto tile = tiles.get_object(handle);

            assert(tile->lock_count >= 1);
            tile->lock_count -= 1;

            if (tile->lock_count == 0)
            {
                if (tile->status == PalettizedTile::Status::Palettizing)
                {
                    jobs_to_cancel.push_back(tile->palettize_image_ticket);
                }

                child_raster_provider->release_tile(tile->lock_ticket, nominate_for_eviction);
                coords_to_tile_handle.erase(tile->coords);
                loading_tiles.erase(handle);
                tiles.release(handle);
            }

            active_locks.erase(it);
        }
    }

    TileStatus get_tile_status(LockTicket lock_ticket) override
    {
        if (status != Status::Ready) return TileStatus::Loading;

        auto it = active_locks.find(lock_ticket);
        if (it != active_locks.end())
        {
            auto handle = it->second;
            auto tile = tiles.get_object(handle);

            switch (tile->status)
            {
                case PalettizedTile::Status::Loading:
                case PalettizedTile::Status::Palettizing: return TileStatus::Loading;
                case PalettizedTile::Status::Loaded:
                case PalettizedTile::Status::Error: return TileStatus::Loaded;
                default: assert(false && "Unhandled case"); return TileStatus::Loaded;
            }
        }

        return TileStatus::Loaded;
    }

    SourceLockTicket request_and_lock_source_tile(
        TileCoords tile_coords,
        AssetsLoader* al,
        uint32_t priority) override
    {
        return child_raster_provider->request_and_lock_source_tile(tile_coords, al, priority);
    }

    SourceLockTicket lock_source_tile_if_ready(TileCoords tile_coords) override
    {
        if (status != Status::Ready) return SourceLockTicket::Invalid;

        return child_raster_provider->lock_source_tile_if_ready(tile_coords);
    }

    TileStatus get_source_tile_status(SourceLockTicket lock_ticket) override
    {
        return child_raster_provider->get_source_tile_status(lock_ticket);
    }

    TileImage get_source_tile_image(SourceLockTicket lock_ticket) override
    {
        return child_raster_provider->get_source_tile_image(lock_ticket);
    }

    void release_source_tile(SourceLockTicket lock_ticket) override
    {
        child_raster_provider->release_source_tile(lock_ticket);
    }

    void work(
        AssetsLoader* al,
        BlobAllocator* ba,
        JobScheduler* js,
        AttributionRegistry* attributions) override
    {
        if (status == Status::Error) return;

        for (auto job : jobs_to_cancel)
        {
            hrz_jobs::cancel_job(js, job);
        }
        jobs_to_cancel.clear();

        child_raster_provider->work(al, ba, js, attributions);

        status = child_raster_provider->get_status();

        if (status == Status::Error) return;

        for (auto it = loading_tiles.begin(); it != loading_tiles.end();)
        {
            auto handle = *it;
            auto tile = tiles.get_object(handle);

            if (tile->status == PalettizedTile::Status::Loading)
            {
                auto child_status = child_raster_provider->get_tile_status(tile->lock_ticket);

                if (child_status == TileStatus::Loaded)
                {
                    auto child_image = child_raster_provider->get_tile_image(tile->lock_ticket);
                    if (child_image.image.valid())
                    {
                        hrz_jobs::PalettizeImageParams palettize_image_params;
                        palettize_image_params.image = child_image.image;
                        palettize_image_params.nodata = child_raster_provider->get_nodata();
                        palettize_image_params.palette = palette;
                        palettize_image_params.nodata_color_srgb = nodata_color_srgb;

                        tile->attribution = std::move(child_image.attribution);
                        tile->palettize_image_ticket = hrz_jobs::add_job_palettize_image(
                            js, std::move(palettize_image_params),
                            {monitoring::systems::PlanetSurface, raster_id});
                        tile->status = PalettizedTile::Status::Palettizing;
                    }
                    else
                    {
                        tile->status = PalettizedTile::Status::Error;
                    }
                }
            }
            else if (tile->status == PalettizedTile::Status::Palettizing)
            {
                assert(hrz_jobs::is_job_valid(js, tile->palettize_image_ticket));

                if (hrz_jobs::is_job_finished(js, tile->palettize_image_ticket))
                {
                    auto image = hrz_jobs::get_job_response(js, tile->palettize_image_ticket);

                    if (image.valid() && image.width() > 0 && image.height() > 0)
                    {
                        image.register_blob_metadata(
                            ba, "image type"_ss, "palettized planet tile"_ss);
                        image.register_blob_owner(
                            ba, {monitoring::systems::PlanetSurface, raster_id});

                        tile->image = std::move(image);
                        tile->status = PalettizedTile::Status::Loaded;
                    }
                    else
                    {
                        HRZ_LOG_WARNING(
                            "Could not palettize image for tile {}-{}-{}", tile->coords.lod,
                            tile->coords.x, tile->coords.y);
                        tile->status = PalettizedTile::Status::Error;
                    }
                }
            }
            else
            {
                assert(false && "Invalid state");
            }

            if (tile->status != PalettizedTile::Status::Loading
                && tile->status != PalettizedTile::Status::Palettizing)
            {
                loading_tiles.erase(it++);
            }
            else
            {
                ++it;
            }
        }
    }

    bool is_working() const override
    {
        return status == Status::Loading
            || (status == Status::Ready && child_raster_provider->is_working());
    }

    UpdateAction notify_model_update(
        const scene_model::RasterProviderPath& path,
        const hrz_proto::RasterProvider& provider_model) override
    {
        if (path.leaf() || !path.is_palettized())
        {
            return UpdateAction::RecreateProvider;
        }
        else
        {
            auto palettized_image_provider_path = path.clone().palettized();

            if (palettized_image_provider_path.leaf())
            {
                return UpdateAction::RecreateProvider;
            }
            else if (palettized_image_provider_path.is_palette())
            {
                palette = hrz::palette::from_proto(provider_model.palettized().palette());

                return UpdateAction::RestartTiles;
            }
            else if (
                palettized_image_provider_path.is_nodata_color()
                || palettized_image_provider_path.is_nodata())
            {
                nodata_color_srgb =
                    hrz::convert_proto_color_to_bytes(provider_model.palettized().nodata_color());

                return UpdateAction::RestartTiles;
            }
            else if (palettized_image_provider_path.is_provider())
            {
                return child_raster_provider->notify_model_update(
                    palettized_image_provider_path.clone().provider(),
                    provider_model.palettized().provider());
            }
            else
            {
                return UpdateAction::RecreateProvider;
            }
        }
    }

    void dev_ui(mu_Context* ctx) override
    {
        fmt::memory_buffer buffer;

        unsigned int images_count = 0;
        unsigned int bytes_count = 0;
        for (const auto& it : coords_to_tile_handle)
        {
            auto handle = it.second;
            auto tile = tiles.get_object(handle);

            if (tile->image.valid())
            {
                ++images_count;
                bytes_count += tile->image.size_in_bytes();
            }
        }

        if (mu_begin_treenode(ctx, "PalettizedRasterProvider"))
        {
            static int layout[] = {100, -1};
            mu_layout_row(ctx, 2, layout, 0);

            buffer.clear();
            fmt::format_to(std::back_inserter(buffer), "images: {}", images_count);
            buffer.push_back(0);
            mu_text(ctx, buffer.data());

            buffer.clear();
            fmt::format_to(std::back_inserter(buffer), "memory: ");
            bytes_to_string(bytes_count, buffer, false, true);
            mu_text(ctx, buffer.data());

            child_raster_provider->dev_ui(ctx);
            mu_end_treenode(ctx);
        }
    }

    bool set_http_headers(const HttpHeaders& http_headers) override
    {
        return child_raster_provider->set_http_headers(http_headers);
    }
};

std::unique_ptr<RasterProvider> create_palettized_provider(
    const hrz_proto::PalettizedRasterProviderParams& params,
    assets_loader::Queue queue,
    uint32_t default_tile_cache_size,
    uint64_t raster_id)
{
    return std::make_unique<PalettizedRasterProvider>(
        params, queue, default_tile_cache_size, raster_id);
}

} // namespace hrz::planet
