def _make_run_script_impl(ctx):
    is_windows = ctx.target_platform_has_constraint(ctx.attr._windows_constraint[platform_common.ConstraintValueInfo])

    file_name = ctx.label.name + (".bat" if is_windows else ".sh")
    file = ctx.actions.declare_file(file_name)

    exec_path = ctx.executable.executable.short_path
    if is_windows:
        exec_path = exec_path.replace("/", "\\")

    command = [exec_path] + ctx.attr.arguments
    script_content = " ".join(command)
    ctx.actions.write(file, script_content, is_executable = True)

    transitive_runfiles = []
    for runfile_attr in (ctx.attr.data, [ctx.attr.executable]):
        for target in runfile_attr:
            transitive_runfiles.append(target[DefaultInfo].default_runfiles)

    runfiles = ctx.runfiles(files = ctx.files.data)
    runfiles = runfiles.merge_all(transitive_runfiles)

    return [DefaultInfo(
        executable = file,
        runfiles = runfiles,
    )]

run_binary = rule(
    implementation = _make_run_script_impl,
    attrs = {
        "executable": attr.label(
            allow_files = True,
            executable = True,
            cfg = "exec",
        ),
        "arguments": attr.string_list(),
        "data": attr.label_list(),
        "_windows_constraint": attr.label(default = "@platforms//os:windows"),
    },
    executable= True,
)
