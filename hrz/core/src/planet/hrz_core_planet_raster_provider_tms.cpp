#include "assets_loader/hrz_core_assets_loader.h"
#include "hrz_core_base_url.h"
#include "planet/hrz_core_planet_raster_provider.h"
#include "planet/hrz_core_planet_tile_fetcher.h"

#include <hrz_common_planet.h>
#include <hrz_common_tickets.h>
#include <hrz_fnd_url_utils.h>

#include <gsl/gsl-lite.hpp>
#include <pugixml/pugixml.hpp>

#include <cassert>
#include <optional>
#include <vector>

namespace
{

struct TilesetTileUrlGenerator : public hrz::TileUrlGenerator
{
    TilesetTileUrlGenerator(const hrz::BaseUrl& base_url, gsl::span<const std::string> tileset_urls)
    {
        assert(tileset_urls.size() > 0);

        this->tileset_urls.reserve(tileset_urls.size());
        for (const auto& url : tileset_urls)
        {
            this->tileset_urls.push_back(base_url.derive(url));
        }
    }

    std::string make_url(uint32_t x, uint32_t y, uint32_t z) override
    {
        if (z > tileset_urls.size())
        {
            HRZ_LOG_ERROR("Cannot generate URL for z={}", z);
            z = 0;
        }

        const auto& pattern = tileset_urls.at(z);
        const uint32_t tile_count = 1 << z;
        const uint32_t ry = tile_count - y - 1;

        return fmt::format(pattern, fmt::arg("x", x), fmt::arg("y", y), fmt::arg("ry", ry));
    }

private:
    std::vector<std::string> tileset_urls;
};
} // namespace

namespace hrz::planet
{
bool is_provider_model_complete(const hrz_proto::TmsProviderParams& model)
{
    if (model.url().empty()) return false;

    return true;
}

hrz_proto::ImageFormat get_image_format(const hrz_proto::TmsProviderParams& params)
{
    return params.image_format();
}

class TileMapServiceRasterProvider : public RasterProvider
{
public:
    TileMapServiceRasterProvider(
        const hrz_proto::TmsProviderParams& params,
        assets_loader::Queue queue,
        uint32_t default_tile_cache_size,
        uint64_t raster_id) :
        status(InternalStatus::LoadingDescriptor),
        image_format(params.image_format()),
        nodata(params.nodata()),
        url(params.url()),
        headers(assets_loader::from_proto(params.http_headers())),
        additional_attribution(params.attribution()),
        preserve_query_parameters(params.preserve_query_parameters()),
        al_queue(queue),
        missing_tile_policy(params.missing_tile_policy()),
        tile_cache_capacity(
            params.override_tile_cache_size() ? params.tile_cache_size() : default_tile_cache_size),
        download_ticket(0),
        raster_id(raster_id)
    {
    }

    hrz_proto::RasterProviderType get_raster_provider_type() const override
    {
        return hrz_proto::RasterProviderType::TILED_IMAGE_PROVIDER;
    }

    Status get_status() const override
    {
        switch (status)
        {
            case InternalStatus::LoadingDescriptor:
            case InternalStatus::ParsingDescriptor: return Status::Loading;
            case InternalStatus::Ready: return Status::Ready;
            case InternalStatus::Error: return Status::Error;
            default: assert(false && "Unhandled case"); return Status::Error;
        }
    }

    hrz_proto::ImageFormat get_image_format() const override { return image_format; }

    void set_load_queue(assets_loader::Queue queue) override { al_queue = queue; }

    TileStatus get_tile_status(LockTicket lock_ticket) override
    {
        if (!fetcher.has_value()) return TileStatus::Loading;

        return fetcher->get_tile_status((TileFetcher::LockTicket)lock_ticket);
    }

    TileImage get_tile_image(LockTicket lock_ticket) override
    {
        assert(status == InternalStatus::Ready);
        return fetcher->get_tile_image((TileFetcher::LockTicket)lock_ticket);
    }

    const hrz::planet::TiledRasterGeometry& get_geometry() const override
    {
        assert(status == InternalStatus::Ready);

        return geometry;
    }

    const hrz_proto::RasterNodata& get_nodata() const override { return nodata; }

    void cancel_jobs_and_release_tiles(AssetsLoader* al, BlobAllocator* ba, JobScheduler* js)
        override
    {
        assert(al && ba && js);

        if (status == InternalStatus::LoadingDescriptor)
        {
            assets_loader::end(al, download_ticket);
        }
        else if (status == InternalStatus::ParsingDescriptor)
        {
            hrz_jobs::cancel_job(js, parsing_ticket);
        }
        else if (status == InternalStatus::Ready)
        {
            fetcher->cancel_jobs_and_release_tiles(al, ba, js);
        }
    }

    void release_tile(LockTicket lock_ticket, bool nominate_for_eviction = false) override
    {
        if (fetcher.has_value())
        {
            fetcher->release_tile((TileFetcher::LockTicket)lock_ticket, nominate_for_eviction);
        }
    }

    LockTicket request_and_lock_tile(TileCoords tile_coords, AssetsLoader* al, uint32_t priority)
        override
    {
        if (status != InternalStatus::Ready) return LockTicket::Invalid;

        return (LockTicket)fetcher->request_and_lock_tile(tile_coords, al, al_queue, priority);
    }

    LockTicket lock_tile_if_ready(TileCoords tile_coords) override
    {
        if (status != InternalStatus::Ready) return LockTicket::Invalid;

        return (LockTicket)fetcher->lock_tile_if_ready(tile_coords);
    }

