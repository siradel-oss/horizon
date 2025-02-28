load("//:version.bzl", "HRZ_VERSION")

def _make_doc_impl(ctx):
    ref_pages_dir = ctx.actions.declare_directory(ctx.attr.ref_pages_tmp_folder)

    pages_list_file = ctx.actions.declare_file(ctx.label.name + "_input_pages_list.json")
    ctx.actions.write(
        output = pages_list_file,
        content = json.encode([f.path for f in ctx.files.pages]),
    )

    page_outputs = [
        ctx.actions.declare_file(f.basename.removesuffix(".md") + ".html")
        for f in ctx.files.pages
    ]

    common_inputs = [
        ctx.file._protocol,
        pages_list_file,
    ] + ctx.files._templates + ctx.files.pages + ctx.files.embedded_files

    ctx.actions.run(
        executable = ctx.executable._generator,
        outputs = [
            ref_pages_dir,
        ],
        inputs = common_inputs,
        arguments = [
            ctx.file._protocol.path,
            "doc_ref_pages",
            ref_pages_dir.path,
            HRZ_VERSION,
            pages_list_file.path,
        ],
    )

    ctx.actions.run(
        executable = ctx.executable._generator,
        outputs = [
            ctx.outputs.search_index_js,
        ],
        inputs = common_inputs,
        arguments = [
            ctx.file._protocol.path,
            "doc_search_index_script",
            ctx.outputs.search_index_js.dirname,
            HRZ_VERSION,
            pages_list_file.path,
            ctx.outputs.search_index_js.basename,
        ],
    )

    ctx.actions.run(
        executable = ctx.executable._generator,
        outputs = page_outputs,
        inputs = common_inputs,
        arguments = [
            ctx.file._protocol.path,
            "doc_pages",
            page_outputs[0].dirname,
            HRZ_VERSION,
            pages_list_file.path,
        ],
    )

    return [
        OutputGroupInfo(
            doc_pages = page_outputs,
            ref_pages = [ref_pages_dir],
        ),
    ]

make_doc = rule(
    implementation = _make_doc_impl,
    attrs = {
        "ref_pages_tmp_folder": attr.string(mandatory = True),
        "search_index_js": attr.output(
            mandatory = True,
        ),
        "pages": attr.label_list(
            allow_files = True,
        ),
        "embedded_files": attr.label_list(
            allow_files = True,
        ),
        "_protocol": attr.label(
            default = Label("//hrz:hrz_protocol.xml"),
            allow_single_file = True,
        ),
        "_templates": attr.label(
            default = Label("//hrz/generators:documentation_tpl"),
        ),
        "_generator": attr.label(
            default = Label("//hrz/generators:generator"),
            executable = True,
            cfg = "exec",
        ),
    },
)
