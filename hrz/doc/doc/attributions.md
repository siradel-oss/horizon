+++
title = "Attributions"
+++

# Attributions

Some data providers require displaying attribution text or logos next to the Horizon canvas. To help in doing so, Horizon provides a mechanism to periodically return a list of attributions that corresponds (on a best effort basis) to what is displayed on screen. It is then up to the integration to display those attributions properly.

## Enabling attributions feedback

In order to enable attributions feedback, call the `SetAttributionEnabled` method of [ViewerService]($proto). When enabled, the engine will periodically send an [AttributionsMessage]($proto) listing the attributions to display through the [message queue](message_queue.html).

## Displaying attributions

Each [Attribution]($proto) may contain some text (which itself might be HTML), and a logo URL. All those elements must be displayed. Please refer to your data providers to check for guidelines.

> [!note] Images & CORS
> Some data providers may require displaying a logo, but this logo might be misconfigured and not displayable in a secure context due to CORS headers. A possible strategy if you know what resources are misconfigured is to hardcode a replacement for this resource. [A notable bad actor is Bing](https://github.com/CesiumGS/cesium/blob/1974ff43158efb0e1e3f554a80c22881e52c4401/packages/engine/Source/Scene/BingMapsImageryProvider.js#L673).

## Attribution sources

Some data provider types have their attributions encoded in the data source, others do not, but it might still be necessary to include an attribution for them. So all data providers have an `attribution` field to set (or add, if it's already defined by the data itself) some user-defined attribution, if it is known by the application. Usually this field is as close as possible to where the source is defined. For example it is present on raster providers, vector data sources, etc.
