#include "planet/hrz_core_planet_raster_provider.h"
#include "planet/hrz_core_planet_tile_fetcher.h"

#include <hrz_common_color.h>
#include <hrz_common_geo.h>
#include <hrz_common_planet.h>
#include <hrz_common_proj.h>
#include <hrz_common_tickets.h>
#include <hrz_fnd_format.h>
#include <hrz_fnd_maths.h>
#include <hrz_fnd_unique_vector.h>
#include <hrz_fnd_url_utils.h>

#include <fmt/format.h>

#include <optional>
#include <vector>

namespace
{
struct UrlGenerator : public hrz::TileUrlGenerator
{
    UrlGenerator(
        const std::string& url_template_,
        const hrz::planet::TiledRasterGeometry& geometry) :
        url_template(url_template_), geometry(geometry)
    {
        static constexpr std::string_view kValidArgs[] = {"west", "south", "east", "north"};
        url_template = hrz::str::sanitize_named_fmt_arguments(url_template, kValidArgs);
    }

    std::string make_url(uint32_t x, uint32_t y, uint32_t z) override
    {
        double west, south, east, north;

        if (geometry.tiling_scheme.type() == hrz_proto::TilingSchemeType::GLOBAL
            && geometry.projection.descriptor() == hrz_proj::wmerc_proj_str)
        {
            uint64_t tile_count = (uint64_t)1 << z;
            double tile_size = hrz::MERCATOR_RANGE / tile_count;
            west = x * tile_size - hrz::HALF_MERCATOR_RANGE;
            south = hrz::MERCATOR_RANGE - (y + 1) * tile_size - hrz::HALF_MERCATOR_RANGE;
            east = west + tile_size;
            north = south + tile_size;
        }
        else if (
            geometry.tiling_scheme.type() == hrz_proto::TilingSchemeType::GLOBAL
            && geometry.projection.descriptor() == hrz_proj::lonlat_deg_proj_str)
        {
            uint64_t tile_count = (uint64_t)1 << z;
            double tile_size = 180.0 / tile_count;
            west = x * tile_size - 180.0;
            south = 180.0 - (y + 1) * tile_size - 90.0;
            east = west + tile_size;
            north = south + tile_size;
        }
        else if (geometry.tiling_scheme.type() == hrz_proto::TilingSchemeType::LOCAL)
        {
            const auto& tiling = geometry.tiling_scheme.local_tiling();
            const auto& bounds = geometry.projection_bounds;

            uint64_t tile_pixel_size = tiling.tile_size();
            uint64_t level_multiplier = 1 << (tiling.max_level() - z);
            uint64_t tile_max_level_pixel_size = tile_pixel_size * level_multiplier;
            lm::ulvec2 tile_max_level_pixel_origin = {
                x * tile_pixel_size * level_multiplier, y * tile_pixel_size * level_multiplier};

            double projection_width = bounds.max.x - bounds.min.x;
            double pixel_size = projection_width / tiling.full_image_width();

            west = tile_max_level_pixel_origin.x * pixel_size + bounds.min.x;
            south = bounds.max.y
                - (tile_max_level_pixel_origin.y + tile_max_level_pixel_size) * pixel_size;
            east = (tile_max_level_pixel_origin.x + tile_max_level_pixel_size) * pixel_size
                + bounds.min.x;
            north = bounds.max.y - tile_max_level_pixel_origin.y * pixel_size;
        }
        else
        {
            assert(false && "Unexpected tiling scheme");
            return "";
        }

        return fmt::format(
            url_template, fmt::arg("west", west), fmt::arg("south", south), fmt::arg("east", east),
            fmt::arg("north", north));
    }

private:
    std::string url_template;
    hrz::planet::TiledRasterGeometry geometry;
};
} // namespace

