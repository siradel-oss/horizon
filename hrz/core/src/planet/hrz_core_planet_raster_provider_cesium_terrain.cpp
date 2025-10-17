#include "hrz_core_base_url.h"
#include "hrz_core_job_scheduler.h"
#include "hrz_core_tile_url_generator.h"
#include "planet/hrz_core_planet_raster_provider.h"
#include "planet/hrz_core_planet_tile_fetcher.h"

#include <hrz_common_geo.h>
#include <hrz_common_planet.h>
#include <hrz_common_profiling.h>
#include <hrz_common_proj.h>
#include <hrz_fnd_hash.h>
#include <hrz_fnd_json_utils.h>
#include <hrz_fnd_url_utils.h>
#include <hrz_jobs_tickets.h>

#include <fmt/format.h>
#include <lin_maths.h>
#include <rapidjson/document.h>

#include <cassert>
#include <optional>
#include <vector>

namespace hrz::planet
{
namespace
{
// https://github.com/CesiumGS/quantized-mesh
HttpHeaders build_headers(hrz_proto::HttpHeaderList header_list)
{
    auto header = header_list.add_headers();
    *header->mutable_name() = "Accept";
    *header->mutable_value() = "application/vnd.quantized-mesh,application/octet-stream;q=0.9";
    return assets_loader::from_proto(header_list);
}

struct TileUrlGenerator : public hrz::TileUrlGenerator
{
    std::vector<std::string> url_patterns;
    bool use_tms_tile_coords;

    std::string make_url(uint32_t x, uint32_t y, uint32_t z) override
    {
        // The pattern index is static based on the tile coords instead of RNG
        // so that it doesn't mess with the cache: a tile will always have the
        // same URL, but not all tiles will be fetched from the same pattern.
        char pattern_index = hrz::hash_values(x, y, z) % url_patterns.size();
        const auto& url_pattern = url_patterns.at(pattern_index);

        if (use_tms_tile_coords)
        {
            uint32_t tile_count = 1 << z;
            y = tile_count - y - 1;
        }

        return fmt::format(
            fmt::runtime(url_pattern), fmt::arg("x", x), fmt::arg("y", y), fmt::arg("z", z));
    }
};

class CesiumTerrainTileDecoder : public TileDecoder
{
public:
    CesiumTerrainTileDecoder(const std::string& tile_format, uint64_t raster_id) :
        tile_format(tile_format), raster_id(raster_id)
    {
    }

    Ticket decode_tile(blobs::BlobHandle blob, std::string_view /* mime_type */, JobScheduler* js)
        override
    {
        auto handle = tiles.alloc();
        auto tile = tiles.get_object(handle);
        tile->status = Status::Decoding;
        CesiumTerrainTileData params;
        params.blob = blob;
        params.format = tile_format;
        tile->rasterize_ticket = hrz_jobs::add_job_rasterize_cesium_terrain_tile(
            js, params, {monitoring::systems::PlanetSurface, raster_id});
        return handle;
    }

    Status get_tile_status(Ticket handle) override
    {
        auto tile = tiles.get_object(handle);
        if (tile != nullptr)
        {
            return tile->status;
        }
        return Status::Error;
    }

    void work_tile(Ticket handle, JobScheduler* js) override
    {
        auto tile = tiles.get_object(handle);
        if (tile != nullptr && tile->status == Status::Decoding)
        {
            if (hrz_jobs::is_job_valid(js, tile->rasterize_ticket)
                && hrz_jobs::is_job_finished(js, tile->rasterize_ticket))
            {
                auto status = hrz_jobs::get_job_status(js, tile->rasterize_ticket);
                if (status == job_scheduler::JobStatus::Finished_Success)
                {
                    hrz::BlobImage response;
                    hrz_jobs::get_job_response(js, tile->rasterize_ticket, response);
                    tile->image = std::move(response);
                    tile->status = Status::Decoded;
                }
                else
                {
                    hrz_jobs::cancel_job(js, tile->rasterize_ticket);
                    tile->status = Status::Error;
                }
            }
        }
    }

    BlobImage get_tile_image(Ticket handle) override
    {
        auto tile = tiles.get_object(handle);
        assert(tile != nullptr && tile->status == Status::Decoded);
        auto image = std::move(tile->image);
        tiles.release(handle);
        return image;
    }

    void cancel_tile(Ticket handle, JobScheduler* js) override
    {
        auto tile = tiles.get_object(handle);
        if (tile != nullptr)
        {
            if (tile->status == Status::Decoding)
            {
                hrz_jobs::cancel_job(js, tile->rasterize_ticket);
            }
            tiles.release(handle);
        }
    }

private:
    struct Tile
    {
        Status status;
        hrz_jobs::RasterizeCesiumTerrainTileTicket rasterize_ticket;
        BlobImage image;
    };

