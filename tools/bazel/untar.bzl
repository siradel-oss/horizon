def _untar_impl(ctx):
    output_dir = ctx.actions.declare_directory(ctx.attr.output_dir)

    ctx.actions.run(
        executable = ctx.executable._untar,
        inputs = [ctx.file.tar],
        outputs = [output_dir],
        arguments = [
            ctx.file.tar.path,
            output_dir.path,
        ],
    )

    return [
        DefaultInfo(
            files = depset([output_dir]),
        ),
    ]

untar = rule(
    implementation = _untar_impl,
    attrs = {
        "output_dir": attr.string(mandatory = True),
        "tar": attr.label(mandatory = True, allow_single_file = True),
        "_untar": attr.label(
            default = Label("//tools/bazel:untar"),
            executable = True,
            cfg = "exec",
        ),
    },
)
