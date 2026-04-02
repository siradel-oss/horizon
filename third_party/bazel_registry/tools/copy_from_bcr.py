import sys
import argparse
import json

from integrity import compute_integrity_file
from utils import (
    is_executed_in_registry,
    module_directory,
    download_text_file,
    download_json_file,
    registry_file_url,
)

if __name__ == "__main__":
    if not is_executed_in_registry():
        raise Exception(
            "This script should be executed from the root of the registry directory."
        )

    parser = argparse.ArgumentParser(
        description="Copy a module from BCR to our local registry."
    )
    parser.add_argument("module_name", help="The name of the module to copy.")
    parser.add_argument("version", help="The version of the module to copy.")
    parser.add_argument(
        "new_version", help="The new version to use for the copied module."
    )
    args = parser.parse_args()

    module_name = args.module_name
    version = args.version
    new_version = args.new_version

    module_dir = module_directory(module_name, new_version)

    if module_dir.is_dir():
        raise Exception(
            f"Module {module_name} version {new_version} already exists in the local registry."
        )

    source_json_url = registry_file_url(module_name, version, "source.json")
    source_json_path = module_dir / "source.json"

    source_json = download_json_file(source_json_url)
    if source_json.get("type", "archive") != "archive":
        raise Exception(
            f"Module {module_name} version {version} is not an archive module. Only archive modules are supported."
        )

    module_bazel_url = registry_file_url(module_name, version, "MODULE.bazel")
    module_bazel_path = module_dir / "MODULE.bazel"
    module_bazel_path.parent.mkdir(parents=True, exist_ok=True)
    try:
        module_bazel_content = download_text_file(module_bazel_url)
        module_bazel_content = module_bazel_content.replace(
            f"{version}", f"{new_version}"
        )
        module_bazel_path.write_bytes(module_bazel_content.encode("utf-8"))
    except Exception as e:
        pass

    for overlay in source_json.get("overlay", {}).keys():
        overlay_url = registry_file_url(module_name, version, f"overlay/{overlay}")
        overlay_path = module_dir / "overlay" / overlay
        overlay_path.parent.mkdir(parents=True, exist_ok=True)
        overlay_content = download_text_file(overlay_url)
        if overlay == "MODULE.bazel":
            overlay_content = overlay_content.replace(f"{version}", f"{new_version}")
        overlay_path.write_bytes(overlay_content.encode("utf-8"))
        source_json["overlay"][overlay] = compute_integrity_file(overlay_path)

    for patch in source_json.get("patches", {}).keys():
        patch_url = registry_file_url(module_name, version, f"patches/{patch}")
        patch_path = module_dir / "patches" / patch
        patch_path.parent.mkdir(parents=True, exist_ok=True)
        patch_content = download_text_file(patch_url)
        patch_path.write_bytes(patch_content.encode("utf-8"))
        source_json["patches"][patch] = compute_integrity_file(patch_path)

    source_json_path.parent.mkdir(parents=True, exist_ok=True)
    source_json_path.write_bytes(json.dumps(source_json, indent=2).encode("utf-8"))
