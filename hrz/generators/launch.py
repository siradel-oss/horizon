import sys
import markdown
from os import path
from pathlib import Path
import io
import re
import json

from hrz.generators import protocol_parser
from hrz.generators.common import prepare_env, output_template, prepare_api_tpl_data

generators = {}

def generator(name):
    def generator_inner(func):
        generators[name] = func
        return func
    return generator_inner

@generator("cpp_protocol")
def cpp_protocol_generator(protocol, tpl_env, output_dir, extra):
    tpl = tpl_env.get_template("cpp_protocol/services.tpl.h")
    output_template(protocol, tpl, output_dir, "hrz_services.h")
    tpl = tpl_env.get_template("cpp_protocol/protocol_all.tpl.h")
    output_template(protocol, tpl, output_dir, "hrz_protocol_all.h")

    tpl_data = prepare_api_tpl_data(protocol)
    tpl = tpl_env.get_template("cpp_protocol/path_builder.tpl.h")
    output_template(tpl_data, tpl, output_dir, "hrz_protocol_path_builder.h")

@generator("cpp_core")
def cpp_core_generator(protocol, tpl_env, output_dir, extra):
    tpl_data = prepare_api_tpl_data(protocol)
    tpl = tpl_env.get_template("cpp_core/dispatcher.tpl.cpp")
    output_template(protocol, tpl, output_dir, "src/hrz_core_rpc_dispatcher.cpp")
    tpl = tpl_env.get_template("cpp_core/dispatcher.tpl.h")
    output_template(protocol, tpl, output_dir, "include/hrz_core_rpc_dispatcher.h")
    tpl = tpl_env.get_template("cpp_core/scene_path.tpl.h")
    output_template(tpl_data, tpl, output_dir, "include/hrz_core_scene_path.h")
    tpl = tpl_env.get_template("cpp_core/scene_path.tpl.cpp")
    output_template(tpl_data, tpl, output_dir, "src/hrz_core_scene_path.cpp")
    tpl = tpl_env.get_template("cpp_core/scene_model_accessor.tpl.h")
    output_template(tpl_data, tpl, output_dir, "include/hrz_core_scene_model_accessor.h")
    tpl = tpl_env.get_template("cpp_core/scene_model_accessor.tpl.cpp")
    output_template(tpl_data, tpl, output_dir, "src/hrz_core_scene_model_accessor.cpp")
    tpl = tpl_env.get_template("cpp_core/client_messages.tpl.cpp")
    output_template(tpl_data, tpl, output_dir, "src/hrz_core_client_messages.cpp")
    tpl = tpl_env.get_template("cpp_core/client_messages.tpl.h")
    output_template(tpl_data, tpl, output_dir, "include/hrz_core_client_messages.h")
    tpl = tpl_env.get_template("cpp_core/style_enums.tpl.cpp")
    output_template(tpl_data, tpl, output_dir, "include/hrz_core_style_enums.cpp")

@generator("cpp_api")
def cpp_api_generator(protocol, tpl_env, output_dir, extra):
    tpl_data = prepare_api_tpl_data(protocol)
    tpl_cpp = tpl_env.get_template("cpp_api/api.tpl.cpp")
    output_template(tpl_data, tpl_cpp, output_dir, "hrz_api.cpp")
    tpl_h = tpl_env.get_template("cpp_api/api.tpl.h")
    output_template(tpl_data, tpl_h, output_dir, "hrz_api.h")

@generator("ts_api")
def ts_api_generator(protocol, tpl_env, output_dir, extra):
    tpl_data = prepare_api_tpl_data(protocol)
    tpl_data["version"] = extra[0]
    tpl = tpl_env.get_template("ts_api/api.tpl.ts")
    output_template(tpl_data, tpl, output_dir, "hrz_api.ts")

@generator("job_declarations")
def jobs_declarations_generator(protocol, tpl_env, output_dir, extra):
    tpl_data = prepare_api_tpl_data(protocol)
    tpl = tpl_env.get_template("jobs/declarations.tpl.cpp")
    output_template(tpl_data, tpl, output_dir, "src/hrz_jobs_declarations.cpp")
    tpl = tpl_env.get_template("jobs/declarations.tpl.h")
    output_template(tpl_data, tpl, output_dir, "include/hrz_jobs_declarations.h")
    tpl = tpl_env.get_template("jobs/tickets.tpl.cpp")
    output_template(tpl_data, tpl, output_dir, "src/hrz_jobs_tickets.cpp")
    tpl = tpl_env.get_template("jobs/tickets.tpl.h")
    output_template(tpl_data, tpl, output_dir, "include/hrz_jobs_tickets.h")
    tpl = tpl_env.get_template("jobs/enum_names.tpl.h")
    output_template(tpl_data, tpl, output_dir, "include/hrz_jobs_enum_names.h")