    void work(
        AssetsLoader* al,
        BlobAllocator* ba,
        JobScheduler* js,
        AttributionRegistry* attributions) override
    {
        assert(al && ba && js);

        if (status == InternalStatus::LoadingDescriptor)
        {
            if (!assets_loader::is_valid(al, download_ticket))
            {
                download_ticket = assets_loader::begin(
                    al, url, headers, al_queue, 0, {monitoring::systems::PlanetSurface, raster_id});
            }
            else if (assets_loader::is_finished(al, download_ticket))
            {
                if (assets_loader::get_status(al, download_ticket)
                    == assets_loader::RequestStatus::Loaded)
                {
                    auto raw_data = assets_loader::get_blob(al, ba, download_ticket);

                    TilemapResourceParams params;
                    params.raw_xml = std::move(raw_data);
                    parsing_ticket = hrz_jobs::add_job_parse_tilemap_resource(
                        js, params, {monitoring::systems::PlanetSurface, raster_id});

                    status = InternalStatus::ParsingDescriptor;
                }
                else
                {
                    HRZ_LOG_ERROR("Couldn't load TileMap resource at {}", url);
                    status = InternalStatus::Error;
                }

                assets_loader::end(al, download_ticket);
            }
        }
        else if (status == InternalStatus::ParsingDescriptor)
        {
            if (hrz_jobs::is_job_finished(js, parsing_ticket))
            {
                if (hrz_jobs::get_job_status(js, parsing_ticket)
                    == job_scheduler::JobStatus::Finished_Success)
                {
                    TilemapResourceResponse response;
                    hrz_jobs::get_job_response(js, parsing_ticket, response);

                    auto url_patterns = std::move(response.url_patterns);

                    BaseUrl base_url = {url, preserve_query_parameters};

                    AttributionHandle attribution_handles[] = {
                        attribution::register_attribution(
                            attributions, {additional_attribution, ""}),
                        attribution::register_attribution(
                            attributions, {response.attribution_title, response.attribution_logo})};
                    auto attribution =
                        attribution::register_attribution_group(attributions, attribution_handles);

                    fetcher.emplace(
                        std::make_unique<UrlTileRequester>(
                            std::make_unique<TilesetTileUrlGenerator>(base_url, url_patterns),
                            headers),
                        0,
                        missing_tile_policy == hrz_proto::MissingTilePolicy::USE_LOWER_RESOLUTION,
                        std::make_unique<ImageTileDecoder>(image_format, raster_id),
                        std::make_unique<SimpleTileAttributionPolicy>(attribution),
                        tile_cache_capacity, raster_id,
                        TileFetcher::MetricInfo{
                            provider_request_tally_metric_name(
                                hrz_proto::RasterProviderType::TMS_PROVIDER),
                            url.c_str()});

                    geometry = std::move(response.geometry);

                    status = InternalStatus::Ready;
                }
                else
                {
                    HRZ_LOG_ERROR("Couldn't parse TileMap resource at {}", url);
                    status = InternalStatus::Error;
                    hrz_jobs::cancel_job(js, parsing_ticket);
                }
            }
        }
        else if (status == InternalStatus::Ready)
        {
            fetcher->work(al, ba, js, attributions);
        }
    }

    bool is_working() const override
    {
        return status == InternalStatus::LoadingDescriptor
            || status == InternalStatus::ParsingDescriptor
            || (status == InternalStatus::Ready && fetcher->is_working());
    }

    UpdateAction notify_model_update(
        const scene_model::RasterProviderPath& path,
        const hrz_proto::RasterProvider& provider_model) override
    {
        if (path.leaf() || path.is_type())
        {
            return UpdateAction::RecreateProvider;
        }
        else if (path.is_tms())
        {
            auto provider_path = path.clone().tms();
            if (provider_path.is_http_headers())
            {
                if (set_http_headers(
                        assets_loader::from_proto(provider_model.tms().http_headers())))
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

    bool set_http_headers(const HttpHeaders& new_headers) override
    {
        auto old = std::exchange(headers, new_headers);
        if (fetcher.has_value())
        {
            fetcher->set_http_headers(headers);
        }
        return old.hash_content() != headers.hash_content();
    }

    void dev_ui(mu_Context* ctx) override
    {
        fmt::memory_buffer buffer;

        fmt::format_to(std::back_inserter(buffer), "TileMapServiceRasterProvider [{}]", url);
        buffer.push_back(0);

        if (status == InternalStatus::Ready)
        {
            if (mu_begin_treenode(ctx, buffer.data()))
            {
                fetcher->dev_ui(ctx);
                mu_end_treenode(ctx);
            }
        }
    }

private:
    enum class InternalStatus
    {
        LoadingDescriptor,
        ParsingDescriptor,
        Ready,
        Error,
    };

    InternalStatus status;
    hrz_proto::ImageFormat image_format;
    hrz_proto::RasterNodata nodata;
    std::string url;
    HttpHeaders headers;
    std::string additional_attribution;
    bool preserve_query_parameters;
    assets_loader::Queue al_queue;
    hrz_proto::MissingTilePolicy missing_tile_policy;
    size_t tile_cache_capacity;

    assets_loader::Ticket download_ticket;
    hrz_jobs::ParseTilemapResourceTicket parsing_ticket;

    std::optional<TileFetcher> fetcher;
    hrz::planet::TiledRasterGeometry geometry;

    uint64_t raster_id;
};

std::unique_ptr<RasterProvider> create_tms_provider(
    const hrz_proto::TmsProviderParams& params,
    assets_loader::Queue queue,
    uint32_t default_tile_cache_size,
    uint64_t raster_id)
{
    return std::make_unique<TileMapServiceRasterProvider>(
        params, queue, default_tile_cache_size, raster_id);
}

} // namespace hrz::planet
