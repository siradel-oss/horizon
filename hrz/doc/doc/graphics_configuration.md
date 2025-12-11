---
Title: Graphics configuration
Category: General
---

At initialization, a [[GraphicsLevel]] can be set in the [[ViewerOptions]]. These levels are performance profiles going from low to high: low provides the best performance and high the best graphical quality. The `Auto` level tries to select the best level for the detected platform.

The results of the graphics selection process can be retrieved using the `GetConfiguration` method of [[ViewerService]], which also returns information about the available features and the graphics settings used by the engine. Information about the plaform detection is also displayed in the logs during the initialization.

## Level–feature matrix

| Feature              |  Low  | Medium | High  |
| -------------------- | :---: | :----: | :---: |
| Shadows              |   ❌   |   ❌    |   ✅   |
| Simulated atmosphere |   ❌   |   ✅    |   ✅   |
| High-quality UI[^1]  |   ❌   |   ✅    |   ✅   |

[^1]: This enables high-quality transparency for UI elements such as gizmos and grids.

Additionally, the quality of raster and flat overlays layers are affected by the graphics level.

## Level auto-selection rules

These are the rules applied when auto-selecting a graphics level, from highest to lowest priority. An empty cell means "any value".

|   OS    |      Runtime      |             GPU             | Selected level |
| :-----: | :---------------: | :-------------------------: | :------------: |
| Android |         -         |              -              |    **Low**     |
|   iOS   |         -         |              -              |    **Low**     |
|    -    | Web[^any_browser] |     Intel [HD] Graphics     |    **Low**     |
|    -    |         -         |          Intel Arc          |    **High**    |
|    -    |         -         |           Nvidia            |    **High**    |
|    -    |         -         | AMD Radeon HD/RX (discrete) |    **High**    |
|    -    |         -         |              -              |   **Medium**   |

[^any_browser]: Any browser

## Low video memory situations

When the quantity of video memory is low (as set by `max_video_memory_size` in [[ViewerOptions]]), certain layers or effects can be degraded automatically, regardless of the graphics level.

They comprise:

* Raster layers,
* Flat overlays,
* Shadows.

The quality is degraded if the available video memory is below 800 MiB, and further degraded if it is below 600 MiB. The settings affected by this degradation process can also be retrieved with the `GetConfiguration` method of [[ViewerService]].

## Overriding graphics settings

[[ViewerOptions]] has a `graphics_settings_overrides` property, which allows to force the use of a specific value for any of the settings affected by the graphics level. Doing so requires caution as they can drastically affect the quality and performance of the rendering: changing the graphics level used by the engine should be sufficient in most cases.
