#include "hrz/common/crs_database.h"
#include "hrz/common/fmt.h" // IWYU pragma: keep
#include "hrz/common/geo.h"
#include "hrz/common/profiling.h"
#include "hrz/core/image_decoder.h"
#include "hrz/core/jobs/generate_mipmaps.h"
#include "hrz/core/jobs/jobs_tickets.h"
#include "hrz/core/planet/raster_provider.h"
#include "hrz/fnd/format.h"

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

bool is_provider_model_complete(const hrz_proto::UntiledRasterProviderParams& model)
{
    if (model.url().empty()) return false;

    const auto& geometry = model.geometry();

    if (!check_crs(geometry.projection())) return false;

    return true;
}

hrz_proto::ImageFormat get_image_format(const hrz_proto::UntiledRasterProviderParams& params)
{
    return params.image_format();
}

class UntiledRasterProvider : public RasterProvider
{
public:
    UntiledRasterProvider(
        const hrz_proto::UntiledRasterProviderParams& params,
        assets_loader::Queue queue,
        uint64_t raster_id) :
        image_url(params.url()),
        headers(assets_loader::from_proto(params.http_headers())),
        attribution(params.attribution()),
        image_format(params.image_format()),
        nodata(params.nodata()),
        geometry(TiledRasterGeometry::from_geometry_and_tiling_scheme(params.geometry(), {})),
        al_queue(queue),
        raster_id(raster_id)
    {
        if (!params.mime_type_override().empty())
        {
            mime_type_override = params.mime_type_override();
        }
    }

    hrz_proto::RasterProviderType get_raster_provider_type() const override
    {
        return hrz_proto::RasterProviderType::UNTILED_RASTER_PROVIDER;
    }

    hrz_proto::ImageFormat get_image_format() const override { return image_format; }

    void set_load_queue(assets_loader::Queue queue) override { al_queue = queue; }

    Status get_status() const override
    {
        switch (image_status)
        {
            case ImageStatus::Loaded: return Status::Ready;
            case ImageStatus::Error: return Status::Error;
            default: return Status::Loading;
        }
    }

    void release_tile(LockTicket lock_ticket, bool nominate_for_eviction = false) override
    {
        auto it = active_locks.find(lock_ticket);
        if (it != active_locks.end())
        {
            active_locks.erase(it);
        }
    }

    TileStatus get_tile_status(LockTicket lock_ticket) override
    {
        switch (image_status)
        {
            case ImageStatus::Loaded: return TileStatus::Loaded;
            case ImageStatus::Error: return TileStatus::Loaded;
            default: return TileStatus::Loading;
        }
    }

    TileImage get_tile_image(LockTicket lock_ticket) override
    {
        assert(image_status == ImageStatus::Loaded);

        auto it = active_locks.find(lock_ticket);
        if (it == active_locks.end())
        {
            return {};
        }

        return TileImage{
            it->second,
            tiles.at(it->second),
            {std::get<AttributionHandle>(attribution)}};
    }

    const hrz::planet::TiledRasterGeometry& get_geometry() const override
    {
        assert(image_status == ImageStatus::Loaded);

        return geometry;
    }

    const hrz_proto::RasterNodata& get_nodata() const override { return nodata; }

    void cancel_jobs_and_release_tiles(AssetsLoader* al, BlobAllocator* ba, JobScheduler* js)
        override
    {
        assets_loader::end(al, load_ticket);
        hrz_jobs::cancel_job(js, decode_ticket);
        hrz_jobs::cancel_job(js, generate_mipmaps_ticket);
    }

    LockTicket request_and_lock_tile(TileCoords tile_coords, AssetsLoader* al, uint32_t priority)
        override
    {
        auto it = tiles.find(tile_coords);
        if (it == tiles.end())
        {
            return LockTicket::Invalid;
        }

        auto lock_ticket = generate_lock_ticket();
        active_locks[lock_ticket] = tile_coords;
        return lock_ticket;
    }

    LockTicket lock_tile_if_ready(TileCoords tile_coords) override
    {
        auto it = tiles.find(tile_coords);
        if (it != tiles.end())
        {
            if (image_status == ImageStatus::Loaded)
            {
                auto lock_ticket = generate_lock_ticket();
                active_locks[lock_ticket] = tile_coords;
                return lock_ticket;
            }
        }

        return LockTicket::Invalid;
    }

