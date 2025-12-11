#pragma once

#include "hrz/common/blob_image.h"
#include "hrz/common/planet/tiled_raster_geometry.h"
#include "hrz/common/tile_coords.h"
#include "hrz/core/assets_loader/assets_loader.h"
#include "hrz/core/attribution.h"
#include "hrz/core/scene_path/raster/provider_paths.h"
#include "hrz/fnd/inlined_vector.h"

extern "C"
{
#include <microui/microui.h>
}

namespace hrz
{
struct BlobAllocator;
struct JobScheduler;

namespace planet
{

uint32_t get_min_lod(const hrz_proto::TilingSchemeParams& tiling_scheme);

const char* provider_request_tally_metric_name(hrz_proto::RasterProviderType type);

struct RasterProvider
{
    enum class LockTicket : uint64_t
    {
        Invalid = 0
    };

    enum class SourceLockTicket : uint64_t
    {
        Invalid = 0
    };

    enum class Status
    {
        Loading,
        Ready,
        Error,
    };

    enum class TileStatus
    {
        Loading,
        Loaded,
    };

    enum class UpdateAction
    {
        KeepTiles,
        RestartTiles,     // Re-fetch the tiles from the provider, but do not reproject them.
        RecreateProvider, // Re-project and re-fetch tiles.
    };

    struct TileImage
    {
        TileCoords coords;
        BlobImage image;
        InlinedVector<AttributionHandle, 8> attribution;
    };

    // Provider API.

    // Provider information.
    virtual hrz_proto::RasterProviderType get_raster_provider_type() const = 0;
    virtual Status get_status() const = 0;
    virtual hrz_proto::ImageFormat get_image_format() const = 0;

    virtual hrz_proto::ImageFormat get_source_image_format() const { return get_image_format(); }

    virtual void set_load_queue(assets_loader::Queue) = 0;

    // Provider tiles related.
    virtual TileStatus get_tile_status(LockTicket lock_ticket) = 0;
    virtual TileImage get_tile_image(LockTicket lock_ticket) = 0;

    // Request the tile at given coordinates. The caller is ensured that
    // the tile will be available anytime (once loaded) as long as `release_tile()`
    // hasn't been called.
    virtual LockTicket request_and_lock_tile(
        TileCoords tile_coords,
        AssetsLoader* al,
        uint32_t priority = 0) = 0;

    // Return `LockTicket::Invalid` is the tile isn't fully loaded.
    virtual LockTicket lock_tile_if_ready(TileCoords tile_coords) = 0;

    // Release the lock on a tile previously acquired by `request_and_lock_tile()`.
    //
    // `nominate_for_eviction` hints the cache about the tile usage. When the caller nows that
    // the tile won't be needed anytime soon, then setting this parameter will try to tag the
    // tile as 'old'. Thus, it will likely be evicted from cache in the next few frames.
    virtual void release_tile(LockTicket lock_ticket, bool nominate_for_eviction = false) = 0;

    // Request the source tile at given coordinates. The caller is ensured that
    // the tile will be available anytime (once loaded) as long as `release_source_tile()`
    // hasn't been called.
    // The source tile is the one that contains the original information of the layer, but
    // not necessarily the one used for drawing. For example in the case of a palettised
    // raster, the source tile contains scalar values but the tiles used for drawing contain
    // RGBA colours.
    virtual SourceLockTicket request_and_lock_source_tile(
        TileCoords tile_coords,
        AssetsLoader* al,
        uint32_t priority = 0)
    {
        return (SourceLockTicket)request_and_lock_tile(tile_coords, al, priority);
    }

    // Return `SourceLockTicket::Invalid` is the tile isn't fully loaded.
    virtual SourceLockTicket lock_source_tile_if_ready(TileCoords tile_coords)
    {
        return (SourceLockTicket)lock_tile_if_ready(tile_coords);
    }

    virtual TileStatus get_source_tile_status(SourceLockTicket lock_ticket)
    {
        return get_tile_status((LockTicket)lock_ticket);
    }

    virtual TileImage get_source_tile_image(SourceLockTicket lock_ticket)
    {
        return get_tile_image((LockTicket)lock_ticket);
    }

    // Release the lock on a source tile previously acquired by `request_and_lock_source_tile()`.
    virtual void release_source_tile(SourceLockTicket lock_ticket)
    {
        release_tile((LockTicket)lock_ticket);
    }

    // Delete tiles and cancel jobs. Called prior to destruction of the provider instance.
    virtual void cancel_jobs_and_release_tiles(AssetsLoader*, BlobAllocator*, JobScheduler*) = 0;

    // Provider internal work.
    virtual void work(AssetsLoader*, BlobAllocator*, JobScheduler*, AttributionRegistry*) = 0;

    virtual void dev_ui(mu_Context* ctx) = 0;

    // Get the geometry parameters that are actually used for this raster.
    // They can be different from those supplied through the raster provider
    // model (when they exist).
    // For example single image rasters are mipmapped and tiled after they
    // are loaded, and are treated as tiled rasters from the point of view
    // of the rest of the system.
    virtual const hrz::planet::TiledRasterGeometry& get_geometry() const = 0;

    virtual const hrz_proto::RasterNodata& get_nodata() const = 0;

    virtual const hrz_proto::RasterNodata& get_source_nodata() const { return get_nodata(); }

    virtual UpdateAction notify_model_update(
        const scene_model::RasterProviderPath&,
        const hrz_proto::RasterProvider&) = 0;