    using IndexPool = GenIndexPool<Ticket, 32, 32>;
    using TilesPool = GenObjectPool<Tile, IndexPool, 128>;

    TilesPool tiles;

    std::string tile_format;
    uint64_t raster_id;
};
} // namespace

bool is_provider_model_complete(const hrz_proto::CesiumTerrainRasterProviderParams& params)
{
    return !params.url().empty();
}

hrz_proto::ImageFormat get_image_format(const hrz_proto::CesiumTerrainRasterProviderParams&)
{
    return hrz_proto::ImageFormat::R_F32;
}

class CesiumProvider : public RasterProvider
{
public:
    CesiumProvider(
        const hrz_proto::CesiumTerrainRasterProviderParams& params,
        assets_loader::Queue queue,
        uint32_t default_tile_cache_size,
        uint64_t raster_id) :
        status(InternalStatus::LoadingDescriptor),
        raw_url(params.url()),
        headers(build_headers(params.http_headers())),
        additional_attribution(params.attribution()),
        al_queue(queue),
        missing_tile_policy(params.missing_tile_policy()),
        tile_cache_capacity(
            params.override_tile_cache_size() ? params.tile_cache_size() : default_tile_cache_size),
        base_url(params.url(), params.preserve_query_parameters()),
        descriptor_download_ticket(0),
        raster_id(raster_id)
    {
        // Add a slash at the end of the URL if not present,
        // in order to reproduce what Cesium accepts.
        base_url.add_slash();
    }

    hrz_proto::RasterProviderType get_raster_provider_type() const override
    {
        return hrz_proto::RasterProviderType::CESIUM_TERRAIN_RASTER_PROVIDER;
    }

    hrz_proto::ImageFormat get_image_format() const override
    {
        return hrz_proto::ImageFormat::R_F32;
    }

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

    const hrz_proto::RasterNodata& get_nodata() const override
    {
        return hrz_proto::RasterNodata::default_instance();
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

        if (tile_coords.lod >= available_tiles.size()) return LockTicket::Invalid;

        lm::uvec2 xy = {tile_coords.x, tile_coords.y};
        if (use_tms_tile_coords)
        {
            uint32_t tile_count = 1 << tile_coords.lod;
            xy.y = tile_count - xy.y - 1;
        }
        for (const auto& bbox : available_tiles.at(tile_coords.lod))
        {
            if (lm::contains(bbox, xy))
            {
                return (LockTicket)fetcher->request_and_lock_tile(
                    tile_coords, al, al_queue, priority);
            }
        }

        return LockTicket::Invalid;
    }

