#include "planet/hrz_core_planet_raster_provider.h"

#include <hrz_common_image_processing.h>

#include <cassert>

namespace hrz::planet
{
std::unique_ptr<RasterProvider> create_provider(
    const hrz_proto::RasterProvider& provider_model,
    assets_loader::Queue queue,
    uint32_t default_tile_cache_size,
    uint64_t raster_id)
{
    switch (provider_model.type())
    {
        case hrz_proto::RasterProviderType::UNTILED_RASTER_PROVIDER:
            return hrz::planet::create_untiled_provider(provider_model.untiled(), queue, raster_id);
        case hrz_proto::RasterProviderType::TILED_RASTER_PROVIDER:
            return hrz::planet::create_tiled_provider(
                provider_model.tiled(), queue, default_tile_cache_size, raster_id);
        case hrz_proto::RasterProviderType::TMS_RASTER_PROVIDER:
            return hrz::planet::create_tms_provider(
                provider_model.tms(), queue, default_tile_cache_size, raster_id);
        case hrz_proto::RasterProviderType::BING_RASTER_PROVIDER:
            return hrz::planet::create_bing_provider(
                provider_model.bing(), queue, default_tile_cache_size, raster_id);
        case hrz_proto::RasterProviderType::ARCGIS_RASTER_PROVIDER:
            return hrz::planet::create_arcgis_provider(
                provider_model.arcgis(), queue, default_tile_cache_size, raster_id);
        case hrz_proto::RasterProviderType::WMTS_RASTER_PROVIDER:
            return hrz::planet::create_wmts_provider(
                provider_model.wmts(), queue, default_tile_cache_size, raster_id);
        case hrz_proto::RasterProviderType::WMS_RASTER_PROVIDER:
            return hrz::planet::create_wms_provider(
                provider_model.wms(), queue, default_tile_cache_size, raster_id);
        case hrz_proto::RasterProviderType::TILEJSON_RASTER_PROVIDER:
            return hrz::planet::create_tilejson_provider(
                provider_model.tilejson(), queue, default_tile_cache_size, raster_id);
        case hrz_proto::RasterProviderType::PMTILES_RASTER_PROVIDER:
            return hrz::planet::create_pmtiles_provider(
                provider_model.pmtiles(), queue, default_tile_cache_size, raster_id);
        case hrz_proto::RasterProviderType::CESIUM_TERRAIN_RASTER_PROVIDER:
            return hrz::planet::create_cesium_terrain_provider(
                provider_model.cesium_terrain(), queue, default_tile_cache_size, raster_id);
        case hrz_proto::RasterProviderType::PALETTIZED_RASTER_PROVIDER:
            return hrz::planet::create_palettized_provider(
                provider_model.palettized(), queue, default_tile_cache_size, raster_id);
        default: assert(false && "Unhandled case"); return nullptr;
    }
}

bool is_provider_model_complete(const hrz_proto::RasterProvider& provider_model)
{
    switch (provider_model.type())
    {
        case hrz_proto::RasterProviderType::UNTILED_RASTER_PROVIDER:
            return provider_model.has_untiled()
                && is_provider_model_complete(provider_model.untiled());
        case hrz_proto::RasterProviderType::TILED_RASTER_PROVIDER:
            return provider_model.has_tiled() && is_provider_model_complete(provider_model.tiled());
        case hrz_proto::RasterProviderType::TMS_RASTER_PROVIDER:
            return provider_model.has_tms() && is_provider_model_complete(provider_model.tms());
        case hrz_proto::RasterProviderType::BING_RASTER_PROVIDER:
            return provider_model.has_bing() && is_provider_model_complete(provider_model.bing());
        case hrz_proto::RasterProviderType::ARCGIS_RASTER_PROVIDER:
            return provider_model.has_arcgis()
                && is_provider_model_complete(provider_model.arcgis());
        case hrz_proto::RasterProviderType::WMTS_RASTER_PROVIDER:
            return provider_model.has_wmts() && is_provider_model_complete(provider_model.wmts());
        case hrz_proto::RasterProviderType::WMS_RASTER_PROVIDER:
            return provider_model.has_wms() && is_provider_model_complete(provider_model.wms());
        case hrz_proto::RasterProviderType::TILEJSON_RASTER_PROVIDER:
            return provider_model.has_tilejson()
                && is_provider_model_complete(provider_model.tilejson());
        case hrz_proto::RasterProviderType::PMTILES_RASTER_PROVIDER:
            return provider_model.has_pmtiles()
                && is_provider_model_complete(provider_model.pmtiles());
        case hrz_proto::RasterProviderType::CESIUM_TERRAIN_RASTER_PROVIDER:
            return provider_model.has_cesium_terrain()
                && is_provider_model_complete(provider_model.cesium_terrain());
        case hrz_proto::RasterProviderType::PALETTIZED_RASTER_PROVIDER:
            return provider_model.has_palettized()
                && is_provider_model_complete(provider_model.palettized());
        default: assert(false && "Unhandled case"); return false;
    }
}

bool is_tiling_scheme_model_complete(const hrz_proto::TilingSchemeParams& tiling_scheme)
{
    auto type = tiling_scheme.type();
    switch (type)
    {
        case hrz_proto::TilingSchemeType::UNTILED: return true;
        case hrz_proto::TilingSchemeType::LOCAL: return tiling_scheme.has_local_tiling();
        case hrz_proto::TilingSchemeType::GLOBAL: return tiling_scheme.has_global_tiling();
        default: assert(false && "Unhandled case"); return false;
    }
}

hrz_proto::ImageFormat get_image_format(const hrz_proto::RasterProvider& provider_model)
{
    switch (provider_model.type())
    {
        case hrz_proto::RasterProviderType::UNTILED_RASTER_PROVIDER:
            return get_image_format(provider_model.untiled());
        case hrz_proto::RasterProviderType::TILED_RASTER_PROVIDER:
            return get_image_format(provider_model.tiled());
        case hrz_proto::RasterProviderType::TMS_RASTER_PROVIDER:
            return get_image_format(provider_model.tms());
        case hrz_proto::RasterProviderType::BING_RASTER_PROVIDER:
            return get_image_format(provider_model.bing());
        case hrz_proto::RasterProviderType::ARCGIS_RASTER_PROVIDER:
            return get_image_format(provider_model.arcgis());
        case hrz_proto::RasterProviderType::WMTS_RASTER_PROVIDER:
            return get_image_format(provider_model.wmts());
        case hrz_proto::RasterProviderType::WMS_RASTER_PROVIDER:
            return get_image_format(provider_model.wms());
        case hrz_proto::RasterProviderType::TILEJSON_RASTER_PROVIDER:
            return get_image_format(provider_model.tilejson());
        case hrz_proto::RasterProviderType::PMTILES_RASTER_PROVIDER:
            return get_image_format(provider_model.pmtiles());
        case hrz_proto::RasterProviderType::CESIUM_TERRAIN_RASTER_PROVIDER:
            return get_image_format(provider_model.cesium_terrain());
        case hrz_proto::RasterProviderType::PALETTIZED_RASTER_PROVIDER:
            return get_image_format(provider_model.palettized());
        default: assert(false && "Unhandled case"); return hrz_proto::ImageFormat::SRGBA_8;
    }
}
} // namespace hrz::planet
