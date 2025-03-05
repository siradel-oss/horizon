#include "planet/hrz_core_planet_raster_provider.h"
#include "planet/hrz_core_planet_tile_fetcher.h"

#include <hrz_common_fmt.h>
#include <hrz_common_proj.h>
#include <hrz_fnd_string_utils.h>

#include <pugixml/pugixml.hpp>

#include <cassert>
#include <optional>
#include <string_view>

namespace
{
static constexpr const char* BING_API_URL_PATTERN =
    "https://dev.virtualearth.net/REST/V1/Imagery/Metadata/"
    "{imagery_type}?output=xml&include=ImageryProviders&key={api_key}&culture={culture}&uriScheme="
    "https";

const char* imagery_type_to_str(hrz_proto::BingProviderImageryType type)
{
    switch (type)
    {
        case hrz_proto::BingProviderImageryType::ROAD: return "RoadOnDemand";
        case hrz_proto::BingProviderImageryType::AERIAL: return "Aerial";
        case hrz_proto::BingProviderImageryType::AERIAL_WITH_LABELS:
            return "AerialWithLabelsOnDemand";
        case hrz_proto::BingProviderImageryType::ROAD_DARK: return "CanvasDark";
        case hrz_proto::BingProviderImageryType::ROAD_GRAYSCALE: return "CanvasGray";
        case hrz_proto::BingProviderImageryType::ROAD_LIGHT: return "CanvasLight";
        default: assert(false && "Unhandled case"); return "<error>";
    }
}

struct TilesetInfo
{
    std::string url_pattern;
    std::string culture;
    std::vector<std::string> subdomains;
    uint8_t lod_min;
    uint8_t lod_max;
};

static constexpr std::string_view kSupportedUrlSubstitutions[] = {
    "subdomain", "quadkey", "culture", "zoom", "tileId"};

struct UrlGenerator : public hrz::TileUrlGenerator
{
    TilesetInfo tileset_info;

    std::string make_url(uint32_t x, uint32_t y, uint32_t z) override
    {
        // The subdomain index is static based on the tile id instead of random
        // so that it doesn't mess with the cache: a tile will always have the
        // same URL, but not all tiles will be fetched on the same subdomain.
        size_t subdomain = hrz::hash_values(x, y, z) % tileset_info.subdomains.size();

        std::string target_url = fmt::format(
            tileset_info.url_pattern, fmt::arg("subdomain", tileset_info.subdomains.at(subdomain)),
            fmt::arg("quadkey", hrz::tile_coords_to_quadkey(x, y, z).c_str()),
            fmt::arg("culture", tileset_info.culture), fmt::arg("zoom", z),
            fmt::arg("tileId", (uint64_t)x + (uint64_t)y * (1ULL << z)));

        return target_url;
    }
};

} // anonymous namespace

namespace hrz::planet
{
bool is_provider_model_complete(const hrz_proto::BingProviderParams& model)
{
    return !model.api_key().empty();
}

hrz_proto::ImageFormat get_image_format(const hrz_proto::BingProviderParams&)
{
    return hrz_proto::ImageFormat::SRGBA_8;
}

struct BingProvider : RasterProvider
{
public:
    BingProvider(
        const hrz_proto::BingProviderParams& params,
        assets_loader::Queue download_queue,
        uint32_t default_tile_cache_size,
        uint64_t raster_id) :
        status(Status::Loading),
        imagery_type(params.imagery_type()),
        culture(params.culture()),
        api_key(params.api_key()),
        headers(assets_loader::from_proto(hrz_proto::HttpHeaderList::default_instance())),
        al_queue(download_queue),
        missing_tile_policy(params.missing_tile_policy()),
        tile_cache_capacity(
            params.override_tile_cache_size() ? params.tile_cache_size() : default_tile_cache_size),
        setup_ticket(0),
        raster_id(raster_id)
    {
    }

    hrz_proto::RasterProviderType get_raster_provider_type() const override
    {
        return hrz_proto::RasterProviderType::BING_PROVIDER;
    }

    hrz_proto::ImageFormat get_image_format() const override { return hrz_proto::SRGBA_8; }

    void set_load_queue(assets_loader::Queue queue) override { al_queue = queue; }

    Status get_status() const override { return status; }

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

    RasterProvider::TileImage get_tile_image(LockTicket lock_ticket) override
    {
        assert(status == Status::Ready);
        return fetcher->get_tile_image((TileFetcher::LockTicket)lock_ticket);
    }

    const hrz::planet::TiledRasterGeometry& get_geometry() const override
    {
        assert(status == Status::Ready);

        return geometry;
    }

    const hrz_proto::RasterNodata& get_nodata() const override
    {
        return hrz_proto::RasterNodata::default_instance();
    }

