load("//:version.bzl", "HRZ_VERSION")

# Used to identify the version numbers that are managed by
# the build system of this source repository. Ultimately used
# to add date qualifiers to snapshot version numbers, so that
# npm considers them to be different one to another.
PACKAGE_JSON_HRZ_VERSION = "HRZ_VERSION(" + HRZ_VERSION + ")"

PACKAGE_JSON_HRZ_LICENSE = "Copyright (c) 2018 Siradel"

def _package_json_impl(ctx):
    # Append date to package.json version for snapshot builds.
    # This prevents npm from caching outdated versions.
    ctx.actions.run(
        executable = ctx.executable._version_script,
        inputs = [ctx.file.base, ctx.file._build_date],
        outputs = [ctx.outputs.out],
        arguments = [
            ctx.file.base.path,
            ctx.attr.version,
            ctx.attr.license,
            ctx.file._build_date.path,
            ctx.var["COMPILATION_MODE"],
            ctx.outputs.out.path,
        ],
    )

package_json = rule(
    implementation = _package_json_impl,
    attrs = {
        "version": attr.string(mandatory = True),
        "license": attr.string(),
        "base": attr.label(allow_single_file = True, mandatory = True),
        "out": attr.output(mandatory = True),
        "_build_date": attr.label(
            default = Label("//:build_date.txt"),
            allow_single_file = True,
        ),
        "_version_script": attr.label(
            default = Label("//tools/bazel:qualify_package_json_version"),
            executable = True,
            cfg = "exec",
        ),
    },
)
