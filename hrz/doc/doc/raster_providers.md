---
Title: Raster data providers
Category: Rasters
---

In Horizon, rasters come in two flavors, represented by two different layer types:

- [Imagery rasters](HrzProtocol.ImageryRasterLayer.html) for displaying aerial imagery, base maps, computation results, etc.
- [DTM rasters](HrzProtocol.DtmRasterLayer.html) for providing a heightmap to the terrain.

Each layer must specify what kind of data it uses, where it can be retrieved, and how it should be handled by the engine so that it can be displayed. This is configured through an object named a provider. Below is the exhaustive list of raster providers.

## Tile cache

Raster providers can keep a configurable number of tiles cached in memory, whether they’re currently visible on screen or not.

The field `raster_provider_tile_cache_size` of [[ViewerOptions]] determines the default number of tiles that are kept in memory for all providers. This value can be overridden for each provider individually, through the `tile_cache_size` and `override_tile_cache_size` fields of the provider’s configuration.

The larger the cache, the fewer tiles are redownloaded when moving the camera around. But this is at the expense of increased memory consumption: expect 256 KiB of memory used per cached tile.

When an HTTP cache is available (through a web browser or using Horizon's native HTTP cache implementation), it can be beneficial to use that cache instead of the providers' caches, especially since the memory budget in Web Assembly can be tight. This is at the added cost of having to re-decode the tile data when it is needed again. Note that some browsers might not correctly cache range requests, which are used for instance for PMTiles data sources.

!!! warning "Interaction with picking"
    The picking system queries the cached raster tiles’ data to retrieve values. Thus using a cache size of 0 on a provider will disable picking for its layer. A cache size of 150 is a good default value for maximizing the probability of picked tiles being in cache.

    To fetch a raster's value at a given point, even when caching is disabled, the [ViewerService.FetchRasterData](HrzProtocol.ViewerService.html#method-FetchRasterData) method can be used.

## Missing tile policy

When a tile could not be loaded (for example on a 404 not found HTTP error), the engine must decide what to do. This is the purpose of the [missing tile policy](HrzProtocol.MissingTilePolicy.html) setting present on all raster data providers.

A tile of lower resolution can be used if available, or an empty tile can be displayed instead.

## Raster geometry

Most rasters have accompanying descriptors that indicate how their data is projected as well as their extent. But some rasters do not, for example in the case a simple tree of tile files. Providers that allow loading and displaying these rasters include a [[RasterGeometry]] field, in order to provide projection and bounds.

The projection can be defined either as a [proj-string](https://proj.org/en/9.4/usage/quickstart.html) or as an SRID, such as `EPSG:3857`. See the [list of supported SRIDs](crs_database.html).

Additionally, when the chosen projection is not `EPSG:4326` (WGS84 lat-long) or `EPSG:3857` (Web mercator) and the chosen tiling scheme is global (when using a provider requiring a tiling scheme, such as [the tiled provider](HrzProtocol.TiledImageRasterProviderParams.html)), the bounds of the projection domain must be provided. These bounds can be retrieved from the [epsg.io](https://epsg.io) website. For example, the valid bounds of RGF93 v1/Lambert-93 ([`EPSG:2154`](https://epsg.io/2154)) are `xmin=-378305.81, ymin=6005281.2, xmax=1320649.57 ymax=7235612.72`.

## Raster data providers

### ArcGIS

[ArcGIS Map Service](https://developers.arcgis.com/rest/services-reference/enterprise/map-service.htm) is a specification for serving raster maps requested with bounds or, when they are pre-cached, through tiled raster coordinates over the Web.

Through the [ArcGIS raster provider](HrzProtocol.ArcGisProviderParams.html), it is possible to select which layers are displayed by giving their IDs, which are shown on the ArcGIS Map Service’s web page. If no IDs are given, all the layers are displayed.

The same layer can be available under multiple image formats, such as JPEG or PNG. By default the format is selected by finding the best compromise between quality and weight. If the user wishes to use a specific file format, they can specify it explicitly in the configuration.

!!! note ""
    The support of the pre-cached tile is only available for both EPSG:3857 and EPSG:4326 projection system. If the raster is projection in another system, it will be requested through the default way which will be slower.

<gallery-card demo="arcGisRaster"></gallery-card>

### Bing

Bing imagery rasters have [a dedicated provider](HrzProtocol.BingProviderParams.html). It allows selecting between multiple [map variants](HrzProtocol.BingProviderImageryType.html) in several languages. See the [official documentation](https://docs.microsoft.com/en-us/bingmaps/rest-services/common-parameters-and-types/culture-parameter) for more details.

A Bing API key must be provided.


### Cesium terrain

Horizon can also load terrain tiles that follow the format specification for Cesium. They can be retrieved by the [dedicated raster provider](HrzProtocol.CesiumTerrainProviderParams.html). The parameter URL must point to the tileset directory, which contains the `layer.json` descriptor file. Both types of terrain tiles are supported (`heightmap` and `quantized-mesh`, in version 1.0).

Cesium terrain tiles do not have explicit nodata values, but areas without actual data have a value of `0`. Horizon treats `0` as nodata. This can be used to combine a Cesium terrain tile layer with other DEM rasters.

### PMTiles

> [PMTiles](https://github.com/protomaps/PMTiles) is a single-file archive format for tiled data. A PMTiles archive can be hosted on a commodity storage platform such as S3, and enables low-cost, zero-maintenance map applications that are "serverless"—free of a custom tile backend or third party provider.

Horizon supports version 3 through the [PMTiles raster provider](HrzProtocol.PmTilesProviderParams.html).

<gallery-card demo="pmTilesRaster"></gallery-card>

<gallery-card demo="dtmLod1"></gallery-card>

### Single image

Not all rasters are tiled, some are stored as a single image, and they too can be displayed. They are entirely loaded in memory, then displayed on the planet according to their projection and bounds, set in the [[SingleImageRasterProviderParams]].

Do not load too large single image rasters, as you may run out of memory.

<gallery-card demo="singleImageRaster"></gallery-card>

### Tiled image

See the [dedicated documentation page](tiled_raster_provider.html).

<gallery-card demo="tiledImageRaster"></gallery-card>

### TileJSON

[TileJSON](https://github.com/mapbox/tilejson-spec) is a specification for serving tiled data over the web. It can be used for either vector or raster data, this is the documentation for loading raster data. Horizon supports TileJSON versions 1.0.0 through 3.0.0.

The entry point to a TileJSON service is a URL pointing to a JSON document describing the layer data on the server.

Horizon can display TileJSON raster layers by using a [dedicated raster provider](HrzProtocol.TileJsonProviderParams.html). The parameters that are specific to this provider are:

* The URL to the TileJSON document,
* An image file format.

!!! note ""
    TileJSON allows identifying features in raster data through the use of [UTFGrid](https://github.com/mapbox/utfgrid-spec). However this functionality is not supported by Horizon.

<gallery-card demo="tileJsonRaster"></gallery-card>

### Tile Map Service (TMS)

[Tile Map Service](https://wiki.osgeo.org/wiki/Tile_Map_Service_Specification) (TMS) is a specification for serving tiled raster maps over the web. Multiple XML documents, describing the data, can be requested:

* The root resource,
* The TileMapService resource,
* The TileMap resource.

Only the last one describes a raster dataset, and is the one used by Horizon in the [TMS raster provider](HrzProtocol.TmsProviderParams.html). It is usually named `tilemapresource.xml`. That file describes where the tiles can be fetched, and how their grid is laid out and projected.

!!! note ""
    The [gdal2tiles](https://github.com/OSGeo/gdal/blob/master/gdal/swig/python/scripts/gdal2tiles.py) tool can generate `tilemapresource.xml` files when it is used to tile a raster. The files it generates are not always spec conforming, but they should be usable most of the time.

TileMap resources can follow a subset of specifications, named profiles. Currently, only tilesets adhering to one the following profiles are supported:

 * `global-mercator` (sometimes named `mercator`),
 * `global-geodetic` (sometimes named `geodetic`),
 * `raster`.

!!! note ""
    When tiling an image with [gdal2tiles](https://gdal.org/en/stable/programs/gdal2tiles.html) in the `geodetic` profile, the `--tmscompatible` option must be used. Without this option, the tile set produced by gdal2tiles does not conform to the specifications of the profile.

!!! note ""
    The `raster` profile is specific to gdal2tiles. To be usable with Horizon, the projection string inside the `tilemapresource.xml` file, originally in WKT, must be replaced by its PROJ.4 equivalent. (By fetching the string from [epsg.io](https://epsg.io/) or [converting it manually](http://cfconventions.org/wkt-proj-4.html).)

<gallery-card demo="tmsRaster"></gallery-card>

### WMS

[Web Map Service](https://www.ogc.org/standards/wms) (WMS) is a specification for serving raster maps over the web. Horizon supports WMS versions 1.1.0, 1.1.1, and 1.3.0, through the [WMS raster provider](HrzProtocol.WmsProviderParams.html).

The entry point to a WMS service is the base URL of a web service, that can be queried to retrieve information on the rasters it can serve, or to get raster extracts, in the form of images. WMS URLs may include query parameters, such as `service=WMS&version=1.3.0&request=GetCapabilities`, but this is not required, as they are automatically added when not present. (The requested protocol version is 1.3.0 unless the parameter is included in the URL, and set to another version.)

Multiple layers can be available on a single WMS endpoint. WMS supports requesting one or more layers in a single request. When multiple layers are requested, they are composed by the server onto a single image, in the order they have been requested. (They still constitute a single raster layer from Horizon’s point of view.) Each layer can be served under multiple styles, and the desired style can be indicated in the configuration. If the style is left blank, the default style for the layer is used.

Some WMS layers lack data in some geographical areas. They are usually represented by fully transparent pixels. It is possible to indicate that opaque images are desired. In this case, the background colour (used where there is no data) can be configured.

Some WMS layers are multi-dimensional: they have other dimensions on top of the geographic coordinates. They can be time, elevation, additional wavelengths, etc. They can be configured by setting the dimension name and its associated value in the provider parameters.

The same layer can be available under multiple image formats, such as JPEG or PNG. By default the format is selected by finding the best compromise between quality and weight. If the user wishes to use a specific file format, they can specify it explicitly in the configuration.

!!! note ""
    Not all projections used by WMS servers are supported by Horizon, so it may happen that a valid WMS layer cannot be displayed.

WMS rasters can be requested at any scale. Many servers however have limits on how big or small the scale can be. These limits are used internally to derive minimum and maximum zoom levels for the raster layer. Some servers return scale limits that are overly restrictive, and are actually capable of serving a wider scale range. In that case, the API offers a way to override the automatically computed zoom levels.

!!! note ""
    When a WMS raster is also available as WMTS, TMS, or ArcGIS map service, you should use one of these protocols instead, as they are much faster.

<gallery-card demo="wmsRaster"></gallery-card>

### WMTS

[Web Map Tile Service](https://www.ogc.org/standards/wmts) (WMTS) is a specification for serving tiled raster maps over the web. Horizon supports WMTS version 1.0.0 through the [WMTS raster provider](HrzProtocol.WmtsProviderParams.html).

The entry point to a WMTS service is a URL pointing to an XML document describing what a given server can provide, named the `Capabilities` resource. WMTS capabilities URLs generally have one of two forms:

* `{BaseURL}?SERVICE=WMTS&REQUEST=GetCapabilities` or,
* `{BaseURL}/1.0.0/WMTSCapabilities.xml`.

Multiple layers can be available on a single WMTS endpoint. Each layer can have multiple styles, among which one is the default style. A given layer can be projected and tiled according to multiple spatial reference systems.

The parameters that are specific to the WMTS provider are:

* The URL to the capabilities XML document,
* A layer identifier (optional),
* A style identifier (optional),
* An image file format (optional).

When no layer identifier has been given, the first layer in the document is used. When no style identifier has been given, the default style for the layer is used. If no style is declared as default, the first one is used instead.

The same layer can be available under multiple image formats, such as JPEG or PNG. By default the format is selected by finding the best compromise between quality and weight. If the user wishes to use a specific file format, they can specify it explicitly in the configuration.

!!! note ""
    The WMTS specification includes multiples methods for requesting tiles. RESTful and KVP methods are supported, but SOAP isn’t.

!!! note ""
    Not all projections used by WMTS servers are supported by Horizon, so it may happen that a valid WMTS layer cannot be displayed.

<gallery-card demo="wmtsRaster"></gallery-card>
