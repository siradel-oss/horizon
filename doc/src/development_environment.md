# Development environment

## Pre-requisites

### Windows

- [Enable developer mode](https://learn.microsoft.com/en-us/windows/apps/get-started/enable-your-device-for-development).
- Visual Studio 2022 (only Build Tools are necessary)
    - (Optional) Install *only* the English language pack when installing the build tools. This reduces spam during building.
      If you do this, please also set the `VSLANG` environment variable to 1033.
- Bazel via [Bazelisk](https://docs.bazel.build/versions/master/install-bazelisk.html)
- Python >= 3.8
    - Run the installer as administrator
    - Select "Add python.exe to PATH"
    - Select "Customize installation"
    - In "Advanced options", select "Install Python 3.x for all users"
- Install the following Python packages with pip. (You might also need to install them from an administrator command prompt if you're getting errors when building.)
    - jinja2
    - markdown (if you have *ImportError: No module named 'pkg_resources'* try *pip install --upgrade setuptools*)
    - pygments
    - mkdocs
    - mkdocs-material
    - requests
    - lark (optional)
    - lark-parser (optional)
    - imgui\[sdl2] (optional)
    - pysdl2-dll (optional)
- pnpm 8.x (not 9 or above!) (optional, necessary for managing npm dependencies)
- fd-find (use `cargo install fd-find` or download at https://github.com/sharkdp/fd/releases)
- CMake >= 3.1 (optional, for external dependencies)
- LLVM >= 12 (optional, for `compile_commands.json` generation)
    - The 2022 Visual Studio build tools only support clang >= 16.0.0.
- emsdk (version from `hrz-packages.json`) (optional, for external dependencies)
- JDK >= 17 (optional, for external dependencies & publishing the repository)

#### Certificate issues

Self-signed certificates in your certificate chain, typically added by corporate proxies, might cause issues with Bazel. Though this issue should not happen currently, here are the steps to fix it in case it ever reappears.

Add the following line to your `.bazelrc` file (`%USERPROFILE%\.bazelrc`):

```
startup --host_jvm_args="-Djavax.net.ssl.trustStoreType=Windows-ROOT"
```

If the issue still occurs, install a recent version of the JDK and add to your `.bazelrc` file the following:

```
startup --server_javabase=<path to you Java install> # For example C:\Program Files\Java\jre1.8.0_271
```

### Linux

- A C++ compiler that supports C++17
- Bazel via [Bazelisk](https://docs.bazel.build/versions/master/install-bazelisk.html)
- OpenGL headers (package `libgl1-mesa-dev` on Ubuntu)
- The X11 Input extension library, libXi (package `libxi-dev` on Ubuntu)
- The X cursor management library (package `libxcursor-dev` on Ubuntu)
- pnpm 8.x (not 9 or above!) (optional, necessary for managing npm dependencies)
- Python >= 3.8, along with the following packages:
    - jinja2
    - markdown
    - pygments
    - mkdocs
    - mkdocs-material
    - requests
    - lark (optional)
    - lark-parser (optional)
    - imgui\[sdl2] (optional)
    - pysdl2-dll (optional)
- fd-find (use `cargo install fd-find` or download at https://github.com/sharkdp/fd/releases)
- CMake >= 3.1 (optional, for external dependencies)
- emsdk (version from `hrz-packages.json`) (optional, for external dependencies)
- JDK >= 17 (optional, for external dependencies & publishing the repository)

- TK bindings for Python (package `python3-tk` on Ubuntu) (optional)

### More certificate issues!

Using Bazel inside a Linux VM on a Windows host leads to similar certificate issues. The VM probably doesn't have the needed certificate to download content from the web, so the first step is to export that certificate from Windows and give it to the Linux VM:

* Open a command prompt and run the command: `certmgr.msc`; a window should open.
* In the left menu, unfold "Trusted Root Certification Authorities" and click on the "Certificates" folder.
* On the right part of the window look for the line with your problematic certificate (for example "ISINFRA ROOT CA") and right-click on it.
* Hover the "All tasks" entry and click "Export..." in the list that opens.
* Click "Next", on the second page select the "Base-64 encoded X.509 (.CER)" option, and click "Next" again.
* Select a directory to export the certificate to and name it `isinfra_root_ca.cer`. Click "Next" then "Finish" to export the certificate.

Now that the certificate has been retrieved it needs to be passed to the Linux VM. When using a Docker container, the following command can copy the certificate to a running container:

* `docker cp isinfra_root_ca.cer container_id:/home/isinfra_root_ca.crt`

You can find your running container's ID with `docker ps`.
Note that we give the certificate a `.crt` extension on Linux, which is required for the certificate to be recognized.

Now the Linux VM should add this certificate to its list of known certificates.

* Navigate to the directory containing your copied certificate.
* Copy it here: `cp isinfra_root_ca.crt /usr/local/share/ca-certificates/isinfra_root_ca.crt`. **Make sure its extension is `.crt`!**
* Update the certificates storage with `update-ca-certificates`.

The output should look something like this:

```
Updating certificates in /etc/ssl/certs...
1 added, 0 removed; done.
```

Now your certificate should be properly set up, you can try it with `ping google.com` or by downloading something with `curl`.
Chances are that Bazel will still not be able to download anything though, because it manages its own certificates using a Java VM. One way to fix it is to do the following:

* Install the tools needed to manage Java certificates with `apt install ca-certificates-java`.
* Using the newly installed `keytool`, import the certificate to the Java certificates store: `keytool -importcert -v -noprompt -file isinfra_root_ca.crt -keystore /etc/ssl/certs/java/cacerts -storepass changeit`.
* Add the following content to your `.bazelrc` file in your `$HOME` directory:

```
startup --host_jvm_args="-Djavax.net.ssl.trustStore=/etc/ssl/certs/java/cacerts"
startup --host_jvm_args="-Djavax.net.ssl.trustStorePassword=changeit"
```

Now Bazel should have access to the certificate and be able to download glorious things from the Internet.

## Updating the development environment

When updating your (or the CI's) development environment, some false cache hits might trigger, which can make builds fail. This can happen for instance when upgrading the compiler's version as some include paths will change, but Bazel might not see this change.

In this case, it is recommended to update the seed of the build. This is given in the `ci/gitlab-ci.bazelrc` file, under the `HRZ_BUILD_SEED` name. The value can be anything, but having it be time-based is probably a good idea.

The same concept of seeding can be applied to your local development environment by using the same setup in your home's `.bazelrc`.

## Initializing the development environment

- Clone the git repository
    - `git@vsi-git-001.siradel.local:horizon/Horizon.git`
    - `git@github.com:siradel-oss/Horizon.git`
- Run `tools/git/setup.(sh, bat)`
    - On Linux you may also need to `chmod +x` this file and the ones in `tools/git/hooks` before executing this script.
- Fetch or build prebuilt dependencies
    - `python3 tools/dependencies/fetch_or_build_prebuilt_deps.py`
    - You may need to do this in an `emsdk` environment to build WASM dependencies.
        - You can optionally specify which dependencies to build if not all, and the target platform.
    - See the [external dependencies documentation](external_dependencies.md) for more information.
    - This script might need to be re-run periodically, when dependencies are updated. The build process will error out with an appropriate message when this is necessary.

Additionally, on Windows:

- Set the `BAZEL_SH` environment variable to point to "Git for Windows" `sh.exe`.
    - An alternative option is to use MSYS2, install `pacman -S zip unzip patch diffutils git`, then set `BAZEL_SH` to `usr\bin\bash.exe` inside of the MSYS2 installation directory.
- Set the `BAZEL_VC` environment variable to point to your MSVC build tools (`C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC`).
- (Optional) Set the `BAZEL_LLVM` environment variable to point to your LLVM installation (`C:\Program Files\LLVM`).

- To build anything, use `bazel build <target> <options> --config=<config>`
- To run anything, use `bazel run <target> <option> --config=<config>`

Possible configs are:

- `windows` to build the native Windows version on Windows.
- `windows_clang` to build clangd's compile_commands.json file on Windows. (See below, not tested for building.)
- `linux` to build the native Linux version on Linux.
- `wasm_windows` to build the web version on Windows.
- `wasm_linux` to build the web version on Linux.

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
python tools\ide_integration\generate_compilation_database.py windows -- --//:gl_api=gles
```

There are issues when using `clangd` >= 17.0.0, as some macros defined in the compilation commands seem to be ignored.

## Bazel targets

There are several bazel targets available the most important ones are:

- `//apps/native_client` builds the Horizon desktop client.
- `//apps/web_client:server` builds the Horizon web client, then serves it at `http://localhost:8080`.
- `//hrz/doc:server` builds the Horizon documentation, then serves it at `http://localhost:8081`.

The `-c opt` option can be used to build in release mode, and `-c dbg` for debug mode. Not adding any `-c` flag builds in `fastbuild` version: faster than debug at runtime, and faster build time than the optimized version.

Full command example: `bazel run //apps/native_client --config=windows -c opt`.

### Command line arguments

The client can be given command line argument referenced in the documentation. You must tell Bazel that the arguments you provide aren't Bazel arguments. For that, follow the following pattern: `bazel run //apps/native_client BZL_ARGS -- HRZ_ARGS`. When using file paths, please use absolute paths.

Example: `bazel run //apps/native_client -c opt --config=linux -- --disable-dev-ui true`

## Using Bazel's cache

Bazel can cache build artifacts in order to speedup subsequent builds, even in the case of full rebuilds. This can be achieved by using one of two options.

- For remote caching (useful when access to said server is fast and cheap), use `--remote_cache`. Horizon's cache server is `lfrn1mmp03.siradel.local:8090`.
- For local caching (useful when working remotely), use `--disk_cache` with a folder created for this purpose (use a disk that is fast enough, but also has a lot of free space). Flush this folder regularly as there is no limit to its size.

In order to save your configuration across branches, put these settings in a `.bazelrc` file located at:

- `$HOME/.bazelrc` on Linux & co.
- `%USERPROFILE%\.bazelrc` on Windows.

Example of config file:

```
build --disk_cache=D:/bazel_cache

# or...

build --remote_cache=http://lfrn1mmp03.siradel.local:8090/
```

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
