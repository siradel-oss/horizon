+++
title = "Project structure"
+++

# Project structure

* `apps`: Integration examples.
    * `gallery`: The demos gallery.
* `changelogs`: Full & partial changelogs, and related tooling.
* `ci`: Gitlab CI setup, and related tooling.
* `doc`: Developer documentation.
* `hrz`: The Horizon engine, bindings, and closely related tooling.
    * `branding`: Horizon branding assets, such as icons.
    * `common`: Utilities used by multiple modules. Can have dependencies on any other library.
    * `core`: The Horizon engine itself.
        * `jobs`: Asynchronous tasks.
        * `js`: JS interaction library.
        * `shaders`: All GLSL shaders used by the viewer.
    * `cpp_api`: C++ implementation of the API.
    * `cpp_protocol`: C++ implementation of the protocol.
    * `doc`: User documentation.
    * `fnd`: Utilities used my multiple modules. Only non-usercase-specific dependencies are allowed.
    * `generator`: Common code generation utilities.
    * `http_cache`: Implementation of an HTTP cache.
    * `mapbox`: Mapbox to Horizon scene conversion library.
    * `monitoring`: Intrusive performance monitoring library.
    * `protocol`: Protocol definition & migration library.
    * `scene_dump`: C++ scene dump manipulation utilities.
    * `ts_api`: TypeScript implementation of the API.
    * `ts_core`: TypeScript interface to the Core viewer.
    * `ts_monitoring_protocol`: TypeScript monitoring protocol implementation.
    * `ts_protocol`: TypeScript protocol implementation.
    * `ts_scene_dump`: TypeScript scene dump manipulation library.
    * `xml_desc_generator`: Protoc plugin to generate an XML description of the protocol.
* `libs`: Non-Horizon, developed here, libraries.
    * `argparser`: Very simple CLI argument parser.
    * `lm`: Vector, matrix & quaternion math library.
    * `mycelium`: OpenGL & WebGL rendering backend, renderer, and render graph.
    * `proj_lite`: Geographic reprojection library, with an EPSG database, proj.4-compatible.
    * `ws`: WebSockets library.
    * `wsi`: Windowing library for Windows & Linux.
* `third_party`: Third-party libraries & build files.
* `tools`: Development or build tooling.
    * `bazel`: Horizon-specific Bazel build infrastructure.
    * `build_info`: Tools to provide or set build information (date, version, ...).
    * `ide_integration`: Tools to help develop Horizon in IDEs.
    * `licenses`: Tools to generate a list of third-party licenses.
    * `monitoring_client`: Client application that connects to Horizon for performance monitoring.
    * `scene_dump`: Tools to manipulate scene dumps.
    * `scene_model`: Tools to manage scene model versions and their migrations.
    * `visual_testing`: Tool to run and manage visual tests.
