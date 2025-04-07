#include "hrz_core_pmtiles.h"
#include "planet/hrz_core_planet_raster_provider.h"
#include "planet/hrz_core_planet_tile_fetcher.h"

#include <hrz_common_fmt.h>
#include <hrz_common_geo.h>
#include <hrz_common_profiling.h>

namespace hrz::planet
{
class PmTilesRequester : public TileRequester
{
    std::string _url;
    std::string _mime_type_override;
    std::unique_ptr<PmTiles> _pmtiles;

public:
    explicit PmTilesRequester(
        std::string_view url,
        std::optional<std::string_view> mime_type_override,
        std::unique_ptr<PmTiles> pmtiles) :
        _url(url),
        _mime_type_override(mime_type_override.value_or("")),
        _pmtiles(std::move(pmtiles))
    {
        assert(_pmtiles->get_status() == PmTiles::Status::kReady);
    }

    std::string make_description(const TileCoords& tile_coords) const override
    {
        return fmt::format("{}, tile {}", _url, tile_coords);
    }

    Ticket request_tile(
        TileCoords tile_coords,
        AssetsLoader*,
        assets_loader::Queue queue,
        uint32_t priority,
        const monitoring::ResourceOwner& owner) override
    {
        return hrz::bit_cast<Ticket>(_pmtiles->request_tile(tile_coords, queue, priority, owner));
    }

    void cancel(Ticket ticket, AssetsLoader*, JobScheduler* js) override
    {
        _pmtiles->cancel(hrz::bit_cast<PmTiles::QueryHandle>(ticket), js);
    }

    bool is_finished(Ticket ticket, AssetsLoader*) override
    {
        return _pmtiles->is_finished(hrz::bit_cast<PmTiles::QueryHandle>(ticket));
    }

    bool is_success(Ticket ticket, AssetsLoader*) override
    {
        return _pmtiles->is_success(hrz::bit_cast<PmTiles::QueryHandle>(ticket));
    }

    std::pair<blobs::BlobHandle, std::string> retrieve_blob(
        Ticket ticket,
        AssetsLoader*,
        BlobAllocator*) override
    {
        return std::make_pair(
            _pmtiles->retrieve_blob(hrz::bit_cast<PmTiles::QueryHandle>(ticket)),
            _mime_type_override);
    }

    bool set_http_headers(const HttpHeaders& http_headers) override
    {
        return _pmtiles->set_http_headers(http_headers);
    }

    void work(AssetsLoader*, JobScheduler* js, BlobAllocator* ba) override
    {
        _pmtiles->work(js, ba);
    }

    void cancel_all(AssetsLoader*, BlobAllocator*) override
    {
        _pmtiles->destroy();
        _pmtiles.reset();
    }
};

bool is_provider_model_complete(const hrz_proto::PmTilesRasterProviderParams& params)
{
    if (params.url().empty()) return false;

    return true;
}

hrz_proto::ImageFormat get_image_format(const hrz_proto::PmTilesRasterProviderParams& params)
{
    return params.image_format();
}

class PmTilesRasterProvider : public RasterProvider
{
    enum InternalStatus
    {
        kLoadingDescriptor,
        kReady,
        kError,
    } _status;

    std::string _url;
    HttpHeaders _http_headers;
    assets_loader::Queue _load_queue;
    hrz_proto::ImageFormat _image_format;
    std::optional<std::string> _mime_type_override;
    hrz_proto::RasterNodata _nodata;
    hrz_proto::MissingTilePolicy _missing_tile_policy;
    uint64_t _raster_id;
    std::string _additional_attribution;
    size_t _tile_cache_capacity;
    std::unique_ptr<PmTiles> _pmtiles;
    std::unique_ptr<TileFetcher> _fetcher;
    hrz::planet::TiledRasterGeometry _geometry;

public:
    PmTilesRasterProvider(
        const hrz_proto::PmTilesRasterProviderParams& params,
        assets_loader::Queue queue,
        uint32_t default_tile_cache_size,
        uint64_t raster_id) :
        _status{kLoadingDescriptor},
        _url{params.url()},
        _http_headers{assets_loader::from_proto(params.http_headers())},
        _load_queue{queue},
        _image_format{params.image_format()},
        _nodata(params.nodata()),
        _missing_tile_policy{params.missing_tile_policy()},
        _raster_id{raster_id},
        _additional_attribution{params.attribution()},
        _tile_cache_capacity(
            params.override_tile_cache_size() ? params.tile_cache_size() : default_tile_cache_size)
    {
        if (!params.mime_type_override().empty())
        {
            _mime_type_override = params.mime_type_override();
        }
    }

    hrz_proto::RasterProviderType get_raster_provider_type() const override
    {
        return hrz_proto::RasterProviderType::PMTILES_RASTER_PROVIDER;
    }

    Status get_status() const override
    {
        switch (_status)
        {
            case kLoadingDescriptor: return Status::Loading;
            case kReady: return Status::Ready;
            case kError: return Status::Error;
            default: assert(false && "Unhandled case"); return Status::Error;
        }
    }

    hrz_proto::ImageFormat get_image_format() const override { return _image_format; }

    void set_load_queue(assets_loader::Queue queue) override { _load_queue = queue; }

    TileStatus get_tile_status(LockTicket ticket) override
    {
        if (_status == kReady)
        {
            return _fetcher->get_tile_status((TileFetcher::LockTicket)ticket);
        }
        else
        {
            return TileStatus::Loading;
        }
    }

