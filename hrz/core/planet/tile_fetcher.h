#pragma once

#include "hrz/common/blob_allocator.h"
#include "hrz/common/blob_image.h"
#include "hrz/common/geo.h"
#include "hrz/common/metrics.h"
#include "hrz/core/assets_loader/assets_loader.h"
#include "hrz/core/image_decoder.h"
#include "hrz/core/job_scheduler.h"
#include "hrz/core/jobs/jobs_tickets.h"
#include "hrz/core/planet/raster_provider.h"
#include "hrz/core/tile_url_generator.h"
#include "hrz/fnd/class.h"
#include "hrz/fnd/flat_hash_map.h"
#include "hrz/fnd/flat_hash_set.h"
#include "hrz/fnd/gen_object_pool.h"
#include "hrz/fnd/string_utils.h"
#include "hrz/fnd/variant.h"

#include <bit>
#include <span>

namespace hrz::planet
{
class TileDecoder
{
public:
    using Ticket = uint64_t;

    enum class Status
    {
        Decoding,
        Decoded,
        Error,
    };

    TileDecoder() = default;
    HRZ_DELETE_COPY_MOVE(TileDecoder);
    virtual ~TileDecoder() = default;

    virtual Ticket decode_tile(blobs::BlobHandle, std::string_view mime_type, JobScheduler*) = 0;
    virtual Status get_tile_status(Ticket) = 0;
    virtual void work_tile(Ticket, JobScheduler*) = 0;
    virtual BlobImage get_tile_image(Ticket) = 0;
    virtual void cancel_tile(Ticket, JobScheduler*) = 0;
};

class ImageTileDecoder : public TileDecoder
{
public:
    ImageTileDecoder(hrz_proto::ImageFormat image_format, uint64_t raster_id) :
        image_format(image_format), raster_id(raster_id)
    {
    }

    Ticket decode_tile(blobs::BlobHandle, std::string_view mime_type, JobScheduler*) override;
    Status get_tile_status(Ticket) override;
    void work_tile(Ticket, JobScheduler*) override;
    BlobImage get_tile_image(Ticket) override;
    void cancel_tile(Ticket, JobScheduler*) override;

private:
    struct Tile
    {
        Status status;
        hrz_jobs::DecodeBlobImageTicket decode_ticket;
        BlobImage image;
    };

    using IndexPool = GenIndexPool<Ticket, 32, 32>;
    using TilesPool = GenObjectPool<Tile, IndexPool, 128>;

    TilesPool tiles;

    hrz_proto::ImageFormat image_format;
    uint64_t raster_id;
};

class TileAttributionPolicy
{
public:
    TileAttributionPolicy() = default;
    HRZ_DELETE_COPY_MOVE(TileAttributionPolicy);
    virtual ~TileAttributionPolicy() = default;

    virtual void work(AttributionRegistry*) = 0;

    virtual hrz::InlinedVector<AttributionHandle, 4> get_tile_attribution(TileCoords) const = 0;
};

class SimpleTileAttributionPolicy : public TileAttributionPolicy
{
    std::variant<std::string, AttributionHandle> _attribution;

public:
    explicit SimpleTileAttributionPolicy(AttributionHandle attribution) : _attribution(attribution)
    {
    }

    explicit SimpleTileAttributionPolicy(std::string_view attribution) :
        _attribution(std::string(attribution))
    {
    }

    void work(AttributionRegistry* reg) override
    {
        if (std::holds_alternative<std::string>(_attribution))
        {
            _attribution =
                attribution::register_attribution(reg, {std::get<std::string>(_attribution), ""});
        }
    }

    hrz::InlinedVector<AttributionHandle, 4> get_tile_attribution(TileCoords) const override
    {
        if (std::holds_alternative<AttributionHandle>(_attribution))
        {
            return {std::get<AttributionHandle>(_attribution)};
        }
        else
        {
            return {};
        }
    }
};

class WebMercatorZonesTileAttributionPolicy : public TileAttributionPolicy
{
    struct Zone
    {
        int lod_min, lod_max;
        lm::dbbox2 webmercator_bounds;
        AttributionHandle attribution;
    };

    // The zones are sorted by lod min so that we can speedup lookup a bit.
    // We could implement more advanced strategies but for now it's fine.
    std::vector<Zone> _zones;

public:
    void add_zone(int lod_min, int lod_max, const hrz::GeoBounds&, AttributionHandle);

