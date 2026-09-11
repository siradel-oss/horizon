// SPDX-FileCopyrightText: Copyright 2019 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/core/planet/raster_provider.h"

#include <cassert>

namespace hrz::planet
{

uint32_t get_min_lod(const hrz_proto::TilingSchemeParams& tiling_scheme)
{
    switch (tiling_scheme.scheme_type_case())
    {
        case hrz_proto::TilingSchemeParams::SchemeTypeCase::SCHEME_TYPE_NOT_SET: return 0;
        case hrz_proto::TilingSchemeParams::SchemeTypeCase::kGlobalTiling:
            return tiling_scheme.global_tiling().min_level();
        case hrz_proto::TilingSchemeParams::SchemeTypeCase::kLocalTiling:
            return tiling_scheme.local_tiling().has_min_level()
                ? tiling_scheme.local_tiling().min_level()
                : 0;
        default: return 0;
    }
}

const char* provider_request_tally_metric_name(hrz_proto::RasterProvider::ProviderTypeCase type)
{
    switch (type)
    {
        case hrz_proto::RasterProvider::ProviderTypeCase::kUntiled:
            return "Untiled (requests tally)";
        case hrz_proto::RasterProvider::ProviderTypeCase::kTiled: return "Tiled (requests tally)";
        case hrz_proto::RasterProvider::ProviderTypeCase::kBing: return "Bing (requests tally)";
        case hrz_proto::RasterProvider::ProviderTypeCase::kPalettized:
            return "Palettized (requests tally)";
        case hrz_proto::RasterProvider::ProviderTypeCase::kArcgis: return "ArcGIS (requests tally)";
        case hrz_proto::RasterProvider::ProviderTypeCase::kTms: return "TMS (requests tally)";
        case hrz_proto::RasterProvider::ProviderTypeCase::kWmts: return "WMTS (requests tally)";
        case hrz_proto::RasterProvider::ProviderTypeCase::kWms: return "WMS (requests tally)";
        case hrz_proto::RasterProvider::ProviderTypeCase::kTilejson:
            return "TileJSON (requests tally)";
        case hrz_proto::RasterProvider::ProviderTypeCase::kCesiumTerrain:
            return "Cesium terrain (requests tally)";
        case hrz_proto::RasterProvider::ProviderTypeCase::kPmtiles:
            return "PMTiles (requests tally)";
        default: assert(false && "Unhandled case");
    }

    return "";
}

std::unique_ptr<RasterProvider> create_provider(
    const hrz_proto::RasterProvider& provider_model,
    assets_loader::Queue queue,
    uint32_t default_tile_cache_size,
    uint64_t raster_id)
{
    switch (provider_model.provider_type_case())
    {
        case hrz_proto::RasterProvider::ProviderTypeCase::kUntiled:
            return hrz::planet::create_untiled_provider(provider_model.untiled(), queue, raster_id);
        case hrz_proto::RasterProvider::ProviderTypeCase::kTiled:
            return hrz::planet::create_tiled_provider(
                provider_model.tiled(), queue, default_tile_cache_size, raster_id);
        case hrz_proto::RasterProvider::ProviderTypeCase::kTms:
            return hrz::planet::create_tms_provider(
                provider_model.tms(), queue, default_tile_cache_size, raster_id);
        case hrz_proto::RasterProvider::ProviderTypeCase::kBing:
            return hrz::planet::create_bing_provider(
                provider_model.bing(), queue, default_tile_cache_size, raster_id);
        case hrz_proto::RasterProvider::ProviderTypeCase::kArcgis:
            return hrz::planet::create_arcgis_provider(
                provider_model.arcgis(), queue, default_tile_cache_size, raster_id);
        case hrz_proto::RasterProvider::ProviderTypeCase::kWmts:
            return hrz::planet::create_wmts_provider(
                provider_model.wmts(), queue, default_tile_cache_size, raster_id);
        case hrz_proto::RasterProvider::ProviderTypeCase::kWms:
            return hrz::planet::create_wms_provider(
                provider_model.wms(), queue, default_tile_cache_size, raster_id);
        case hrz_proto::RasterProvider::ProviderTypeCase::kTilejson:
            return hrz::planet::create_tilejson_provider(
                provider_model.tilejson(), queue, default_tile_cache_size, raster_id);
        case hrz_proto::RasterProvider::ProviderTypeCase::kPmtiles:
            return hrz::planet::create_pmtiles_provider(
                provider_model.pmtiles(), queue, default_tile_cache_size, raster_id);
        case hrz_proto::RasterProvider::ProviderTypeCase::kCesiumTerrain:
            return hrz::planet::create_cesium_terrain_provider(
                provider_model.cesium_terrain(), queue, default_tile_cache_size, raster_id);
        case hrz_proto::RasterProvider::ProviderTypeCase::kPalettized:
            return hrz::planet::create_palettized_provider(
                provider_model.palettized(), queue, default_tile_cache_size, raster_id);
        case hrz_proto::RasterProvider::ProviderTypeCase::PROVIDER_TYPE_NOT_SET: return nullptr;
        default: assert(false && "Unhandled case"); return nullptr;
    }
}

bool is_provider_model_complete(const hrz_proto::RasterProvider& provider_model)
{
    switch (provider_model.provider_type_case())
    {
        case hrz_proto::RasterProvider::ProviderTypeCase::kUntiled:
            return provider_model.has_untiled()
                && is_provider_model_complete(provider_model.untiled());
        case hrz_proto::RasterProvider::ProviderTypeCase::kTiled:
            return provider_model.has_tiled() && is_provider_model_complete(provider_model.tiled());
        case hrz_proto::RasterProvider::ProviderTypeCase::kTms:
            return provider_model.has_tms() && is_provider_model_complete(provider_model.tms());
        case hrz_proto::RasterProvider::ProviderTypeCase::kBing:
            return provider_model.has_bing() && is_provider_model_complete(provider_model.bing());
        case hrz_proto::RasterProvider::ProviderTypeCase::kArcgis:
            return provider_model.has_arcgis()
                && is_provider_model_complete(provider_model.arcgis());
        case hrz_proto::RasterProvider::ProviderTypeCase::kWmts:
            return provider_model.has_wmts() && is_provider_model_complete(provider_model.wmts());
        case hrz_proto::RasterProvider::ProviderTypeCase::kWms:
            return provider_model.has_wms() && is_provider_model_complete(provider_model.wms());
        case hrz_proto::RasterProvider::ProviderTypeCase::kTilejson:
            return provider_model.has_tilejson()
                && is_provider_model_complete(provider_model.tilejson());
        case hrz_proto::RasterProvider::ProviderTypeCase::kPmtiles:
            return provider_model.has_pmtiles()
                && is_provider_model_complete(provider_model.pmtiles());
        case hrz_proto::RasterProvider::ProviderTypeCase::kCesiumTerrain:
            return provider_model.has_cesium_terrain()
                && is_provider_model_complete(provider_model.cesium_terrain());
        case hrz_proto::RasterProvider::ProviderTypeCase::kPalettized:
            return provider_model.has_palettized()
                && is_provider_model_complete(provider_model.palettized());
        case hrz_proto::RasterProvider::ProviderTypeCase::PROVIDER_TYPE_NOT_SET: return false;
        default: assert(false && "Unhandled case"); return false;
    }
}

bool is_tiling_scheme_model_complete(const hrz_proto::TilingSchemeParams& tiling_scheme)
{
    switch (tiling_scheme.scheme_type_case())
    {
        case hrz_proto::TilingSchemeParams::SchemeTypeCase::SCHEME_TYPE_NOT_SET: return false;
        case hrz_proto::TilingSchemeParams::SchemeTypeCase::kLocalTiling:
            return tiling_scheme.has_local_tiling();
        case hrz_proto::TilingSchemeParams::SchemeTypeCase::kGlobalTiling:
            return tiling_scheme.has_global_tiling();
        default: assert(false && "Unhandled case"); return false;
    }
}

hrz_proto::ImageFormat get_image_format(const hrz_proto::RasterProvider& provider_model)
{
    switch (provider_model.provider_type_case())
    {
        case hrz_proto::RasterProvider::ProviderTypeCase::kUntiled:
            return get_image_format(provider_model.untiled());
        case hrz_proto::RasterProvider::ProviderTypeCase::kTiled:
            return get_image_format(provider_model.tiled());
        case hrz_proto::RasterProvider::ProviderTypeCase::kTms:
            return get_image_format(provider_model.tms());
        case hrz_proto::RasterProvider::ProviderTypeCase::kBing:
            return get_image_format(provider_model.bing());
        case hrz_proto::RasterProvider::ProviderTypeCase::kArcgis:
            return get_image_format(provider_model.arcgis());
        case hrz_proto::RasterProvider::ProviderTypeCase::kWmts:
            return get_image_format(provider_model.wmts());
        case hrz_proto::RasterProvider::ProviderTypeCase::kWms:
            return get_image_format(provider_model.wms());
        case hrz_proto::RasterProvider::ProviderTypeCase::kTilejson:
            return get_image_format(provider_model.tilejson());
        case hrz_proto::RasterProvider::ProviderTypeCase::kPmtiles:
            return get_image_format(provider_model.pmtiles());
        case hrz_proto::RasterProvider::ProviderTypeCase::kCesiumTerrain:
            return get_image_format(provider_model.cesium_terrain());
        case hrz_proto::RasterProvider::ProviderTypeCase::kPalettized:
            return get_image_format(provider_model.palettized());
        case hrz_proto::RasterProvider::ProviderTypeCase::PROVIDER_TYPE_NOT_SET:
            return hrz_proto::ImageFormat::SRGBA_8;
        default: assert(false && "Unhandled case"); return hrz_proto::ImageFormat::SRGBA_8;
    }
}

} // namespace hrz::planet
