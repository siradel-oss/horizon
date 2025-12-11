import argparse
import sys
import subprocess
import platform

from pathlib import Path

sys.path.append("")
from hrz.protocol.history.manifest import Manifest, read_manifest

BZL_CONFIG = "--config=" + platform.system().lower()

MANIFEST_PATH = "hrz/protocol/history/versions_manifest.csv"
MANIFEST: Manifest = None

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("-m", "--manifest", help="Manifest path", default=MANIFEST_PATH)
    parser.add_argument(
        "-v",
        "--version",
        help="Model version as 8 hex digits (latest if not specified)",
        default="latest",
    )
    parser.add_argument("dump_file", type=Path)

    args = parser.parse_args(sys.argv[1:])
    MANIFEST_PATH = args.manifest
    MANIFEST = read_manifest(MANIFEST_PATH)

    version = args.version
    if version == "latest":
        version = MANIFEST.last_entry().id

    if not MANIFEST.is_id_in(version):
        print("Version ID not in manifest")
        sys.exit(1)

    path = args.dump_file.absolute()

    ret = subprocess.run(
        [
            "bazel",
            "run",
            "//tools/scene_dump:set_dump_version",
            BZL_CONFIG,
            "--",
            path,
            version,
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
    )

    if ret.returncode == 0:
        print("OK")
    else:
        print(ret.stdout.decode("utf-8"))
        sys.exit(ret.returncode)
