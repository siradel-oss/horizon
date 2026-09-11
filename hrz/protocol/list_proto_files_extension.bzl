# SPDX-FileCopyrightText: Copyright 2025 Siradel
# SPDX-License-Identifier: MIT

def _list_repo_files_repo_impl(rctx):
    base = rctx.path(Label("@horizon//hrz/protocol:BUILD.bazel")).dirname
    base_str = str(base)
    files = []

    # No recursion in Starlark, so...
    # We could also execute a script to do that.
    root_files = base.readdir(watch = "yes")
    for f in root_files:
        if f.is_dir:
            subdir_files = f.readdir(watch = "yes")
            for sf in subdir_files:
                if sf.is_dir:
                    subsubdir_files = sf.readdir(watch = "yes")
                    for ssf in subsubdir_files:
                        if ssf.is_dir:
                            fail("Unexpected directory depth in proto files")
                        else:
                            files.append(ssf)
                else:
                    files.append(sf)
        else:
            files.append(f)

    proto_basename_list = []
    for f in files:
        if not f.basename.endswith(".proto"):
            continue
        f = str(f)[len(base_str) + 1:-6]
        proto_basename_list.append(f)

    list_file_content = "PROTO_FILES = [\n"
    for f in sorted(proto_basename_list):
        list_file_content += '    "{}",\n'.format(f)
    list_file_content += "]\n"

    rctx.file("list.bzl", list_file_content)
    rctx.file("BUILD.bazel", "exports_files([\"list.bzl\"])\n")

_list_repo_files_repo = repository_rule(
    implementation = _list_repo_files_repo_impl,
    local = True,
)

proto_files_list = module_extension(
    implementation = lambda ctx: _list_repo_files_repo(name = "proto_files_list"),
)
