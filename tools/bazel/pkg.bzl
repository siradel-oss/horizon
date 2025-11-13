load("@rules_pkg//pkg:mappings.bzl", "pkg_files")
load("@rules_pkg//pkg:tar.bzl", "pkg_tar")
load(":os_info.bzl", "OsInfo")

def make_dirs_structure(files, strip_prefix, add_prefix):
    """
    Args:
        files: list of files paths as strings
        strip_prefix: string to strip from the beginning of each file path
        add_prefix: string to add at the beginning of each file path
    Returns:
        A dict mapping from folder paths to list of file paths.
    """
    dirs = {}
    for f in files:
        if not f.startswith(strip_prefix):
            fail("File '%s' does not start with prefix '%s'" % (f, strip_prefix))
        relative_path = f[len(strip_prefix):]
        folder = add_prefix + relative_path.rsplit("/", 1)[0] if "/" in relative_path else add_prefix
        if folder not in dirs:
            dirs[folder] = []
        dirs[folder].append(f)
    return dirs

def pkg_tar_aggregate(name, extension = "tar.gz", dirs = {}, visibility = ["//visibility:public"]):
    """
    dirs has this structure:
    {
        "folder": ["label1", "label2, ...],
        ...
    }
    """
    deps = []
    for d in dirs:
        pkg_name = name + "_" + d.replace("/", "_")
        deps.append(pkg_name)

        pkg_files(
            name = pkg_name,
            prefix = d,
            srcs = dirs[d],
        )
    pkg_tar(
        name = name,
        extension = extension,
        srcs = deps,
        visibility = visibility,
    )

def _structured_files_copy_impl(ctx):
    root = ctx.attr.output_dir.rstrip("/") + "/"
    if root == "/":
        root = ""

    files = []
    is_windows = ctx.attr._exec_os_info[OsInfo].is_windows

    for dep in ctx.attr.files:
        path_on_server = ctx.attr.files[dep]

        for f in dep.files.to_list():
            copied_file = ctx.actions.declare_file(root + path_on_server + "/" + f.basename)
            files.append(copied_file)
            if is_windows:
                ctx.actions.run(
                    outputs = [copied_file],
                    inputs = [f],
                    executable = "cmd.exe",
                    arguments = [
                        "/c",
                        "echo",
                        "F",
                        "|",
                        "@xcopy",
                        "/y",
                        "/q",
                        f.path.replace("/", "\\"),
                        copied_file.path.replace("/", "\\"),
                        ">NUL",
                    ],
                    use_default_shell_env = True,
                )
            else:
                ctx.actions.run_shell(
                    outputs = [copied_file],
                    inputs = [f],
                    command = "mkdir -p %s; cp %s %s" % (
                        copied_file.dirname,
                        f.path,
                        copied_file.path,
                    ),
                    use_default_shell_env = True,
                )

    return [
        DefaultInfo(
            files = depset(files),
            runfiles = ctx.runfiles(files = files),
        ),
    ]

_structured_files_copy = rule(
    implementation = _structured_files_copy_impl,
    attrs = {
        "files": attr.label_keyed_string_dict(
            allow_files = True,
        ),
        "output_dir": attr.string(default = ""),
        "_exec_os_info": attr.label(
            default = Label(":os_info"),
            cfg = "exec",
        ),
    },
)

"""
files has this structure:
{
    "folder": ["label1", "label2, ...],
    ...
}
"""

def structured_files_copy(name, files, output_dir):
    # We transform "files" into a structure like
    # {
    #    "label": "folder",
    # }
    files2 = {}
    for folder in files:
        for label in files[folder]:
            files2[label] = folder
    _structured_files_copy(
        name = name,
        files = files2,
        output_dir = output_dir,
    )

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
            default = Label("//scripts:untar"),
            executable = True,
            cfg = "exec",
        ),
    },
)
