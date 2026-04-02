import argparse
import json

from utils import is_executed_in_registry, module_directory
from integrity import compute_integrity_url_streaming


def create_from_archive(module_name: str, version: str, url: str, prefix: str) -> None:
    module_dir = module_directory(module_name, version)
    if module_dir.is_dir():
        raise Exception(
            f"Module {module_name} version {version} already exists in the local registry."
        )

    integrity = compute_integrity_url_streaming(url)
    source_json = {
        "url": url,
        "strip_prefix": prefix,
        "integrity": integrity,
        "overlay": {"MODULE.bazel": ""},
    }

    module_dir.mkdir(parents=True, exist_ok=False)
    (module_dir / "overlay").mkdir(parents=True, exist_ok=False)
    (module_dir / "source.json").write_bytes(json.dumps(source_json, indent=2).encode())
    (module_dir / "MODULE.bazel").write_bytes(
        f'module(name = "{module_name}", version = "{version}")\n'.encode()
    )
    (module_dir / "overlay" / "MODULE.bazel").write_bytes(b"../MODULE.bazel\n")

    print(f"Module {module_name} version {version} created successfully.")


if __name__ == "__main__":
    if not is_executed_in_registry():
        raise Exception(
            "This script should be executed from the root of the registry directory."
        )

    parser = argparse.ArgumentParser(
        description="Create a new module from an HTTP archive."
    )
    parser.add_argument("module_name", help="The name of the module to create.")
    parser.add_argument("version", help="The version of the module to create.")
    parser.add_argument("url", help="The URL of the archive to use as source.")
    parser.add_argument("prefix", help="The prefix to strip from the archive files.")
    args = parser.parse_args()

    module_name = args.module_name
    version = args.version
    url = args.url
    prefix = args.prefix

    create_from_archive(module_name, version, url, prefix)
