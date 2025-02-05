# Changed

* **Rasters**
    * The terrain’s staircase effect when the DTM is incompletely loaded is no longer present on devices that support the `OES_texture_float_linear` WebGL extension.
    * The `R_F32` and `R_F32_SILICIUM` image formats are now usable as DTM rasters.
    * The `SIGNED_FIXED_24_8` image format is now usable in the palettised raster provider.
    * The [[NodataValue]] structure now has a `type` property of type [[NodataValueType]], as well as dedicated properties for values of each type.
        * `COLOR_NODATA`, `UINT_VALUE_NODATA`, `INT_VALUE_NODATA`, and `FLOAT_VALUE_NODATA` allow matching a value in the source images, regardless of the actual encoding of the values in the images.
        * `NAN_NODATA` allow matching all [NaN](https://en.wikipedia.org/wiki/NaN) values in formats that support them (`R_F32` and `R_F32_SILICIUM`).
        * `BIT_PATTERN_NODATA` allows matching values according to their exact binary representation in the source images. The value given in `bit_pattern` is interpreted as a little-endian binary array, whose length matches the size of one pixel of the image format. This mode can be useful to match float numbers without worrying about the value being altered when going through multiple representations (such as JSON).

# Upgrade notes

* **Rasters**
    * For rasters that had their nodata value defined as the binary representation of the value, cast to a signed integer (typically DTM rasters), either cast the value as an unsigned integer and use a `BIT_PATTERN_NODATA` value, or use another mode and give the number value.
        * `SIGNED_FIXED_24_8` rasters with a nodata value of `-2147483648` should be converted to `INT_VALUE_NODATA` and a value of `-8388608`.
