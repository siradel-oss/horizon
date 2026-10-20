# SPDX-FileCopyrightText: Copyright 2025 Siradel
# SPDX-License-Identifier: MIT

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

def _assemble_shader(ctx, input, stage):
    config = ctx.attr.config[GlslConfigInfo]
    glsl_info = ctx.toolchains["//tools/bazel/glsl:toolchain_type"].glsl_info
    glslang = ctx.toolchains["@glslang_prebuilt//glslang:toolchain_type"].tool

    shader_name = ctx.label.name

    output = ctx.actions.declare_file("{}.compiled.{}".format(shader_name, stage))
    output_compressed = ctx.actions.declare_file("{}.compiled.{}.lz4".format(shader_name, stage))
    validation = ctx.actions.declare_file("{}.validation.{}".format(shader_name, stage))

    include_dirs = [f.path for i in config.includes for f in i.files.to_list()]
    common_files = [f for c in config.common for f in c.files.to_list()]

    # Compile shader
    args = ctx.actions.args()
    args.add(input)
    args.add(output)
    args.add(glsl_info.check_version)
    args.add(glsl_info.profile)

    for d in include_dirs:
        args.add("-I" + d)
        args.add("-I" + ctx.bin_dir.path + "/" + d)
    for d in config.defines:
        args.add("-D" + d)
    for d in ctx.attr.defines:
        args.add("-D" + d)

    ctx.actions.run(
        mnemonic = "CompileShader",
        outputs = [output],
        inputs = [input] + common_files,
        executable = ctx.executable._assembler,
        arguments = [args],
    )

    # Compress shader
    args = ctx.actions.args()
    args.add(output)
    args.add(output_compressed)
    ctx.actions.run(
        mnemonic = "CompressShader",
        outputs = [output_compressed],
        inputs = [output],
        executable = ctx.executable._compressor,
        arguments = [args],
    )

    # Validate shader
    args = ctx.actions.args()
    args.add(output)
    args.add(validation)
    args.add(stage)
    args.add(glsl_info.check_version)
    args.add(glslang)

    ctx.actions.run(
        mnemonic = "ValidateShader",
        outputs = [validation],
        inputs = [output],
        executable = ctx.executable._validator,
        arguments = [args],
        tools = [
            glslang,
        ],
    )

    return output_compressed, validation

def _glsl_program_impl(ctx):
    vert_compiled, vert_validation = _assemble_shader(ctx, ctx.file.vert, "vert")
    frag_compiled, frag_validation = _assemble_shader(ctx, ctx.file.frag, "frag")

    return [
        DefaultInfo(
            files = depset([
                vert_compiled,
                frag_compiled,
            ]),
        ),
        OutputGroupInfo(
            _validation = depset([
                vert_validation,
                frag_validation,
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
        "_assembler": attr.label(
            default = "//tools/bazel/glsl:shader_assembler",
            executable = True,
            cfg = "exec",
        ),
        "_validator": attr.label(
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
    toolchains = [
        "//tools/bazel/glsl:toolchain_type",
        "@glslang_prebuilt//glslang:toolchain_type",
    ],
)
