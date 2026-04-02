load("@rules_cc//cc:cc_library.bzl", "cc_library")

def _cc_resources_gen_impl(ctx):
    generated_srcs_files = []
    generated_hdrs_files = []

    for (input, key) in ctx.attr.files.items():
        input_file = input.files.to_list()[0]
        output_file = ctx.actions.declare_file("%s_%s.cpp" % (ctx.label.name, key))
        generated_srcs_files.append(output_file)

        ctx.actions.run(
            outputs = [output_file],
            inputs = [input_file],
            executable = ctx.executable._make_resource_source,
            arguments = [key, input_file.path, "cpp", output_file.path],
        )

    interface_cc_file = ctx.actions.declare_file("%s.cpp" % ctx.label.name)
    interface_h_file = ctx.outputs.header
    generated_srcs_files.append(interface_cc_file)
    generated_hdrs_files.append(interface_h_file)

    names = [k for k in ctx.attr.files.values()]

    ctx.actions.run(
        outputs = [interface_cc_file, interface_h_file],
        inputs = [],
        executable = ctx.executable._make_collection,
        arguments = [interface_cc_file.path, interface_h_file.path, ctx.attr.namespace] + names,
    )

    return [
        DefaultInfo(files = depset(generated_srcs_files + generated_hdrs_files)),
        OutputGroupInfo(
            srcs = depset(generated_srcs_files),
            hdrs = depset(generated_hdrs_files),
        ),
    ]

_cc_resources_gen = rule(
    implementation = _cc_resources_gen_impl,
    attrs = {
        "namespace": attr.string(mandatory = True),
        "files": attr.label_keyed_string_dict(allow_files = True),
        "header": attr.output(mandatory = True),
        "_make_resource_source": attr.label(
            default = Label("//tools/bazel/cc_resources:make_resource_source"),
            cfg = "exec",
            executable = True,
        ),
        "_make_collection": attr.label(
            default = Label("//tools/bazel/cc_resources:make_collection"),
            cfg = "exec",
            executable = True,
        ),
    },
)

def cc_resources(name, namespace, header_name, files, **kwargs):
    _cc_resources_gen(
        name = name + "_gen",
        namespace = namespace,
        files = files,
        header = header_name,
    )

    native.filegroup(
        name = name + "_gen_srcs",
        srcs = [":" + name + "_gen"],
        output_group = "srcs",
    )

    native.filegroup(
        name = name + "_gen_hdrs",
        srcs = [":" + name + "_gen"],
        output_group = "hdrs",
    )
    cc_library(
        name = name,
        srcs = [":" + name + "_gen_srcs"],
        hdrs = [":" + name + "_gen_hdrs"],
        **kwargs
    )
