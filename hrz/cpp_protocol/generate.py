import sys
from pathlib import Path
from dataclasses import asdict
from python.runfiles import Runfiles

from hrz.generator import protocol_parser, api_parser
from hrz.generator.common import (
    prepare_env,
    output_template,
    remove_prefix,
)

if __name__ == "__main__":
    r = Runfiles.Create()

    if r is None:
        raise Exception("Failed to create Runfiles instance")

    protocol_path = r.Rlocation("horizon/hrz/hrz_protocol.xml")
    if protocol_path is None:
        raise Exception("Failed to locate protocol XML file")

    protocol = protocol_parser.parse(protocol_path)
    tpl_env = prepare_env("horizon/hrz/cpp_protocol/templates")

    output_dir = Path(sys.argv[1])

    tpl_data = asdict(protocol)
    tpl = tpl_env.get_template("services.tpl.h")
    output_template(tpl_data, tpl, output_dir, "services.h")

    tpl_data = asdict(api_parser.parse(protocol))
    tpl = tpl_env.get_template("path_builder_common.tpl.h")
    output_template(tpl_data, tpl, output_dir, "path_builder_common.h")

    tpl = tpl_env.get_template("path_builder.tpl.h")

    for f in tpl_data["protocol"]["files"]:
        tpl_data["filename"] = f["name"]
        tpl_data["dependencies"] = f["dependencies"]
        f = remove_prefix(f["name"], "hrz/protocol/")
        output_template(tpl_data, tpl, output_dir, f"path_builder/{f}.h")
