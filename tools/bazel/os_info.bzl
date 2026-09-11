# SPDX-FileCopyrightText: Copyright 2025 Siradel
# SPDX-License-Identifier: MIT

OsInfo = provider(fields = ["is_windows"])

def _impl(ctx):
    return OsInfo(
        is_windows = ctx.attr.is_windows,
    )

os_info = rule(
    implementation = _impl,
    attrs = {
        "is_windows": attr.bool(mandatory = True),
    },
)
