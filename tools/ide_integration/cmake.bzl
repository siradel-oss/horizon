load("@protobuf//bazel/common:proto_info.bzl", "ProtoInfo")

CMakeAspectInfo = provider(fields = ["cmake_commands", "source_files"])

_cpp_header_extensions = [
    "hh",
    "hxx",
    "ipp",
    "hpp",
    "inc",
    "",
]

_c_or_cpp_header_extensions = ["h"] + _cpp_header_extensions

_cpp_extensions = [
    "cc",
    "cpp",
    "cxx",
] + _cpp_header_extensions

_c_or_cpp_extensions = ["h", "c"] + _cpp_extensions

_cc_rules = [
    "cc_binary",
    "cc_library",
    "cc_import",
    "cc_proto_library",
]

def _target_path(target):
    path = ""

    if target.label.workspace_name:
        path += "external/" + target.label.workspace_name

    if target.label.package:
        if path:
            path += "/"
        path += target.label.package

    return path

def _bazel_label_to_cmake_target_name(target):
    # Turn the target's label into a valid CMake target identifier.
    return str(target.label).replace("@", "").replace("//:", "_").replace("//", "").replace("/", "_").replace(":", "__").replace("~", "-")

# Defines with the form `VAR="Value"`, with quotes, go through Bourne shell tokenisation
# (https://bazel.build/reference/be/common-definitions#sh-tokenization) between BUILD.bazel
# files and the compiler. Among other things, it removes quotes from values.
# It allows passing values containing spaces.
# However the same syntax in CMake preserves the quotes and passes them to the compiler.
# To pass values with spaces, escaping these characters (like this: `\ `) is necessary.
# This function converts between the two syntaxes.
def _remove_define_quotes_and_escape(str):
    if "=" in str:
        parts = str.split("=", 1)
        if '"' in parts[0]:
            return str
        if len(parts[1]) < 2:
            return str
        if parts[1][0] == '"' and parts[1][-1] == '"':
            return parts[0] + "=" + parts[1][1:-1].replace(" ", "\\ ")
    return str

def _cmakelists_txt(cmake_commands):
    # Return a CMakeLists.txt string for the CMake commands.

    txt = ""

    for command in cmake_commands:
        txt += command.command + "(" + command.name + " "
        txt += " ".join(command.prps.to_list())
        txt += "\n"
        for src in command.srcs.to_list():
            txt += "    " + src + "\n"
        txt += ")\n\n"

    return txt

def _sources(ctx):
    srcs = []
    if "srcs" in dir(ctx.rule.attr):
        srcs += [f for src in ctx.rule.attr.srcs for f in src.files.to_list()]
    if "hdrs" in dir(ctx.rule.attr):
        srcs += [f for src in ctx.rule.attr.hdrs for f in src.files.to_list()]

    return srcs

def _is_cpp_target(srcs):
    if all([src.extension in _c_or_cpp_header_extensions for src in srcs]):
        return True  # assume header-only lib is c++
    return any([src.extension in _cpp_extensions for src in srcs])

