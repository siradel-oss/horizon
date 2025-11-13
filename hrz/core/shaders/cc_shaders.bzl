load("//tools/bazel/glsl:providers.bzl", "GlslProgramInfo")

def _create_cpp_file_from_shader(ctx, ns, name, stage, f):
    cpp_file = ctx.actions.declare_file("src/%s_%s.cpp" % (name, stage))

    args = ctx.actions.args()
    args.add(ns)
    args.add(name)
    args.add(stage)
    args.add(f)
    args.add(cpp_file)

    ctx.actions.run(
        outputs = [cpp_file],
        inputs = [f],
        executable = ctx.executable._make_cpp,
        arguments = [args],
    )

    return cpp_file

def _cc_shaders_collection_impl(ctx):
    header_file = ctx.actions.declare_file(ctx.label.name + ".h")
    source_file = ctx.actions.declare_file(ctx.label.name + ".cpp")
    source_files = [source_file]
    header_files = [header_file]

    header_content = "#pragma once\n"
    header_content += "#include <stddef.h>\n"
    header_content += "namespace %s {\n" % ctx.attr.namespace
    header_content += "void decompress_shaders();\n"

    for p in ctx.attr.programs:
        program = p[GlslProgramInfo]
        vert_cpp = _create_cpp_file_from_shader(ctx, ctx.attr.namespace, program.name, "vert", program.vert)
        frag_cpp = _create_cpp_file_from_shader(ctx, ctx.attr.namespace, program.name, "frag", program.frag)
        source_files += [vert_cpp, frag_cpp]
        header_content += "extern const char* %s_name;\n" % program.name
        header_content += "extern size_t %s_vert_len;\n" % program.name
        header_content += "extern size_t %s_frag_len;\n" % program.name
        header_content += "extern const char* %s_vert;\n" % program.name
        header_content += "extern const char* %s_frag;\n" % program.name

    header_content += "}\n"

    source_content = "#include \"" + ctx.label.package + "/" + ctx.label.name + ".h\"\n"
    source_content += "#include \"hrz/core/shaders/decompressor.h\"\n"
    source_content += "namespace " + ctx.attr.namespace + " {\n"
    for p in ctx.attr.programs:
        name = p[GlslProgramInfo].name
        source_content += "const char* %s_name = \"%s\";\n" % (name, name)
        source_content += "extern const size_t %s_vert_compressed_len;\n" % name
        source_content += "extern const size_t %s_frag_compressed_len;\n" % name
        source_content += "extern const unsigned char* %s_vert_compressed;\n" % name
        source_content += "extern const unsigned char* %s_frag_compressed;\n" % name
    source_content += "void decompress_shaders() {\n"
    for p in ctx.attr.programs:
        name = p[GlslProgramInfo].name
        for part in ["vert", "frag"]:
            source_content += "{\n"
            source_content += "auto decompressed = hrz::shaders_decompressor::decompress((const char*)%s_%s_compressed, %s_%s_compressed_len);\n" % (name, part, name, part)
            source_content += "%s_%s_len = decompressed.second;\n" % (name, part)
            source_content += "%s_%s = decompressed.first.release();\n" % (name, part)
            source_content += "}\n"

    source_content += "}}\n"

    ctx.actions.write(
        output = header_file,
        content = header_content,
    )

    ctx.actions.write(
        output = source_file,
        content = source_content,
    )

    return [
        DefaultInfo(files = depset(source_files + header_files)),
        OutputGroupInfo(
            srcs = source_files,
            hdrs = header_files,
        ),
    ]

cc_shaders_collection = rule(
    implementation = _cc_shaders_collection_impl,
    attrs = {
        "namespace": attr.string(mandatory = True),
        "programs": attr.label_list(
            providers = [
                GlslProgramInfo,
            ],
        ),
        "_make_cpp": attr.label(
            default = "//hrz/core/shaders:generate_shader_source",
            executable = True,
            cfg = "exec",
        ),
    },
)
