load("@rules_pkg//pkg:providers.bzl", "PackageFilegroupInfo", "PackageFilesInfo")

def _pkg_make_copy_manifest_entry(prefix, dest, src):
    if src.is_directory:
        return ["d", src.path, prefix + dest]
    else:
        return ["f", src.path, prefix + dest]

def _pkg_copy_impl(ctx):
    dst_dir = ctx.actions.declare_directory(ctx.attr.out_dir)
    prefix = dst_dir.path + "/"

    # Each entry is : ['f' | 'd', src_path, dest_path]
    # where 'f' means file and 'd' means directory.
    copy_manifest = []
    input_files = []

    for src in ctx.attr.srcs:
        if PackageFilegroupInfo in src:
            for (pkg_files, _) in src[PackageFilegroupInfo].pkg_files:
                for (dest, src) in pkg_files.dest_src_map.items():
                    input_files.append(src)
                    copy_manifest.append(_pkg_make_copy_manifest_entry(prefix, dest, src))
        else:
            for (dest, src) in src[PackageFilesInfo].dest_src_map.items():
                input_files.append(src)
                copy_manifest.append(_pkg_make_copy_manifest_entry(prefix, dest, src))

    manifest_file = ctx.actions.declare_file(ctx.label.name + "_copy_manifest.json")
    ctx.actions.write(
        output = manifest_file,
        content = json.encode(copy_manifest),
    )

    ctx.actions.run(
        outputs = [dst_dir],
        inputs = [manifest_file] + input_files,
        executable = ctx.executable._pkg_copy_bin,
        arguments = [
            manifest_file.path,
        ],
    )

    return [
        DefaultInfo(
            files = depset([dst_dir]),
            runfiles = ctx.runfiles(files = [dst_dir]),
        ),
    ]

pkg_copy = rule(
    implementation = _pkg_copy_impl,
    attrs = {
        "srcs": attr.label_list(
            allow_files = True,
            providers = [
                [PackageFilegroupInfo, DefaultInfo],
                [PackageFilesInfo, DefaultInfo],
            ],
        ),
        "out_dir": attr.string(mandatory = True),
        "_pkg_copy_bin": attr.label(
            default = Label("//tools/bazel:pkg_copy"),
            executable = True,
            cfg = "exec",
        ),
    },
)