@generator("web_ui_info")
def web_ui_info_generator(protocol, tpl_env, output_dir, extra):
    tpl_data = prepare_api_tpl_data(protocol)
    tpl_data["version"] = extra[0]
    tpl = tpl_env.get_template("web_ui_info/ui_info.tpl.ts")
    output_template(tpl_data, tpl, output_dir, "ui_info.ts")

def trim_indentation(lines):
    min_leading_spaces = 100000

    for line in lines:
        if len(line) != 0: # Ignore empty lines
            leading_spaces = len(line) - len(line.lstrip(" "))
            min_leading_spaces = min(min_leading_spaces, leading_spaces)

    for index, line in enumerate(lines):
        lines[index] = line[min_leading_spaces:]

    return lines

comment_by_file_ext = {
    ".ts": "//"
}

def find_block(lines, block_name, prefix):
    block_start = prefix + "embed-block-start " + block_name
    block_end = prefix + "embed-block-end " + block_name

    block_start_line = None
    block_end_line = None

    for index, line in enumerate(lines):
        line = line.lstrip()
        if line == block_start:
            block_start_line = index + 1
        if block_start_line is not None and line == block_end:
            block_end_line = index - 1

    # Only return entire blocks
    if block_end_line is None:
        block_start_line = None

    return block_start_line, block_end_line

# Code can be embedded in documentation with four patterns:
# A whole file:
#     @[hrz/doc/src/examples.ts]
# One line:
#     @[hrz/doc/src/examples.ts#L123]
# Line range:
#     @[hrz/doc/src/examples.ts#L100-L200]
# Named block:
#     @[hrz/doc/src/examples.ts#blockName]
# Named blocks are defined in the code with comments, like so:
#     //embed-block-start blockName
#     ... code ...
#     //embed-block-end blockName
# The comment prefix is language-dependant and selected with the
# file extension.
# Indentation is automatically removed.
def resolve_code_embeddings(md_content):
    lines = md_content.splitlines()

    line_pattern = re.compile(r"@\[\s*([^ #]+)(?:#(?:L(\d+)(?:-L(\d+))?|(.*)))?\s*\]")

    embeddings = []

    for index, line in enumerate(lines):
        match = line_pattern.match(line)
        if match:
            file_name = match.group(1)
            start_line = match.group(2)
            end_line = match.group(3)
            block_name = match.group(4)

            if start_line is not None:
                start_line = int(start_line) - 1
            if end_line is not None:
                end_line = int(end_line) - 1

            if start_line is not None and end_line is None:
                end_line = start_line

            embeddings.append({
                "dest_line": index,
                "file_name": file_name,
                "start_line": start_line,
                "end_line": end_line,
                "block_name": block_name
            })

    for embedding in embeddings:
        file_name = embedding["file_name"]
        dest_line = embedding["dest_line"]
        start_line = embedding["start_line"]
        end_line = embedding["end_line"]
        block_name = embedding["block_name"]

        fp = io.open(file_name, mode="r", encoding="utf8")
        file_content = fp.read()
        fp.close()

        file_lines = file_content.splitlines()

        if block_name is not None:
            file_extension = path.splitext(file_name)[1]
            comment_prefix = comment_by_file_ext.get(file_extension, None)
            if comment_prefix is None:
                print("Error: Unknown file extension for code embedding: " + file_extension)
                sys.exit(2)
            start_line, end_line = find_block(file_lines, block_name, comment_prefix)

        if start_line is None:
            if block_name is not None:
                print("Error: Embed block not found: " + block_name)
                sys.exit(2)
            else:
                # Embed the whole file
                lines[dest_line] = file_content
        else:
            # Embed a line range
            lines[dest_line] = "\n".join(trim_indentation(file_lines[start_line:(end_line + 1)]))

    md_content = "\n".join(lines)

    return md_content

