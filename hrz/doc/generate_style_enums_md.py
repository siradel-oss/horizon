# SPDX-FileCopyrightText: Copyright 2026 Siradel
# SPDX-License-Identifier: MIT

from dataclasses import asdict, dataclass
import sys
from pathlib import Path

import jinja2
from python.runfiles import Runfiles

from hrz.generator.protocol_model import HrzProtocol
from hrz.generator import protocol_parser
from hrz.generator.common import (
    prepare_env,
    output_template,
)


def generate_style_enums(
    protocol: HrzProtocol, tpl_env: jinja2.Environment, output_dir: Path
):
    tpl = tpl_env.get_template("style_enums.tpl.md")
    output_template(asdict(protocol), tpl, output_dir, "style_enums.md")


def main():
    r = Runfiles.Create()

    if r is None:
        raise Exception("Failed to create Runfiles instance")

    protocol_path = r.Rlocation("horizon/hrz/hrz_protocol.xml")
    if protocol_path is None:
        raise Exception("Failed to locate protocol XML file")
    protocol = protocol_parser.parse(protocol_path)

    tpl_env = prepare_env("horizon/hrz/doc/templates")
    output_dir = Path(sys.argv[1])

    generate_style_enums(protocol, tpl_env, output_dir)


if __name__ == "__main__":
    main()
