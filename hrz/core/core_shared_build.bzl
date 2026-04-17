load("@bazel_skylib//rules:copy_file.bzl", "copy_file")
load("@bazel_skylib//rules:expand_template.bzl", "expand_template")
load("@emsdk//emscripten_toolchain:wasm_rules.bzl", "wasm_cc_binary")
load("@rules_cc//cc:cc_binary.bzl", "cc_binary")
load("@rules_cc//cc:cc_import.bzl", "cc_import")
load("@rules_cc//cc:cc_library.bzl", "cc_library")

def _em_cc_binary(name, visibility = ["//visibility:public"], **kwargs):
    cc_binary(
        name = name + "_cc",
        features = ["-wasm_warnings_as_errors"],
        # This target won't build successfully on its own because of missing emscripten
        # headers etc. Therefore, we hide it from wildcards.
        tags = ["manual"],
        **kwargs
    )

    wasm_cc_binary(
        name = name + "_wasm_cc",
        cc_target = ":" + name + "_cc",
        threads = "emscripten",
        exit_runtime = True,
        outputs = [
            name + "_cc.wasm",
            name + "_cc.js",
        ],
    )

    copy_file(
        name = name + "_rename_wasm_file",
        src = name + "_cc.wasm",
        out = name + ".wasm",
        visibility = visibility,
    )

    expand_template(
        name = name + "_rename_js_file",
        template = name + "_cc.js",
        out = name + ".js",
        substitutions = {
            name + "_cc.wasm": name + ".wasm",
            "new URL(\"" + name + "_cc\"": "new URL(\"" + name + "\"",
        },
        visibility = visibility,
    )

    native.filegroup(
        name = name + "_wasm",
        srcs = [
            name + ".wasm",
            name + ".js",
        ],
        visibility = visibility,
    )

def _cc_shared_windows(name, srcs, hdrs = [], visibility = ["//visibility:private"], **kwargs):
    cc_binary(
        name = name + ".dll",
        linkshared = True,
        srcs = srcs + hdrs,
        visibility = visibility,
        **kwargs
    )

    native.filegroup(
        name = name + "_import_lib_win",
        srcs = [":" + name + ".dll"],
        output_group = "interface_library",
    )
    cc_import(
        name = name + "_lib_win",
        interface_library = ":" + name + "_import_lib_win",
        shared_library = ":" + name + ".dll",
    )
    cc_library(
        name = name + "_win",
        hdrs = hdrs,
        deps = [":" + name + "_lib_win"],
        visibility = visibility,
        includes = kwargs.get("includes", []),
    )

def _cc_shared_linux(name, srcs, visibility, hdrs = [], linkopts = [], **kwargs):
    cc_binary(
        name = name + ".so",
        srcs = srcs + hdrs,
        linkstatic = True,
        linkshared = True,
        linkopts = linkopts + [
            "-Wl,--exclude-libs,ALL",
        ],
        visibility = visibility,
        **kwargs
    )
    cc_import(
        name = name + "_lib_linux",
        shared_library = ":" + name + ".so",
    )
    cc_library(
        name = name + "_linux",
        hdrs = hdrs,
        visibility = visibility,
        deps = [":" + name + "_lib_linux"],
        includes = kwargs.get("includes", []),
    )

def hrz_cc_shared(name, srcs, hdrs = [], alwayslink = True, visibility = ["//visibility:private"], **kwargs):
    _cc_shared_windows(name = name, srcs = srcs, hdrs = hdrs, visibility = visibility, **kwargs)
    _cc_shared_linux(name = name, srcs = srcs, hdrs = hdrs, visibility = visibility, **kwargs)

    _em_cc_binary(
        name = name,
        srcs = srcs + hdrs,
        visibility = visibility,
        **kwargs
    )

    native.alias(
        name = name,
        actual = select({
            "@platforms//os:windows": name + "_win",
            "@platforms//os:linux": name + "_linux",
            "@platforms//os:emscripten": name + "_wasm",
        }),
        visibility = visibility,
    )

    native.alias(
        name = name + "_import_lib",
        actual = select({
            "@platforms//os:windows": name + "_import_lib_win",
            "@platforms//os:linux": name + "_linux",
            "@platforms//os:emscripten": name + "_wasm",
        }),
        visibility = visibility,
    )

    native.alias(
        name = name + "_shared_lib",
        actual = select({
            "@platforms//os:windows": name + ".dll",
            "@platforms//os:linux": name + ".so",
            "@platforms//os:emscripten": name + "_wasm",
        }),
        visibility = visibility,
    )
