+++
title = "Picking"
+++

# Picking

In Horizon picking is asynchronous and comes in three flavors. The picked coordinates are always given in pixels relative to the canvas, and the origin is the top-left corner.

## Picking

This is the most traditional version of picking. It is requested at a given point through the [PickScreen](reference/HrzProtocol.ViewerService) method and results are returned asynchronously through the message queue in a [`pick`](reference/HrzProtocol.PickResultMessage) message. The result message can be identified using the ticket returned by the `PickScreen` method.

The results contain whether anything was picked or not, what layer was picked (with the geodetic coordinates and whatever information was available for the given feature/object), and information about the rasters at the same position vertically. The `position` field of [`PickResults`](reference/HrzProtocol.PickResults) is unset if nothing was picked.

Raster layers (both imagery and DTM) are excluded from the picking process by default, which means that Horizon will not check any of them. To request some of them to be checked, their handles must be passed to the `PickScreen` function, through the `includedRasters` property of [`PickRequest`](reference/HrzProtocol.PickRequest).

> [!note] Raster picking
> The value returned for rasters when picking is retrieved on a best effort basis: it uses cached data and can thus be inaccurate or missing. This also means that the caching strategy of each raster provider affects the probability of receiving an accurate value. For example, disabling the cache of a raster provider means there will never be a value returned on picking. A special API is available to safely fetch data from rasters, which doesn't rely on cache: see the [Raster data fetch](picking.html#raster-data-fetch) section.

{{< gallery-card "sceneEditor" >}}

## Mouse hover info

This is a lighter version of picking that returns only basic information at regular intervals through the message queue in a [`mouse_hover_info`](reference/HrzProtocol.MouseHoverInfoMessage) message, without having to schedule individual picking requests. It is enabled using the [ConfigureMouseHover](reference/HrzProtocol.ViewerService) method. Its refresh rate can be configured in the [MouseHoverConfiguration]($proto) message.

It only returns the layer handle and possibly feature identifier of whatever is under the mouse.

> [!note]
> Mouse hover info should be the preferred way of receiving information about whatever is under the mouse at regular intervals. Using traditional picking for this is discouraged because it must return a lot more information, and retrieving this information can take longer, while the mouse hover system retrieves only basic information (that the application can still act upon) and doesn't require a round-trip through the application.

For highlighting the object under the mouse, see the [mouse hover highlighting](selection.html#mouse-hover-highlighting) system.

{{< gallery-card "csvData" >}}

## Area picking

This flavor of picking returns the list of all features **visible on screen** in a given rectangle. Just like the mouse hover info flavor, it only returns very basic information. It is scheduled with the [PickScreenArea](reference/HrzProtocol.ViewerService) method and the results are returned through the message queue in a [`pick_area`](reference/HrzProtocol.PickAreaResultMessage) message. The result message can be identified using the ticket returned by the scheduling method.

## Raster data fetch

The [FetchRasterData](reference/HrzProtocol.ViewerService.html#method-FetchRasterData) method allows the retrieval of precise raster data, using a [GeographicPosition]($proto) (instead of a screen position in pixels) and a list of layer handles to examine as input. It will **always** return data from the highest LOD tile of a tiled raster's tileset.

This method has the advantage of being able to retrieve the data from a raster in the absence of a cache, while `PickScreen` cannot. On the other hand, the result will be returned more slowly than `PickScreen`, as the relevant raster tile likely needs to be loaded in. As such, consider using `PickScreen` instead if you don't need the most accurate data, and if you are using a tile cache for the relevant raster providers.

Just like the other picking methods, the method returns a ticket, while the results will be returned later in a [RasterDataFetchMessage]($proto). But remember that the delay between the method call and the result message should be expected to be larger than when using the other picking methods.

{{< gallery-card "palettizedRaster" >}}

## Terrain elevation query

The [GetTerrainElevation](reference/HrzProtocol.ViewerService.html#method-GetTerrainElevation) method allows a fast retrieval of terrain elevation at any given [GeographicPosition]($proto). The response is immediately available in the return message of the call.

Contrarily to [FetchRasterData](reference/HrzProtocol.ViewerService.html#method-FetchRasterData), this method does not download new data and only uses what is present in the tile cache. This brings a few limitations:

* Querying a location whose terrain data has not been downloaded before (through regular visualisation, or calls to [FetchRasterData](reference/HrzProtocol.ViewerService.html#method-FetchRasterData)) does not return any value.
* If the cache is small, the likelihood of getting a return value is low.
* Only the top-most visible DTM layer is taken into account.
* The returned value may not be the most accurate, depending on the zoom level of the most detailed loaded tile at the location. Querying the same location multiple times may not always return the same exact value, as new tiles are loaded and unloaded.

Use this method to obtain rough elevation values in the vicinity of the camera rapidly or at a frequent rate (up to once every frame). For example to place an object in relation to the terrain.
