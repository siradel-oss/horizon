+++
title = "SIRADEL_data_texture"
+++

# SIRADEL_data_texture

For visualisation purposes we need to draw textures that contain data values, i.e. scalar numbers, on 3D models. glTF models that have a material using a data texture must be able to declare it. This glTF extension, named `SIRADEL_data_texture`, enables declaring the use of such materials so that data can be rendered appropriately at runtime with a palette specified by the user.

As specified by the glTF specifications the extension **MUST** appear in the top level object in the `extensionsUsed` array object. If a viewer does not support the extension, it can fall back to using the [default material](https://www.khronos.org/registry/glTF/specs/2.0/glTF-2.0.html#default-material) instead.

## Properties

### In `materials`

In glTF files, the extension can appear in a `material` object in the list of materials. When the extension is present, other materials like `pbrMetallicRoughness` should not appear. The extension then describes the information for the data texture. The extension `SIRADEL_data_texture` contains the following information:

#### `dataTexture` (required)

A [`Texture Info`](https://www.khronos.org/registry/glTF/specs/2.0/glTF-2.0.html#reference-textureinfo) object pointing to a data texture.

If a sampler is defined, it **MUST** have the following properties:

- Mipmaps are not used.
- Magnification and minification parameters must be set to `NEAREST` mode (i.e. `9728`).

If no sampler is defined, a default sampler with the same properties, as well as repeat wrapping (in both directions), is used.

Because they are not used, mipmaps should not be generated.

### In `textures`

Image formats supported in glTF models are designed to store colours and not scalar values. In order to be able to store scalar values in these formats, the data bits of each pixel must be interpretated in a non-traditional fashion.

The extension `SIRADEL_data_texture` describes how scalar values must be obtained from the data in the image:

#### `dataInterpretation` (required)

An string value telling how each pixel’s bits must be interpreted to obtain a scalar value.

The only allowed value is:

* `rgba8BitsToFloat`: Decode each channel as an 8-bit unsigned integer, in this order, and interpret the 32 bits as a little endian [IEEE 754 single-precision floating-point number](https://en.wikipedia.org/wiki/Single-precision_floating-point_format). (The red channel’s byte becomes the least significant byte and the alpha channel’s byte the most significant byte of the 32-bit value.)

## Example

```json
{
    "materials": [
        {
            "doubleSided": false,
            "name": "data_material_0",
            "extensions": {
                "SIRADEL_data_texture": {
                    "dataTexture": {
                        "index": 0,
                        "texCoord": 0
                    }
                }
            }
        }
    ],
    "textures": [
        {
            "source" : 0,
            "extensions": {
                "SIRADEL_data_texture": {
                    "dataInterpretation": "rgba8BitsToFloat"
                }
            }
        }
    ],
}
```

## Notes

Some image formats designed to store colours can use lossy compression methods to decrease file size. These methods assume they work on multi-channel colours and usually degrade scalar values beyond usability. To avoid this, data texture files should be stored in files with lossless formats, such as PNG.
