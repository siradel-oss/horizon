from dataclasses import asdict, dataclass, is_dataclass, fields
import sys
from pathlib import Path
import typing

import jinja2
from python.runfiles import Runfiles
import re
import markdown

from hrz.generator import protocol_parser
from hrz.generator.common import (
    prepare_env,
    output_template,
)
from hrz.generator.protocol_model import HrzProtocol
from hrz.generator.filters import path_to_snake_case


RUNFILES = Runfiles.Create()
if RUNFILES is None:
    raise Exception("Failed to create Runfiles instance")


def trim_indentation(lines: list[str]) -> list[str]:
    min_leading_spaces = 100000

    for line in lines:
        if len(line) != 0:  # Ignore empty lines
            leading_spaces = len(line) - len(line.lstrip(" "))
            min_leading_spaces = min(min_leading_spaces, leading_spaces)

    for index, line in enumerate(lines):
        lines[index] = line[min_leading_spaces:]

    return lines


def find_block(
    lines: list[str], block_name: str, prefix: str
) -> tuple[int, int] | None:
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

    if block_start_line is not None and block_end_line is not None:
        return block_start_line, block_end_line
    else:
        return None


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

COMMENT_BY_FILE_EXT = {".ts": "//"}
BLOCK_REFERENCE_PATTERN = re.compile(
    r"@\[\s*([^ #]+)(?:#(?:L(\d+)(?:-L(\d+))?|(.*)))?\s*\]"
)


def resolve_code_embeddings(md_content: str) -> str:
    lines = md_content.splitlines()

    @dataclass
    class Embedding:
        dest_line: int
        file_name: str
        start_end_line: tuple[int, int] | None
        block_name: str | None

    embeddings: list[Embedding] = []

    for index, line in enumerate(lines):
        match = BLOCK_REFERENCE_PATTERN.match(line)
        if match:
            file_name = match.group(1)
            start_line = match.group(2)
            end_line = match.group(3)
            block_name = match.group(4)

            if start_line is not None and end_line is None:
                end_line = start_line

            start_end_line = (
                (int(start_line) - 1, int(end_line) - 1)
                if start_line is not None
                else None
            )

            embeddings.append(
                Embedding(
                    dest_line=index,
                    file_name=file_name,
                    start_end_line=start_end_line,
                    block_name=block_name,
                )
            )

    for embedding in embeddings:
        start_end_line = embedding.start_end_line

        path = RUNFILES.Rlocation("horizon/" + embedding.file_name)
        if path is None:
            raise Exception("Failed to locate file " + embedding.file_name)

        file_content = Path(path).read_bytes().decode("utf8")
        file_lines = file_content.splitlines()

        if embedding.block_name is not None:
            file_extension = Path(embedding.file_name).suffix
            comment_prefix = COMMENT_BY_FILE_EXT.get(file_extension, None)
            if comment_prefix is None:
                raise Exception(
                    "Unknown file extension for code embedding: " + file_extension
                )
            start_end_line = find_block(
                file_lines, embedding.block_name, comment_prefix
            )

        if start_end_line is None:
            if embedding.block_name is not None:
                raise Exception("Embed block not found: " + embedding.block_name)
            # Embed the whole file
            lines[embedding.dest_line] = file_content
        else:
            # Embed a line range
            lines[embedding.dest_line] = "\n".join(
                trim_indentation(
                    file_lines[start_end_line[0] : (start_end_line[1] + 1)]
                )
            )

    md_content = "\n".join(lines)
    return md_content


@dataclass
class DocPage:
    name: str
    title: str
    category: str | None
    menu_category: str  # What parts of the menu must be open on this page
    contents: str
    toc: str