def read_crs_database(crs_database_path):
    crs_list = {}
    with open(crs_database_path) as in_file:
        for line in in_file:
            parts = line.split("\t")
            auth = parts[0]
            l = crs_list.get(auth, [])
            l.append({
                "srid": int(parts[1]),
                "name": parts[2],
                "proj_str": parts[3]
            })
            crs_list[auth] = l
    for _, l in crs_list.items():
        l.sort(key=lambda crs: crs["srid"])
    return crs_list

def convert_doc_fields_markdown(values):
    if isinstance(values, list):
        for value in values:
            convert_doc_fields_markdown(value)
    elif isinstance(values, dict):
        for key, value in values.items():
            if key == "documentation" and isinstance(value, str):
                md = markdown.Markdown(
                    extensions = ["extra", "sane_lists", "codehilite", "wikilinks"],
                    extension_configs = {
                        "codehilite": [("guess_lang", False)],
                        "wikilinks": [
                            ("base_url", "HrzProtocol."),
                            ("end_url", ".html"),
                            ("html_class", "protocol-type-link"),
                        ],
                    })
                html_contents = md.convert(value)
                values[key] = html_contents
            else:
                convert_doc_fields_markdown(value)


def read_doc_markdown(in_file):
    name = path.basename(in_file)[:-3]

    fp = io.open(in_file, mode="r", encoding="utf8")
    md_contents = fp.read()
    fp.close()

    md = markdown.Markdown(extensions = ["meta"])
    # Parse only metadata first
    if md_contents.startswith("---\n"):
        index = md_contents[4:].find("---\n")
        if index >= 0:
            md.convert(md_contents[:index + 8])

    toc_depth = "1-3"
    if "tocdepth" in md.Meta:
        toc_depth = md.Meta["tocdepth"][0]

    md_contents = resolve_code_embeddings(md_contents)
    md = markdown.Markdown(
        extensions = ["meta", "extra", "sane_lists", "toc", "codehilite", "admonition", "wikilinks"],
        extension_configs = {
            "codehilite": [("guess_lang", False)],
            "toc": [("toc_depth", toc_depth)],
            "wikilinks": [
                ("base_url", "HrzProtocol."),
                ("end_url", ".html"),
                ("html_class", "protocol-type-link"),
            ]
        })

    separator = "<p>@@@@@@@@@@@TOCSEPARATOR@@@@@@@@@@@@@@</p>"

    # We have:
    #  - content
    #  - separator
    #  - TOC
    #  - separator
    #  - footnotes
    html_contents_toc = md.convert(md_contents + "\n\n" + separator + "\n\n[TOC]\n\n" + separator)
    html_contents_toc = html_contents_toc.split(separator)

    title = md.Meta["title"][0]
    category = md.Meta["category"][0] if "category" in md.Meta else None
    menu_category = md.Meta["menucategory"][0] if "menucategory" in md.Meta else "doc"

    return html_contents_toc[0] + html_contents_toc[2], name, title, category, menu_category, html_contents_toc[1]

def output_doc_html(values, tpl, contents, name, title, toc, menu_category, output_dir):
    filename = name + ".html"
    out_file = Path(output_dir)  / filename

    doc_values = values.copy()
    doc_values["this"] = {
        "contents": contents,
        "title": title,
        "toc": toc,
    }
    doc_values["menu_category"] = menu_category

    output_template(doc_values, tpl, output_dir, filename)
    return out_file

def output_templates_html(values, key_to_iterate, tpl, output_path):
    output_path = Path(output_path)
    outputs = []
    for k in values[key_to_iterate]:
        v = values.copy()
        v["this"] = k
        filename = k["full_name"] + ".html"
        output_template(v, tpl, output_path, filename)
        outputs.append(output_path / filename)
    return outputs

def make_documentation_pages_values(protocol, pages_list_file):
    doc_pages = []
    doc_contents = []
    doc_categories = {}
    pages_list = []
    with open(pages_list_file, "r") as fp:
        pages_list = json.loads(fp.read())

    for page_in_file in pages_list:
        if not page_in_file.endswith(".md"):
            continue
        md, name, title, category, menu_category, toc = read_doc_markdown(page_in_file)
        doc_contents.append({"contents": md, "name": name, "title": title, "toc": toc, "menu_category": menu_category})

        page_ref = {"name": name, "title": title}
        doc_pages.append(page_ref)

        if category != None:
            if not category in doc_categories:
                doc_categories[category] = [page_ref]
            else:
                doc_categories[category].append(page_ref)

    values = protocol.copy()
    values["doc_pages"] = doc_pages
    values["doc_categories"] = doc_categories
    values["doc_contents"] = doc_contents
    values["version"] = extra[0]
    values["scene_model_roots"] = [{"type": msg["full_name"], "root_field": msg["path_root"]} for msg in protocol["messages"] if msg["is_path_root"]]

    return values

