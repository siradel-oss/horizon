+++
title = "Single 3D model"
+++

# Single 3D model

A 3D model (in the [`glTF`](https://www.khronos.org/gltf/)) or `glb` format, can be loaded and displayed through the [single model layer](reference/HrzProtocol.SingleModelLayer). The model can be placed on the surface of the planet by giving it longitude, latitude, and altitude values. The model can also be offset from its origin, scaled and rotated.

Because there are competing conventions for which axis is up, and how the axes are oriented one to another, the `frame` property structure is needed in order to have the model be properly oriented.

Some glTF extensions are supported:

* [`KHR_draco_mesh_compression`](https://github.com/KhronosGroup/glTF/blob/master/extensions/2.0/Khronos/KHR_draco_mesh_compression/README.md) for meshes that are compressed using the [Draco](https://github.com/google/draco) library,
* [`EXT_texture_webp`](https://github.com/KhronosGroup/glTF/blob/master/extensions/2.0/Vendor/EXT_texture_webp/README.md) for texture files encoded in the [WebP](https://developers.google.com/speed/webp) format,
* [`KHR_texture_basisu`](https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_texture_basisu/README.md) for texture files encoded in the [KTX 2.0](https://registry.khronos.org/KTX/specs/2.0/ktxspec.v2.html) format with [Basis Universal](https://github.com/BinomialLLC/basis_universal) supercompressed data,
* [`KHR_materials_variants`](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_materials_variants) for defining multiple materials in a glTF asset, and switching between them at runtime,
* [`KHR_materials_unlit`](https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_materials_unlit/README.md) for disabling the shading of materials in a glTF asset.
* [`SIRADEL_templated_image_url`](SIRADEL_templated_image_url.html) for referencing textures produced after the glTF asset has been produced using templated URLs,
* [`SIRADEL_data_texture`](SIRADEL_data_texture.html) for displaying textures containing arbitrary scalar data using a user-defined palette.
* [`CESIUM_RTC`](https://github.com/KhronosGroup/glTF/blob/main/extensions/1.0/Vendor/CESIUM_RTC/README.md) for placing the model at specific [ECEF](https://en.wikipedia.org/wiki/Earth-centered,_Earth-fixed_coordinate_system) coordinates. Note that this extension should not be used in new datasets and is only supported for compatibility. Instead:
    * In 3D Tiles [Batched 3D Model files](https://github.com/CesiumGS/3d-tiles/tree/main/specification/TileFormats/Batched3DModel#coordinate-system), use the `RTC_CENTER` value.
    * For glTF files, use [a root transform](https://github.com/CesiumGS/3d-tiles/blob/main/specification/TileFormats/glTF/MIGRATION.adoc#batched-3d-model-b3dm).

{{< gallery-card "sceneEditor" >}}

## Animations

A glTF file can contain animations. Horizon can play `translation`, `rotation`, and `scale` animations (but not skeletal or morph animations). Moreover, those files can contain multiple animations. Horizon can play all of them at once, or only a subset selected through the [scene model](reference/HrzProtocol.ModelAnimation). Note that some animations are incompatible with one another, Horizon does not attempt to detect incompatible animations, and it is up to the integrating application to properly select which animations should play.

See the documentation for the [`animation`](reference/HrzProtocol.ModelAnimation) field of [SingleModelLayer]($proto).

{{< gallery-card "modelAnimations" >}}
