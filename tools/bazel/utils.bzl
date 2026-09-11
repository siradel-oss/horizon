# SPDX-FileCopyrightText: Copyright 2019 Siradel
# SPDX-License-Identifier: MIT

def merge_dicts(dict1, dict2):
    """
    Merges two dictionaries, up to one level deep.
    """

    if not dict1:
        return dict2
    if not dict2:
        return dict1

    result = {}

    for key, value in dict1.items():
        result[key] = value

    for key, value in dict2.items():
        if key in result and type(result[key]) == "dict" and type(value) == "dict":
            merged_nested = result[key] | value
            result[key] = merged_nested
        else:
            result[key] = value

    return result

def _dbg_transition(settings, attr):
    return {"//command_line_option:compilation_mode": "dbg"}

dbg_transition = transition(
    implementation = _dbg_transition,
    inputs = [],
    outputs = ["//command_line_option:compilation_mode"],
)

def _make_dbg_impl(ctx):
    return [
        DefaultInfo(files = depset(ctx.files.cc_target)),
    ]

make_dbg = rule(
    implementation = _make_dbg_impl,
    attrs = {
        "cc_target": attr.label(
            cfg = dbg_transition,
            mandatory = True,
        ),
        "_allowlist_function_transition": attr.label(
            default = "@bazel_tools//tools/allowlists/function_transition_allowlist",
        ),
    },
)