    void work(
        AssetsLoader* al,
        BlobAllocator* ba,
        JobScheduler* js,
        AttributionRegistry* attributions) override
    {
        if (image_status == ImageStatus::NotStarted)
        {
            attribution = attribution::register_attribution(
                attributions, {std::get<std::string>(attribution), ""});
            load_ticket = assets_loader::begin(
                al, image_url, headers, al_queue, 0,
                {monitoring::systems::PlanetSurface, raster_id});
            image_status = ImageStatus::Loading;
            return;
        }

        if (assets_loader::is_valid(al, load_ticket) && assets_loader::is_finished(al, load_ticket))
        {
            HRZ_SCOPED_SAMPLE("load job finished");

            auto load_ticket_status = assets_loader::get_status(al, load_ticket);
            if (load_ticket_status == assets_loader::RequestStatus::Loaded)
            {
                auto mime_type = assets_loader::get_content_type(al, load_ticket);
                if (mime_type_override)
                {
                    mime_type = mime_type_override.value();
                }

                auto blob = assets_loader::get_blob(al, ba, load_ticket);
                decode_ticket = image_decoder::decode_async(
                    js, blob, {monitoring::systems::PlanetSurface, raster_id}, image_format,
                    mime_type);
                image_status = ImageStatus::Decoding;
                assets_loader::end(al, load_ticket);
            }
            else if (load_ticket_status == assets_loader::RequestStatus::Error)
            {
                HRZ_LOG_WARNING("Could not load image at {}", image_url);
                image_status = ImageStatus::Error;
                assets_loader::end(al, load_ticket);
            }
        }

        if (hrz_jobs::is_job_valid(js, decode_ticket)
            && hrz_jobs::is_job_finished(js, decode_ticket))
        {
            HRZ_SCOPED_SAMPLE("decode job finished");

            auto convert_ticket_status = hrz_jobs::get_job_status(js, decode_ticket);
            if (convert_ticket_status == job_scheduler::JobStatus::Finished_Success)
            {
                hrz::BlobImage response;
                hrz_jobs::get_job_response(js, decode_ticket, response);

                if (response.valid())
                {
                    image_width = response.width();
                    image_height = response.height();

                    max_level = std::ceil(std::log2(std::max(image_width, image_height)));

                    hrz_jobs::MipmapGenerationParams mipmap_generation_params;
                    mipmap_generation_params.image = response;
                    mipmap_generation_params.nodata = nodata;
                    mipmap_generation_params.tile_size = hrz::MERCATOR_TILE_SIZE;
                    generate_mipmaps_ticket = hrz_jobs::add_job_generate_mipmaps(
                        js, mipmap_generation_params,
                        {monitoring::systems::PlanetSurface, raster_id});
                    image_status = ImageStatus::GeneratingMipmaps;
                }
                else
                {
                    HRZ_LOG_WARNING("Could not decode image at {}", image_url);
                    image_status = ImageStatus::Error;
                }
            }
            else if (convert_ticket_status == job_scheduler::JobStatus::Finished_Failure)
            {
                HRZ_LOG_WARNING("Could not decode image at {}", image_url);
                image_status = ImageStatus::Error;
                hrz_jobs::cancel_job(js, decode_ticket);
            }
        }

        if (hrz_jobs::is_job_valid(js, generate_mipmaps_ticket)
            && hrz_jobs::is_job_finished(js, generate_mipmaps_ticket))
        {
            HRZ_SCOPED_SAMPLE("mipmap job finished");

            auto mipmaps_ticket_status = hrz_jobs::get_job_status(js, generate_mipmaps_ticket);

            if (mipmaps_ticket_status == job_scheduler::JobStatus::Finished_Success)
            {
                hrz_jobs::Mipmaps response;
                hrz_jobs::get_job_response(js, generate_mipmaps_ticket, response);

                for (unsigned int i = 0; i < response.tiles.size(); i++)
                {
                    auto tile = response.tiles.at(i);

                    auto img = tile.image;

                    if (img.valid())
                    {
                        tiles.insert({tile.coords, std::move(img)});
                    }
                    else
                    {
                        HRZ_LOG_WARNING(
                            "Could not decode image tile {} for image at {}", tile.coords,
                            image_url);
                        image_status = ImageStatus::Error;
                        break;
                    }
                }

                if (image_status != ImageStatus::Error)
                {
                    // Because the image gets tiled once it has been loaded, the tiling scheme
                    // that that has been set through the API, of type UNTILED, is replaced by
                    // a local tiling scheme.

                    geometry.tiling_scheme.set_type(HrzProtocol::TilingSchemeType::LOCAL);

                    auto local_tiling = geometry.tiling_scheme.mutable_local_tiling();
                    local_tiling->set_full_image_width(image_width);
                    local_tiling->set_full_image_height(image_height);
                    local_tiling->set_tile_size(hrz::UNTILED_TILE_SIZE);
                    local_tiling->set_min_level(0);
                    local_tiling->set_has_min_level(true);
                    local_tiling->set_max_level(max_level);
                    local_tiling->set_has_max_level(true);
                    local_tiling->set_level_offset(0);
                    local_tiling->set_override_level_offset(true);
                    local_tiling->set_border_tile_aspect(hrz_proto::BorderTileAspect::CLIPPED);
                    local_tiling->set_tiling_origin(hrz_proto::TilingOrigin::TOP_ORIGIN);

                    geometry.projection_bounds = geometry.bounds;

                    image_status = ImageStatus::Loaded;
                }
            }
            else
            {
                HRZ_LOG_INFO("Mipmap generation failure for image at \"{}\"", image_url);

                hrz_jobs::cancel_job(js, generate_mipmaps_ticket);

                image_status = ImageStatus::Error;
            }
        }
    }