    void cancel_jobs_and_release_tiles(AssetsLoader* al, BlobAllocator* ba, JobScheduler* js)
        override
    {
        if (fetcher.has_value())
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
        if (status != Status::Ready) return LockTicket::Invalid;

        if (tile_coords.lod < tileset_info.lod_min || tile_coords.lod > tileset_info.lod_max)
        {
            HRZ_LOG_DEBUG("Tile {} is outside Bing imagery zoom levels", tile_coords);
            return LockTicket::Invalid;
        }

        return (LockTicket)fetcher->request_and_lock_tile(tile_coords, al, al_queue, priority);
    }

    LockTicket lock_tile_if_ready(TileCoords tile_coords) override
    {
        if (fetcher.has_value())
        {
            return (LockTicket)fetcher->lock_tile_if_ready(tile_coords);
        }
        else
        {
            return LockTicket::Invalid;
        }
    }

    void work(
        AssetsLoader* al,
        BlobAllocator* ba,
        JobScheduler* js,
        AttributionRegistry* attributions) override
    {
        if (status == Status::Loading)
        {
            prepare_provider(al, ba, attributions);
        }
        else if (status == Status::Ready)
        {
            fetcher->work(al, ba, js, attributions);
        }
    }

    bool is_working() const override
    {
        return status == Status::Loading || (status == Status::Ready && fetcher->is_working());
    }

    UpdateAction notify_model_update(
        const scene_model::RasterProviderPath& path,
        const hrz_proto::RasterProvider& provider_model) override
    {
        if (path.leaf() || path.is_type() || path.is_bing())
        {
            return UpdateAction::RecreateProvider;
        }
        else
        {
            return UpdateAction::KeepTiles;
        }
    }

    bool set_http_headers(const HttpHeaders&) override { return false; }

