# Changed

* **Rasters**
    * The `geometry` field is now a member of the relevant raster providers parameters ([[TiledImageRasterProviderParams]] and [[SingleImageRasterProviderParams]]) instead of being a member of `RasterParams`. For all other providers the geometry parameters are determined from the source data and the values of [[RasterGeometry]] were already ignored.
    * The `tiling_scheme` field has been removed from `RasterGeometry` and added to the only provider that uses it: [[TiledImageRasterProviderParams]].
    * The `sampling`, `blending`, and `display_bounds` fields of `RasterParams` have been moved to [[Raster]].
    * The `RasterParams` structure has been removed.
    * The nodata value is now specified in the `nodata` field of each raster provider parameter structure, instead of being part of [[RasterSampling]]. The value is now inside a [[RasterNodata]] structure, that allows explicitly enabling or disabling the nodata value.
    * All provider parameter fields (`*_provider`) have been moved from [[Raster]] and [[PalettizedImageRasterProviderParams]] to a new structure named [[RasterProvider]]. The new field is named `provider` in both [[Raster]] and [[PalettizedImageRasterProviderParams]].
    * The field `type` of [[RasterProvider]] allows explicitly setting the type of the provider.
    * The field `nodata_color` has been added on [[PalettizedImageRasterProviderParams]]. It allows setting the colour for nodata values coming from the child provider, regardless of what the actual values are.
    * The field `nodata` has been added on [[PalettizedImageRasterProviderParams]]. It allows specifying a colour (after palettisation) that is considered nodata when the layer is drawn on the globe. This colour can be either displayed or discarded according to the value of the `nodata_handling` field of [[RasterSampling]].
* **Picking**
    * The fields `imagery`, `dtm`, and `scalar` of [[RasterPickResult]] have been replaced with `color` and `number`. Depending on the type of each raster that has been picked, the corresponding field gets filled. The image format of the raster is given explicitly in the `image_format` field.
        * `ImageryPickResult`, `DtmPickResult`, and `ScalarPickResult` have been removed.
    * The `type` field of [[RasterPickResult]] has been removed, as well as the [[RasterType]] enumeration. Instead colour and scalar imagery rasters can be distinguished with the `image_format` field and the type of the result. Imagery rasters and DTM rasters can be distinguished with the contents of the value in the `layer` field of [[PickLayerResult]].

# Fixed

* Fixed nodata handling for RGBA rasters, when the alpha channel value of the nodata value is not 255.

# Upgrade notes

* **Rasters**
    * Any nodata value that was set on a palettised raster should now be set on its scalar value child provider. If relying on the `nodata_handling` value of [[RasterSampling]] is desired, use the `nodata_color` field of [[PalettizedImageRasterProviderParams]] to choose how to palettise nodata values.
