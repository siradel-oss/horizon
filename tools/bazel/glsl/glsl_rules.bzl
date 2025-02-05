load(":providers.bzl", "GlslConfigInfo", "GlslProgramInfo")

def _glsl_config_impl(ctx):
    return [GlslConfigInfo(
        defines = ctx.attr.defines,
        includes = ctx.attr.includes,
        common = ctx.attr.common_files,
    )]

glsl_config = rule(
    implementation = _glsl_config_impl,
    attrs = {
        "defines": attr.string_list(),
        "includes": attr.label_list(
            allow_files = True,
        ),
        "common_files": attr.label_list(
            allow_files = True,
        ),
    },
)

def _declare_glsl_file(ctx, spirv):
    tokens = spirv.basename.split(".")
    file_name = ctx.label.name + ".compiled." + tokens[-1]
    return ctx.actions.declare_file(file_name)

def _declare_tmp_file(ctx, f, name):
    return ctx.actions.declare_file(
        f.basename + "." + name,
        sibling = f,
    )

def _assemble_shader(ctx, input, stage):
    output = _declare_glsl_file(ctx, input)
    config = ctx.attr.config[GlslConfigInfo]
    toolchain = ctx.toolchains["//tools/bazel/glsl:toolchain_type"].glsl_info

    include_dirs = [f.path for i in config.includes for f in i.files.to_list()]
    common_files = [f for c in config.common for f in c.files.to_list()]

    input_check = _declare_tmp_file(ctx, output, "precheck")

    args = ctx.actions.args()
    args.add(input)
    args.add(input_check)
    args.add(toolchain.check_version)
    args.add(toolchain.profile)

    for d in include_dirs:
        args.add("-I" + d)
        args.add("-I" + ctx.bin_dir.path + "/" + d)
    for d in config.defines:
        args.add("-D" + d)
    for d in ctx.attr.defines:
        args.add("-D" + d)

    ctx.actions.run(
        outputs = [input_check],
        inputs = [input] + common_files,
        executable = ctx.executable._shader_assembler,
        arguments = [args],
    )

    args = ctx.actions.args()
    args.add(input_check)
    args.add(output)
    args.add(stage)
    args.add(toolchain.check_version)
    args.add(toolchain.validator)
    args.add(ctx.executable._compressor)

    ctx.actions.run(
        outputs = [output],
        inputs = [input_check],
        executable = ctx.executable._shader_validator,
        arguments = [args],
        tools = [
            toolchain.validator,
            ctx.executable._compressor,
        ],
    )

    return output

def _glsl_program_impl(ctx):
    vert_compiled = _assemble_shader(ctx, ctx.file.vert, "vert")
    frag_compiled = _assemble_shader(ctx, ctx.file.frag, "frag")

    return [
        DefaultInfo(
            files = depset([
                vert_compiled,
                frag_compiled,
            ]),
        ),
        GlslProgramInfo(
            name = ctx.label.name,
            vert = vert_compiled,
            frag = frag_compiled,
        ),
    ]

glsl_program = rule(
    implementation = _glsl_program_impl,
    attrs = {
        "config": attr.label(
            mandatory = True,
            providers = [
                GlslConfigInfo,
            ],
        ),
        "vert": attr.label(
            mandatory = True,
            allow_single_file = True,
        ),
        "frag": attr.label(
            mandatory = True,
            allow_single_file = True,
        ),
        "defines": attr.string_list(),
        "_shader_assembler": attr.label(
            default = "//tools/bazel/glsl:shader_assembler",
            executable = True,
            cfg = "exec",
        ),
        "_shader_validator": attr.label(
            default = "//tools/bazel/glsl:shader_validator",
            executable = True,
            cfg = "exec",
        ),
        "_compressor": attr.label(
            default = "//tools/compress_file_lz4",
            executable = True,
            cfg = "exec",
        ),
    },
    toolchains = ["//tools/bazel/glsl:toolchain_type"],
)
