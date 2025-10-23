# Added

* The background colour of the loading screen can be configured with the property `loading_screen_background_color` of [[ViewerOptions]].

# Changed

* Colour blending is now done in linear-sRGB space.
* Flat polyline and cylinder colours are now interpolated in the Oklab colour space.
* Values `PERCEPTUAL_OKLAB` and `LINEAR_RGB` of [[ColorInterpolationMode]] have been renamed `OKLAB` and `LINEAR_SRGB` respectively. (Bringing them closer to their CSS equivalents.)
* Value `SRGB_R_8` of [[ImageFormat]] has been renamed `R8`.

# Fixed

* Fixed ASTC compressed texture format support detection.

# Upgrade notes

* Representations using transparent colours can appear slightly different from before. Colour calculations have been corrected, but representations made with incorrect colour calculations in mind may need some tweaking to conform to the intended appearance. In particular, image files, used for example for symbols, when they contain partially transparent pixels, should be authored using image editing tools that work with linear colours.
* Uses of the values `PERCEPTUAL_OKLAB` and `LINEAR_RGB` of [[ColorInterpolationMode]] must be replaced with `OKLAB` and `LINEAR_SRGB` respectively.
* Uses of the value `SRGB_R_8` of [[ImageFormat]] must be replaced with `R8`.
