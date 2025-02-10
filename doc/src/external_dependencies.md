# External dependencies

## Overview

Some external dependencies are automatically managed.

Most of them are defined in the `hrz-packages.json` file, and from this is generated the `hrz-packages.lock.json` and `bazel/deps.bzl` files which respectively store the references to the artifacts, and describe how to fetch those artifacts to Bazel. Those dependencies are managed by the `tools/dependencies/update_deps.py` script.

npm dependencies are defined in the `package.json` file.

## Prebuilt dependencies

Some dependencies need to be prebuilt (mainly those of type `external`, see below). Those dependencies must have their prebuilt packages available in the source tree, however they are not distributed by default. For internal developers, those prebuilt packages are mirrored on our artifact server. For external developers, those dependencies must be built manually.

In both cases, these dependencies can be built or fetched with the `tools/dependencies/fetch_or_build_prebuilt_deps.py` script. Just run this script, specifying a target platform if you are not building them for your current platform. This will try to download the package from the artifacts server, or to build it from source. You can optionally specify which dependencies to build if you do not wish to build them all. Use the name defined in `hrz-package.json`, that is, the name without the platform suffix.

The same requirements apply for building them than described below in the "building external dependencies" section. For instance, building WASM dependencies must be done in an environment where the Emscripten SDK is available. The only difference is that you are not using the `update_deps.py` script.

## The `hrz-packages.json` file

This file stores the authoritative information for all auto-managed dependencies in the `packages` array. Each entry must contain a `name` and a `type`. Optionally a `platforms` field can be specified listing the platforms for which this entry is applicable. Those platforms can be `x86_64-pc-windows-msvc`, `x86_64-pc-linux-gnu`, and `wasm32-unknown-emscripten`. When the `platforms` field is not defined, it's implied that the package is platform-independent. Each `name` and `platforms` pair must be unique.

### Type `github`

This dependency type downloads an archive from a GitHub repository and mirrors it on the Nexus. The repository is specified as the `repo` field with the `user/repository` format. The version is specified in the `ref` field that can be either a tag name or a commit hash. This type of dependency completely ignores platforms. Additionally a `build_file` field can point to a file to act as the `BUILD.bazel` file for this dependency, if it is not provided.

### Type `external`

External dependencies execute an external Python script to fetch the dependency and write it to a predefined folder. The contents of this folder is then packaged and used as any other dependency. The script receives as arguments:

- The version from the `version` field.
- The target platform (`windows`, `linux`, or `wasm`).
- The path to the destination folder.

The version must be unique per version of the package. For example, if you modify the build script, but the version of the software you're building has not changed, this field must still change, for changes to prebuilt packages to be detected properly. For instance you can append a suffix and parse it in your build script: `v1.0.0` -> `v1.0.0/fix1` -> `v1.0.0/fix2`, etc.

Some of these targets might require you running the update script in a particular environment, for example the Visual Studio Developer Command Prompt on Windows.

### Example

```json
{
    "mirror_repository": "http://redacted.localhost/repository/raw-releases/",
    "packages": [
        {
            "name": "remotery",
            "type": "github",
            "repo": "Celtoys/Remotery",
            "ref": "e3281eebee43f6b41dda30c118bab06d9ce69d09",
            "build_file": "//third_party:remotery.BUILD.bazel"
        },
        {
            "name": "variant-lite",
            "type": "github",
            "repo": "martinmoene/variant-lite",
            "ref": "v2.0.0"
        },
        {
            "name": "clang-format",
            "version": "17.0.6",
            "type": "external",
            "platforms": ["x86_64-pc-windows-msvc", "x86_64-pc-linux-gnu"],
            "script": "scripts/build_clang_format.py"
        }
    ]
}
```

### Building "external" dependencies

Some external dependencies might require additional setup in your environment to be built, for example:

- CMake
- A C++ compiler (same as the project)
- emsdk (same version as the project)
- Ninja

Generally the build scripts will error out with an appropriate error message, more or less cryptic, to let you know what is missing.

#### Installing & using emsdk

