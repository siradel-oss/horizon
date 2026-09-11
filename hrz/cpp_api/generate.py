# SPDX-FileCopyrightText: Copyright 2026 Siradel
# SPDX-License-Identifier: MIT

from dataclasses import asdict
import sys
from pathlib import Path
from python.runfiles import Runfiles

from hrz.generator import protocol_parser, api_parser
from hrz.generator.common import (
    prepare_env,
    output_template,
)

if __name__ == "__main__":
    r = Runfiles.Create()

    if r is None:
        raise Exception("Failed to create Runfiles instance")

    protocol_path = r.Rlocation("horizon/hrz/hrz_protocol.xml")
    if protocol_path is None:
        raise Exception("Failed to locate protocol XML file")

    protocol = protocol_parser.parse(protocol_path)
    tpl_data = api_parser.parse(protocol)
    tpl_env = prepare_env("horizon/hrz/cpp_api/templates")
    output_dir = Path(sys.argv[1])

    tpl_data = asdict(tpl_data)
    tpl_cpp = tpl_env.get_template("api.tpl.cpp")
    output_template(tpl_data, tpl_cpp, output_dir, "api.cpp")
    tpl_h = tpl_env.get_template("api.tpl.h")
    output_template(tpl_data, tpl_h, output_dir, "api.h")