    TileImage get_tile_image(LockTicket ticket) override
    {
        assert(_status == kReady);
        return _fetcher->get_tile_image((TileFetcher::LockTicket)ticket);
    }

    LockTicket request_and_lock_tile(
        TileCoords tile_coords,
        AssetsLoader* al,
        uint32_t priority = 0) override
    {
        if (_status != InternalStatus::kReady) return LockTicket::Invalid;
        return (LockTicket)_fetcher->request_and_lock_tile(tile_coords, al, _load_queue, priority);
    }

    LockTicket lock_tile_if_ready(TileCoords tile_coords) override
    {
        if (_status != InternalStatus::kReady) return LockTicket::Invalid;
        return (LockTicket)_fetcher->lock_tile_if_ready(tile_coords);
    }

    void release_tile(LockTicket lock_ticket, bool nominate_for_eviction = false) override
    {
        if (_fetcher)
        {
            _fetcher->release_tile((TileFetcher::LockTicket)lock_ticket, nominate_for_eviction);
        }
    }

    void cancel_jobs_and_release_tiles(AssetsLoader* al, BlobAllocator* ba, JobScheduler* js)
        override
    {
        assert(al && ba && js);

        if (_pmtiles)
        {
            assert(!_fetcher);
            _pmtiles->destroy();
        }

        if (_fetcher)
        {
            assert(!_pmtiles);
            _fetcher->cancel_jobs_and_release_tiles(al, ba, js);
        }
    }

    void work(
        AssetsLoader* al,
        BlobAllocator* ba,
        JobScheduler* js,
        AttributionRegistry* attributions) override
    {
        if (_status == kLoadingDescriptor)
        {
            if (!_pmtiles)
            {
                _pmtiles = PmTiles::create(
                    _url, _http_headers, _load_queue, assets_loader::create_channel(al));
            }

            _pmtiles->work(js, ba);

            switch (_pmtiles->get_status())
            {
                case PmTiles::Status::kError: _status = kError; break;
                case PmTiles::Status::kReady:
                {
                    _geometry = _pmtiles->get_geometry();

                    AttributionHandle attribution_handles[] = {
                        attribution::register_attribution(
                            attributions, {_pmtiles->get_attribution(), ""}),
                        attribution::register_attribution(
                            attributions, {_additional_attribution, ""}),
                    };
                    AttributionHandle attributions_handle =
                        attribution::register_attribution_group(attributions, attribution_handles);

                    _fetcher = std::make_unique<TileFetcher>(
                        std::make_unique<PmTilesRequester>(
                            _url, _mime_type_override, std::move(_pmtiles)),
                        _geometry.tiling_scheme.global_tiling().min_level(),
                        _missing_tile_policy == hrz_proto::MissingTilePolicy::USE_LOWER_RESOLUTION,
                        std::make_unique<ImageTileDecoder>(_image_format, _raster_id),
                        std::make_unique<SimpleTileAttributionPolicy>(attributions_handle),
                        _tile_cache_capacity, _raster_id,
                        TileFetcher::MetricInfo{
                            provider_request_tally_metric_name(
                                hrz_proto::RasterProviderType::PMTILES_RASTER_PROVIDER),
                            _url.c_str()});
                    _status = kReady;
                    break;
                }
                default: break;
            }
        }
        else if (_status == kReady)
        {
            assert(_fetcher);
            _fetcher->work(al, ba, js, attributions);
        }
    }

    void dev_ui(mu_Context* ctx) override
    {
        fmt::memory_buffer buffer;

        fmt::format_to(std::back_inserter(buffer), "PmTilesProvider [{}]", _url);
        buffer.push_back(0);

        if (mu_begin_treenode(ctx, buffer.data()))
        {
            if (_fetcher)
            {
                _fetcher->dev_ui(ctx);
            }
            mu_end_treenode(ctx);
        }
    }

    const hrz::planet::TiledRasterGeometry& get_geometry() const override { return _geometry; }

    const hrz_proto::RasterNodata& get_nodata() const override { return _nodata; }

    UpdateAction notify_model_update(
        const scene_model::RasterProviderPath& path,
        const hrz_proto::RasterProvider& provider_model) override
    {
        if (path.leaf() || path.is_type())
        {
            return UpdateAction::RecreateProvider;
        }
        else if (path.is_pmtiles())
        {
            auto provider_path = path.clone().pmtiles();
            if (provider_path.is_http_headers())
            {
                if (set_http_headers(
                        assets_loader::from_proto(provider_model.pmtiles().http_headers())))
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

    bool set_http_headers(const HttpHeaders& headers) override
    {
        if (_pmtiles)
        {
            return _pmtiles->set_http_headers(headers);
        }
        else if (_fetcher)
        {
            return _fetcher->set_http_headers(headers);
        }
        else
        {
            return false;
        }
    }

    bool is_working() const override
    {
        switch (_status)
        {
            case kLoadingDescriptor: return true;
            case kReady: return _fetcher->is_working();
            case kError: [[fallthrough]];
            default: return false;
        }
    }
};

std::unique_ptr<RasterProvider> create_pmtiles_provider(
    const hrz_proto::PmTilesRasterProviderParams& params,
    assets_loader::Queue queue,
    uint32_t default_tile_cache_size,
    uint64_t raster_id)
{
    return std::make_unique<PmTilesRasterProvider>(
        params, queue, default_tile_cache_size, raster_id);
}

} // namespace hrz::planet