To build **Emscripten** dependencies, you must have emsdk installed and activated on your machine:

- Get emsdk from `https://github.com/emscripten-core/emsdk.git`.
- Check the version defined in `hrz-packages.json` for emsdk.
- From the emsdk directory:
  - `emsdk install <version>`
  - `emsdk activate <version>`

When you want to build a dependency:

- Set the `EMSDK` environment variable to point to your emsdk clone directory.
- Then, in the same environment where you'll build the dependency:
  - Windows: `%EMSDK%\emsdk_env.bat`
  - Linux: `source $EMSDK/emsdk_env.sh`
- Execute `tools/dependencies/update_deps.py` script as described below.

## Updating the dependencies

Dependencies are updated using the `tools/dependencies/update_deps.py` script. It must be executed from the root of the Horizon repository after the `hrz-packages.json` has been edited. Note that you may have to do the following operations even when the packages have not changed, namely when a package is not available on the Nexus for a platform.

### Updating all dependencies for a given platform

```
python3 tools/dependencies/update_deps.py -u <nexus_user> -p <nexus_pass> -t <target_triple> all
```

This will create the packages for all packages of the specified platform, upload them to the Nexus, update the lock file, and update the Bazel dependencies file.

Because making the packages may not be deterministic, this method is only advised when you really want to build all packages. When you know only a few packages have changed, please see below.

### Updating or adding specific dependencies for a given platform

```
python3 tools/dependencies/update_deps.py -u <nexus_user> -p <nexus_pass> -t <target_triple> <dep1> <dep2> ...

Example:
python3 tools/dependencies/update_deps.py -u <nexus_user> -p <nexus_pass> -t wasm32-unknown-emscripten protobuf harfbuzz
```

This will create the packages for the specified packages of the specified platform, upload them to the Nexus, update the lock file, and update the Bazel dependencies file.

### Removing a dependency

Remove the entries from the `hrz-packages.json` file, then:

```
python3 tools/dependencies/update_deps.py
```

### Regenerating Bazel's dependencies file

```
python3 tools/dependencies/update_deps.py
```

## npm dependencies

npm dependencies are defined in `package.json` files.

The top-level `package.json` file contains global dependencies used by the whole project. Typically this contains tools used during the build process and that have a single version, for example the HTTP server. Those tools can be imported as follows:

```
load("@npm//:http-server/package_json.bzl", http_server_bin = "bin")
```

Each JS subproject must be defined in a separate folder, and have a `package.json` file. Look at the existing subprojects for examples of what to write in them depending on whether they are internal, or supposed to be published. But in any case they specify the dependencies for each subproject. Those dependencies can be imported as follows:

```
load("@npm//<path to package>:sass/package_json.bzl", sass_bin = "bin")
```

They can also be used as rule dependencies using the targets `:node_modules/<package name>`. Note that each project has its own `node_modules` folder, linked at build time, that contains only its dependencies. It is possible to depend on everything using `:node_modules`.

Anytime a `package.json` file is modified, `pnpm install --lockfile-only` must be run at the root of the project.

All subproject must be listed in the `pnpm-workspaces.yaml` file, and when this file is edited, the lockfile must also be updated with `pnpm install --lockfile-only`.

Subprojects that are published as npm packages or that are internal dependencies must define an `npm_package` target that has the same name as the folder they are in (or use an alias). For example the target of the npm package for `//hrz/ts_protocol` must be named `ts_protocol`. See the existing subprojects for examples. The top-level BUILD.bazel file must list those packages using `npm_link_package` so that they can be used as dependencies using their public name. In dependant `package.json` files, the version must be `workspace:*` so that pnpm fetches them internally. For example:

```python
# in /BUILD.bazel
npm_link_package(
    name = "node_modules/@siradel/horizon-protocol",
    src = "//hrz/ts_protocol:npm_pkg",
)
```

```json
{
    "dependencies": {
        "@siradel/horizon-protocol": "workspace:*"
    }
}
```

More information about pnpm and rules_js: https://docs.aspect.build/rulesets/aspect_rules_js/docs/pnpm.