    bool is_working() const override
    {
        return image_status != ImageStatus::Loaded && image_status != ImageStatus::Error;
    }

    UpdateAction notify_model_update(
        const scene_model::RasterProviderPath& path,
        const hrz_proto::RasterProvider& provider_model) override
    {
        if (path.leaf() || path.is_type())
        {
            return UpdateAction::RecreateProvider;
        }
        else if (path.is_untiled())
        {
            auto provider_path = path.clone().untiled();
            if (provider_path.is_http_headers())
            {
                if (set_http_headers(
                        assets_loader::from_proto(provider_model.untiled().http_headers())))
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

    void dev_ui(mu_Context* ctx) override
    {
        fmt::memory_buffer buffer;

        fmt::format_to(std::back_inserter(buffer), "SingleImageProvider [{}]", image_url.c_str());
        buffer.push_back(0);

        unsigned int images_count = 0;
        unsigned int bytes_count = 0;
        for (const auto& pair : tiles)
        {
            auto& image = pair.second;

            if (image.valid())
            {
                ++images_count;
                bytes_count += image.size_in_bytes();
            }
        }

        if (mu_begin_treenode(ctx, buffer.data()))
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

            mu_end_treenode(ctx);
        }
    }

    bool set_http_headers(const HttpHeaders& new_headers) override
    {
        auto old = std::exchange(headers, new_headers);
        return old.hash_content() != headers.hash_content();
    }

private:
    enum class ImageStatus
    {
        NotStarted,
        Loading,
        Decoding,
        GeneratingMipmaps,
        Loaded,
        Error
    };

    std::string image_url;
    HttpHeaders headers;
    std::variant<std::string, AttributionHandle> attribution;
    hrz_proto::ImageFormat image_format;
    std::optional<std::string> mime_type_override;
    hrz_proto::RasterNodata nodata;
    ImageStatus image_status{};
    assets_loader::Ticket load_ticket{};
    hrz_jobs::DecodeBlobImageTicket decode_ticket;
    hrz_jobs::GenerateMipmapsTicket generate_mipmaps_ticket;
    hrz::flat_hash_map<LockTicket, TileCoords> active_locks;

    unsigned int image_width{};
    unsigned int image_height{};
    uint8_t max_level{};
    hrz::planet::TiledRasterGeometry geometry;

    hrz::flat_hash_map<TileCoords, BlobImage> tiles;

    assets_loader::Queue al_queue;
    uint64_t raster_id;
};

std::unique_ptr<RasterProvider> create_untiled_provider(
    const hrz_proto::UntiledRasterProviderParams& params,
    assets_loader::Queue queue,
    uint64_t raster_id)
{
    return std::make_unique<UntiledRasterProvider>(params, queue, raster_id);
}

} // namespace hrz::planet