    void dev_ui(mu_Context* ctx) override
    {
        fmt::memory_buffer buffer;

        if (!culture.empty())
        {
            fmt::format_to(
                std::back_inserter(buffer), "BingProvider [{} ({})]",
                imagery_type_to_str(imagery_type), culture);
        }
        else
        {
            fmt::format_to(
                std::back_inserter(buffer), "BingProvider [{}]", imagery_type_to_str(imagery_type));
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
    void prepare_provider(AssetsLoader* al, BlobAllocator* ba, AttributionRegistry* attributions)
    {
        if (status == Status::Loading && !assets_loader::is_valid(al, setup_ticket))
        {
            if (!assets_loader::is_valid(al, setup_ticket))
            {
                setup_ticket = assets_loader::begin(
                    al,
                    fmt::format(
                        BING_API_URL_PATTERN,
                        fmt::arg("imagery_type", imagery_type_to_str(imagery_type)),
                        fmt::arg("api_key", api_key), fmt::arg("culture", culture)),
                    headers, al_queue);
            }
        }
        else if (assets_loader::is_finished(al, setup_ticket))
        {
            if (assets_loader::get_status(al, setup_ticket) == assets_loader::RequestStatus::Loaded)
            {
                auto raw = assets_loader::get_blob(al, ba, setup_ticket).get_data();

                pugi::xml_document doc;
                pugi::xml_parse_result parse_result = doc.load_buffer(raw.data(), raw.size());

                if (parse_result)
                {
                    const pugi::xml_node imagery_metadata_node =
                        doc.select_node(
                               "/Response/ResourceSets/ResourceSet/Resources/ImageryMetadata")
                            .node();
                    const pugi::xml_node image_url_node = imagery_metadata_node.child("ImageUrl");
                    const pugi::xml_node image_url_subdomains_node =
                        imagery_metadata_node.child("ImageUrlSubdomains");
                    const pugi::xml_node zoom_min_node = imagery_metadata_node.child("ZoomMin");
                    const pugi::xml_node zoom_max_node = imagery_metadata_node.child("ZoomMax");

                    tileset_info.lod_min = zoom_min_node.text().as_uint();
                    tileset_info.lod_max = zoom_max_node.text().as_uint();

                    for (const auto& subdomain_node : image_url_subdomains_node)
                    {
                        tileset_info.subdomains.push_back(subdomain_node.text().get());
                    }

                    assert(tileset_info.subdomains.size() > 0);

                    tileset_info.culture = culture;
                    tileset_info.url_pattern = str::sanitize_named_fmt_arguments(
                        image_url_node.text().get(), kSupportedUrlSubstitutions);

                    // This forces Bing to send us a 0-length response when a
                    // tile is not available. See
                    // https://github.com/CesiumGS/cesium/issues/1353
                    if (str::find(tileset_info.url_pattern, '?') >= 0)
                    {
                        tileset_info.url_pattern += "&n=z";
                    }
                    else
                    {
                        tileset_info.url_pattern += "?n=z";
                    }

                    auto attribution_policy =
                        std::make_unique<WebMercatorZonesTileAttributionPolicy>();

                    // Add an attribution on the whole planet with the Microsoft Bing logo
                    {
                        const pugi::xml_node logo_uri_node =
                            doc.select_node("/Response/BrandLogoUri").node();

                        auto logo_attribution = attribution::register_attribution(
                            attributions, {"", logo_uri_node.text().as_string()});

                        if (logo_attribution != AttributionHandle{})
                        {
                            attribution_policy->add_zone(
                                tileset_info.lod_min, tileset_info.lod_max,
                                {-lm::PI, lm::PI, -lm::PI / 2, lm::PI / 2}, logo_attribution);
                        }
                    }

                    // Then each attribution zone
                    {
                        const auto nodes = imagery_metadata_node.select_nodes("./ImageryProvider");
                        for (const auto& node : nodes)
                        {
                            auto attribution = attribution::register_attribution(
                                attributions,
                                {node.node().child("Attribution").text().as_string(), ""});

                            if (attribution != AttributionHandle{})
                            {
                                auto coverage_area = node.node().child("CoverageArea");
                                attribution_policy->add_zone(
                                    coverage_area.child("ZoomMin").text().as_int(),
                                    coverage_area.child("ZoomMax").text().as_int(),
                                    hrz::GeoBounds{
                                        lm::radians(coverage_area.child("BoundingBox")
                                                        .child("WestLongitude")
                                                        .text()
                                                        .as_double()),
                                        lm::radians(coverage_area.child("BoundingBox")
                                                        .child("EastLongitude")
                                                        .text()
                                                        .as_double()),
                                        lm::radians(coverage_area.child("BoundingBox")
                                                        .child("SouthLatitude")
                                                        .text()
                                                        .as_double()),
                                        lm::radians(coverage_area.child("BoundingBox")
                                                        .child("NorthLatitude")
                                                        .text()
                                                        .as_double()),
                                    },
                                    attribution);
                            }
                        }
                    }

                    std::unique_ptr<UrlGenerator> url_generator(new UrlGenerator());
                    url_generator->tileset_info = tileset_info;

                    fetcher.emplace(TileFetcher(
                        std::make_unique<UrlTileRequester>(std::move(url_generator), headers),
                        tileset_info.lod_min,
                        missing_tile_policy == hrz_proto::MissingTilePolicy::USE_LOWER_RESOLUTION,
                        std::make_unique<ImageTileDecoder>(get_image_format(), raster_id),
                        std::move(attribution_policy), tile_cache_capacity, raster_id,
                        {provider_request_tally_metric_name(
                             hrz_proto::RasterProviderType::BING_PROVIDER),
                         tileset_info.url_pattern.c_str()}));

                    {
                        geometry.projection.set_descriptor_type(
                            hrz_proto::SrsDescriptorType::PROJ4_STRING_DESCRIPTOR);
                        geometry.projection.set_descriptor(hrz_proj::wmerc_proj_str);

                        geometry.tiling_scheme.set_type(hrz_proto::TilingSchemeType::GLOBAL);

                        auto global_tiling = geometry.tiling_scheme.mutable_global_tiling();
                        global_tiling->set_tile_size(256);
                        global_tiling->set_level_zero_tile_count_x(1);
                        global_tiling->set_level_zero_tile_count_y(1);
                        global_tiling->set_min_level(tileset_info.lod_min);
                        global_tiling->set_max_level(tileset_info.lod_max);
                        global_tiling->set_border_tile_aspect(
                            hrz_proto::BorderTileAspect::FULL_SIZED);
                    }

                    status = Status::Ready;
                }
                else
                {
                    HRZ_LOG_INFO("Couldn't parse Bing XML response");
                    status = Status::Error;
                }
            }
            else
            {
                status = Status::Error;
            }

            assets_loader::end(al, setup_ticket);
        }
    }

    Status status;
    hrz_proto::BingProviderImageryType imagery_type;
    std::string culture;
    std::string api_key;
    HttpHeaders headers;
    assets_loader::Queue al_queue;
    hrz_proto::MissingTilePolicy missing_tile_policy;
    size_t tile_cache_capacity;

    TilesetInfo tileset_info;
    assets_loader::Ticket setup_ticket;

    std::optional<TileFetcher> fetcher;
    hrz::planet::TiledRasterGeometry geometry;

    uint64_t raster_id;
};

std::unique_ptr<RasterProvider> create_bing_provider(
    const hrz_proto::BingProviderParams& params,
    assets_loader::Queue download_queue,
    uint32_t default_tile_cache_size,
    uint64_t raster_id)
{
    return std::unique_ptr<RasterProvider>(
        new BingProvider(params, download_queue, default_tile_cache_size, raster_id));
}

} // namespace hrz::planet