    // Update HTTP headers without triggering tiles re-download, unless the new
    // headers change content negotiation headers, like Accept. In which case,
    // we return true.
    virtual bool set_http_headers(const HttpHeaders& http_headers) = 0;

    virtual bool is_working() const = 0;

    virtual ~RasterProvider() = default;
};

std::unique_ptr<RasterProvider> create_bing_provider(
    const hrz_proto::BingRasterProviderParams&,
    assets_loader::Queue,
    uint32_t default_tile_cache_size,
    uint64_t raster_id);

std::unique_ptr<RasterProvider> create_arcgis_provider(
    const hrz_proto::ArcGisRasterProviderParams& params,
    assets_loader::Queue,
    uint32_t default_tile_cache_size,
    uint64_t raster_id);

std::unique_ptr<RasterProvider> create_untiled_provider(
    const hrz_proto::UntiledRasterProviderParams&,
    assets_loader::Queue,
    uint64_t raster_id);

std::unique_ptr<RasterProvider> create_tiled_provider(
    const hrz_proto::TiledRasterProviderParams&,
    assets_loader::Queue,
    uint32_t default_tile_cache_size,
    uint64_t raster_id);

std::unique_ptr<RasterProvider> create_tms_provider(
    const hrz_proto::TmsRasterProviderParams&,
    assets_loader::Queue,
    uint32_t default_tile_cache_size,
    uint64_t raster_id);

std::unique_ptr<RasterProvider> create_wmts_provider(
    const hrz_proto::WmtsRasterProviderParams&,
    assets_loader::Queue,
    uint32_t default_tile_cache_size,
    uint64_t raster_id);

std::unique_ptr<RasterProvider> create_wms_provider(
    const hrz_proto::WmsRasterProviderParams&,
    assets_loader::Queue,
    uint32_t default_tile_cache_size,
    uint64_t raster_id);

std::unique_ptr<RasterProvider> create_tilejson_provider(
    const hrz_proto::TileJsonRasterProviderParams&,
    assets_loader::Queue,
    uint32_t default_tile_cache_size,
    uint64_t raster_id);

std::unique_ptr<RasterProvider> create_pmtiles_provider(
    const hrz_proto::PmTilesRasterProviderParams&,
    assets_loader::Queue,
    uint32_t default_tile_cache_size,
    uint64_t raster_id);

std::unique_ptr<RasterProvider> create_cesium_terrain_provider(
    const hrz_proto::CesiumTerrainRasterProviderParams&,
    assets_loader::Queue,
    uint32_t default_tile_cache_size,
    uint64_t raster_id);

std::unique_ptr<RasterProvider> create_palettized_provider(
    const hrz_proto::PalettizedRasterProviderParams&,
    assets_loader::Queue,
    uint32_t default_tile_cache_size,
    uint64_t raster_id);

std::unique_ptr<RasterProvider> create_provider(
    const hrz_proto::RasterProvider& provider_model,
    assets_loader::Queue,
    uint32_t default_tile_cache_size,
    uint64_t raster_id);

bool is_provider_model_complete(const hrz_proto::ArcGisRasterProviderParams& params);
bool is_provider_model_complete(const hrz_proto::BingRasterProviderParams& params);
bool is_provider_model_complete(const hrz_proto::TiledRasterProviderParams& params);
bool is_provider_model_complete(const hrz_proto::TmsRasterProviderParams& params);
bool is_provider_model_complete(const hrz_proto::WmtsRasterProviderParams& params);
bool is_provider_model_complete(const hrz_proto::WmsRasterProviderParams& params);
bool is_provider_model_complete(const hrz_proto::TileJsonRasterProviderParams& params);
bool is_provider_model_complete(const hrz_proto::PmTilesRasterProviderParams& params);
bool is_provider_model_complete(const hrz_proto::CesiumTerrainRasterProviderParams& params);
bool is_provider_model_complete(const hrz_proto::UntiledRasterProviderParams& params);
bool is_provider_model_complete(const hrz_proto::PalettizedRasterProviderParams& params);
bool is_provider_model_complete(const hrz_proto::RasterProvider&);

bool is_tiling_scheme_model_complete(const hrz_proto::TilingSchemeParams&);

hrz_proto::ImageFormat get_image_format(const hrz_proto::ArcGisRasterProviderParams&);
hrz_proto::ImageFormat get_image_format(const hrz_proto::BingRasterProviderParams&);
hrz_proto::ImageFormat get_image_format(const hrz_proto::TiledRasterProviderParams&);
hrz_proto::ImageFormat get_image_format(const hrz_proto::TmsRasterProviderParams&);
hrz_proto::ImageFormat get_image_format(const hrz_proto::WmtsRasterProviderParams&);
hrz_proto::ImageFormat get_image_format(const hrz_proto::WmsRasterProviderParams&);
hrz_proto::ImageFormat get_image_format(const hrz_proto::TileJsonRasterProviderParams&);
hrz_proto::ImageFormat get_image_format(const hrz_proto::PmTilesRasterProviderParams&);
hrz_proto::ImageFormat get_image_format(const hrz_proto::CesiumTerrainRasterProviderParams&);
hrz_proto::ImageFormat get_image_format(const hrz_proto::UntiledRasterProviderParams&);
hrz_proto::ImageFormat get_image_format(const hrz_proto::PalettizedRasterProviderParams&);
hrz_proto::ImageFormat get_image_format(const hrz_proto::RasterProvider&);
} // namespace planet
} // namespace hrz
