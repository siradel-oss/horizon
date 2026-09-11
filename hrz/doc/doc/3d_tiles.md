+++
title = "3D Tiles"
+++

# 3D Tiles

[3D Tiles](https://github.com/CesiumGS/3d-tiles) allow encoding large 3D datasets in a way that makes them easily streamed over a network, such as the Internet. They are typically used to display buildings and can cover whole cities or even regions.

3D Tiles tilesets can contain tiles of several types:

* [Batched 3D models](https://docs.ogc.org/cs/18-053r2/18-053r2.html#130) (`b3dm`) tiles,
* [Instanced 3D models](https://docs.ogc.org/cs/18-053r2/18-053r2.html#155) (`i3dm`) tiles,
* [Point clouds](https://docs.ogc.org/cs/18-053r2/18-053r2.html#199) (`pnts`) tiles,
* [Composite](https://docs.ogc.org/cs/18-053r2/18-053r2.html#249) (`cmpt`) tiles.

`b3dm`, `i3dm`, and (usually) `cmpt` tiles contain glTF models. The [3D models documentation](single_model.html) gives specific details about the supported variants.

Each tileset comes with a scalar value governing how tiles are refined as the camera gets closer to them. This property is named `max_screen_space_error` and must be provided along with the tileset URL. (It is possible however to make the value slightly smaller or larger to adjust the refinement.)

To make the scene a bit more stable while the camera moves, it is possible to add some hysteresis to the refinement and unrefinement of the tiles, through the `refinement_hysteresis` property. A value of `0` means no hysteresis: the tiles will be loaded and unloaded exactly when crossing the max screen-space error threshold. A higher value delays loading and unloading the tiles by adding a “dead zone” around the max screen-space error threshold.. Typical values range between `0.1` and `0.5`.

It is possible to set a linear transformation, which is applied to every tile, to the layer. But it is not needed for most tilesets, so usually the identity should be used.

3D Tiles can be styled, see [here](styling_3d_tiles.html) for details.

{{< gallery-card "threeDTiles" >}}

{{< gallery-card "threeDTilesVectorOverlay" >}}

{{< gallery-card "threeDTilesStyling" >}}

## Supported extensions

**[`3DTILES_content_gltf`](https://github.com/CesiumGS/3d-tiles/tree/main/extensions/3DTILES_content_gltf)**: makes it possible to define a tile's content using a `glTF` or `GLB` file. This extension isn't required for `i3dm` tiles which natively supports embedded or external glTFs.

**[`SIRADEL_range_request`](SIRADEL_range_request.html)**: to serve 3D Tiles datasets using HTTP range requests, thus reducing the number of files on servers.
