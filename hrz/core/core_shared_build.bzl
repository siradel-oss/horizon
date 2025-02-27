load("@bazel_skylib//rules:copy_file.bzl", "copy_file")
load("@emsdk//emscripten_toolchain:wasm_rules.bzl", "wasm_cc_binary")

def _set_wasm_name_in_js_impl(ctx):
    ctx.actions.expand_template(
        template = ctx.file.src,
        output = ctx.outputs.out,
        substitutions = {
            ctx.attr.from_name + ".wasm": ctx.attr.to_name + ".wasm",
        },
    )

_set_wasm_name_in_js = rule(
    implementation = _set_wasm_name_in_js_impl,
    attrs = {
        "src": attr.label(allow_single_file = [".js"], mandatory = True),
        "out": attr.output(mandatory = True),
        "from_name": attr.string(mandatory = True),
        "to_name": attr.string(mandatory = True),
    },
)

def _em_cc_binary(name, visibility = ["//visibility:public"], copts = [], linkopts = [], module_name = "", js_libs = [], data = [], link_websocket = False, **kwargs):
    additional_linkopts = []

    if module_name != "":
        # @Todo @Robustness Some of these arguments should be taken from copts and linkopts.
        # But I don't see any way of making this work given that selects don't
        # work in macros (for good reasons).
        additional_linkopts += [
            "-lembind",
            "-sALLOW_MEMORY_GROWTH=1",
            "-sINITIAL_MEMORY=134217728",  # 128 MiB
            "-sMAXIMUM_MEMORY=4294967296",  # 4096 MiB
            "-sPTHREAD_POOL_SIZE=Module.workerCount",
            "-sPTHREAD_POOL_SIZE_STRICT=0",
            "-sEXIT_RUNTIME=1",
            "-sERROR_ON_UNDEFINED_SYMBOLS=1",
            "-sLLD_REPORT_UNDEFINED=1",
            "-sENVIRONMENT=web,worker",
            "-sSTACK_SIZE=4MB",
            "-sDEFAULT_PTHREAD_STACK_SIZE=2MB",
        ]

    additional_linkopts += [
        "-sMODULARIZE=1",
        "-sEXPORT_NAME=" + module_name,
        "-sUSE_WEBGL2=1",
        "-sTEXTDECODER=0",  # @Workaround(010-Chromium-Emscripten-TextDecoder)
        "-mbulk-memory",
    ]

    if link_websocket:
        additional_linkopts.append("-lwebsocket.js")

    for js_lib in js_libs:
        additional_linkopts += ["--js-library", "$(location " + js_lib + ")"]

    for data_file in data:
        additional_linkopts += ["--embed-file", "$(location " + data_file + ")@$(location " + data_file + ")"]

    native.cc_binary(
        name = name + "_cc",
        visibility = visibility,
        copts = copts + ["-mbulk-memory"],
        linkopts = linkopts + additional_linkopts,
        additional_linker_inputs = js_libs,
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
    )

    copy_file(
        name = name + "_rename_wasm_file",
        src = name + "_wasm_cc/" + name + "_cc.wasm",
        out = name + ".wasm",
        visibility = visibility,
    )

    for suffix in ["", ".worker"]:
        _set_wasm_name_in_js(
            name = name + suffix + "_rename_js_file",
            src = name + "_wasm_cc/" + name + "_cc" + suffix + ".js",
            out = name + suffix + ".js",
            from_name = name + "_cc",
            to_name = name,
            visibility = visibility,
        )

    native.filegroup(
        name = name + "_wasm",
        srcs = [
            name + ".wasm",
            name + ".js",
        ],
    )

def _cc_shared_windows(name, srcs, hdrs = [], visibility = ["//visibility:private"], **kwargs):
    native.cc_binary(
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

    native.cc_import(
        name = name + "_lib_win",
        interface_library = ":" + name + "_import_lib_win",
        shared_library = ":" + name + ".dll",
    )

    native.cc_library(
        name = name + "_win",
        hdrs = hdrs,
        deps = [":" + name + "_lib_win"],
        visibility = visibility,
        includes = kwargs.get("includes", []),
    )

def _cc_shared_linux(name, **kwargs):
    native.cc_library(
        name = name + "_linux",
        linkstatic = False,
        **kwargs
    )

def hrz_cc_shared(name, srcs, hdrs = [], em_module_name = "", link_websocket = False, js_libs = [], alwayslink = True, visibility = ["//visibility:private"], **kwargs):
    _cc_shared_windows(name = name, srcs = srcs, hdrs = hdrs, visibility = visibility, **kwargs)
    _cc_shared_linux(name = name, srcs = srcs, hdrs = hdrs, alwayslink = alwayslink, visibility = visibility, **kwargs)

    _em_cc_binary(
        name = name,
        srcs = srcs + hdrs,
        visibility = visibility,
        module_name = em_module_name,
        js_libs = js_libs,
        link_websocket = link_websocket,
        **kwargs
    )

    native.alias(
        name = name,
        actual = select({
            "//:os_windows": name + "_win",
            "//:os_linux": name + "_linux",
            "//:os_web": name + "_wasm",
        }),
        visibility = visibility,
    )

    native.alias(
        name = name + "_import_lib",
        actual = select({
            "//:os_windows": name + "_import_lib_win",
            "//:os_linux": name + "_linux",
            "//:os_web": name + "_wasm",
        }),
        visibility = visibility,
    )

    native.alias(
        name = name + "_shared_lib",
        actual = select({
            "//:os_windows": name + ".dll",
            "//:os_linux": name + "_linux",
            "//:os_web": name + "_wasm",
        }),
        visibility = visibility,
    )
