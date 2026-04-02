import sys
import argparse
import json

from integrity import compute_integrity_file, compute_integrity_url_streaming
from utils import is_executed_in_registry, module_directory

if __name__ == "__main__":
    if not is_executed_in_registry():
        print("This script should be executed from the root of the registry directory.")
        sys.exit(1)

    parser = argparse.ArgumentParser(
        description="Update the integrity values for a module in the local registry."
    )
    parser.add_argument("module_name", help="The name of the module to update.")
    parser.add_argument("version", help="The version of the module to update.")
    parser.add_argument(
        "--package",
        action="store_true",
        help="Whether to also update the integrity values of the archive file.",
    )
    args = parser.parse_args()

    module_name = args.module_name
    version = args.version

    module_dir = module_directory(module_name, version)
    if not module_dir.is_dir():
        print(
            f"Module {module_name} version {version} does not exist in the local registry."
        )
        sys.exit(1)

    source_json_path = module_dir / "source.json"
    if not source_json_path.is_file():
        print(
            f"Module {module_name} version {version} does not have a source.json file."
        )
        sys.exit(1)

    source_json = json.loads(source_json_path.read_bytes())

    for overlay in source_json.get("overlay", {}).keys():
        overlay_path = module_dir / "overlay" / str(overlay)
        if not overlay_path.is_file():
            print(
                f"Overlay file {overlay} does not exist for module {module_name} version {version}."
            )
            sys.exit(1)
        source_json["overlay"][overlay] = compute_integrity_file(
            overlay_path, can_be_indirect=True
        )

    for patch in source_json.get("patches", {}).keys():
        patch_path = module_dir / "patches" / str(patch)
        if not patch_path.is_file():
            print(
                f"Patch file {patch} does not exist for module {module_name} version {version}."
            )
            sys.exit(1)
        source_json["patches"][patch] = compute_integrity_file(patch_path)

    if args.package:
        url = source_json.get("url")
        if url is None:
            print(
                f"Module {module_name} version {version} does not have a url field in source.json."
            )
            sys.exit(1)
        integrity = compute_integrity_url_streaming(url)
        source_json["integrity"] = integrity

    source_json_path.write_bytes(json.dumps(source_json, indent=2).encode("utf-8"))