    void work(AttributionRegistry* reg) override {}

    hrz::InlinedVector<AttributionHandle, 4> get_tile_attribution(TileCoords) const override;
};

class TileRequester
{
public:
    struct Ticket
    {
        uint64_t o;
    };

    TileRequester() = default;
    HRZ_DELETE_COPY_MOVE(TileRequester);
    virtual ~TileRequester() = default;

    virtual std::string make_description(const TileCoords& tile_coords) const = 0;

    virtual Ticket request_tile(
        TileCoords,
        AssetsLoader*,
        assets_loader::Queue,
        uint32_t priority,
        const monitoring::ResourceOwner&) = 0;

    virtual void cancel(Ticket, AssetsLoader*, JobScheduler* js) = 0;

    virtual bool is_finished(Ticket, AssetsLoader*) = 0;

    virtual bool is_success(Ticket, AssetsLoader*) = 0;

    // Returns blob + MIME type.
    virtual std::pair<blobs::BlobHandle, std::string> retrieve_blob(
        Ticket,
        AssetsLoader*,
        BlobAllocator*) = 0;

    // Returns whether content negotiation headers have been changed.
    virtual bool set_http_headers(const HttpHeaders& http_headers) = 0;

    virtual void work(AssetsLoader*, JobScheduler*, BlobAllocator*) = 0;

    virtual void cancel_all(AssetsLoader*, BlobAllocator*) = 0;
};

class UrlTileRequester : public TileRequester
{
    std::unique_ptr<TileUrlGenerator> _tile_url_generator;
    HttpHeaders _headers;
    std::optional<std::string> _mime_type_override;

    static_assert(sizeof(Ticket) == sizeof(assets_loader::Ticket));

public:
    UrlTileRequester(
        std::unique_ptr<TileUrlGenerator> tile_url_generator,
        std::optional<std::string_view> mime_type_override,
        const HttpHeaders& headers) :
        _tile_url_generator(std::move(tile_url_generator)),
        _headers(headers),
        _mime_type_override(mime_type_override)
    {
    }

    std::string make_description(const TileCoords& tile_coords) const override
    {
        return _tile_url_generator->make_url(tile_coords.x, tile_coords.y, tile_coords.lod);
    }

    Ticket request_tile(
        TileCoords tile_coords,
        AssetsLoader* al,
        assets_loader::Queue queue,
        uint32_t priority,
        const monitoring::ResourceOwner& owner) override
    {
        std::string target_url =
            _tile_url_generator->make_url(tile_coords.x, tile_coords.y, tile_coords.lod);
        return std::bit_cast<Ticket>(
            assets_loader::begin(al, target_url, _headers, queue, priority, owner));
    }

    void cancel(Ticket ticket, AssetsLoader* al, JobScheduler*) override
    {
        assets_loader::end(al, std::bit_cast<assets_loader::Ticket>(ticket));
    }

    bool is_finished(Ticket ticket, AssetsLoader* al) override
    {
        return assets_loader::is_finished(al, std::bit_cast<assets_loader::Ticket>(ticket));
    }

    bool is_success(Ticket ticket, AssetsLoader* al) override
    {
        return assets_loader::get_status(al, std::bit_cast<assets_loader::Ticket>(ticket))
            == assets_loader::RequestStatus::Loaded;
    }

    std::pair<blobs::BlobHandle, std::string> retrieve_blob(
        Ticket ticket,
        AssetsLoader* al,
        BlobAllocator* ba) override
    {
        std::string_view mime_type = "";
        if (_mime_type_override)
        {
            mime_type = _mime_type_override.value();
        }
        else
        {
            mime_type =
                assets_loader::get_content_type(al, std::bit_cast<assets_loader::Ticket>(ticket));
        }
        return {
            assets_loader::get_blob(al, ba, std::bit_cast<assets_loader::Ticket>(ticket)),
            std::string(mime_type)};
    }

    bool set_http_headers(const HttpHeaders& http_headers) override
    {
        auto old = std::exchange(_headers, http_headers);
        return old.hash_content() != _headers.hash_content();
    }

    void work(AssetsLoader*, JobScheduler*, BlobAllocator*) override {}

    void cancel_all(AssetsLoader*, BlobAllocator*) override {}
};

// Internal logic for tile-based providers.
class TileFetcher
{
public:
    enum class LockTicket : uint64_t
    {
        Invalid = 0
    };

