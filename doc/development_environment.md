+++
title = "Development environment"
+++

# Development environment

## Pre-requisites

### Windows

- [Enable developer mode](https://learn.microsoft.com/en-us/windows/apps/get-started/enable-your-device-for-development).
- Visual Studio 2022 (only Build Tools are necessary)
    - (Optional) Install *only* the English language pack when installing the build tools. This reduces spam during building.
      If you do this, please also set the `VSLANG` environment variable to 1033.
- Bazel via [Bazelisk](https://docs.bazel.build/versions/master/install-bazelisk.html)

Optionally:

- Python >= 3.11
- pnpm 11.x (for managing pnpm dependencies)
    - Use the version that is specified in the root package.json.
    - You can use corepack to automatically use the correct version of pnpm.
- LLVM >= 12 (for `compile_commands.json` generation)
    - The 2022 Visual Studio build tools only support clang >= 16.0.0.
- JDK >= 17 (for OSS publication)

Also consider running Python in a virtual environment. See below.

### Linux

- A C++ compiler that supports C++20
- Bazel via [Bazelisk](https://docs.bazel.build/versions/master/install-bazelisk.html)
- OpenGL headers (package `libgl1-mesa-dev` on Ubuntu)
- The X11 Input extension library, libXi (package `libxi-dev` on Ubuntu)
- The X cursor management library (package `libxcursor-dev` on Ubuntu)

Optionally:

- pnpm 11.x (for managing pnpm dependencies)
    - Use the version that is specified in the root package.json.
    - You can use corepack to automatically use the correct version of pnpm.
- Python >= 3.11
- JDK >= 17 (for OSS publication)
- TK bindings for Python (package `python3-tk` on Ubuntu) (for visual tests GUI)

Also consider running Python in a virtual environment. See below.

## Initializing the development environment

- Clone the git repository
    - `git@redacted.localhost:horizon/Horizon.git`
    - `git@github.com:siradel-oss/horizon.git`
- Run `tools/git/setup.(sh, bat)`
    - On Linux you may also need to `chmod +x` this file and the ones in `tools/git/hooks` before executing this script.

Additionally, on Windows:

- Set the `BAZEL_SH` environment variable to point to "Git for Windows" `sh.exe`.
    - An alternative option is to use MSYS2.
- (Optional) Set the `BAZEL_VC` environment variable to point to your MSVC build tools (`C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC`).
- (Optional) Set the `BAZEL_LLVM` environment variable to point to your LLVM installation (`C:\Program Files\LLVM`).

## Updating the development environment

When updating your (or the CI's) development environment, some false cache hits might trigger, which can make builds fail. This can happen for instance when upgrading the compiler's version as some include paths will change, but Bazel might not see this change.

In this case, it is recommended to update the seed of the build. This is given in the `infra/ci/gitlab-ci.bazelrc` file, under the `HRZ_BUILD_SEED` name. The value can be anything, but having it be time-based is probably a good idea.

The same concept of seeding can be applied to your local development environment by using the same setup in your home's `.bazelrc`.

## Running Python scripts

Several Python scripts have dependencies that are not in Python’s standard library. These dependencies are fetched automatically when a script is run as part of the build process, but some setup is required when running them manually.

The simplest way is to create a virtual environment:

On Windows:
```bash
python -m venv .venv
.venv\Scripts\activate
pip install -r requirements.txt
```

On Linux
```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

The virtual environment must be reactivated every time a new shell is opened.

Alternatively, instead of running scripts with `python3 <file.py>`, you can execute `.venv/bin/python <file.py>`. This allows not having to activate the virtual environment.

### Editor integration

#### CMake

Multiple IDEs support CMake project files, allowing code navigation, autocompletion, and debugging. A `CMakeLists.txt` project file can be generated from Bazel by a dedicated script.

Because a Bazel project can have different dependencies, source files, etc., depending on its configuration, one `CMakeLists.txt` file corresponds to a single configuration (for example: native debug build). It is also not possible to share a single Bazel output base between multiple configurations. (Bazel doesn’t mind, but the CMake build may not always have the files it needs, depending on the last build that was made through Bazel.)

The simplest way to deal with this is to put the options to select an output base and the build parameters for a given configuration in a [bazelrc file](https://bazel.build/docs/bazelrc). The CMake project generation script can then be instructed to use this bazelrc file.

Example bazelrc file:

```
startup --output_base=/path/to/output_base/
common -c dbg
```

Then to generate a project file, execute:

```
python tools/ide_integration/generate_cmakelists.py --bazelrc=/path/to/bazelrc
```

This generates a `CMakeLists.txt` project file at the root of the project, as well as build the necessary Bazel targets in order to generate the dependencies of the C++ source files. In this example, the CMake project is valid for debug builds, because of the `-c dbg` parameter.

The optional argument `-n` can be added. It makes the script only generate the `CMakeLists.txt`.

If your IDE cannot open CMake projects directly, an IDE-specific project file can be generated from the `CMakeLists.txt` file. For example for Visual Studio 2019:

```
cmake <path\to\CMakeLists.txt> -G "Visual Studio 16 2019" -A x64
```

#### Visual Studio Code & other `clangd`-based editors

One can enable autocompletion in VSCode using the `clangd` extension, or in any other editor that supports `clangd` as an LSP (Neovim, helix, etc.). It is necessary to have LLVM installed. This is done by generating a `compile_commands.json` file and placing it at the root of the project.

To generate the file, execute:

```
python tools\ide_integration\generate_compilation_database.py windows
```

This should directly generate a `compile_commands.json` file at the root of the project. This scripts requires an existing Clang installation. See the `windows_clang` config in the `.bazelrc` file.

Additional arguments can be given to Bazel by putting them after `--`:

```
python tools\ide_integration\generate_compilation_database.py -- --//:gles=True
```

There are issues when using `clangd` >= 17.0.0, as some macros defined in the compilation commands seem to be ignored.

## Building and running

- To build anything, use `bazel build <target> <options> --config=<config>`
- To run anything, use `bazel run <target> <option> --config=<config>`

| Config name      | Target platform | Host platform | Graphics API | Notes                                        |
| ---------------- | --------------- | ------------- | ------------ | -------------------------------------------- |
| `windows`        | Windows         | Windows       | OpenGL       |                                              |
| `windows_gles`   | Windows         | Windows       | OpenGL ES    |                                              |
| `windows_clang`  | Windows         | Windows       | OpenGL       | Generates `compile_commands.json` with clang |
| `linux`          | Linux           | Linux         | OpenGL       |                                              |
| `linux_gles`     | Linux           | Linux         | OpenGL ES    |                                              |
| `linux_headless` | Linux           | Linux         | OpenGL       | Headless: does not require a display         |
| `wasm_windows`   | Web (WASM)      | Windows       | WebGL        |                                              |
| `wasm_linux`     | Web (WASM)      | Linux         | WebGL        |                                              |

There are several bazel targets available the most important ones are:

- `//apps/native_client` builds the Horizon desktop client.
- `//apps/web_example:server` builds a very simple Horizon integration in a web page, then serves it at `http://localhost:8080`.
- `//apps/web_client:server` builds the Horizon web client, then serves it at `http://localhost:8080`.
- `//apps/gallery:server` builds a gallery of demos showcasing Horizon's features, then serves it at `http://localhost:8083`.
    - Can also be served through Vite directly with hotreloading by running `npx @bazel/ibazel run //apps/gallery:vite <compilation args>` (requires npm for npx). The short form `-c dbg|opt` cannot be used, `--compilation_mode=dbg|opt` must be used instead.
- `//hrz/doc:pkg` builds the Horizon documentation.

The `-c opt` option can be used to build in release mode, and `-c dbg` for debug mode. Not adding any `-c` flag builds in `fastbuild` version: faster than debug at runtime, and faster build time than the optimized version.

Examples:

- `bazel run //apps/native_client --config=windows -c opt`
- `bazel run //apps/web_client:server --config=wasm_linux -c opt`

### Command line arguments

The client can be given command line argument referenced in the documentation. You must tell Bazel that the arguments you provide aren't Bazel arguments. For that, follow the following pattern: `bazel run //apps/native_client BZL_ARGS -- HRZ_ARGS`. When using file paths, please use absolute paths.

Example: `bazel run //apps/native_client -c opt --config=linux -- --disable-dev-ui true`

### Serving the web client over HTTPS

By default the web client is served locally with the HTTP protocol. However in production it can only be served with the HTTPS protocol (to satisfy secure context requirements). Because there are some behavioural differences on the browser between the two protocols, it can be useful to serve the web client locally with the HTTPS protocol.

A self-signed certificate is provided with the server, just add `_tls` add the end of the target’s name to use it:

```sh
bazel run //apps/web_client:server_tls --config=wasm_linux -c opt
```

You use an existing certificate (and its associated key) by passing the relevant arguments to the server:

```sh
bazel run //apps/web_client:server_tls --config=wasm_linux -c opt -- --certificate path/to/hrz.localhost.pem --keyfile path/to/hrz.localhost-key.pem --hostname hrz.localhost
```

If you can’t or don’t want to use self-signed certificates, you can use a tool like [mkcert](https://github.com/FiloSottile/mkcert) instead. It allows setting up a local certificate authority (CA) and generating certificates for localhost domains.

Note that when the client is served over HTTPS, connecting to a native client over non-secure WebSockets is only possible if it runs locally and is reached through a localhost address.


## Using Bazel's cache

Bazel can cache build artifacts in order to speedup subsequent builds, even in the case of full rebuilds. This can be achieved by using one of two options.

- For remote caching (useful when access to said server is fast and cheap), use `--remote_cache`. Horizon's cache server is `redacted.localhost`.
- For local caching (useful when working remotely), use `--disk_cache` with a folder created for this purpose (use a disk that is fast enough, but also has a lot of free space). Flush this folder regularly as there is no limit to its size.

In order to save your configuration across branches, put these settings in a `.bazelrc` file located at:

- `$HOME/.bazelrc` on Linux & co.
- `%USERPROFILE%\.bazelrc` on Windows.

Example of config file:

```sh
build --disk_cache=D:/bazel_cache

# or...

build --remote_cache=http://redacted.localhost/
```

## Unit-testing

Run tests with `bazel test`:

```sh
bazel test //hrz/fnd:tests --config=windows
```

A `test_suite` exists in the root `BUILD.bazel` to run all tests. Please add newly created test targets to it.

```sh
bazel test //:native_tests --config=windows
bazel test //:web_tests --config=wasm_windows
```

Test logs, outputs, and reports are written to `bazel-testlogs`. Tests can be filtered with `--test_filter=...` using the same syntax as GoogleTest. Note that only tests based on GoogleTest will write an XML report (thanks to some Bazel + GoogleTest magic).

## Debugging

### VS Code

Add a debug configuration of type `cppvsdbg` (on Windows) pointing to the binary built with `-c dbg` (for example `bazel-bin/apps/native_client/native_client.exe`).

Additionally, a natvis file can be used to visualize custom types.

```json
{
    "name": "//apps/native_client",
    "type": "cppvsdbg",
    "request": "launch",
    "program": "${workspaceRoot}/bazel-bin/apps/native_client/native_client.exe",
    "args": [
        "--scene-dump", "C:\\Users\\me\\my_scenes\\awesome_stuff.hrz_scene.pbf",
        "--worker-count", "1",
    ],
    "stopAtEntry": false,
    "cwd": "${workspaceRoot}",
    "environment": [],
    "console": "integratedTerminal",
    "visualizerFile": "${workspaceRoot}/tools/natvis/absl.natvis"
}
```

### Web Assembly

Debug builds of the web version can be debugged (including viewing the code, setting breakpoints, and inspecting variable values) on Chrome. First the [C/C++ DevTools Support (DWARF) extension](https://chromewebstore.google.com/detail/cc++-devtools-support-dwa/pdcpmagijalfljmkmjngeonclgbbannb) must be installed. Then path substitutions have to be set up. They map source file directory paths as they are declared in the debug information of the Web Assembly files to actual paths on your machine. Go to [the extension options page](chrome-extension://pdcpmagijalfljmkmjngeonclgbbannb/ExtensionOptions.html) to add them. You will at least need to map Horizon’s own source code, by adding a substitution from `/proc/self/cwd` to your local clone of the Horizon repository. The C++ source code should then be visible in the source tab of the dev tools. See [this page](https://developer.chrome.com/blog/wasm-debugging-2020/) for more information.

Additionally debug builds have improved stack traces in case of exceptions and assertions on all browsers.

### QtCreator

Some commonly used structures (like vectors) are not displayed in a convenient way in the integrated debugger by default. A script is available at `scripts/personaltypes.py`, that greatly enhances how these types are displayed. See [here](https://doc.qt.io/qtcreator/creator-debugging-helpers.html) for information on how to use the script.
