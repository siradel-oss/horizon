load(":providers.bzl", "GlslInfo")

def _glsl_toolchain_impl(ctx):
    toolchain_info = platform_common.ToolchainInfo(
        glsl_info = GlslInfo(
            check_version = ctx.attr.check_version,
            profile = ctx.attr.profile,
        ),
    )
    return [toolchain_info]

glsl_toolchain = rule(
    implementation = _glsl_toolchain_impl,
    attrs = {
        "check_version": attr.string(
            mandatory = True,
            values = [
                "140",
                "150",
                "300",
                "310",
                "330",
                "450",
            ],
        ),
        "profile": attr.string(
            mandatory = True,
            values = [
                "es",
                "core",
            ],
        ),
    },
)
