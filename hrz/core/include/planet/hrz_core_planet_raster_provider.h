#pragma once

#include "assets_loader/hrz_core_assets_loader.h"
#include "hrz_core_attribution.h"
#include "hrz_core_scene_path.h"

#include <hrz_common_blob_image.h>
#include <hrz_common_tile_coords.h>
#include <hrz_fnd_inlined_vector.h>

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
static uint32_t get_min_lod(const hrz_proto::RasterGeometry& geometry)
{
    const auto& tiling = geometry.tiling_scheme();

    switch (tiling.type())
    {
        case hrz_proto::TilingSchemeType::UNTILED: return 0;
        case hrz_proto::TilingSchemeType::GLOBAL: return tiling.global_tiling().min_level();
        case hrz_proto::TilingSchemeType::LOCAL:
            return tiling.local_tiling().has_min_level() ? tiling.local_tiling().min_level() : 0;
        default: return 0;
    }
}

static const char* provider_request_tally_metric_name(hrz_proto::RasterProviderType type)
{
    switch (type)
    {
        case hrz_proto::RasterProviderType::SINGLE_IMAGE_PROVIDER:
            return "Single image (requests tally)";
        case hrz_proto::RasterProviderType::TILED_IMAGE_PROVIDER:
            return "Tiled image (requests tally)";
        case hrz_proto::RasterProviderType::BING_PROVIDER: return "Bing (requests tally)";
        case hrz_proto::RasterProviderType::PALETTIZED_IMAGE_PROVIDER:
            return "Palettized image (requests tally)";
        case hrz_proto::RasterProviderType::ARCGIS_PROVIDER: return "ArcGIS (requests tally)";
        case hrz_proto::RasterProviderType::TMS_PROVIDER: return "TMS (requests tally)";
        case hrz_proto::RasterProviderType::WMTS_PROVIDER: return "WMTS (requests tally)";
        case hrz_proto::RasterProviderType::WMS_PROVIDER: return "WMS (requests tally)";
        case hrz_proto::RasterProviderType::TILEJSON_PROVIDER: return "TileJSON (requests tally)";
        case hrz_proto::RasterProviderType::CESIUM_TERRAIN_PROVIDER:
            return "Cesium terrain (requests tally)";
        case hrz_proto::RasterProviderType::PMTILES_PROVIDER: return "PMTiles (requests tally)";
        default: assert(false && "Unhandled case");
    }

    return "";
}

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
    virtual const hrz_proto::RasterGeometry& get_geometry() const = 0;

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
    const hrz_proto::BingProviderParams&,
    assets_loader::Queue,
    uint32_t default_tile_cache_size,
    uint64_t raster_id);

std::unique_ptr<RasterProvider> create_arcgis_provider(
    const hrz_proto::ArcGisProviderParams& params,
    assets_loader::Queue,
    uint32_t default_tile_cache_size,
    uint64_t raster_id);

std::unique_ptr<RasterProvider> create_single_image_provider(
    const hrz_proto::SingleImageRasterProviderParams&,
    assets_loader::Queue,
    uint64_t raster_id);

std::unique_ptr<RasterProvider> create_tiled_image_provider(
    const hrz_proto::TiledImageRasterProviderParams&,
    assets_loader::Queue,
    uint32_t default_tile_cache_size,
    uint64_t raster_id);

std::unique_ptr<RasterProvider> create_tms_provider(
    const hrz_proto::TmsProviderParams&,
    assets_loader::Queue,
    uint32_t default_tile_cache_size,
    uint64_t raster_id);

std::unique_ptr<RasterProvider> create_wmts_provider(
    const hrz_proto::WmtsProviderParams&,
    assets_loader::Queue,
    uint32_t default_tile_cache_size,
    uint64_t raster_id);

std::unique_ptr<RasterProvider> create_wms_provider(
    const hrz_proto::WmsProviderParams&,
    assets_loader::Queue,
    uint32_t default_tile_cache_size,
    uint64_t raster_id);

std::unique_ptr<RasterProvider> create_tilejson_provider(
    const hrz_proto::TileJsonProviderParams&,
    assets_loader::Queue,
    uint32_t default_tile_cache_size,
    uint64_t raster_id);

std::unique_ptr<RasterProvider> create_pmtiles_provider(
    const hrz_proto::PmTilesProviderParams&,
    assets_loader::Queue,
    uint32_t default_tile_cache_size,
    uint64_t raster_id);

std::unique_ptr<RasterProvider> create_cesium_terrain_provider(
    const hrz_proto::CesiumTerrainProviderParams&,
    assets_loader::Queue,
    uint32_t default_tile_cache_size,
    uint64_t raster_id);

std::unique_ptr<RasterProvider> create_palettized_image_provider(
    const hrz_proto::PalettizedImageRasterProviderParams&,
    assets_loader::Queue,
    uint32_t default_tile_cache_size,
    uint64_t raster_id);

std::unique_ptr<RasterProvider> create_provider(
    const hrz_proto::RasterProvider& provider_model,
    assets_loader::Queue,
    uint32_t default_tile_cache_size,
    uint64_t raster_id);

bool is_provider_model_complete(const hrz_proto::ArcGisProviderParams& params);
bool is_provider_model_complete(const hrz_proto::BingProviderParams& params);
bool is_provider_model_complete(const hrz_proto::TiledImageRasterProviderParams& params);
bool is_provider_model_complete(const hrz_proto::TmsProviderParams& params);
bool is_provider_model_complete(const hrz_proto::WmtsProviderParams& params);
bool is_provider_model_complete(const hrz_proto::WmsProviderParams& params);
bool is_provider_model_complete(const hrz_proto::TileJsonProviderParams& params);
bool is_provider_model_complete(const hrz_proto::PmTilesProviderParams& params);
bool is_provider_model_complete(const hrz_proto::CesiumTerrainProviderParams& params);
bool is_provider_model_complete(const hrz_proto::SingleImageRasterProviderParams& params);
bool is_provider_model_complete(const hrz_proto::PalettizedImageRasterProviderParams& params);
bool is_provider_model_complete(const hrz_proto::RasterProvider&);

bool is_tiling_scheme_model_complete(const hrz_proto::TilingSchemeParams&);

hrz_proto::ImageFormat get_image_format(const hrz_proto::ArcGisProviderParams&);
hrz_proto::ImageFormat get_image_format(const hrz_proto::BingProviderParams&);
hrz_proto::ImageFormat get_image_format(const hrz_proto::TiledImageRasterProviderParams&);
hrz_proto::ImageFormat get_image_format(const hrz_proto::TmsProviderParams&);
hrz_proto::ImageFormat get_image_format(const hrz_proto::WmtsProviderParams&);
hrz_proto::ImageFormat get_image_format(const hrz_proto::WmsProviderParams&);
hrz_proto::ImageFormat get_image_format(const hrz_proto::TileJsonProviderParams&);
hrz_proto::ImageFormat get_image_format(const hrz_proto::PmTilesProviderParams&);
hrz_proto::ImageFormat get_image_format(const hrz_proto::CesiumTerrainProviderParams&);
hrz_proto::ImageFormat get_image_format(const hrz_proto::SingleImageRasterProviderParams&);
hrz_proto::ImageFormat get_image_format(const hrz_proto::PalettizedImageRasterProviderParams&);
hrz_proto::ImageFormat get_image_format(const hrz_proto::RasterProvider&);
} // namespace planet
} // namespace hrz
