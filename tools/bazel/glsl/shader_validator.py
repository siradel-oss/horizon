# SPDX-FileCopyrightText: Copyright 2023 Siradel
# SPDX-License-Identifier: MIT

import re
import subprocess
import sys
from pathlib import Path

index_re = re.compile(r"^// (\d+): (\S+)")
error_re = re.compile(r"^ERROR: (\d+):(\d+): (.*)$")

input_path = Path(sys.argv[1])
output_path = Path(sys.argv[2])
stage = sys.argv[3]
check_version = sys.argv[4]
validator_path = Path(sys.argv[5])

output = subprocess.run(
    [
        validator_path,
        "--target-env",
        "opengl",
        "-S",
        stage,
        "--auto-map-bindings",
        "--auto-map-locations",
        input_path,
    ],
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT,
    check=False,
)
if output.returncode != 0:
    filenames = {}
    with open(input_path, "rb") as fp:
        lines = fp.readlines()[1:]
        for l in lines:
            m = index_re.search(l.decode())
            if m != None:
                filenames[int(m.group(1))] = m.group(2)
            else:
                break
    output = output.stdout.decode().splitlines()
    for line in output:
        m = error_re.search(line)
        if m != None:
            print(
                f"ERROR: {filenames[int(m.group(1))]}:{m.group(2)}: {m.group(3).rstrip()}"
            )
        else:
            print(line.rstrip())
    sys.exit(1)

with open(input_path, "rb") as fp:
    if not fp.readline().startswith(f"#version {check_version}".encode()):
        print("Invalid version")
        sys.exit(1)

Path(output_path).touch()
