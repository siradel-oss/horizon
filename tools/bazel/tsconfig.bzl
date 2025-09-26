def _tsconfig_util_impl(rctx):
    tsconfig_content = rctx.read(rctx.attr.tsconfig)
    tsconfig_dict = json.decode(tsconfig_content)
    tsconfig_str = repr(tsconfig_dict)

    rctx.file("BUILD.bazel", "")
    rctx.file("defs.bzl", "TSCONFIG = %s" % tsconfig_str)

tsconfig_util = repository_rule(
    implementation = _tsconfig_util_impl,
    attrs = {
        "tsconfig": attr.label(mandatory = True, allow_single_file = True),
    },
    local = True,
)
