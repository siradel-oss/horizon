def _impl(repo_ctx):
    src = repo_ctx.attr.src
    src_path = repo_ctx.workspace_root.get_child(src.package).get_child(src.name)
    if src_path.exists:
        repo_ctx.extract(repo_ctx.attr.src)
    else:
        print("Prebuilt package not found: %s. Now trying mirrored versions." % repo_ctx.name)
        print("If fetching the mirror also fails, please use the fetch_or_build_prebuilt_deps.py script.")

        if not repo_ctx.attr.mirror_url:
            fail("No mirror URL provided for %s." % repo_ctx.name)

        repo_ctx.download_and_extract(
            url = repo_ctx.attr.mirror_url,
            sha256 = repo_ctx.attr.mirror_digest,
        )

    if repo_ctx.attr.build_file:
        repo_ctx.file(
            "BUILD.bazel",
            content = repo_ctx.read(repo_ctx.attr.build_file),
        )

local_archive = repository_rule(
    implementation = _impl,
    attrs = {
        "src": attr.label(allow_single_file = True, mandatory = True),
        "mirror_url": attr.string(mandatory = False),
        "mirror_digest": attr.string(mandatory = False),
        "build_file": attr.label(allow_single_file = True),
    },
)
