def _glsl_version_info_impl(ctx):
    """Generates a header file containing the GLSL version and profile information from the toolchain."""

    glsl_info = ctx.toolchains["//tools/bazel/glsl:toolchain_type"].glsl_info

    header_content = """#pragma once
#define {prefix}GLSL_VERSION {version}
#define {prefix}GLSL_PROFILE {profile}
""".format(
        prefix = ctx.attr.prefix,
        version = glsl_info.version,
        profile = glsl_info.profile,
    )

    ctx.actions.write(
        output = ctx.outputs.header,
        content = header_content,
    )

    return [DefaultInfo(files = depset([ctx.outputs.header]))]

glsl_version_info_header = rule(
    implementation = _glsl_version_info_impl,
    attrs = {
        "header": attr.output(
            mandatory = True,
        ),
        "prefix": attr.string(),
    },
    toolchains = [
        "//tools/bazel/glsl:toolchain_type",
    ],
)
