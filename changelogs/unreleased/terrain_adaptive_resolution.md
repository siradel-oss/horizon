# Changed

* The property `experimental_adaptive_resolution` of [TerrainSettings]($proto) has been removed. Adaptive resolution is now enabled by default. It can be disabled by setting the property `disable_terrain_adaptive_resolution` of [ViewerOptions]($proto) to `true`.

# Upgrade notes

* Enabling terrain adaptive resolution in the terrain settings is no longer needed to benefit from its effects. You can still disable it by setting the property `disable_terrain_adaptive_resolution` of [ViewerOptions]($proto) if you encounter issues with the terrain geometry, such as spurious spikes.
