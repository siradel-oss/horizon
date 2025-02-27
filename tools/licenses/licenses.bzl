def _merge_licenses_impl(ctx):
    inputs = []
    output = ctx.outputs.merged_file
    args = ctx.actions.args()
    args.add(output)
    for target, name in ctx.attr.licenses.items():
        for file in target.files.to_list():
            inputs.append(file)
            arg = "%s=%s" % (name, file.path)
            args.add(arg)

    ctx.actions.run(
        inputs = inputs,
        outputs = [output],
        executable = ctx.executable._merge_tool,
        arguments = [args],
    )

merge_third_party_licenses_txt = rule(
    implementation = _merge_licenses_impl,
    attrs = {
        "merged_file": attr.output(mandatory = True),
        "licenses": attr.label_keyed_string_dict(mandatory = True, allow_files = True),
        "_merge_tool": attr.label(
            default = Label("//tools/licenses:merge_third_party_licenses_txt"),
            cfg = "exec",
            executable = True,
        ),
    },
)

merge_third_party_licenses_md = rule(
    implementation = _merge_licenses_impl,
    attrs = {
        "merged_file": attr.output(mandatory = True),
        "licenses": attr.label_keyed_string_dict(mandatory = True, allow_files = True),
        "_merge_tool": attr.label(
            default = Label("//tools/licenses:merge_third_party_licenses_md"),
            cfg = "exec",
            executable = True,
        ),
    },
)
