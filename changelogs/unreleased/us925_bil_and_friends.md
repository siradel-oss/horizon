# Added

* Added the `mime_type_override` field to most raster providers to override the MIME type of the data. This can be used to specify decoding instructions for the downloaded data.
* Added support for decoding images using the `image/x.raw` MIME type. See [the documentation](raster_image_formats.html#raw-formats) for more details.
* Added support for the [BIL, BIP, and BSQ](raster_image_formats.html#bil-bip-and-bsq) image formats through the `image/x.raw` MIME type.
* Added support for the `IGNF:WGS84G` SRID.

# Changes

* For WMS and WMTS raster providers, when a desired image format is specified, this now always chosen if available in the source, even if it is not supported by Horizon.

# Fixed

* Fixed support for geographic coordinate systems in WMTS raster layers.
