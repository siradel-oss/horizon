# SPDX-FileCopyrightText: Copyright 2026 Siradel
# SPDX-License-Identifier: MIT

from dataclasses import asdict
import sys
from pathlib import Path

import jinja2
from python.runfiles import Runfiles

from hrz.generator.protocol_model import HrzProtocol
from hrz.generator import protocol_parser
from hrz.generator.filters import path_to_snake_case
from hrz.generator.common import (
    prepare_env,
    output_template,
)


def generate_reference(
    protocol: HrzProtocol, tpl_env: jinja2.Environment, output_dir: Path
):
    scene_model_roots = [
        {"type": msg.full_name, "root_field": msg.path_root}
        for msg in protocol.messages
        if msg.is_path_root
    ]

    values = asdict(protocol)
    values["scene_model_roots"] = scene_model_roots

    tpl = tpl_env.get_template("reference.tpl.md")
    output_template(values, tpl, output_dir, "_index.md")


def output_templates_protocol_file_md(
    protocol: HrzProtocol, key_to_iterate: str, tpl: jinja2.Template, output_path: Path
):
    v = asdict(protocol)
    for k in getattr(protocol, key_to_iterate):
        v["this"] = k
        v["file_services"] = [s for s in protocol.services if s.file == k.name]
        v["file_enums"] = [e for e in protocol.enums if e.file == k.name]
        v["file_messages"] = [m for m in protocol.messages if m.file == k.name]
        filename = path_to_snake_case(k.name) + "_proto.md"
        output_template(v, tpl, output_path, filename)


def output_templates_protocol_def_md(
    protocol: HrzProtocol, key_to_iterate: str, tpl: jinja2.Template, output_path: Path
):
    for k in getattr(protocol, key_to_iterate):
        v = asdict(protocol)
        v["this"] = k
        filename = k.full_name + ".md"
        output_template(v, tpl, output_path, filename)


def generate_reference_pages(
    protocol: HrzProtocol, tpl_env: jinja2.Environment, output_dir: Path
):
    tpl_enums = tpl_env.get_template("enum.tpl.md")
    output_templates_protocol_def_md(protocol, "enums", tpl_enums, output_dir)

    tpl_messages = tpl_env.get_template("message.tpl.md")
    output_templates_protocol_def_md(protocol, "messages", tpl_messages, output_dir)

    tpl_services = tpl_env.get_template("service.tpl.md")
    output_templates_protocol_def_md(protocol, "services", tpl_services, output_dir)

    tpl_files = tpl_env.get_template("file.tpl.md")
    output_templates_protocol_file_md(protocol, "files", tpl_files, output_dir)


def main():
    r = Runfiles.Create()

    if r is None:
        raise Exception("Failed to create Runfiles instance")

    protocol_path = r.Rlocation("horizon/hrz/hrz_protocol.xml")
    if protocol_path is None:
        raise Exception("Failed to locate protocol XML file")
    protocol = protocol_parser.parse(protocol_path)

    tpl_env = prepare_env("horizon/hrz/doc/templates")
    output_dir = Path(sys.argv[1]) / "generated_reference"

    generate_reference(protocol, tpl_env, output_dir)
    generate_reference_pages(protocol, tpl_env, output_dir)


if __name__ == "__main__":
    main()
