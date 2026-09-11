# Changed

* The value `UNTILED` of [TilingSchemeType]($proto) has been renamed `UNKNOWN`. This value had been deprecated in the past and was already not usable.

# Fixed

* The properties `has_min_level` and `has_max_level` of [LocalTilingSchemeParams]($proto) are now taken into account.
* Fixed display of tiled rasters with local tiling scheme and bottom tiling origin.
* Fixed raster picking returning erroneous values for some combinations of tiling schemes and picking locations.
* Fixed a crash that occasionally happened when updating a raster layer.
* Fixed a crash that happened when a large image could not be decoded.
