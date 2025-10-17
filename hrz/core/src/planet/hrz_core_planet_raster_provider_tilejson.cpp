#include "hrz_core_base_url.h"
#include "hrz_core_tile_url_generator.h"
#include "hrz_core_tilejson.h"
#include "planet/hrz_core_planet_raster_provider.h"
#include "planet/hrz_core_planet_tile_fetcher.h"

#include <hrz_common_geo.h>
#include <hrz_common_profiling.h>
#include <hrz_common_proj.h>
#include <hrz_fnd_hash.h>

#include <cassert>
#include <optional>

// See https://github.com/mapbox/tilejson-spec

namespace hrz::planet
{
bool is_provider_model_complete(const hrz_proto::TileJsonRasterProviderParams& params)
{
    if (params.url().empty()) return false;

    return true;
}

hrz_proto::ImageFormat get_image_format(const hrz_proto::TileJsonRasterProviderParams& params)
{
    return params.image_format();
}

class TileJsonProvider : public RasterProvider
{
public:
    TileJsonProvider(
        const hrz_proto::TileJsonRasterProviderParams& params,
        assets_loader::Queue queue,
        uint32_t default_tile_cache_size,
        uint64_t raster_id) :
        status(InternalStatus::LoadingDescriptor),
        url(params.url()),
        headers(assets_loader::from_proto(params.http_headers())),
        additional_attribution(params.attribution()),
        al_queue(queue),
        image_format(params.image_format()),
        nodata(params.nodata()),
        missing_tile_policy(params.missing_tile_policy()),
        tile_cache_capacity(
            params.override_tile_cache_size() ? params.tile_cache_size() : default_tile_cache_size),
        base_url(params.url(), params.preserve_query_parameters()),
        descriptor_download_ticket(0),
        raster_id(raster_id)
    {
        if (!params.mime_type_override().empty())
        {
            mime_type_override = params.mime_type_override();
        }
    }

    hrz_proto::RasterProviderType get_raster_provider_type() const override
    {
        return hrz_proto::RasterProviderType::TILEJSON_RASTER_PROVIDER;
    }

    hrz_proto::ImageFormat get_image_format() const override { return image_format; }

    void set_load_queue(assets_loader::Queue queue) override { al_queue = queue; }

    Status get_status() const override
    {
        switch (status)
        {
            case InternalStatus::LoadingDescriptor: return Status::Loading;
            case InternalStatus::Ready: return Status::Ready;
            case InternalStatus::Error: return Status::Error;
            default: assert(false && "Unhandled case"); return Status::Error;
        }
    }

    const hrz::planet::TiledRasterGeometry& get_geometry() const override
    {
        assert(status == InternalStatus::Ready);

        return geometry;
    }

    const hrz_proto::RasterNodata& get_nodata() const override { return nodata; }

    TileStatus get_tile_status(LockTicket lock_ticket) override
    {
        if (fetcher.has_value())
        {
            return fetcher->get_tile_status((TileFetcher::LockTicket)lock_ticket);
        }
        else
        {
            return TileStatus::Loading;
        }
    }

    TileImage get_tile_image(LockTicket lock_ticket) override
    {
        assert(status == InternalStatus::Ready);
        return fetcher->get_tile_image((TileFetcher::LockTicket)lock_ticket);
    }

