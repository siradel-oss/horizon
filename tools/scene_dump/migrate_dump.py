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
    parser.add_argument("dump_file", type=Path)

    args = parser.parse_args(sys.argv[1:])
    MANIFEST_PATH = args.manifest
    MANIFEST = read_manifest(MANIFEST_PATH)

    ret = subprocess.run(
        [
            "bazel",
            "run",
            "//tools/scene_dump:migrate_dump",
            BZL_CONFIG,
            "--",
            args.dump_file.absolute(),
            args.dump_file.absolute(),
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )

    if ret.returncode == 0:
        print("OK")
    else:
        print(ret.stdout.decode("utf-8"))
        sys.exit(ret.returncode)
