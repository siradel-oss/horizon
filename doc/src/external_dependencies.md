# External dependencies

## In-tree dependencies

Some dependencies are included directly in the repository. In this case, simply create a directory inside `third_party/`. Do not forget to include licensing information, and to update the licenses mapping in `//third_party:BUILD.bazel` if necessary.

Very rarely, a dependency, or third party snippet, might be included directly in the source code of one of our packages. In this case preserve any licensing information in the file or around the snippet, and update the licenses mapping in `//third_party:BUILD.bazel` if necessary.

## Bazel dependencies

Bazel rules, some tools, and most C++ dependencies are imported through Bazel's modules system (bzlmod). Each dependency corresponds to a `bazel_dep` directive inside `MODULE.bazel`.

Some modules come directory the the Bazel Central Registry (BCR), but many don't. For the ones that don't, we create our own module in the `third_party/bazel_registry/` directory. Those modules have their version suffixed by `.hrz.n`. The procedures to add modules are described in the accompanying `README` file.

Again, remember to update the licenses mapping in `//third_party:BUILD.bazel` when applicable.

## npm dependencies

npm dependencies are defined in `package.json` files.

The top-level `package.json` file contains global dependencies used by the whole project. Typically this contains tools used during the build process and that have a single version, for example the HTTP server. Those tools can be imported as follows:

```python
load("@npm//:http-server/package_json.bzl", http_server_bin = "bin")
```

Each JS subproject must be defined in a separate folder, and have a `package.json` file. Look at the existing subprojects for examples of what to write in them depending on whether they are internal, or supposed to be published. But in any case they specify the dependencies for each subproject. Those dependencies can be imported as follows:

```python
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

## Python dependencies

Direct Python dependencies are declared in `requirements.in`. However building the project requires all dependencies, including transitive dependencies, to be declared. This is done in `requirements.txt`.

Every time a dependency is added or updated in `requirements.in`, `requirements.txt` must be updated by executing `bazel run //:requirements.update`.

Checking if `requirements.txt` is up-to-date can be done by executing `bazel test //:requirements_test`.

Targets’ dependencies use the `@pypi//<package_name>` syntax. For example for a target depending on `jinja2`:

```python
py_binary(
    ...
    deps = [
        "@pypi//jinja2",
    ],
)
```
