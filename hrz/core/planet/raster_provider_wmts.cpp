#include "hrz/core/jobs/parse_wmts_resource.h"
#include "hrz/core/planet/raster_provider.h"
#include "hrz/core/planet/tile_fetcher.h"
#include "hrz/fnd/string_utils.h"

#include <cassert>
#include <optional>

namespace
{
struct UrlGenerator : public hrz::TileUrlGenerator
{
    UrlGenerator(
        std::vector<std::string> url_patterns,
        std::vector<std::string> tileset_identifiers) :
        url_patterns(std::move(url_patterns)),
        tileset_identifiers(std::move(tileset_identifiers)),
        next_pattern_index(0)
    {
        assert(this->url_patterns.size() > 0);
        assert(this->tileset_identifiers.size() > 0);
        static constexpr std::string_view kValidArgs[] = {"z", "x", "y", "ry"};
        for (auto& pattern : this->url_patterns)
        {
            pattern = hrz::str::sanitize_named_fmt_arguments(pattern, kValidArgs);
        }
    }

    std::string make_url(uint32_t x, uint32_t y, uint32_t z) override
    {
        if (z > tileset_identifiers.size())
        {
            HRZ_LOG_ERROR("Cannot generate URL for z={}", z);
            z = 0;
        }

        size_t pattern_index = next_pattern_index;
        next_pattern_index = (next_pattern_index + 1) % url_patterns.size();

        const auto& pattern = url_patterns.at(pattern_index);
        const auto& identifier = tileset_identifiers.at(z);

        const uint32_t tile_count = 1 << z;
        const uint32_t ry = tile_count - y - 1;

        return fmt::format(
            fmt::runtime(pattern), fmt::arg("z", identifier), fmt::arg("x", x), fmt::arg("y", y),
            fmt::arg("ry", ry));
    }

private:
    std::vector<std::string> url_patterns;
    std::vector<std::string> tileset_identifiers;

    size_t next_pattern_index;
};
} // namespace

namespace hrz::planet
{
bool is_provider_model_complete(const hrz_proto::WmtsRasterProviderParams& params)
{
    if (params.url().empty()) return false;

    return true;
}

hrz_proto::ImageFormat get_image_format(const hrz_proto::WmtsRasterProviderParams& params)
{
    return params.image_format();
}

class WmtsProvider : public RasterProvider
{
public:
    WmtsProvider(
        const hrz_proto::WmtsRasterProviderParams& params,
        assets_loader::Queue queue,
        uint32_t default_tile_cache_size,
        uint64_t raster_id) :
        status(InternalStatus::LoadingDescriptor),
        image_format(params.image_format()),
        nodata(params.nodata()),
        url(params.url()),
        headers(assets_loader::from_proto(params.http_headers())),
        attribution(params.attribution()),
        layer_identifier(params.layer_identifier()),
        style_identifier(params.style_identifier()),
        desired_image_format(params.source_image_format()),
        al_queue(queue),
        missing_tile_policy(params.missing_tile_policy()),
        tile_cache_capacity(
            params.override_tile_cache_size() ? params.tile_cache_size() : default_tile_cache_size),
        download_ticket(0),
        raster_id(raster_id)
    {
        if (!params.mime_type_override().empty())
        {
            mime_type_override = params.mime_type_override();
        }
    }

    hrz_proto::RasterProviderType get_raster_provider_type() const override
    {
        return hrz_proto::RasterProviderType::WMTS_RASTER_PROVIDER;
    }

    hrz_proto::ImageFormat get_image_format() const override { return image_format; }

    void set_load_queue(assets_loader::Queue queue) override { al_queue = queue; }

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

                    hrz_jobs::WmtsResourceParams params;
                    params.raw_xml = std::move(raw_data);
                    params.wmts_url = url;
                    params.layer_identifier = layer_identifier;
                    params.style_identifier = style_identifier;
                    params.image_format = desired_image_format;
                    parsing_ticket = hrz_jobs::add_job_parse_wmts_resource(
                        js, params, {monitoring::systems::PlanetSurface, raster_id});

                    status = InternalStatus::ParsingDescriptor;
                }
                else
                {
                    HRZ_LOG_ERROR("Couldn't load WMTS resource at {}", url);
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
                    hrz_jobs::WmtsResourceResponse response;
                    hrz_jobs::get_job_response(js, parsing_ticket, response);

                    fetcher = TileFetcher(
                        std::make_unique<UrlTileRequester>(
                            std::make_unique<UrlGenerator>(
                                std::move(response.url_patterns),
                                std::move(response.matrix_identifiers)),
                            mime_type_override, headers),
                        response.geometry.tiling_scheme.local_tiling().min_level(),
                        missing_tile_policy == hrz_proto::MissingTilePolicy::USE_LOWER_RESOLUTION,
                        std::make_unique<ImageTileDecoder>(image_format, raster_id),
                        std::make_unique<SimpleTileAttributionPolicy>(attribution),
                        tile_cache_capacity, raster_id,
                        {provider_request_tally_metric_name(
                             hrz_proto::RasterProviderType::WMTS_RASTER_PROVIDER),
                         url.c_str()});

                    geometry = std::move(response.geometry);

                    status = InternalStatus::Ready;
                }
                else
                {
                    HRZ_LOG_ERROR("Couldn't parse WMTS resource at {}", url);
                    hrz_jobs::cancel_job(js, parsing_ticket);
                    status = InternalStatus::Error;
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
        else if (path.is_wmts())
        {
            auto provider_path = path.clone().wmts();
            if (provider_path.is_http_headers())
            {
                if (set_http_headers(
                        assets_loader::from_proto(provider_model.wmts().http_headers())))
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

        if (!style_identifier.empty())
        {
            fmt::format_to(
                std::back_inserter(buffer), "WmtsProvider [{}: {} ({})]", url, layer_identifier,
                style_identifier);
        }
        else
        {
            fmt::format_to(
                std::back_inserter(buffer), "WmtsProvider [{}: {}]", url, layer_identifier);
        }
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
        ParsingDescriptor,
        Ready,
        Error,
    };

    InternalStatus status;
    hrz_proto::ImageFormat image_format;
    hrz_proto::RasterNodata nodata;
    std::string url;
    HttpHeaders headers;
    std::string attribution;
    std::string layer_identifier;
    std::string style_identifier;
    std::string desired_image_format;
    std::optional<std::string> mime_type_override;
    assets_loader::Queue al_queue;
    hrz_proto::MissingTilePolicy missing_tile_policy;
    size_t tile_cache_capacity;

    assets_loader::Ticket download_ticket;
    hrz_jobs::ParseWmtsResourceTicket parsing_ticket;

    std::optional<TileFetcher> fetcher;
    hrz::planet::TiledRasterGeometry geometry;

    uint64_t raster_id;
};

std::unique_ptr<RasterProvider> create_wmts_provider(
    const hrz_proto::WmtsRasterProviderParams& params,
    assets_loader::Queue queue,
    uint32_t default_tile_cache_size,
    uint64_t raster_id)
{
    return std::unique_ptr<RasterProvider>(
        new WmtsProvider(params, queue, default_tile_cache_size, raster_id));
}

} // namespace hrz::planet
