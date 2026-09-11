# SPDX-FileCopyrightText: Copyright 2025 Siradel
# SPDX-License-Identifier: MIT

load(":providers.bzl", "GlslInfo")

def _glsl_toolchain_impl(ctx):
    toolchain_info = platform_common.ToolchainInfo(
        glsl_info = GlslInfo(
            version = ctx.attr.version,
            check_version = ctx.attr.check_version or ctx.attr.version,
            profile = ctx.attr.profile,
        ),
    )
    return [toolchain_info]

_VALID_VERSIONS = [
    "140",
    "150",
    "300",
    "310",
    "330",
    "450",
]

glsl_toolchain = rule(
    implementation = _glsl_toolchain_impl,
    attrs = {
        "version": attr.string(
            mandatory = True,
            values = _VALID_VERSIONS,
        ),
        "check_version": attr.string(
            values = _VALID_VERSIONS,
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
