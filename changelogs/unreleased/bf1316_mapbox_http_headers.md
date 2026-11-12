# Changed

* The [Mapbox service](reference/HrzProtocol.MapboxService.md) now applies the
  `http_headers` of [MapboxTranslationParams]($proto) to every external resource
  of the translated style: the sprite image, the fonts, and the data of
  `raster`, `vector` and `geojson` sources. Previously, they were only applied
  to the sprite index document.
