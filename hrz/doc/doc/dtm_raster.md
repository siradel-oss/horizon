---
Title: DTM rasters
Category: Rasters
---

A DTM raster layer is generally similar to an imagery raster layer. The differences are the layer type (`DTM_RASTER`), the absence of groups, and the image format.

There is also no alpha channel in DTM rasters (and blending options have no effect). However there may be nodata pixels. In order to obtain the desired composite DTM, the values `DISCARD_NODATA_PIXELS` and `SET_NODATA_TO_ZERO` of the enumeration `NodataHandling` can be useful.

Multiple DTM layers can used simultaneously. By default, the layer with at the highest slot takes precedence. Nodata values and handling allow falling back to the elevation value of a layer at a lower slot if a higher layer does not have one.

There are two supported source types for elevation data: heightmap images and [Cesium terrain tiles](raster_providers.html#cesium-terrain).

<gallery-card demo="dtmLod1"></gallery-card>

<gallery-card demo="ignSrtm"></gallery-card>

## Heightmap image tiles

These tiles are regular images, but instead of each pixel having a colour, they contain elevation values. All [raster image formats](raster_image_formats.html) containing scalar values are supported.

Heightmap tiles can be retrieved by any raster provider that supports specifying the image format, such as the [tiled image provider](tiled_raster_provider.html), or the [single image provider](raster_providers.html#single-image).

## Cesium terrain tiles

Horizon can also load terrain tiles that follow the format specification for Cesium. They can be retrieved by using the [Cesium terrain tiles raster provider](raster_providers.html#cesium-terrain). The parameter URL must point to the tileset directory, which contains the `layer.json` descriptor file.

The provider sets up the geometry automatically, according to the descriptor.
