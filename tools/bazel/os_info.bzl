# This is based on the implementation of Windows host detection
# in bazel-skylib.
# https://github.com/bazelbuild/bazel-skylib/blob/56a2abbaf131332835ab2721a258ea3c763a7178/rules/private/copy_file_private.bzl#L15

OsInfo = provider(fields = ["is_windows"])

def _impl(ctx):
    constraint = ctx.attr._os_windows_constraint[platform_common.ConstraintValueInfo]
    return OsInfo(
        is_windows = ctx.target_platform_has_constraint(constraint),
    )

os_info = rule(
    implementation = _impl,
    attrs = {
        "_os_windows_constraint": attr.label(default = "@platforms//os:windows"),
    },
)
