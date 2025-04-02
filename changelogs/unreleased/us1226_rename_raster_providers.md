# Added

# Changed

* All raster providers have been renamed:
    * `TiledImageRasterProviderParams` to [[TiledRasterProviderParams]].
    * `SingleImageRasterProviderParams` to [[UntiledRasterProviderParams]].
    * `PalettizedImageProviderParams` to [[PalettizedRasterProviderParams]].
    * `BingProviderParams` to [[BingRasterProviderParams]].
    * `ArcGisProviderParams` to [[ArcGisRasterProviderParams]].
    * `TmsProviderParams` to [[TmsRasterProviderParams]].
    * `WmtsProviderParams` to [[WmtsRasterProviderParams]].
    * `WmsProviderParams` to [[WmsRasterProviderParams]].
    * `CesiumTerrainProviderParams` to [[CesiumTerrainRasterProviderParams]].
    * `TileJsonProviderParams` to [[TileJsonRasterProviderParams]].
    * `PmTilesProviderParams` to [[PmTilesRasterProviderParams]].
* All values of [[RasterProviderType]] have been renamed:
    * `SINGLE_IMAGE_PROVIDER` to `UNTILED_RASTER_PROVIDER`.
    * `TILED_IMAGE_PROVIDER` to `TILED_RASTER_PROVIDER`.
    * `PALETTIZED_IMAGE_PROVIDER` to `PALETTIZED_RASTER_PROVIDER`.
    * `ARCGIS_PROVIDER` to `ARCGIS_RASTER_PROVIDER`.
    * `TMS_PROVIDER` to `TMS_RASTER_PROVIDER`.
    * `WMS_PROVIDER` to `WMS_RASTER_PROVIDER`.
    * `WMTS_PROVIDER` to `WMTS_RASTER_PROVIDER`.
    * `CESIUM_TERRAIN_PROVIDER` to `CESIUM_TERRAIN_RASTER_PROVIDER`.
    * `TILEJSON_PROVIDER` to `TILEJSON_RASTER_PROVIDER`.
    * `PMTILES_PROVIDER` to `PMTILES_RASTER_PROVIDER`.
* Some fields of [[RasterProvider]] have been renamed:
    * `tiled_image` to `tiled`.
    * `single_image` to `untiled`.
    * `palettized_image` to `palettized`.

# Deprecated

# Removed

# Fixed

# Upgrade notes

# Integration notes
