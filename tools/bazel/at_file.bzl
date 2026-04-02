"""
This rule generates an "@ file" containing a list of arguments,
each corresponding to a file in the "srcs" attribute.
The order is preserved.
"""

def _impl(ctx):
    args = [f.path for f in ctx.files.srcs]

    content = "\n".join(args)

    file = ctx.actions.declare_file(ctx.label.name)
    ctx.actions.write(file, content)

    return [DefaultInfo(files = depset([file]))]

at_file = rule(
    implementation = _impl,
    attrs = {
        "srcs": attr.label_list(allow_files = True),
    },
)
