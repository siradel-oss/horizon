# SPDX-FileCopyrightText: Copyright 2026 Siradel
# SPDX-License-Identifier: MIT

def _http_file_repo_impl(ctx):
    ctx.file("BUILD.bazel", """exports_files(["%s"])""" % ctx.attr.file_name)
    ctx.download(
        url = ctx.attr.url,
        output = ctx.attr.file_name,
        integrity = ctx.attr.integrity,
        executable = ctx.attr.executable,
    )

_http_file_repo = repository_rule(
    implementation = _http_file_repo_impl,
    doc = "A repository rule to fetch a file from a URL.",
    attrs = {
        "file_name": attr.string(mandatory = True, doc = "The name of the file to be used in the build."),
        "url": attr.string(mandatory = True, doc = "The URL to fetch the file from."),
        "integrity": attr.string(mandatory = True, doc = "The integrity hash of the file, used for verification."),
        "executable": attr.bool(default = False, doc = "Whether the downloaded file should be marked as executable."),
    },
)

_file = tag_class(
    attrs = {
        "name": attr.string(mandatory = True, doc = "The name of the generated repository."),
        "file_name": attr.string(mandatory = True, doc = "The name of the file to be used in the build."),
        "url": attr.string(mandatory = True, doc = "The URL to fetch the file from."),
        "integrity": attr.string(mandatory = True, doc = "The integrity hash of the file, used for verification."),
        "executable": attr.bool(default = False, doc = "Whether the downloaded file should be marked as executable."),
    },
)

def _http_file_impl(ctx):
    for mod in ctx.modules:
        for file in mod.tags.file:
            _http_file_repo(
                name = file.name,
                file_name = file.file_name,
                url = file.url,
                integrity = file.integrity,
                executable = file.executable,
            )

http_download = module_extension(
    implementation = _http_file_impl,
    doc = "An extension to fetch files from a URL.",
    tag_classes = {
        "file": _file,
    },
)
