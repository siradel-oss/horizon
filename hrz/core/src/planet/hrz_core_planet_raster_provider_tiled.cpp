#include "assets_loader/hrz_core_assets_loader.h"
#include "planet/hrz_core_planet_raster_provider.h"
#include "planet/hrz_core_planet_tile_fetcher.h"

#include <hrz_common_crs_database.h>
#include <hrz_common_tickets.h>

#include <cassert>

namespace hrz::planet
{
namespace
{
unsigned int level_zero_tile_count_y(const hrz_proto::TilingSchemeParams& tiling_scheme)
{
    switch (tiling_scheme.type())
    {
        case hrz_proto::TilingSchemeType::GLOBAL:
            return tiling_scheme.global_tiling().level_zero_tile_count_y();
        case hrz_proto::TilingSchemeType::LOCAL: return 1;
        default: assert(false && "Unhandled case"); return 1;
    }
}
} // namespace

bool is_provider_model_complete(const hrz_proto::TiledRasterProviderParams& model)
{
    if (model.url_pattern().empty()) return false;

    const auto& geometry = model.geometry();

    if (!check_crs(geometry.projection())) return false;

    if (!is_tiling_scheme_model_complete(model.tiling_scheme())) return false;

    return true;
}

hrz_proto::ImageFormat get_image_format(const hrz_proto::TiledRasterProviderParams& params)
{
    return params.image_format();
}

class TiledImageRasterProvider : public RasterProvider
{
public:
    TiledImageRasterProvider(
        const hrz_proto::TiledRasterProviderParams& params,
        assets_loader::Queue queue,
        uint32_t default_tile_cache_size,
        uint64_t raster_id) :
        image_format(params.image_format()),
        nodata(params.nodata()),
        url(params.url_pattern()),
        al_queue(queue),
        missing_tile_policy(params.missing_tile_policy()),
        fetcher(
            std::make_unique<UrlTileRequester>(
                std::make_unique<PatternTileUrlGenerator>(
                    params.url_pattern(),
                    level_zero_tile_count_y(params.tiling_scheme())),
                ((!params.mime_type_override().empty())
                     ? std::optional<std::string>(params.mime_type_override())
                     : std::nullopt),
                assets_loader::from_proto(params.http_headers())),
            get_min_lod(params.tiling_scheme()),
            missing_tile_policy == hrz_proto::MissingTilePolicy::USE_LOWER_RESOLUTION,
            std::make_unique<ImageTileDecoder>(image_format, raster_id),
            std::make_unique<SimpleTileAttributionPolicy>(params.attribution()),
            params.override_tile_cache_size() ? params.tile_cache_size() : default_tile_cache_size,
            raster_id,
            TileFetcher::MetricInfo{
                provider_request_tally_metric_name(
                    hrz_proto::RasterProviderType::TILED_RASTER_PROVIDER),
                params.url_pattern().c_str()}),
        geometry(TiledRasterGeometry::from_geometry_and_tiling_scheme(
            params.geometry(),
            params.tiling_scheme()))
    {
    }

    hrz_proto::RasterProviderType get_raster_provider_type() const override
    {
        return hrz_proto::RasterProviderType::TILED_RASTER_PROVIDER;
    }

    Status get_status() const override { return Status::Ready; }

    hrz_proto::ImageFormat get_image_format() const override { return image_format; }

    void set_load_queue(assets_loader::Queue queue) override { al_queue = queue; }

    TileStatus get_tile_status(LockTicket lock_ticket) override
    {
        return fetcher.get_tile_status((TileFetcher::LockTicket)lock_ticket);
    }

    TileImage get_tile_image(LockTicket lock_ticket) override
    {
        return fetcher.get_tile_image((TileFetcher::LockTicket)lock_ticket);
    }

    const hrz::planet::TiledRasterGeometry& get_geometry() const override { return geometry; }

    const hrz_proto::RasterNodata& get_nodata() const override { return nodata; }

    void cancel_jobs_and_release_tiles(AssetsLoader* al, BlobAllocator* ba, JobScheduler* js)
        override
    {
        fetcher.cancel_jobs_and_release_tiles(al, ba, js);
    }

    void release_tile(LockTicket lock_ticket, bool nominate_for_eviction = false) override
    {
        fetcher.release_tile((TileFetcher::LockTicket)lock_ticket, nominate_for_eviction);
    }

    LockTicket request_and_lock_tile(TileCoords tile_coords, AssetsLoader* al, uint32_t priority)
        override
    {
        return (LockTicket)fetcher.request_and_lock_tile(tile_coords, al, al_queue, priority);
    }

    LockTicket lock_tile_if_ready(TileCoords tile_coords) override
    {
        return (LockTicket)fetcher.lock_tile_if_ready(tile_coords);
    }

    void work(
        AssetsLoader* al,
        BlobAllocator* ba,
        JobScheduler* js,
        AttributionRegistry* attributions) override
    {
        assert(al && ba && js);
        fetcher.work(al, ba, js, attributions);
    }

    bool is_working() const override { return fetcher.is_working(); }

    UpdateAction notify_model_update(
        const scene_model::RasterProviderPath& path,
        const hrz_proto::RasterProvider& provider_model) override
    {
        if (path.leaf() || path.is_type())
        {
            return UpdateAction::RecreateProvider;
        }
        else if (path.is_tiled())
        {
            auto provider_path = path.clone().tiled();
            if (provider_path.is_http_headers())
            {
                if (set_http_headers(
                        assets_loader::from_proto(provider_model.tiled().http_headers())))
                {
                    return UpdateAction::RecreateProvider;
                }
                else
                {
                    return UpdateAction::KeepTiles;
                }
            }
            else
            {
                return UpdateAction::RecreateProvider;
            }
        }
        else
        {
            return UpdateAction::KeepTiles;
        }
    }

    bool set_http_headers(const HttpHeaders& http_headers) override
    {
        return fetcher.set_http_headers(http_headers);
    }

    void dev_ui(mu_Context* ctx) override
    {
        fmt::memory_buffer buffer;

        fmt::format_to(std::back_inserter(buffer), "TileWebMapProvider [{}]", url);
        buffer.push_back(0);

        if (mu_begin_treenode(ctx, buffer.data()))
        {
            fetcher.dev_ui(ctx);
            mu_end_treenode(ctx);
        }
    }

private:
    hrz_proto::ImageFormat image_format;
    hrz_proto::RasterNodata nodata;
    std::string url;
    assets_loader::Queue al_queue;
    hrz_proto::MissingTilePolicy missing_tile_policy;

    TileFetcher fetcher;
    hrz::planet::TiledRasterGeometry geometry;
};

std::unique_ptr<RasterProvider> create_tiled_provider(
    const hrz_proto::TiledRasterProviderParams& params,
    assets_loader::Queue queue,
    uint32_t default_tile_cache_size,
    uint64_t raster_id)
{
    return std::make_unique<TiledImageRasterProvider>(
        params, queue, default_tile_cache_size, raster_id);
}

} // namespace hrz::planet