    void cancel_jobs_and_release_tiles(AssetsLoader* al, BlobAllocator* ba, JobScheduler* js)
        override
    {
        assert(al && ba && js);

        if (status == InternalStatus::LoadingDescriptor)
        {
            assets_loader::end(al, descriptor_download_ticket);
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

private:
    bool setup_provider(AttributionRegistry* attributions, std::span<const std::byte> raw_data)
    {
        HRZ_SCOPED_SAMPLE("setup provider");

        auto tilejson_opt = tilejson::parse_tilejson(raw_data, base_url);

        if (!tilejson_opt.has_value()) return false;

        auto& tilejson = tilejson_opt.value();

        geometry.projection.set_descriptor_type(
            hrz_proto::SrsDescriptorType::PROJ4_STRING_DESCRIPTOR);
        geometry.projection.set_descriptor_(hrz_proj::wmerc_proj_str);

        // The spec says "The global-mercator (aka Spherical Mercator) profile is assumed".
        geometry.tiling_scheme.set_type(hrz_proto::TilingSchemeType::GLOBAL);
        auto tiling = geometry.tiling_scheme.mutable_global_tiling();
        tiling->set_tile_size(256);
        tiling->set_level_zero_tile_count_x(1);
        tiling->set_level_zero_tile_count_y(1);
        tiling->set_min_level(tilejson.min_level);
        tiling->set_max_level(tilejson.max_level);

        // The spec says nothing about how the edge raster tiles are made when the bounds
        // clip them. However at least OpenLayers and Leaflet have this behaviour.
        tiling->set_border_tile_aspect(hrz_proto::BorderTileAspect::FULL_SIZED);

        auto web_mercator_bounds = lm::dbbox2(
            hrz::geo_to_web_mercator(GeoPosition2{tilejson.bounds.south, tilejson.bounds.west}),
            hrz::geo_to_web_mercator(GeoPosition2{tilejson.bounds.north, tilejson.bounds.east}));

        web_mercator_bounds.min.y =
            std::max(web_mercator_bounds.min.y, -hrz::MERCATOR_MAX_LAT_METERS);
        web_mercator_bounds.max.y =
            std::min(web_mercator_bounds.max.y, hrz::MERCATOR_MAX_LAT_METERS);

        geometry.bounds = web_mercator_bounds;

        AttributionHandle attribution_handles[] = {
            attribution::register_attribution(attributions, {additional_attribution, ""}),
            attribution::register_attribution(attributions, {tilejson.attribution, ""})};
        auto attribution =
            attribution::register_attribution_group(attributions, attribution_handles);

        fetcher.emplace(
            std::make_unique<UrlTileRequester>(
                std::make_unique<MultiPatternTileUrlGenerator>(
                    std::span<const std::string>(tilejson.url_patterns), 1),
                mime_type_override, headers),
            0, missing_tile_policy == hrz_proto::MissingTilePolicy::USE_LOWER_RESOLUTION,
            std::make_unique<ImageTileDecoder>(image_format, raster_id),
            std::make_unique<SimpleTileAttributionPolicy>(attribution), tile_cache_capacity,
            raster_id,
            TileFetcher::MetricInfo{
                provider_request_tally_metric_name(
                    hrz_proto::RasterProviderType::TILEJSON_RASTER_PROVIDER),
                url.c_str()});

        return true;
    }

public:
    void work(
        AssetsLoader* al,
        BlobAllocator* ba,
        JobScheduler* js,
        AttributionRegistry* attributions) override
    {
        assert(al && ba && js);

        if (status == InternalStatus::LoadingDescriptor)
        {
            if (!assets_loader::is_valid(al, descriptor_download_ticket))
            {
                descriptor_download_ticket = assets_loader::begin(al, url, headers, al_queue);
            }
            else if (assets_loader::is_finished(al, descriptor_download_ticket))
            {
                if (assets_loader::get_status(al, descriptor_download_ticket)
                    == assets_loader::RequestStatus::Loaded)
                {
                    auto raw_data =
                        assets_loader::get_blob(al, ba, descriptor_download_ticket).get_data();

                    if (setup_provider(attributions, raw_data))
                    {
                        status = InternalStatus::Ready;
                    }
                    else
                    {
                        status = InternalStatus::Error;
                    }
                }
                else
                {
                    HRZ_LOG_ERROR("Couldn't load TileJSON resource at {}", url);
                    status = InternalStatus::Error;
                }

                assets_loader::end(al, descriptor_download_ticket);
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
        else if (path.is_tilejson())
        {
            auto provider_path = path.clone().tilejson();
            if (provider_path.is_http_headers())
            {
                if (set_http_headers(
                        assets_loader::from_proto(provider_model.tilejson().http_headers())))
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

        fmt::format_to(std::back_inserter(buffer), "TileJsonProvider [{}]", url);
        buffer.push_back(0);

        if (mu_begin_treenode(ctx, buffer.data()))
        {
            if (fetcher.has_value())
            {
                fetcher->dev_ui(ctx);
            }
            mu_end_treenode(ctx);
        }
    }

private:
    enum class InternalStatus
    {
        LoadingDescriptor,
        Ready,
        Error,
    };

    InternalStatus status;
    std::string url;
    HttpHeaders headers;
    std::string additional_attribution;
    assets_loader::Queue al_queue;
    hrz_proto::ImageFormat image_format;
    std::optional<std::string> mime_type_override;
    hrz_proto::RasterNodata nodata;
    hrz_proto::MissingTilePolicy missing_tile_policy;
    size_t tile_cache_capacity;

    BaseUrl base_url;

    assets_loader::Ticket descriptor_download_ticket;

    std::optional<TileFetcher> fetcher;
    hrz::planet::TiledRasterGeometry geometry;

    uint64_t raster_id;
};

std::unique_ptr<RasterProvider> create_tilejson_provider(
    const hrz_proto::TileJsonRasterProviderParams& params,
    assets_loader::Queue queue,
    uint32_t default_tile_cache_size,
    uint64_t raster_id)
{
    return std::unique_ptr<RasterProvider>(
        new TileJsonProvider(params, queue, default_tile_cache_size, raster_id));
}
} // namespace hrz::planet
