# Copyright 2024 The Bazel Authors.
# Copyright 2025 Siradel.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Compilation database generation Bazel rules.

compilation_database will generate a compile_commands.json file for the
given targets. This approach uses the aspects feature of bazel.

An alternative approach is the one used by the kythe project using
(experimental) action listeners.
https://github.com/google/kythe/blob/master/tools/cpp/generate_compilation_database.sh
"""

# This has been extracted from https://github.com/grailbio/bazel-compilation-database/
# Changes make by Siradel:
#   - Remove everything related to Objective-C
#   - Rename CompilationAspect to CompilationAspectInfo
#   - Combine the contents of defs.bzl and aspects.bzl.
#   - Add fix for implementation_deps not working (_combine_with_deps)

load(
    "@bazel_tools//tools/build_defs/cc:action_names.bzl",
    "CPP_COMPILE_ACTION_NAME",
    "C_COMPILE_ACTION_NAME",
)
load("@bazel_tools//tools/cpp:toolchain_utils.bzl", "find_cpp_toolchain")

CompilationAspectInfo = provider(fields = ["compilation_db"])

_cpp_header_extensions = [
    "hh",
    "hxx",
    "ipp",
    "hpp",
]

_c_or_cpp_header_extensions = ["h"] + _cpp_header_extensions

_cpp_extensions = [
    "cc",
    "cpp",
    "cxx",
] + _cpp_header_extensions

_cc_rules = [
    "cc_library",
    "cc_binary",
    "cc_test",
    "cc_inc_library",
    "cc_proto_library",
]

_all_rules = _cc_rules

# Temporary fix for https://github.com/grailbio/bazel-compilation-database/issues/101.
DISABLED_FEATURES = [
    "module_maps",
]

def _is_cpp_target(srcs):
    if all([src.extension in _c_or_cpp_header_extensions for src in srcs]):
        return True  # assume header-only lib is c++
    return any([src.extension in _cpp_extensions for src in srcs])

def _sources(ctx, target):
    srcs = []
    if hasattr(ctx.rule.attr, "srcs"):
        srcs += [f for src in ctx.rule.attr.srcs for f in src.files.to_list()]
    if hasattr(ctx.rule.attr, "hdrs"):
        srcs += [f for src in ctx.rule.attr.hdrs for f in src.files.to_list()]

    return srcs

# From https://github.com/bazelbuild/intellij/pull/5220/files
# This fixes a possible bug in Bazel (nobody has confirmed whether this was intended or not)
# where CcInfo doesn't contain stuff from implementation_deps.
# https://github.com/bazelbuild/bazel/issues/19663
def _combine_with_deps(direct, deps, attr_name):
    """Returns a list from a depset containing direct and all transitive compilation_context.attr_name values from deps"""
    return depset(
        direct,
        transitive = [getattr(dep[CcInfo].compilation_context, attr_name, depset()) for dep in deps],
    ).to_list()

# Function copied from https://gist.github.com/oquenchil/7e2c2bd761aa1341b458cc25608da50c
# TODO: Directly use create_compile_variables and get_memory_inefficient_command_line.
def _get_compile_flags(ctx, dep):
    options = []
    compilation_context = dep[CcInfo].compilation_context

    defines = compilation_context.defines.to_list()
    system_includes = compilation_context.system_includes.to_list()
    includes = compilation_context.includes.to_list()
    quote_includes = compilation_context.quote_includes.to_list()

    if hasattr(ctx.rule.attr, "implementation_deps"):
        impl_deps = ctx.rule.attr.implementation_deps
        defines = _combine_with_deps(defines, impl_deps, "defines")
        system_includes = _combine_with_deps(system_includes, impl_deps, "system_includes")
        includes = _combine_with_deps(includes, impl_deps, "includes")
        quote_includes = _combine_with_deps(quote_includes, impl_deps, "quote_includes")

    for define in defines:
        options.append("-D\"{}\"".format(define))

    for define in compilation_context.local_defines.to_list():
        options.append("-D\"{}\"".format(define))

    for system_include in system_includes:
        if len(system_include) == 0:
            system_include = "."
        options.append("-isystem {}".format(system_include))

    for include in includes:
        if len(include) == 0:
            include = "."
        options.append("-I {}".format(include))

    for quote_include in quote_includes:
        if len(quote_include) == 0:
            quote_include = "."
        options.append("-iquote {}".format(quote_include))

    return options

def _cc_compile_commands(ctx, target, feature_configuration, cc_toolchain):
    compiler = str(
        cc_common.get_tool_for_action(
            feature_configuration = feature_configuration,
            action_name = C_COMPILE_ACTION_NAME,
        ),
    )
    compile_flags = _get_compile_flags(ctx, target)

    srcs = _sources(ctx, target)
    if ctx.rule.kind == "cc_proto_library":
        srcs += [f for f in target.files.to_list() if f.extension in ["h", "cc"]]

    # We currently recognize an entire target as C++ or C. This can probably be
    # made better for targets that have a mix of C and C++ files.
    is_cpp_target = _is_cpp_target(srcs)

    compiler_options = None
    if is_cpp_target:
        compile_variables = cc_common.create_compile_variables(
            feature_configuration = feature_configuration,
            cc_toolchain = cc_toolchain,
            user_compile_flags = ctx.fragments.cpp.cxxopts +
                                 ctx.fragments.cpp.copts,
            add_legacy_cxx_options = True,
        )
        compiler_options = cc_common.get_memory_inefficient_command_line(
            feature_configuration = feature_configuration,
            action_name = CPP_COMPILE_ACTION_NAME,
            variables = compile_variables,
        )
        compile_flags.append("-x c++")  # Force language mode for header files.
    else:
        compile_variables = cc_common.create_compile_variables(
            feature_configuration = feature_configuration,
            cc_toolchain = cc_toolchain,
            user_compile_flags = ctx.fragments.cpp.copts,
        )
        compiler_options = cc_common.get_memory_inefficient_command_line(
            feature_configuration = feature_configuration,
            action_name = C_COMPILE_ACTION_NAME,
            variables = compile_variables,
        )

    compile_flags.extend(ctx.rule.attr.copts if "copts" in dir(ctx.rule.attr) else [])

    cmdline_list = [compiler]
    cmdline_list.extend(compiler_options)
    cmdline_list.extend(compile_flags)
    cmdline = " ".join(cmdline_list)

    compile_commands = []
    for src in srcs:
        compile_commands.append(struct(
            cmdline = cmdline + " -c " + src.path,
            src = src,
        ))
    return compile_commands

def _compilation_database_aspect_impl(target, ctx):
    # Write the compile commands for this target to a file, and return
    # the commands for the transitive closure.

    # Collect any aspects from all transitive dependencies.
    # Note that this should also apply to filegroup type targets which may have
    # cc_binary targets in their srcs attribute.
    deps = []
    if hasattr(ctx.rule.attr, "srcs"):
        deps.extend(ctx.rule.attr.srcs)
    if hasattr(ctx.rule.attr, "deps"):
        deps.extend(ctx.rule.attr.deps)
    if hasattr(ctx.rule.attr, "implementation_deps"):
        deps.extend(ctx.rule.attr.implementation_deps)

    transitive_compilation_db = []
    all_compdb_files = []
    all_header_files = []
    for dep in deps:
        if CompilationAspectInfo not in dep:
            continue
        transitive_compilation_db.append(dep[CompilationAspectInfo].compilation_db)
        all_compdb_files.append(dep[OutputGroupInfo].compdb_files)
        all_header_files.append(dep[OutputGroupInfo].header_files)

    if ctx.rule.kind not in _all_rules:
        return [
            CompilationAspectInfo(compilation_db = depset(transitive = transitive_compilation_db)),
            OutputGroupInfo(
                compdb_files = depset(transitive = all_compdb_files),
                header_files = depset(transitive = all_header_files),
                direct_src_files = [],
            ),
        ]

    compilation_db = []

    cc_toolchain = find_cpp_toolchain(ctx)
    feature_configuration = cc_common.configure_features(
        ctx = ctx,
        cc_toolchain = cc_toolchain,
        requested_features = ctx.features,
        unsupported_features = ctx.disabled_features + DISABLED_FEATURES,
    )

    if ctx.rule.kind in _cc_rules:
        compile_commands = _cc_compile_commands(ctx, target, feature_configuration, cc_toolchain)
    else:
        fail("unsupported rule: " + ctx.rule.kind)

    srcs = []
    for compile_command in compile_commands:
        exec_root_marker = "__EXEC_ROOT__"
        compilation_db.append(
            struct(command = compile_command.cmdline, directory = exec_root_marker, file = compile_command.src.path),
        )
        srcs.append(compile_command.src)

    # Write the commands for this target.
    compdb_file = ctx.actions.declare_file(ctx.label.name + ".compile_commands.json")
    ctx.actions.write(
        content = json.encode(compilation_db),
        output = compdb_file,
    )

    compilation_db = depset(compilation_db, transitive = transitive_compilation_db)
    all_compdb_files = depset([compdb_file], transitive = all_compdb_files)
    all_header_files.append(target[CcInfo].compilation_context.headers)

    return [
        CompilationAspectInfo(compilation_db = compilation_db),
        OutputGroupInfo(
            compdb_files = all_compdb_files,
            header_files = depset(transitive = all_header_files),
            # Provide direct src files of this target for people who want to
            # run clang-tidy or similar tools with the compilation database
            # on the source files of this target.
            # See https://github.com/grailbio/bazel-compilation-database/pull/53.
            direct_src_files = srcs,
        ),
    ]

compilation_database_aspect = aspect(
    # Also include srcs in the attribute aspects so people can use filegroup targets.
    # See https://github.com/grailbio/bazel-compilation-database/issues/84.
    attr_aspects = ["srcs", "deps", "implementation_deps"],
    attrs = {
        "_cc_toolchain": attr.label(
            default = Label("@bazel_tools//tools/cpp:current_cc_toolchain"),
        ),
    },
    fragments = ["cpp"],
    provides = [CompilationAspectInfo],
    toolchains = ["@bazel_tools//tools/cpp:toolchain_type"],
    implementation = _compilation_database_aspect_impl,
    apply_to_generating_rules = True,
)

def _compilation_database_impl(ctx):
    # Generates a single compile_commands.json file with the
    # transitive depset of specified targets.

    if ctx.attr.disable:
        ctx.actions.write(output = ctx.outputs.filename, content = "[]\n")
        return

    compilation_db = []
    all_headers = []
    for target in ctx.attr.targets:
        compilation_db.append(target[CompilationAspectInfo].compilation_db)
        all_headers.append(target[OutputGroupInfo].header_files)

    compilation_db = depset(transitive = compilation_db)

    all_headers = depset(transitive = all_headers)

    exec_root = ctx.attr.output_base + "/execroot/" + ctx.workspace_name

    content = compilation_db.to_list()
    if ctx.attr.unique:
        content = list({element.file: element for element in content}.values())
    content = json.encode(content)
    content = content.replace("__EXEC_ROOT__", exec_root)
    ctx.actions.write(output = ctx.outputs.filename, content = content)

    return [
        OutputGroupInfo(
            default = all_headers,
        ),
    ]

_compilation_database = rule(
    attrs = {
        "targets": attr.label_list(
            aspects = [compilation_database_aspect],
            doc = "List of all cc targets which should be included.",
        ),
        "output_base": attr.string(
            default = "__OUTPUT_BASE__",
            doc = ("Output base of Bazel as returned by 'bazel info output_base'. " +
                   "The exec_root is constructed from the output_base as " +
                   "output_base + '/execroot/' + workspace_name. "),
        ),
        "disable": attr.bool(
            default = False,
            doc = ("Makes this operation a no-op; useful in combination with a 'select' " +
                   "for platforms where the internals of this rule are not properly " +
                   "supported."),
        ),
        "unique": attr.bool(
            default = True,
            doc = ("Remove duplicate entries before writing the database, reducing file size " +
                   "and potentially being faster."),
        ),
        "filename": attr.output(
            doc = "Name of the generated compilation database.",
        ),
    },
    implementation = _compilation_database_impl,
)

def compilation_database(**kwargs):
    _compilation_database(
        filename = kwargs.pop("filename", "compile_commands.json"),
        **kwargs
    )