def _cmakelists_aspect_impl(target, ctx):
    # Write the compile commands for this target to a file, and return
    # the commands for the transitive closure.

    # This is for writing and debugging C/C++ code in an IDE, so we only
    # care about cc rules.

    is_cc_proto_library = ctx.rule.kind == "cc_proto_library"

    transitive_cmake_commands = []
    transitive_source_files = []

    deps = []
    if hasattr(ctx.rule.attr, "srcs"):
        deps.extend(ctx.rule.attr.srcs)
    if hasattr(ctx.rule.attr, "deps"):
        deps.extend(ctx.rule.attr.deps)
    if hasattr(ctx.rule.attr, "implementation_deps"):
        deps.extend(ctx.rule.attr.implementation_deps)

    if is_cc_proto_library:
        deps.extend([ctx.attr._protobuf_runtime])

    for dep in deps:
        if CMakeAspectInfo in dep:
            transitive_cmake_commands.append(dep[CMakeAspectInfo].cmake_commands)
            transitive_source_files.append(dep[CMakeAspectInfo].source_files)

    if ctx.rule.kind not in _cc_rules:
        return [
            CMakeAspectInfo(
                cmake_commands = depset(transitive = transitive_cmake_commands),
                source_files = depset(transitive = transitive_source_files),
            ),
        ]

    target_name = _bazel_label_to_cmake_target_name(target)

    srcs = _sources(ctx)
    if is_cc_proto_library:
        srcs += [f for f in target.files.to_list() if f.extension in ["h", "cc"]]

    is_cpp = _is_cpp_target(srcs)

    files = []
    uses_generated_files = False
    for src in srcs:
        if src.path.startswith(ctx.genfiles_dir.path):
            uses_generated_files = True

        if src.extension in _c_or_cpp_extensions:
            files.append("__EXEC_ROOT__/" + src.path)

    header_only = True
    has_headers = False
    for src in srcs:
        if src.extension in _c_or_cpp_header_extensions:
            has_headers = True
        elif src.extension in _c_or_cpp_extensions:
            header_only = False

    cmake_commands = []
    shared_library = False

    if ctx.rule.kind == "cc_binary":
        shared_library = hasattr(ctx.rule.attr, "linkshared") and ctx.rule.attr.linkshared

        command = "add_library" if shared_library else "add_executable"
        prps = ["SHARED"] if shared_library else []

        cmake_commands.append(
            struct(
                command = command,
                name = target_name,
                prps = depset(prps),
                srcs = depset(files),
            ),
        )

        if is_cpp:
            cmake_commands.append(
                struct(
                    command = "set_target_properties",
                    name = target_name,
                    prps = depset(["PROPERTIES"]),
                    srcs = depset(["LINKER_LANGUAGE CXX"]),
                ),
            )
    elif ctx.rule.kind == "cc_library" or is_cc_proto_library:
        prps = []
        if not files:
            prps.append("INTERFACE")
            prps.append("IMPORTED")
        elif header_only:
            prps.append("INTERFACE")
            files = []
        else:
            prps.append("STATIC")

        cmake_commands.append(
            struct(
                command = "add_library",
                name = target_name,
                prps = depset(prps),
                srcs = depset(files),
            ),
        )

        include_prps = ["INTERFACE" if header_only else "PUBLIC"]

        paths = []
        if is_cc_proto_library:
            for dep in ctx.rule.attr.deps:
                if ProtoInfo in dep:
                    paths += ["__EXEC_ROOT__/" + p for p in dep[ProtoInfo].transitive_proto_path.to_list()]
        elif has_headers:
            # [strip_]include_prefix options make Bazel generate a _virtual_includes directory
            # in the genfiles directory tree. The headers are copied there, with the expected
            # path prefix, given the rules.
            # So in order to make includes work, if there are prefix rules, we add this directory
            # to the include search path.
            strip_include_prefix = ctx.rule.attr.strip_include_prefix if hasattr(ctx.rule.attr, "strip_include_prefix") else None
            include_prefix = ctx.rule.attr.include_prefix if hasattr(ctx.rule.attr, "include_prefix") else None

            if strip_include_prefix or include_prefix:
                path = "__EXEC_ROOT__/" + ctx.genfiles_dir.path + "/"
                if target.label.workspace_name:
                    path += "external/" + target.label.workspace_name + "/"

                if target.label.package:
                    path += target.label.package + "/"

                path += "_virtual_includes/" + ctx.rule.attr.name
                paths.append(path)
            else:
                path = "__EXEC_ROOT__/"
                if target.label.workspace_name:
                    path += "external/" + target.label.workspace_name + "/"
                paths.append(path)

        cmake_commands.append(
            struct(
                command = "target_include_directories",
                name = target_name,
                prps = depset(include_prps),
                srcs = depset(paths),
            ),
        )

        if not header_only and _is_cpp_target(srcs):
            cmake_commands.append(
                struct(
                    command = "set_target_properties",
                    name = target_name,
                    prps = depset(["PROPERTIES"]),
                    srcs = depset(["LINKER_LANGUAGE CXX"]),
                ),
            )
    elif ctx.rule.kind == "cc_import":
        if ctx.rule.attr.static_library:
            cmake_commands.append(
                struct(
                    command = "add_library",
                    name = target_name,
                    prps = depset(["STATIC", "IMPORTED"]),
                    srcs = depset([]),
                ),
            )

            cmake_commands.append(
                struct(
                    command = "set_target_properties",
                    name = target_name,
                    prps = depset(["PROPERTIES"]),
                    srcs = depset(["IMPORTED_LOCATION __EXEC_ROOT__/" + ctx.rule.attr.static_library.files.to_list()[0].path]),
                ),
            )
        elif ctx.rule.attr.shared_library:
            cmake_commands.append(
                struct(
                    command = "add_library",
                    name = target_name,
                    prps = depset(["INTERFACE", "IMPORTED"]),
                    srcs = depset([]),
                ),
            )

            deps = []
            if CcInfo in ctx.rule.attr.shared_library:
                # The dependency is built as part of the C++ build process.
                # It has a CMake target as well, so we want to depend on it.
                # @Fixme The test is probably not the best possible. The intention is to find if the
                #        dependency is built as part of a rule (and isn't a file in the source tree),
                #        and has CMake commands to build it. But checking for CMakeAspectInfo doesn't work.
                deps.append("INTERFACE_LINK_LIBRARIES " + _bazel_label_to_cmake_target_name(ctx.rule.attr.shared_library))
            else:
                # This is a pre-built dependency.
                # We just point to the files.
                deps.append("IMPORTED_LOCATION __EXEC_ROOT__/" + ctx.rule.attr.shared_library.files.to_list()[0].path)
                if ctx.rule.attr.interface_library:
                    deps.append("IMPORTED_IMPLIB __EXEC_ROOT__/" + ctx.rule.attr.interface_library.files.to_list()[0].path)

            cmake_commands.append(
                struct(
                    command = "set_target_properties",
                    name = target_name,
                    prps = depset(["PROPERTIES"]),
                    srcs = depset(deps),
                ),
            )
    else:
        fail("Unhandled rule kind")

    if hasattr(ctx.rule.attr, "deps") and ctx.rule.attr.deps:
        deps = []

        if is_cc_proto_library:
            deps.append(_bazel_label_to_cmake_target_name(ctx.attr._protobuf_runtime))
        else:
            for dep in ctx.rule.attr.deps:
                deps.append(_bazel_label_to_cmake_target_name(dep))

        prps = ["INTERFACE" if header_only else "PRIVATE" if shared_library else "PUBLIC"]

        cmake_commands.append(
            struct(
                command = "target_link_libraries",
                name = target_name,
                prps = depset(prps),
                srcs = depset(deps),
            ),
        )

    if hasattr(ctx.rule.attr, "implementation_deps") and ctx.rule.attr.implementation_deps:
        deps = []
        for dep in ctx.rule.attr.implementation_deps:
            deps.append(_bazel_label_to_cmake_target_name(dep))

        prps = ["PRIVATE"]

        cmake_commands.append(
            struct(
                command = "target_link_libraries",
                name = target_name,
                prps = depset(prps),
                srcs = depset(deps),
            ),
        )

    if hasattr(ctx.rule.attr, "includes") and ctx.rule.attr.includes:
        prps = "INTERFACE" if header_only else "PUBLIC"

        includes = []
        for include in ctx.rule.attr.includes:
            includes.append(prps + " __EXEC_ROOT__/" + _target_path(target) + "/" + include)
            if uses_generated_files:
                # If there are generated header files, their directory must be added to the include
                # search path. However it cannot be added unconditionally, because CMake throws an
                # error if an INTERFACE target references a non-existent directory.
                includes.append(prps + " __EXEC_ROOT__/" + ctx.genfiles_dir.path + "/" + _target_path(target) + "/" + include)

        cmake_commands.append(
            struct(
                command = "target_include_directories",
                name = target_name,
                prps = depset([]),
                srcs = depset(includes),
            ),
        )

    if hasattr(ctx.rule.attr, "defines") and ctx.rule.attr.defines:
        prps = ["INTERFACE" if header_only else "PRIVATE" if shared_library else "PUBLIC"]

        defines = []
        for define in ctx.rule.attr.defines:
            defines.append(_remove_define_quotes_and_escape(define))

        cmake_commands.append(
            struct(
                command = "target_compile_definitions",
                name = target_name,
                prps = depset(prps),
                srcs = depset(defines),
            ),
        )

    if hasattr(ctx.rule.attr, "local_defines") and ctx.rule.attr.local_defines:
        prps = ["INTERFACE" if header_only else "PRIVATE"]

        local_defines = []
        for local_define in ctx.rule.attr.local_defines:
            local_defines.append(_remove_define_quotes_and_escape(local_define))

        cmake_commands.append(
            struct(
                command = "target_compile_definitions",
                name = target_name,
                prps = depset(prps),
                srcs = depset(local_defines),
            ),
        )

    compile_opts = ctx.fragments.cpp.copts
    if is_cpp:
        compile_opts += ctx.fragments.cpp.cxxopts

    if hasattr(ctx.rule.attr, "copts") and ctx.rule.attr.copts:
        compile_opts += ctx.rule.attr.copts

    if is_cpp and hasattr(ctx.rule.attr, "cxxopts") and ctx.rule.attr.cxxopts:
        compile_opts += ctx.rule.attr.cxxopts

    if len(compile_opts) > 0:
        prps = ["INTERFACE" if header_only else "PRIVATE"]

        compile_definitions = []
        compile_options = []

        for copt in compile_opts:
            if copt.startswith("-D"):
                compile_definitions.append(_remove_define_quotes_and_escape(copt[2:]))
            else:
                compile_options.append(copt)

        if compile_definitions:
            cmake_commands.append(
                struct(
                    command = "target_compile_definitions",
                    name = target_name,
                    prps = depset(prps),
                    srcs = depset(compile_definitions),
                ),
            )

        if compile_options and not header_only:
            cmake_commands.append(
                struct(
                    command = "target_compile_options",
                    name = target_name,
                    prps = depset(prps),
                    srcs = depset(compile_options),
                ),
            )

    linkopts = ctx.fragments.cpp.linkopts
    if hasattr(ctx.rule.attr, "linkopts") and ctx.rule.attr.linkopts:
        linkopts += ctx.rule.attr.linkopts

    if len(linkopts) > 0:
        prps = ["INTERFACE" if header_only else "PRIVATE" if shared_library else "PUBLIC"]

        libs = []
        opts = []

        for opt in linkopts:
            if opt.startswith("-l"):
                libs.append(opt[2:])
            elif opt.endswith(".lib"):
                libs.append(opt)
            else:
                opts.append(opt)

        cmake_commands.append(
            struct(
                command = "target_link_options",
                name = target_name,
                prps = depset(prps),
                srcs = depset(opts),
            ),
        )

        cmake_commands.append(
            struct(
                command = "target_link_libraries",
                name = target_name,
                prps = depset(prps),
                srcs = depset(libs),
            ),
        )

    transitive_source_files.append(target[CcInfo].compilation_context.headers)

    return [
        CMakeAspectInfo(
            cmake_commands = depset(cmake_commands, transitive = transitive_cmake_commands),
            source_files = depset(srcs, transitive = transitive_source_files),
        ),
    ]

