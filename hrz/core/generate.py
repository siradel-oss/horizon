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
    api_model = api_parser.parse(protocol)
    tpl_env = prepare_env("horizon/hrz/core/templates")
    output_dir = Path(sys.argv[1])

    tpl_data = asdict(protocol)
    tpl = tpl_env.get_template("dispatcher.tpl.cpp")
    output_template(tpl_data, tpl, output_dir, "rpc_dispatcher.cpp")
    tpl = tpl_env.get_template("dispatcher.tpl.h")
    output_template(tpl_data, tpl, output_dir, "rpc_dispatcher.h")
    tpl = tpl_env.get_template("scene_model_accessor.tpl.h")

    tpl_data = asdict(api_model)
    output_template(tpl_data, tpl, output_dir, "scene_model_accessor.h")
    tpl = tpl_env.get_template("scene_model_accessor.tpl.cpp")
    output_template(tpl_data, tpl, output_dir, "scene_model_accessor.cpp")
    tpl = tpl_env.get_template("client_messages.tpl.cpp")
    output_template(tpl_data, tpl, output_dir, "client_messages.cpp")
    tpl = tpl_env.get_template("client_messages.tpl.h")
    output_template(tpl_data, tpl, output_dir, "client_messages.h")
    tpl = tpl_env.get_template("style_enums.tpl.cpp")
    output_template(tpl_data, tpl, output_dir, "style/enums.cpp")

    tpl = tpl_env.get_template("scene_path.tpl.cpp")
    output_template(tpl_data, tpl, output_dir, "scene_path/scene_path.cpp")
    tpl = tpl_env.get_template("scene_path_common.tpl.h")
    output_template(tpl_data, tpl, output_dir, "scene_path/common.h")

    tpl = tpl_env.get_template("scene_path.tpl.h")
    for f in api_model.protocol.files:
        tpl_data["filename"] = f.name
        tpl_data["dependencies"] = f.dependencies
        f = remove_prefix(f.name, "hrz/protocol/")
        output_template(tpl_data, tpl, output_dir, f"scene_path/{f}_paths.h")
