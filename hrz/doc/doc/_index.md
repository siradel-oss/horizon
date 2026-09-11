+++
title = "Introduction"
layout = "single"

[cascade]
    outputs = ["html", "markdown"]
[cascade.params]
    title_suffix = "Horizon Documentation"

[[doc_menu]]
    name = "General"
    entries = [
        "/doc/",
        "/doc/changelog/",
        "/doc/artifacts/",
        "/doc/getting_started/",
        "/doc/platform/",
        "/doc/graphics_configuration/",
        "/doc/custom_backend/",
        "/doc/using_api/",
        "/doc/scene_model/",
        "/doc/message_queue/",
        "/doc/events_handling/",
        "/doc/attributions/",
        "/doc/ambient_settings/",
        "/doc/numeric_palettes/",
        "/doc/blend_modes/",
        "/doc/monitoring/",
        "/doc/mapbox_scenes/",
        "/doc/visibility_constraints/",
        "/doc/loading_priorities/",
        "/doc/client_asset_data/"
    ]
[[doc_menu]]
    name = "Rasters"
    entries = [
        "/doc/raster_providers/",
        "/doc/tiled_raster_provider/",
        "/doc/raster_image_formats/",
        "/doc/palettized_image_raster/",
        "/doc/dtm_raster/",
        "/doc/multiple_rasters/",
        "/doc/terrain_settings/"
    ]
[[doc_menu]]
    name = "Vectors"
    entries = [
        "/doc/vector_data_layers/",
        "/doc/vector_attributes/",
        "/doc/client_vector_data/",
        "/doc/vector_tile_layers/",
        "/doc/styling_api/",
        "/doc/extruded_vectors/",
        "/doc/instanced_models/",
        "/doc/cylinders/",
        "/doc/flat_overlays/",
        "/doc/heatmaps/",
        "/doc/symbols/",
        "/doc/style_enums/"
    ]
[[doc_menu]]
    name = "3D models"
    entries = [
        "/doc/single_model/",
        "/doc/dynamic_materials/",
        "/doc/3d_tiles/",
        "/doc/styling_3d_tiles/",
        "/doc/flat_overlay_over_3d_tiles/"
    ]
[[doc_menu]]
    name = "Interactions"
    entries = [
        "/doc/picking/",
        "/doc/selection/",
        "/doc/gizmos/",
        "/doc/clipping/",
        "/doc/viewshed_analysis/",
        "/doc/camera_controls/",
        "/doc/multiview/",
        "/doc/shape_editor/",
        "/doc/screen_captures/"
    ]
[[doc_menu]]
    name = "glTF extensions"
    entries = [
        "/doc/SIRADEL_templated_image_url/",
        "/doc/SIRADEL_data_texture/"
    ]
[[doc_menu]]
    name = "3D Tiles extensions"
    entries = [
        "/doc/SIRADEL_range_request/"
    ]
[[doc_menu]]
    name = "Resources"
    entries = [
        "/doc/crs_database/",
        "/doc/third_party_licenses/"
    ]
+++

# Introduction

Horizon is a multiplatform geographical rendering engine. It can run on Windows, Linux and web browsers that support Web Assembly. It ships with APIs in C++ and TypeScript. Its rendering backend runs on OpenGL 3.3 for the native versions, and WebGL 2 for its web version.

Note that we call **user** the application that integrates Horizon.

## Protocol

The communication channels with Horizon are defined by [Protocol Buffers](https://developers.google.com/protocol-buffers) schemas. All commands are serialized messages sent to and from the engine. We call the set of all those messages the protocol.

## API

[The API](using_api.html) is the set of commands that can be used to communicate with Horizon. It is divided in categories called services, and methods that each have an input and output message type.

## Backend

A backend is an implementation of the API. The only thing it has to be able to do is receive and send back serialized messages as defined in the API and the protocol.

The simplest backend is [Horizon Core](getting_started.html), which is the viewer itself.

A user can implement a [custom backend](custom_backend.html) that can intercept those messages. For instance, it is possible to implement a backend sending the messages via websockets to be able to control a remote instance of Horizon. Other usecases include dumping all messages to be able to replay sessions, or having a backend that does nothing at all, useful for tests.

![](architecture.svg)
{ style="height: 350px;" }

## Packages

In order to enable its integration in applications, Horizon is distributed as packages that can readily be used as build dependencies, for all target languages. The protocol, the [API](using_api.html), and the [Core backend](getting_started.html) are distributed as separate packages. (Make sure to combine packages with matching versions.)

These packages, as well as some other tools, are available [here](artifacts.html).

## Platform requirements

- 448 MiB RAM
- 256 MiB video RAM

### Native

- [OpenGL](https://www.opengl.org/) 3.3
- [SSE4.1](https://en.wikipedia.org/wiki/SSE4) SIMD instruction set support

When building with EGL as the OpenGL loader:

- [EGL](https://www.khronos.org/egl) 1.5

Optionally, OpenGL ES can be used instead of regular OpenGL, when available:

- [OpenGL ES](https://www.khronos.org/opengles/) 3.0

### Web

- [WebGL](https://www.khronos.org/webgl/) 2
- [WebAssembly](https://webassembly.org/)
    - Including [support for threads](https://github.com/WebAssembly/threads/blob/master/proposals/threads/Overview.md)
- [`SharedArrayBuffer`](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/SharedArrayBuffer)
- The Web page must be in a [secure context](https://developer.mozilla.org/en-US/docs/Web/Security/Secure_Contexts).
- The Web server must set the following headers to these values:
    - `Cross-Origin-Opener-Policy: same-origin`
    - `Cross-Origin-Embedder-Policy: require-corp`

## Dependencies

### C++

- [protobuf](https://github.com/protocolbuffers/protobuf) (lite runtime, see version in `MODULE.bazel`)

### TypeScript

- [protobufjs](https://www.npmjs.com/package/protobufjs) (see version in `package.json`)

## For LLMs

This documentation is also distributed as Markdown for consumption by LLMs. The entry point is the [llms.txt](/llms?format=markdown) file. You can find the Markdown file for each page by replacing the `.html` extension in the URL with `.md`. For example, the Markdown version of this page is available at [index.md](/doc?format=markdown).