cmakelists_aspect = aspect(
    attr_aspects = ["deps", "implementation_deps"],
    attrs = {
        "_cc_toolchain": attr.label(
            default = Label("@bazel_tools//tools/cpp:current_cc_toolchain"),
        ),
        "_protobuf_runtime": attr.label(
            default = Label("@protobuf//:protobuf"),
        ),
    },
    fragments = ["cpp"],
    required_aspect_providers = [CMakeAspectInfo],
    toolchains = ["@bazel_tools//tools/cpp:toolchain_type"],
    implementation = _cmakelists_aspect_impl,
)

def _cmakelists_impl(ctx):
    # Generates a single CMakeLists.txt file with the
    # transitive depset of the specified targets.

    if ctx.attr.disable:
        ctx.actions.write(output = ctx.outputs.filename, content = "\n")
        return []

    cmake_commands = []
    source_files = []
    for target in ctx.attr.targets:
        cmake_commands.append(target[CMakeAspectInfo].cmake_commands)
        source_files.append(target[CMakeAspectInfo].source_files)

    cmake_commands = depset(transitive = cmake_commands)
    source_files = depset(transitive = source_files)

    content = "cmake_minimum_required(VERSION 3.5)\n" + \
              "project(__PROJ_NAME__)\n" + \
              "set(CMAKE_CXX_STANDARD 17)\n\n" + \
              "#__GLOBAL_OPTIONS__\n\n"
    content += _cmakelists_txt(cmake_commands.to_list())
    content = content.replace("__EXEC_ROOT__", ctx.attr.exec_root)

    ctx.actions.write(output = ctx.outputs.filename, content = content)

    return [
        OutputGroupInfo(
            default = source_files.to_list(),
        ),
    ]

_cmakelists = rule(
    attrs = {
        "targets": attr.label_list(
            aspects = [cmakelists_aspect],
            doc = "List of all cc targets which should be included.",
        ),
        "exec_root": attr.string(
            default = "__EXEC_ROOT__",
            doc = "Execution root of Bazel as returned by 'bazel info execution_root'.",
        ),
        "project_name": attr.string(
            default = "__PROJ_NAME__",
            doc = "Name of the CMake project.",
        ),
        "disable": attr.bool(
            default = False,
            doc = ("Makes this operation a no-op; useful in combination with a 'select' " +
                   "for platforms where the internals of this rule are not properly " +
                   "supported."),
        ),
        "filename": attr.output(
            doc = "Name of the generated CMakeLists file.",
        ),
    },
    implementation = _cmakelists_impl,
)

def cmakelists(**kwargs):
    _cmakelists(
        filename = kwargs.pop("filename", "CMakeLists.txt"),
        **kwargs
    )