namespace hrz::planet
{
bool is_provider_model_complete(const hrz_proto::WmsRasterProviderParams& params)
{
    if (params.url().empty() || params.layers_size() == 0) return false;

    return true;
}

hrz_proto::ImageFormat get_image_format(const hrz_proto::WmsRasterProviderParams& params)
{
    return params.image_format();
}

class WmsProvider : public RasterProvider
{
public:
    WmsProvider(
        const hrz_proto::WmsRasterProviderParams& params,
        assets_loader::Queue queue,
        uint32_t default_tile_cache_size,
        uint64_t raster_id) :
        status(InternalStatus::LoadingDescriptor),
        image_format(params.image_format()),
        nodata(params.nodata()),
        url(params.url()),
        headers(assets_loader::from_proto(params.http_headers())),
        additional_attribution(params.attribution()),
        background_color(hrz::convert_proto_color_to_uint(params.background_color())),
        force_opaque(params.force_opaque()),
        desired_image_format(params.source_image_format()),
        al_queue(queue),
        missing_tile_policy(params.missing_tile_policy()),
        override_min_level(params.override_min_level()),
        min_level(hrz::clamp_cast<uint32_t, uint8_t>(params.min_level())),
        override_max_level(params.override_max_level()),
        max_level(hrz::clamp_cast<uint32_t, uint8_t>(params.max_level())),
        tile_cache_capacity(
            params.override_tile_cache_size() ? params.tile_cache_size() : default_tile_cache_size),
        download_ticket(0)
    {
        for (int i = 0; i < params.layers_size(); ++i)
        {
            const auto& layer = params.layers(i);
            layer_names.push_back(layer.layer_name());
            layer_styles.push_back(layer.style_name());
        }

        for (int i = 0; i < params.dimensions_size(); ++i)
        {
            const auto& dimension = params.dimensions(i);
            dimension_names.push_back(dimension.name());
            dimension_values.push_back(dimension.value());
        }
    }

