+++
title = "SIRADEL_range_request"
+++

# SIRADEL_range_request

## Dependencies

Written against the 3D Tiles 1.0 and 1.1 specifications.

This extension cannot be used in conjunction with [Implicit Tiling](https://github.com/CesiumGS/3d-tiles/tree/main/specification/ImplicitTiling) for 3DTiles 1.1, nor the [3DTILES_implicit_tiling](https://github.com/CesiumGS/3d-tiles/tree/main/extensions/3DTILES_implicit_tiling) extension for 3DTiles 1.0.

## Overview

3D Tiles datasets can sometimes comprise a large number of files, which can slow down file systems and increase data transfer times, especially when copying a whole dataset.
This extension allows packing multiple contents into a single file, by adding into the tileset JSON the information needed to know where each content’s data is located in this file. The information provided can be used to make HTTP range requests when in a web context, allowing a simple file server to serve individual contents without any specific support required.

When using this extension, a full 3D Tiles dataset is composed of at least the root tileset JSON, as well as one data file. It may contain multiple data files if needed.

If a dataset is split into multiple tilesets, using external tileset contents, the child tileset JSON files can be included in the data file (at the expense of losing the ability to load them individually).

## Optional vs. Required

This extension is required, meaning it **MUST** be placed in the `extensionsRequired` and `extensionsUsed` lists.

## Extension JSON

The `SIRADEL_range_request` extension may be defined on any `content` object in the tileset JSON. When the extension is present, the client should not read the whole file referenced by the `uri` property, but only part of it, as described by the extension properties. If the content is fetched through HTTP, the `range` request header should be used.

If the `mimeType` field is defined, it takes priority over the `Content-Type` HTTP header served along with the data, if present. This can be useful when multiple content types are served through the same file URL, and thus neither the `Content-Type` header, nor the file extension, can be used to reliably define the content type.

| Property | Description |
| ------ | ----------- |
| `offset` | The offset, in bytes, at which that tile content starts in the file referenced by the `uri` property (required) |
| `length` | The size of the content, in bytes (required) |
| `mimeType` | MIME type of the content (optional) |


## Example

```json
{
  "asset": {
    "version": "1.0"
  },
  "geometricError": 1000000,
  "extensionsUsed": [
    "SIRADEL_range_request"
  ],
  "extensionsRequired": [
    "SIRADEL_range_request"
  ],
  "root": {
    ...
    "children": [
      {
        ...
        "content": {
          "uri": "tileset.data",
          "extensions": {
            "SIRADEL_range_request": {
              "offset": 123456,
              "length": 56190,
              "mimeType": "model/gltf-binary"
            }
          }
        }
      }
    ]
  }
}
```
