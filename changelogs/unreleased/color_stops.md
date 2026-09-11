# Changed

* The protocol for palette definitions has been updated for better alignment with other tools and technologies, such as [the Web platform](https://developer.mozilla.org/en-US/docs/Web/SVG/Reference/Element/stop). `ColorPoint` has been renamed [ColorStop]($proto) and the property `color_points` of [NumericPalette]($proto) has been renamed `color_stops`.

# Upgrade notes

* Rename every mention of `ColorPoint` to [ColorStop]($proto), and every mention of `color_points` of [NumericPalette]($proto) to `color_stops`.
