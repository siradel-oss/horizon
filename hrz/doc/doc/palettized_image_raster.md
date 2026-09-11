+++
title = "Palettized image raster"
+++

# Palettized image raster

[Palettized image rasters](reference/HrzProtocol.PalettizedRasterProviderParams) are displayed on the planet according to their internal provider and [numeric palette](numeric_palettes.html). The internal provider retrieves image pixels values as numbers then applies the given palette to colorize these values. The internal provider’s [image format](raster_image_formats.html) must be one that contains scalar values (and not colours).

The internal provider can define a nodata value. The `nodata_color` property of [PalettizedRasterProviderParams]($proto) enables mapping pixels with this nodata value to a given color without having to know the specific value in advance.

In turn the palettized provider can define its own nodata value with the `nodata` property. This nodata is handled after the colorization, and hence is based on RGBA values. This allows treating one or multiple specific floating point values or ranges as nodata, and mapping these values to a specific color. Then the nodata handling parameter of the raster layer can be toggled to show or hide the nodata pixels.

When picking on a palettized raster, the results contain the original values from the internal provider, i.e. numbers. If the value is nodata (as specified by the internal provider), the `nodata` property of [RasterPickResult]($proto) is set to `true`.

{{< gallery-card "palettizedRaster" >}}

{{< gallery-card "palettizedTerrain" >}}
