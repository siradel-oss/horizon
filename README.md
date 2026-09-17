<div align="center">

![](public/terrain.webp)

# Horizon

*Horizon is a real-time 3D engine for visualizing map data on a globe. \
It is cross-platform, cross-browser, and defines a language-independent API.*

</div>

* [Features](#features)
* [Getting started](#getting-started)
* [Building](#building)
* [Contributing](#contributing)
* [License](#license)

*This repository is an extract of our internal version. Some components are missing, notably test data.*

## Features

* **Platform**: Web (Wasm/WebGL 2/TypeScript), native Windows/Linux (OpenGL 3/C++), and any language via Protocol Buffers.
* **Terrain**: Stream dozens of imagery/DTM rasters worldwide down to centimeter resolution, from many source formats (TMS, WMS, WMTS, ArcGIS, TileJSON, PMTiles, Bing, and more), with dynamic colorization of arbitrary raster data.
* **Graphics**: Full control over lighting, atmosphere, and ambiance.
* **Vector**: Stream and combine vector data from many sources in various formats (PMTiles, TileJSON, GeoJSON, MVT, Geobuf, etc.), render in 3D with extrusions, cylinders, instanced models, heatmaps, and customizable symbol layout, styled via a scripting system, with Mapbox scene import.
* **3D models**: Visualize glTF, 3D Tiles, and point clouds at any scale, with styling scripts and dynamic multi-material binding for digital twins.
* **Interactions**: Multi-view, flexible camera control, application-driven procedural data, picking/selection/highlighting/gizmos, viewshed & clipping analysis, and in-view vector editing.

## Getting started

The documentation and prebuilt artifacts for C++ and TypeScript (through npm packages) are available on the "Releases" page on GitHub, but you can also [build them yourself](#building). Some integration examples are given in the [apps folder](apps/).

The documentation provides a [guide on how to get started](https://siradel-oss.github.io/horizon/doc/getting_started.html).

The npm packages (published to npm) are:
* `@siradel-oss/horizon-protocol`: Type definitions.
* `@siradel-oss/horizon-api`: API & utilities.
* `@siradel-oss/horizon-core`: The actual Horizon engine.
* `@siradel-oss/horizon-scene-dump`: Utilities to dump & load the scene model.
* `@siradel-oss/horizon-monitoring-protocol`: Type definitions for monitoring the engine.


## Building

To build Horizon, follow the [development environment documentation](https://siradel-oss.github.io/horizon/doc/dev_guide/development_environment.html) to setup your environment and learn how to build. Once you have built the artifacts, you are free to work with the engine outside of the Bazel environment if you are developing an application. Artifacts are available in the `bazel-bin` directory.

The project structure is described in the [technical documentation](https://siradel-oss.github.io/horizon/doc/dev_guide/project_structure.html).

|  | Bazel target |
|-----------|----------|
| C++ viewer package | `build //hrz/core:pkg_native` |
| C++ protocol library package | `build //hrz/cpp_protocol:pkg` |
| C++ API library package | `build //hrz/cpp_api:pkg` |
| npm TypeScript viewer package | `build //hrz/ts_core:npm_pkg` |
| npm TypeScript protocol package | `build //hrz/ts_protocol:npm_pkg` |
| npm TypeScript API package | `build //hrz/ts_api:npm_pkg` |
| C++ integration | `run //apps/native_client` |
| Web TypeScript integration example | `run //apps/web_example:server` |
| Demos gallery | `run //apps/gallery:server` |
| Documentation | `build //hrz/doc:pkg` |

## Contributing

We welcome contributions to this project. Please read our [Contributor Guide](CONTRIBUTING.md) for information on how to get involved,
and to review our contributor terms.

Please see the [developer's documentation](https://siradel-oss.github.io/horizon/doc/dev_guide/index.html) for information regarding development environment and workflow.

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.