    struct MetricInfo
    {
        const char* name;
        const char* url;
    };

private:
    struct Tile
    {
        enum class Status
        {
            Canceled,
            Loading,
            Decoding,
            Loaded,
            Missing,
            UseLowerRes,
        };

        struct LoadingTicket
        {
            TileRequester::Ticket ticket;
            assets_loader::Queue queue;
            uint32_t priority;
        };

        struct DecodingTicket
        {
            TileDecoder::Ticket ticket;
        };

        struct ParentTicket
        {
            LockTicket ticket;
        };

        struct Empty
        {
        };

        Status status;
        TileCoords coords;

        using Payload = std::variant<Empty, BlobImage, LoadingTicket, DecodingTicket, ParentTicket>;

        Payload payload;

        double last_touch_time_ms;
        int32_t lock_count;

        Tile() = default;
        HRZ_DELETE_COPY(Tile);
        HRZ_DEFAULT_MOVE(Tile);
        ~Tile() = default;
    };

    using TileHandle = uint32_t;
    using IndexPool = GenIndexPool<TileHandle, 16, 16>;
    using TilesPool = GenObjectPool<Tile, IndexPool, 128>;

    TilesPool _tiles_pool;

    hrz::flat_hash_map<TileCoords, TileHandle> _coords_to_tile_handle;
    hrz::flat_hash_set<TileHandle> _loading_tiles;
    hrz::flat_hash_set<TileHandle> _decoding_tiles;
    // This is a duplicate sorted version of `coords_to_tile_handle` for caching purposes.
    // @Todo @Performance: it could be interesting to put in here only tiles that have a
    // `lock_count` of 0 as they are the only one that can be evicted.
    std::vector<TileHandle> _ordered_tiles;
    hrz::flat_hash_map<LockTicket, TileHandle> _active_locks;

    std::unique_ptr<TileRequester> _tile_requester;

    bool _try_lower_resolution;
    uint32_t _cache_capacity;
    uint32_t _download_count = 0;
    uint32_t _lod_min = 0;

    std::unique_ptr<TileDecoder> _tile_decoder;
    std::unique_ptr<TileAttributionPolicy> _tile_attribution;

    uint64_t _raster_id;

    metrics::MetricDesc _request_count_metric;

    void evict_tile(AssetsLoader* al, BlobAllocator* ba, JobScheduler* js, TileHandle handle);

public:
    TileFetcher(
        std::unique_ptr<TileRequester> tile_requester,
        uint32_t lod_min,
        bool try_lower_resolution,
        std::unique_ptr<TileDecoder> tile_decoder,
        std::unique_ptr<TileAttributionPolicy> tile_attribution,
        size_t cache_capacity,
        uint64_t raster_id,
        MetricInfo metric_info);

    RasterProvider::TileStatus get_tile_status(LockTicket lock_ticket) const;

    RasterProvider::TileImage get_tile_image(LockTicket lock_ticket);

    void release_tile(LockTicket lock_ticket, bool nominate_for_eviction = false);

    LockTicket request_and_lock_tile(
        TileCoords tile_coords,
        AssetsLoader* al,
        assets_loader::Queue queue,
        uint32_t priority);

    LockTicket lock_tile_if_ready(TileCoords tile_coords);

    // This should be called only when the provider is no longer needed. In other circumstances, it
    // is preferred to use release_tile() that will correctly handle tiles no longer referenced. An
    // unreferenced tile will have its jobs canceled. This function is different because it cancels
    // tiles no matter the number of references thus, it forces a full restart of the loading
    // process for tiles not loaded yet.
    void cancel_jobs_and_release_tiles(AssetsLoader*, BlobAllocator*, JobScheduler*);

    void work(AssetsLoader*, BlobAllocator*, JobScheduler*, AttributionRegistry*);

    bool is_working() const;

    // Returns whether content negotiation headers have been changed.
    bool set_http_headers(const HttpHeaders& http_headers)
    {
        return _tile_requester->set_http_headers(http_headers);
    }

    static inline RasterProvider::TileStatus convert_tile_status(Tile::Status status)
    {
        switch (status)
        {
            case Tile::Status::Loaded: return RasterProvider::TileStatus::Loaded;
            case Tile::Status::Missing: return RasterProvider::TileStatus::Loaded;
            default: return RasterProvider::TileStatus::Loading;
        }
    }

    void dev_ui(mu_Context* ctx);
};

} // namespace hrz::planet
