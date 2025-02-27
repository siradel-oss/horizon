def target_alias(name, workspace, has_wasm = True):
    aliases = {
        "//:os_windows": "@%s_windows//:%s" % (workspace, name),
        "//:os_linux": "@%s_linux//:%s" % (workspace, name),
    }

    if has_wasm:
        aliases["//:os_web"] = "@%s_wasm//:%s" % (workspace, name)

    native.alias(
        name = name,
        actual = select(aliases),
    )

def _make_template_impl(ctx):
    ctx.actions.expand_template(
        template = ctx.file.input,
        output = ctx.outputs.output,
        substitutions = ctx.attr.substitutions,
    )

make_template = rule(
    implementation = _make_template_impl,
    attrs = {
        "input": attr.label(
            allow_single_file = True,
            mandatory = True,
        ),
        "output": attr.output(
            mandatory = True,
        ),
        "substitutions": attr.string_dict(),
    },
)

def _dbg_transition(settings, attr):
    return {"//command_line_option:compilation_mode": "dbg"}

dbg_transition = transition(
    implementation = _dbg_transition,
    inputs = [],
    outputs = ["//command_line_option:compilation_mode"],
)

def _make_dbg_impl(ctx):
    return [
        DefaultInfo(files = depset(ctx.files.cc_target)),
    ]

make_dbg = rule(
    implementation = _make_dbg_impl,
    attrs = {
        "cc_target": attr.label(
            cfg = dbg_transition,
            mandatory = True,
        ),
        "_allowlist_function_transition": attr.label(
            default = "@bazel_tools//tools/allowlists/function_transition_allowlist",
        ),
    },
)