@generator("doc_pages")
def documentation_generator(protocol, tpl_env, output_dir, extra):
    output_dir = Path(output_dir)

    values = make_documentation_pages_values(protocol, extra[1])
    convert_doc_fields_markdown(values)

    tpl_doc = tpl_env.get_template("documentation/fragment_doc.tpl.html")
    for doc in values["doc_contents"]:
        out_file = output_doc_html(values, tpl_doc, doc["contents"], doc["name"], doc["title"], doc["toc"], doc["menu_category"], output_dir)

@generator("doc_search_index_script")
def doc_search_index_script_generator(protocol, tpl_env, output_dir, extra):
    output_dir = Path(output_dir)

    values = make_documentation_pages_values(protocol, extra[1])
    convert_doc_fields_markdown(values)

    tpl_script = tpl_env.get_template("documentation/searchIndex.tpl.js")
    output_template(values, tpl_script, output_dir, extra[2])

@generator("doc_ref_pages")
def doc_ref_pages_generator(protocol, tpl_env, output_dir, extra):
    output_dir = Path(output_dir)

    values = make_documentation_pages_values(protocol, extra[1])
    convert_doc_fields_markdown(values)

    tpl_enums = tpl_env.get_template("documentation/fragment_enum.tpl.html")
    output_templates_html(values, "enums", tpl_enums, output_dir)

    tpl_messages = tpl_env.get_template("documentation/fragment_message.tpl.html")
    output_templates_html(values, "messages", tpl_messages, output_dir)

    tpl_services = tpl_env.get_template("documentation/fragment_service.tpl.html")
    output_templates_html(values, "services", tpl_services, output_dir)


@generator("doc_crs_database_md")
def doc_crs_database_md_generator(protocol, tpl_env, output_dir, extra):
    values = {}
    values["crs_db"] = read_crs_database(extra[0])
    tpl = tpl_env.get_template("documentation/crs_database.tpl.md")
    output_template(values, tpl, output_dir, "crs_database.md")

