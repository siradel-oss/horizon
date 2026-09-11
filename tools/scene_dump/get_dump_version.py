# SPDX-FileCopyrightText: Copyright 2025 Siradel
# SPDX-License-Identifier: MIT

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

    path = args.dump_file.absolute()

    ret = subprocess.run(
        ["bazel", "run", "//tools/scene_dump:get_dump_version", BZL_CONFIG, "--", path],
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
    )

    if ret.returncode == 0:
        last_line = ret.stdout.splitlines()[-1]
        print(last_line.decode("utf-8"))
    else:
        sys.exit(ret.returncode)
