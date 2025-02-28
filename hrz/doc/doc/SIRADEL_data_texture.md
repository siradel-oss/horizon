---
Title: SIRADEL_data_texture
Category: glTF extensions
---

For visualisation purposes we need to be able to render textures that contain data values, i.e. scalar numbers, on 3D models. glTF models that have a material using a data texture must be able to declare it. Therefore, we need an glTF extension to declare the use of such materials so that data can be rendered appropriately at runtime with a palette specified by the user. We propose a glTF extension with the name `SIRADEL_data_texture` that defines a new type of material.

As specified by the glTF specifications the extension **MUST** appear in the top level object in the `extensionsUsed` array object. If a viewer does not support the extension, it can fall back to using the [default material](https://www.khronos.org/registry/glTF/specs/2.0/glTF-2.0.html#default-material) instead.

## Properties

In the glTFs, the extension can appear in a `material` object in the list of materials. When the extension is present, other materials like `pbrMetallicRoughness` should not appear. The extension then describes the information for the data texture. The extension `SIRADEL_data_texture` contains the following information:

### `dataTexture` (required)

A [`Texture Info`](https://www.khronos.org/registry/glTF/specs/2.0/glTF-2.0.html#reference-textureinfo) object pointing to a data texture. It **MUST** use a default sampler with the following properties:

- No mipmaps must be generated.
- Magnification and minification parameters must be set to `NEAREST` mode.

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
    ]
}
```
