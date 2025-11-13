import argparse
import sys
import jinja2
from python.runfiles import Runfiles

from pathlib import Path

sys.path.append("")
from hrz.protocol.history.manifest import Manifest, read_manifest

MANIFEST: Manifest = None

def make_tpl_env():
    r = Runfiles.Create()
    template_dir = Path(r.Rlocation("horizon/hrz/scene_dump/templates/model_version.tpl.h")).parent
    loader = jinja2.FileSystemLoader(str(template_dir))
    return jinja2.Environment(loader = loader)

def render_tpl(tpl_env, tpl_name, params, output_fp):
    tpl = tpl_env.get_template(tpl_name)
    output_fp.write(tpl.render(params))

def gen_cpp_migrations_list(args):
    tpl_env = make_tpl_env()
    params = {
        "migration_ids": [row.id for row in MANIFEST.entries],
    }
    render_tpl(tpl_env, "migrations_list.tpl.h", params, args.header_output_file)

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("-m", "--manifest", help="Manifest path")

    subparsers = parser.add_subparsers()

    subparser = subparsers.add_parser("gen-cpp-migrations-list")
    subparser.set_defaults(func = gen_cpp_migrations_list)
    subparser.add_argument("header_output_file", type=argparse.FileType("w+"))

    args = parser.parse_args(sys.argv[1:])
    MANIFEST = read_manifest(args.manifest)

    if hasattr(args, "func"):
        args.func(args)