def read_doc_markdown(in_file: Path) -> DocPage:
    name = in_file.stem

    md_contents = in_file.read_bytes().decode("utf8")

    md = markdown.Markdown(extensions=["meta"])
    # Parse only metadata first
    if md_contents.startswith("---"):
        index = md_contents[4:].find("---")
        if index >= 0:
            md.convert(md_contents[: index + 8])

    meta: dict[str, list[str]] = md.Meta  # type: ignore

    toc_depth = "1-3"
    if "tocdepth" in meta:
        toc_depth = meta["tocdepth"][0]

    md_contents = resolve_code_embeddings(md_contents)
    md = markdown.Markdown(
        extensions=[
            "meta",
            "extra",
            "sane_lists",
            "toc",
            "codehilite",
            "admonition",
            "wikilinks",
        ],
        extension_configs={
            "codehilite": {
                "guess_lang": False,
            },
            "toc": {
                "toc_depth": toc_depth,
            },
            "wikilinks": {
                "base_url": "HrzProtocol.",
                "end_url": ".html",
                "html_class": "protocol-type-link",
            },
        },
    )

    separator = "<p>@@@@@@@@@@@TOCSEPARATOR@@@@@@@@@@@@@@</p>"

    # We have:
    #  - content
    #  - separator
    #  - TOC
    #  - separator
    #  - footnotes
    html_contents_toc = md.convert(
        md_contents + "\n\n" + separator + "\n\n[TOC]\n\n" + separator
    )
    html_contents_toc = html_contents_toc.split(separator)

    meta: dict[str, list[str]] = md.Meta  # type: ignore

    title = meta["title"][0]
    category = meta["category"][0] if "category" in meta else None
    menu_category = meta["menucategory"][0] if "menucategory" in meta else "doc"

    return DocPage(
        name=name,
        title=title,
        category=category,
        menu_category=menu_category,
        contents=html_contents_toc[0] + html_contents_toc[2],
        toc=html_contents_toc[1],
    )


@dataclass
class SceneModelRootModel:
    type: str
    root_field: str


@dataclass
class DocModel(HrzProtocol):
    doc_pages: list[DocPage]
    doc_categories: dict[str, list[DocPage]]
    version: str
    scene_model_roots: list[SceneModelRootModel]


def make_documentation_model(
    protocol: HrzProtocol, pages_list_file: Path, version: str
) -> DocModel:
    doc_pages: list[DocPage] = []
    doc_categories: dict[str, list[DocPage]] = {}
    pages_list = Path(pages_list_file).read_bytes().decode("utf8").splitlines()

    for page_in_file in pages_list:
        if not page_in_file.endswith(".md"):
            continue

        page = read_doc_markdown(Path(page_in_file))
        doc_pages.append(page)

        if page.category != None:
            if not page.category in doc_categories:
                doc_categories[page.category] = [page]
            else:
                doc_categories[page.category].append(page)

    return DocModel(
        services=protocol.services,
        enums=protocol.enums,
        files=protocol.files,
        messages=protocol.messages,
        doc_pages=doc_pages,
        doc_categories=doc_categories,
        version=version,
        scene_model_roots=[
            SceneModelRootModel(type=msg.full_name, root_field=msg.path_root)
            for msg in protocol.messages
            if msg.is_path_root and msg.path_root is not None
        ],
    )


SIMPLE_MARKDOWN_CONTEXT = markdown.Markdown(
    extensions=["extra", "sane_lists", "codehilite", "wikilinks"],
    extension_configs={
        "codehilite": {
            "guess_lang": False,
        },
        "wikilinks": {
            "base_url": "HrzProtocol.",
            "end_url": ".html",
            "html_class": "protocol-type-link",
        },
    },
)


def convert_documentation_fields_markdown(values: typing.Any):
    if isinstance(values, list):
        for value in values:
            convert_documentation_fields_markdown(value)
    elif is_dataclass(values):
        for field in fields(values):
            value = getattr(values, field.name)
            if field.name == "documentation" and isinstance(value, str):
                setattr(values, field.name, SIMPLE_MARKDOWN_CONTEXT.convert(value))
            else:
                convert_documentation_fields_markdown(value)
    elif isinstance(values, dict):
        for key, value in values.items():
            if key == "documentation" and isinstance(value, str):
                values[key] = SIMPLE_MARKDOWN_CONTEXT.convert(value)
            else:
                convert_documentation_fields_markdown(value)


