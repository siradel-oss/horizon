# SPDX-FileCopyrightText: Copyright 2022 Siradel
# SPDX-License-Identifier: MIT

def _impl(ctx):
    csv = [[field for field in line.split(",")] for line in ctx.read(ctx.attr.manifest).splitlines()]
    ids = [row[0] for row in csv]
    content = ctx.attr.var_name + " = [" + ", ".join(ids) + "]"
    ctx.file("list.bzl", content)
    ctx.file("BUILD.bazel", "exports_files([\"list.bzl\"])")

make_versions_list = repository_rule(
    implementation = _impl,
    attrs = {
        "var_name": attr.string(mandatory = True),
        "manifest": attr.label(
            allow_single_file = True,
            mandatory = True,
        ),
    },
)
