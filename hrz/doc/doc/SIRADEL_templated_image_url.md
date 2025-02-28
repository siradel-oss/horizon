---
Title: SIRADEL_templated_image_url
Category: glTF extensions
---

This glTF extension adds a way for the URL of images to be templated. Instead of using URLs, `images` entries can define a set of key-value we call parameters, associated with the name of a template. At runtime, the loader uses a set of user-defined templated URLs identified by a name, with the parameters of images in the glTF files, to create new image URLs. This mechanism allows for applying images on the model that have been produced after the glTF model, without having to modify the latter.

## Properties

The extension object appears in the glTF asset on [`Image`](https://www.khronos.org/registry/glTF/specs/2.0/glTF-2.0.html#reference-image) objects. When this extension is used on an image, `uri` and `bufferView` should not be defined. Its properties are:

### `templateName` (required)

Name of the templated URL provided at runtime where the parameters will be replaced.

### `parameters` (optional)

Key-value map of the template parameters values. The values must be strings.

## Example

```json
{
    "images": [
        {
            "extensions": {
                "SIRADEL_templated_image_url": {
                    "templateName": "imageryEndpoint",
                    "parameters": {
                        "source": "bing",
                        "imageId": "4951"
                    }
                }
            }
        },
        {
            "extensions": {
                "SIRADEL_templated_image_url": {
                    "templateName": "computationResults",
                    "parameters": {
                        "imageId": "4951"
                    }
                }
            }
        }
    ]
}
```

In the above example, the runtime may provide the following templates:

- `imageryEndpoint`: `http://redacted.localhost/3dtiles/mytileset/{source}/{imageId}.jpg`
- `computationResults`: `http://redacted.localhost/results/4a8ef31b3c/{imageId}.png`

The images above would then point to:

- `http://redacted.localhost/3dtiles/mytileset/bing/4951.jpg`
- `http://redacted.localhost/results/4a8ef31b3c/4951.png`