def output_doc_html(
    values: DocModel,
    tpl: jinja2.Template,
    contents: str,
    name: str,
    title: str,
    toc: str,
    menu_category: str,
    output_dir: Path,
):
    filename = name + ".html"

    doc_values = asdict(values)
    doc_values["this"] = {
        "contents": contents,
        "title": title,
        "toc": toc,
    }
    doc_values["menu_category"] = menu_category

    output_template(doc_values, tpl, output_dir, filename)


def output_templates_protocol_def_html(
    model: DocModel, key_to_iterate: str, tpl: jinja2.Template, output_path: Path
):
    for k in getattr(model, key_to_iterate):
        v = asdict(model)
        v["this"] = k
        filename = k.full_name + ".html"
        output_template(v, tpl, output_path, filename)


def output_templates_protocol_file_html(
    model: DocModel, key_to_iterate: str, tpl: jinja2.Template, output_path: Path
):
    v = asdict(model)
    for k in getattr(model, key_to_iterate):
        v["this"] = k
        v["file_services"] = [s for s in model.services if s.file == k.name]
        v["file_enums"] = [e for e in model.enums if e.file == k.name]
        v["file_messages"] = [m for m in model.messages if m.file == k.name]
        filename = path_to_snake_case(k.name) + "_proto.html"
        output_template(v, tpl, output_path, filename)


def generate_search_index(
    model: DocModel, tpl_env: jinja2.Environment, output_dir: Path
):
    tpl_script = tpl_env.get_template("searchIndex.tpl.js")
    output_template(asdict(model), tpl_script, output_dir, "searchIndex.js")


def generate_documentation_pages(
    model: DocModel, tpl_env: jinja2.Environment, output_dir: Path
):
    tpl_doc = tpl_env.get_template("fragment_doc.tpl.html")

    for doc in model.doc_pages:
        output_doc_html(
            model,
            tpl_doc,
            doc.contents,
            doc.name,
            doc.title,
            doc.toc,
            doc.menu_category,
            output_dir,
        )


def generate_reference_pages(
    model: DocModel, tpl_env: jinja2.Environment, output_dir: Path
):
    tpl_enums = tpl_env.get_template("fragment_enum.tpl.html")
    output_templates_protocol_def_html(model, "enums", tpl_enums, output_dir)

    tpl_messages = tpl_env.get_template("fragment_message.tpl.html")
    output_templates_protocol_def_html(model, "messages", tpl_messages, output_dir)

    tpl_services = tpl_env.get_template("fragment_service.tpl.html")
    output_templates_protocol_def_html(model, "services", tpl_services, output_dir)

    tpl_files = tpl_env.get_template("fragment_file.tpl.html")
    output_templates_protocol_file_html(model, "files", tpl_files, output_dir)


def main():
    r = RUNFILES

    templates_path = r.Rlocation("horizon/hrz/doc/templates")
    if templates_path is None:
        raise Exception("Failed to locate templates directory")

    protocol_path = r.Rlocation("horizon/hrz/hrz_protocol.xml")
    if protocol_path is None:
        raise Exception("Failed to locate protocol XML file")
    protocol = protocol_parser.parse(protocol_path)

    tpl_env = prepare_env(templates_path)
    output_dir = Path(sys.argv[1])
    version = sys.argv[2]
    pages_list_file = Path(sys.argv[3])

    convert_documentation_fields_markdown(protocol)
    model = make_documentation_model(protocol, pages_list_file, version)

    generate_search_index(model, tpl_env, output_dir)
    generate_documentation_pages(model, tpl_env, output_dir)
    generate_reference_pages(model, tpl_env, output_dir)


if __name__ == "__main__":
    main()