@generator("doc_artifacts_md")
def doc_artifacts_md_generator(protocol, tpl_env, output_dir, extra):
    version = extra[1]
    npm_version = version

    is_opensource = extra[2] == "opensource"
    is_snapshot = "SNAPSHOT" in version

    values = {}

    if is_snapshot:
        # npm snapshot package names have an extra qualifier appended to them (the date of the build).
        # See `tools/bazel/qualify_package_json_version.py`.
        npm_qualifier = Path(extra[0]).read_text().strip()
        npm_version += "." + npm_qualifier

    if not is_opensource:
        npm_base = ""
        raw_base = ""
        if is_snapshot:
            npm_base = "http://redacted.localhost/repository/npm-snapshots/"
            raw_base = "http://redacted.localhost/repository/raw-snapshots/horizon/"
        else:
            npm_base = "http://redacted.localhost/repository/npm-releases/"
            raw_base = "http://redacted.localhost/repository/raw-releases/horizon/"

        values["core_windows"] = f"{raw_base}core-windows-{version}.tar.gz"
        values["api_windows"] = f"{raw_base}cpp-api-windows-{version}.tar.gz"
        values["protocol_windows"] = f"{raw_base}cpp-protocol-windows-{version}.tar.gz"

        values["core_linux"] = f"{raw_base}core-linux-{version}.tar.gz"
        values["api_linux"] = f"{raw_base}cpp-api-linux-{version}.tar.gz"
        values["protocol_linux"] = f"{raw_base}cpp-protocol-linux-{version}.tar.gz"

        values["core_npm"] = f"{npm_base}@siradel/horizon-core/-/horizon-core-{npm_version}.tgz"
        values["api_npm"] = f"{npm_base}@siradel/horizon-api/-/horizon-api-{npm_version}.tgz"
        values["protocol_npm"] = f"{npm_base}@siradel/horizon-protocol/-/horizon-protocol-{npm_version}.tgz"

        values["scene_dump_npm"] = f"{npm_base}@siradel/horizon-scene-dump/-/horizon-scene-dump-{npm_version}.tgz"
        values["monitoring_protocol_npm"] = f"{npm_base}@siradel/horizon-monitoring-protocol/-/horizon-monitoring-protocol-{npm_version}.tgz"

        values["monitoring_app_windows"] = f"{raw_base}monitoring-client-windows-{version}.exe"
        values["monitoring_app_linux"] = f"{raw_base}monitoring-client-linux-{version}"

        values["testing_kit_linux_x11"] = f"{raw_base}testing-kit-linux-x11-{version}.tar.gz"
        values["testing_kit_linux_headless"] = f"{raw_base}testing-kit-linux-headless-{version}.tar.gz"
        values["testing_kit_windows"] = f"{raw_base}testing-kit-windows-{version}.tar.gz"

        values["documentation"] = f"{raw_base}api-doc-{version}.tar.gz"
    else:
        base = f"https://github.com/siradel-oss/Horizon/releases/download/v{version}/"

        values["core_windows"] = f"{base}horizon-core-{version}-cpp-windows.tar.gz"
        values["api_windows"] = f"{base}horizon-api-{version}-cpp-windows.tar.gz"
        values["protocol_windows"] = f"{base}horizon-protocol-{version}-cpp-windows.tar.gz"

        values["core_linux"] = f"{base}horizon-core-{version}-cpp-linux.tar.gz"
        values["api_linux"] = f"{base}horizon-api-{version}-cpp-linux.tar.gz"
        values["protocol_linux"] = f"{base}horizon-protocol-{version}-cpp-linux.tar.gz"

        values["core_npm"] = f"{base}horizon-core-{version}-ts-npm.tgz"
        values["api_npm"] = f"{base}horizon-api-{version}-ts-npm.tgz"
        values["protocol_npm"] = f"{base}horizon-protocol-{version}-ts-npm.tgz"

        values["scene_dump_npm"] = f"{base}horizon-scene-dump-{version}-ts-npm.tgz"
        values["monitoring_protocol_npm"] = f"{base}horizon-monitoring-protocol-{version}-ts-npm.tgz"

        values["monitoring_app_windows"] = f"{base}horizon-monitoring-client-{version}-windows.exe"
        values["monitoring_app_linux"] = f"{base}horizon-monitoring-client-{version}-linux"

        values["testing_kit_linux_x11"] = f"{base}horizon-testing-kit-{version}-linux-x11.tar.gz"
        values["testing_kit_linux_headless"] = f"{base}horizon-testing-kit-{version}-linux-headless.tar.gz"
        values["testing_kit_windows"] = f"{base}horizon-testing-kit-{version}-windows.tar.gz"

        values["documentation"] = f"{base}horizon-documentation-{version}.tar.gz"

    tpl = tpl_env.get_template("documentation/artifacts.tpl.md")
    output_template(values, tpl, output_dir, "artifacts.md")

@generator("doc_reference_md")
def doc_reference_md_generator(protocol, tpl_env, output_dir, extra):
    values = protocol.copy()
    values["scene_model_roots"] = [{"type": msg["full_name"], "root_field": msg["path_root"]} for msg in protocol["messages"] if msg["is_path_root"]]

    tpl = tpl_env.get_template("documentation/reference.tpl.md")
    output_template(values, tpl, output_dir, "reference.md")

@generator("style_enums_md")
def style_enums_md_generator(protocol, tpl_env, output_dir, extra):
    values = protocol.copy()

    tpl = tpl_env.get_template("documentation/style_enums.tpl.md")
    output_template(values, tpl, output_dir, "style_enums.md")

if __name__ == "__main__":
    if len(sys.argv) < 4:
        print("Usage:")
        print("     %s <protocol.xml> <generator type> <output folder>" % sys.argv[0])
        print("")
        print("     Generator types:")
        for gen in generators:
            print("         - %s" % gen)
        print("         - * (all the above)")
        sys.exit(1)

    input_file_name = sys.argv[1]

    if not path.isfile(input_file_name):
        print("File %s does not exist" % input_file_name)
        sys.exit(1)

    protocol = protocol_parser.parse(input_file_name)
    tpl_env = prepare_env()
    gen = sys.argv[2]

    extra = []
    if len(sys.argv) >= 4:
        extra = sys.argv[4:]

    if gen == "*":
        for k, v in generators.items():
            v(protocol, tpl_env, sys.argv[3], extra)
    elif gen in generators:
        generators[gen](protocol, tpl_env, sys.argv[3], extra)
    else:
        print("Unknown generator %s" % sys.argv[2])