    hrz_proto::RasterProviderType get_raster_provider_type() const override
    {
        return hrz_proto::RasterProviderType::WMS_RASTER_PROVIDER;
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
        assert(al && js);

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
                auto protocol = hrz::url::protocol_s(url);
                auto authority = hrz::url::authority_s(url);
                auto path = hrz::url::path_s(url);
                auto query = hrz::url::query(url);

                auto get_value = [&](const char* key1, const char* key2)
                {
                    auto value = url::parameter_value_from_query(query, key1);
                    if (value.empty())
                    {
                        value = url::parameter_value_from_query(query, key2);
                    }
                    return value;
                };

                auto service_value = get_value("SERVICE", "service");
                auto version_value = get_value("VERSION", "version");
                auto request_value = get_value("REQUEST", "request");

                if (!query.empty() && query.back() == '&')
                {
                    query.pop_back();
                }

                if (service_value.empty())
                {
                    if (!query.empty()) query += '&';
                    query += "SERVICE=WMS";
                }
                if (version_value.empty())
                {
                    if (!query.empty()) query += '&';
                    query += "VERSION=1.3.0";
                }
                if (request_value.empty())
                {
                    if (!query.empty()) query += '&';
                    query += "REQUEST=GetCapabilities";
                }

                std::string full_url = fmt::format(
                    "{}://{}{}?{}", fmt::basic_string_view<char>(protocol.data(), protocol.size()),
                    fmt::basic_string_view<char>(authority.data(), authority.size()),
                    fmt::basic_string_view<char>(path.data(), path.size()), query);

                download_ticket = assets_loader::begin(
                    al, full_url, headers, al_queue, 0,
                    {monitoring::systems::PlanetSurface, raster_id});
            }
            else if (assets_loader::is_finished(al, download_ticket))
            {
                if (assets_loader::get_status(al, download_ticket)
                    == assets_loader::RequestStatus::Loaded)
                {
                    auto raw_data = assets_loader::get_blob(al, ba, download_ticket);

                    WmsResourceParams params;
                    params.raw_xml = std::move(raw_data);
                    params.wms_url = url;
                    for (size_t i = 0; i < layer_names.size(); ++i)
                    {
                        params.layers.push_back({layer_names.at(i), layer_styles.at((i))});
                    }
                    for (size_t i = 0; i < dimension_names.size(); ++i)
                    {
                        params.dimensions.push_back(
                            {dimension_names.at(i), dimension_values.at((i))});
                    }
                    params.background_color = background_color;
                    params.force_opaque = force_opaque;
                    params.image_format = desired_image_format;
                    params.override_min_level = override_min_level;
                    params.min_level = min_level;
                    params.override_max_level = override_max_level;
                    params.max_level = max_level;
                    parsing_ticket = hrz_jobs::add_job_parse_wms_resource(
                        js, params, {monitoring::systems::PlanetSurface, raster_id});

                    status = InternalStatus::ParsingDescriptor;
                }
                else
                {
                    HRZ_LOG_ERROR("Couldn't load WMS resource at {}", url);
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
                    WmsResourceResponse response;
                    hrz_jobs::get_job_response(js, parsing_ticket, response);

                    fmt::memory_buffer buffer;
                    hrz::InlinedUniqueVector<AttributionHandle, 8> unique_attributions;

                    if (!additional_attribution.empty())
                    {
                        unique_attributions.push_back(attribution::register_attribution(
                            attributions, {additional_attribution, ""}));
                    }

                    for (const auto& resp_attribution : response.attributions)
                    {
                        std::string_view text = resp_attribution.title;

                        if (!resp_attribution.link.empty())
                        {
                            text = format_to_buffer(
                                buffer, "<a href=\"{}\">{}</a>", resp_attribution.link,
                                resp_attribution.title);
                        }

                        AttributionHandle handle = attribution::register_attribution(
                            attributions, {text, resp_attribution.logo});
                        unique_attributions.push_back(handle);
                    }

                    AttributionHandle attribution = attribution::register_attribution_group(
                        attributions, unique_attributions.as_span());

                    fetcher.emplace(
                        std::make_unique<UrlTileRequester>(
                            std::make_unique<UrlGenerator>(
                                response.url_template, response.geometry),
                            headers),
                        geometry.tiling_scheme.local_tiling().min_level(),
                        missing_tile_policy == hrz_proto::MissingTilePolicy::USE_LOWER_RESOLUTION,
                        std::make_unique<ImageTileDecoder>(image_format, raster_id),
                        std::make_unique<SimpleTileAttributionPolicy>(attribution),
                        tile_cache_capacity, raster_id,
                        TileFetcher::MetricInfo{
                            provider_request_tally_metric_name(
                                hrz_proto::RasterProviderType::WMS_RASTER_PROVIDER),
                            response.url_template.c_str()});

                    geometry = std::move(response.geometry);

                    status = InternalStatus::Ready;
                }
                else
                {
                    HRZ_LOG_ERROR("Couldn't parse WMS resource at {}", url);
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
        else if (path.is_wms())
        {
            auto provider_path = path.clone().wms();
            if (provider_path.is_http_headers())
            {
                if (set_http_headers(
                        assets_loader::from_proto(provider_model.wms().http_headers())))
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
        std::string str = "WmsProvider [";
        str += url;
        str += ": {";

        for (size_t i = 0; i < layer_names.size(); ++i)
        {
            str += layer_names.at(i);

            const auto& style = layer_styles.at(i);
            if (!style.empty())
            {
                str += " (";
                str += style;
                str += ')';
            }

            if (i != layer_names.size() - 1)
            {
                str += ", ";
            }
        }

        str += "}]";

        if (mu_begin_treenode(ctx, str.c_str()))
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
    std::string additional_attribution;
    std::vector<std::string> layer_names;
    std::vector<std::string> layer_styles;
    std::vector<std::string> dimension_names;
    std::vector<std::string> dimension_values;
    uint32_t background_color;
    bool force_opaque;
    std::string desired_image_format;
    assets_loader::Queue al_queue;
    hrz_proto::MissingTilePolicy missing_tile_policy;
    bool override_min_level;
    uint8_t min_level;
    bool override_max_level;
    uint8_t max_level;
    size_t tile_cache_capacity;

    assets_loader::Ticket download_ticket;
    hrz_jobs::ParseWmsResourceTicket parsing_ticket;

    std::optional<TileFetcher> fetcher;
    hrz::planet::TiledRasterGeometry geometry;

    uint64_t raster_id;
};

std::unique_ptr<RasterProvider> create_wms_provider(
    const hrz_proto::WmsRasterProviderParams& params,
    assets_loader::Queue queue,
    uint32_t default_tile_cache_size,
    uint64_t raster_id)
{
    return std::unique_ptr<RasterProvider>(
        new WmsProvider(params, queue, default_tile_cache_size, raster_id));
}
} // namespace hrz::planet
