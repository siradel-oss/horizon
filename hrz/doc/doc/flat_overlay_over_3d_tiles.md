+++
title = "Flat overlays over 3D Tiles"
+++

# Flat overlays over 3D Tiles

Sometimes 3D Tiles contain data that represents the terrain, with or without buildings, for example photomeshes. In this case, it can be useful to draw flat overlays (flat geometries and heatmaps) over the 3D Tiles. This can be used for example to classify parts of meshes that lack any kind of attributes.

This can be configured through the `draw_under_flat_overlays` property of [ThreeDTilesLayer]($proto). Setting the value to `true` enables drawing flat overlays on the tileset.

Both 3D Tiles and flat overlay layer features may be selected and highlighted, if they are identifiable (i.e. they have IDs). When flat overlays are drawn on top of 3D Tiles, they take priority for all picking operations. To select a 3D Tile feature, there must be no flat overlay feature drawn over it.

{{< gallery-card "threeDTilesVectorOverlay" >}}
