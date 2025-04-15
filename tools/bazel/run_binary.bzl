def _make_run_script_impl(ctx):
    is_windows = ctx.target_platform_has_constraint(ctx.attr._windows_constraint[platform_common.ConstraintValueInfo])

    file_name = ctx.label.name + (".bat" if is_windows else ".sh")
    file = ctx.actions.declare_file(file_name)

    exec_path = ctx.executable.executable.short_path
    if is_windows:
        exec_path = exec_path.replace("/", "\\")

    command = [exec_path] + ctx.attr.arguments
    script_content = ""

    # The child executable requires its own runfiles, but we are already in the runfiles
    # of the bat/sh script. We need to set the RUNFILES_DIR variable to point to the root
    # of the bat/sh script's runfiles so that the Python launcher uses these runfiles, and does
    # not try to find its runfiles directory in the parent's executable runfiles.
    if is_windows:
        script_content += "@set RUNFILES_DIR=..\n"
    else:
        script_content += "export RUNFILES_DIR=..\n"

    script_content += " ".join(command)
    ctx.actions.write(file, script_content, is_executable = True)

    runfiles = ctx.runfiles(files = ctx.files.data)
    for runfile_attr in (ctx.attr.data, [ctx.attr.executable]):
        for target in runfile_attr:
            runfiles = runfiles.merge(target[DefaultInfo].default_runfiles)

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
    executable = True,
)
