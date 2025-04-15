import argparse
import sys
import jinja2
import os.path as path
from python.runfiles import Runfiles

from pathlib import Path

sys.path.append("")
from hrz.proto.history.manifest import Manifest, read_manifest

MANIFEST: Manifest = None

def make_tpl_env():
    r = Runfiles.Create()
    template_dir = Path(r.Rlocation("horizon/hrz/proto/history/templates/model_version.tpl.h")).parent
    loader = jinja2.FileSystemLoader(str(template_dir))
    return jinja2.Environment(loader = loader)

def render_tpl(tpl_env, tpl_name, params, output_fp):
    tpl = tpl_env.get_template(tpl_name)
    output_fp.write(tpl.render(params))

def gen_cpp_descriptor_sets_list(args):
    tpl_env = make_tpl_env()

    params = {}
    params["descriptor_sets"] = {}

    for f in args.descriptor_file:
        id = path.basename(f.name).split(".")[0]
        if not MANIFEST.is_id_in(id):
            print(f"Descriptor set with ID {id} is not in the manifest")
            sys.exit(1)

        data = f.read()
        size = len(data)
        data = ",".join([f"{c}" for c in data])
        params["descriptor_sets"][id] = {
            "size": size,
            "data_str": data,
        }

    render_tpl(tpl_env, "descriptor_sets_list.tpl.h", params, args.header_output_file)
    render_tpl(tpl_env, "descriptor_sets_list.tpl.cpp", params, args.impl_output_file)

def gen_cpp_model_version(args):
    tpl_env = make_tpl_env()

    params = {}
    params["version"] = MANIFEST.last_entry().id

    render_tpl(tpl_env, "model_version.tpl.h", params, args.header_output_file)

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("-m", "--manifest", help="Manifest path")

    subparsers = parser.add_subparsers()

    subparser = subparsers.add_parser("gen-cpp-descriptor-sets-list")
    subparser.set_defaults(func = gen_cpp_descriptor_sets_list)
    subparser.add_argument("header_output_file", type=argparse.FileType("w+"))
    subparser.add_argument("impl_output_file", type=argparse.FileType("w+"))
    subparser.add_argument("descriptor_file", type=argparse.FileType("rb"), nargs="+")

    subparser = subparsers.add_parser("gen-cpp-model-version")
    subparser.set_defaults(func = gen_cpp_model_version)
    subparser.add_argument("header_output_file", type=argparse.FileType("w+"))

    args = parser.parse_args(sys.argv[1:])
    MANIFEST = read_manifest(args.manifest)

    if hasattr(args, "func"):
        args.func(args)