    LockTicket lock_tile_if_ready(TileCoords tile_coords) override
    {
        if (status != InternalStatus::Ready) return LockTicket::Invalid;

        return (LockTicket)fetcher->lock_tile_if_ready(tile_coords);
    }

private:
    // References:
    // https://github.com/CesiumGS/quantized-mesh/issues/15
    // https://community.cesium.com/t/sampleterrainmostdetailed-problem/7647/7
    // https://github.com/CesiumGS/cesium/blob/1.91/Source/Core/CesiumTerrainProvider.js
    // https://github.com/geo-data/cesium-terrain-builder
    // https://github.com/geo-data/cesium-terrain-builder/pull/64
    // https://github.com/ahuarte47/cesium-terrain-builder/tree/master-quantized-mesh
    //
    // layer.json files for Cesium terrain rasters look a bit like TileJSON descriptors
    // from afar, but they are a different format.
    // The fork/MR of Cesium Terrain builder that adds support for writing layer.json
    // files erroneously includes some fields from the TileJSON specification, and also
    // names the `scheme` property `schema`.
    // Cesium's source code for its terrain provider is the actual source of truth.
    bool setup_provider(AttributionRegistry* attributions, std::span<const std::byte> raw_data)
    {
        HRZ_SCOPED_SAMPLE("parse cesium terrain tile json");

        rapidjson::Document doc;
        doc.Parse((const char*)raw_data.data(), raw_data.size());

        if (doc.HasParseError())
        {
            HRZ_LOG_INFO("Couldn't parse layer descriptor");
            return false;
        }

        auto format = hrz::json::get_str_or(doc, "format", "");
        if (std::strcmp(format, "heightmap-1.0") != 0
            && std::strcmp(format, "quantized-mesh-1.0") != 0)
        {
            HRZ_LOG_ERROR("Invalid tile format: {}", format);
            return false;
        }
        tile_format = format;

        auto version = hrz::json::get_str_or(doc, "version", "");

        std::vector<std::string> url_patterns;
        if (doc.HasMember("tiles") && doc["tiles"].IsArray())
        {
            const auto& patterns = doc["tiles"].GetArray();
            for (const auto& pattern : patterns)
            {
                if (pattern.IsString())
                {
                    static constexpr std::string_view kValidArgs[] = {"x", "y", "z", "version"};
                    url_patterns.push_back(base_url.derive(fmt::format(
                        fmt::runtime(hrz::str::sanitize_named_fmt_arguments(
                            pattern.GetString(), kValidArgs)),
                        fmt::arg("x", "{x}"), fmt::arg("y", "{y}"), fmt::arg("z", "{z}"),
                        fmt::arg("version", version))));
                }
            }
        }
        if (url_patterns.empty())
        {
            HRZ_LOG_ERROR("No URL patterns found");
            return false;
        }

        use_tms_tile_coords = false;
        auto scheme = hrz::json::get_str_or(doc, "scheme", "");
        if (std::strcmp(scheme, "") == 0 || std::strcmp(scheme, "tms") == 0)
        {
            use_tms_tile_coords = true;
        }
        else if (std::strcmp(scheme, "slippyMap") != 0)
        {
            HRZ_LOG_ERROR("Invalid tile scheme: {}", scheme);
            return false;
        }

        int max_level = -1;
        if (doc.HasMember("available") && doc["available"].IsArray())
        {
            const auto& levels = doc["available"].GetArray();
            for (size_t level = 0;
                 level < levels.Size() && level <= std::numeric_limits<uint8_t>::max(); ++level)
            {
                const auto& level_data = levels[level];
                if (!level_data.IsArray()) break;

                available_tiles.resize(level + 1);

                int max_x = (1 << level) * 2 - 1;
                int max_y = (1 << level) - 1;

                for (const auto& range : level_data.GetArray())
                {
                    if (range.IsObject())
                    {
                        uint32_t start_x = hrz::json::get_int_or(range, "startX", 0);
                        uint32_t start_y = hrz::json::get_int_or(range, "startY", 0);
                        uint32_t end_x = hrz::json::get_int_or(range, "endX", max_x);
                        uint32_t end_y = hrz::json::get_int_or(range, "endY", max_y);

                        lm::ubbox2 bbox = {{start_x, start_y}, {end_x, end_y}};
                        available_tiles.at(level).push_back(bbox);
                    }
                }

                max_level = (int)level;
            }
        }
        if (max_level == -1)
        {
            HRZ_LOG_ERROR("No levels available");
            return false;
        }

        auto projection = hrz::json::get_str_or(doc, "projection", "EPSG:4326");
        if (std::strcmp(projection, "EPSG:4326") == 0)
        {
            geometry.projection.set_descriptor_type(
                hrz_proto::SrsDescriptorType::PROJ4_STRING_DESCRIPTOR);
            geometry.projection.set_descriptor_(hrz_proj::lonlat_deg_proj_str);

            geometry.tiling_scheme.set_type(hrz_proto::TilingSchemeType::GLOBAL);
            auto tiling = geometry.tiling_scheme.mutable_global_tiling();
            tiling->set_tile_size(256);
            tiling->set_level_zero_tile_count_x(2);
            tiling->set_level_zero_tile_count_y(1);
            tiling->set_min_level(0);
            tiling->set_max_level(max_level);
            tiling->set_border_tile_aspect(hrz_proto::BorderTileAspect::FULL_SIZED);

            auto bounds = GeoBounds::empty();
            for (size_t level = 0; level < available_tiles.size(); ++level)
            {
                const auto& level_bboxes = available_tiles.at(level);

                for (const auto& bbox : level_bboxes)
                {
                    bounds = hrz::merge(
                        bounds,
                        hrz::geodetic_tile_bounds(
                            {bbox.min.x, bbox.min.y, (uint8_t)level}, use_tms_tile_coords));
                    bounds = hrz::merge(
                        bounds,
                        hrz::geodetic_tile_bounds(
                            {bbox.max.x, bbox.max.y, (uint8_t)level}, use_tms_tile_coords));
                }
            }

            geometry.bounds.min.x = lm::degrees(bounds.west);
            geometry.bounds.min.y = lm::degrees(bounds.south);
            geometry.bounds.max.x = lm::degrees(bounds.east);
            geometry.bounds.max.y = lm::degrees(bounds.north);
        }
        else if (std::strcmp(projection, "EPSG:3857") == 0)
        {
            geometry.projection.set_descriptor_type(
                hrz_proto::SrsDescriptorType::PROJ4_STRING_DESCRIPTOR);
            geometry.projection.set_descriptor_(hrz_proj::wmerc_proj_str);

            geometry.tiling_scheme.set_type(hrz_proto::TilingSchemeType::GLOBAL);
            auto tiling = geometry.tiling_scheme.mutable_global_tiling();
            tiling->set_tile_size(256);
            tiling->set_level_zero_tile_count_x(1);
            tiling->set_level_zero_tile_count_y(1);
            tiling->set_min_level(0);
            tiling->set_max_level(max_level);
            tiling->set_border_tile_aspect(hrz_proto::BorderTileAspect::FULL_SIZED);

            auto bounds = lm::dbbox2::invalid();
            for (size_t level = 0; level < available_tiles.size(); ++level)
            {
                const auto& level_bboxes = available_tiles.at(level);

                for (const auto& bbox : level_bboxes)
                {
                    bounds = lm::merge(
                        bounds,
                        hrz::mercator_tile_bbox_meters(
                            {bbox.min.x, bbox.min.y, (uint8_t)level}, use_tms_tile_coords));
                    bounds = lm::merge(
                        bounds,
                        hrz::mercator_tile_bbox_meters(
                            {bbox.max.x, bbox.max.y, (uint8_t)level}, use_tms_tile_coords));
                }
            }

            geometry.bounds = bounds;
        }
        else
        {
            HRZ_LOG_ERROR("Invalid projection: {}", projection);
            return false;
        }

        // @Todo(HRZ-814) Support the "parentUrl" property.

        AttributionHandle attribution_handles[] = {
            attribution::register_attribution(attributions, {additional_attribution, ""}),
            attribution::register_attribution(
                attributions, {hrz::json::get_str_or(doc, "attribution", ""), ""}),
        };
        auto attribution =
            attribution::register_attribution_group(attributions, attribution_handles);

        auto url_generator = std::make_unique<TileUrlGenerator>();
        url_generator->url_patterns = std::move(url_patterns);
        url_generator->use_tms_tile_coords = use_tms_tile_coords;

        fetcher.emplace(
            std::make_unique<UrlTileRequester>(std::move(url_generator), std::nullopt, headers), 0,
            missing_tile_policy == hrz_proto::MissingTilePolicy::USE_LOWER_RESOLUTION,
            std::make_unique<CesiumTerrainTileDecoder>(tile_format, raster_id),
            std::make_unique<SimpleTileAttributionPolicy>(attribution), tile_cache_capacity,
            raster_id,
            TileFetcher::MetricInfo{
                provider_request_tally_metric_name(
                    hrz_proto::RasterProviderType::CESIUM_TERRAIN_RASTER_PROVIDER),
                raw_url.c_str()});

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
                descriptor_url = base_url.derive("layer.json", true);
                descriptor_download_ticket =
                    assets_loader::begin(al, descriptor_url, headers, al_queue);
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
                    HRZ_LOG_ERROR("Couldn't load Cesium terrain resource at {}", descriptor_url);
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
        else if (path.is_cesium_terrain())
        {
            auto provider_path = path.clone().cesium_terrain();
            if (provider_path.is_http_headers())
            {
                if (set_http_headers(build_headers(provider_model.cesium_terrain().http_headers())))
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
            return fetcher->set_http_headers(headers);
        }
        return old.hash_content() != headers.hash_content();
    }

    void dev_ui(mu_Context* ctx) override
    {
        fmt::memory_buffer buffer;

        fmt::format_to(std::back_inserter(buffer), "CesiumTerrainProvider [{}]", raw_url);
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
    std::string raw_url;
    HttpHeaders headers;
    std::string additional_attribution;
    assets_loader::Queue al_queue;
    hrz_proto::MissingTilePolicy missing_tile_policy;
    size_t tile_cache_capacity;

    BaseUrl base_url;
    std::string descriptor_url;

    assets_loader::Ticket descriptor_download_ticket;

    std::optional<TileFetcher> fetcher;
    hrz::planet::TiledRasterGeometry geometry;
    bool use_tms_tile_coords;
    std::string tile_format;

    std::vector<std::vector<lm::ubbox2>> available_tiles;

    uint64_t raster_id;
};

std::unique_ptr<RasterProvider> create_cesium_terrain_provider(
    const hrz_proto::CesiumTerrainRasterProviderParams& params,
    assets_loader::Queue queue,
    uint32_t default_tile_cache_size,
    uint64_t raster_id)
{
    return std::unique_ptr<RasterProvider>(
        new CesiumProvider(params, queue, default_tile_cache_size, raster_id));
}
} // namespace hrz::planet
