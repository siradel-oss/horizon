import sys
from pathlib import Path
import shutil
import subprocess
import re

index_re = re.compile(r"^// (\d+): (\S+)")
error_re = re.compile(r"^ERROR: (\d+):(\d+): (.*)$")

input_path = Path(sys.argv[1])
output_path = Path(sys.argv[2])
stage = sys.argv[3]
check_version = sys.argv[4]
validator_path = Path(sys.argv[5])
compressor_path = Path(sys.argv[6])

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
)
if output.returncode != 0:
    filenames = {}
    with open(input_path, "rb") as fp:
        lines = fp.readlines()[1:]
        for l in lines:
            m = index_re.search(l.decode("utf-8"))
            if m != None:
                filenames[int(m.group(1))] = m.group(2)
            else:
                break
    output = output.stdout.decode("utf-8").splitlines()
    for line in output:
        m = error_re.search(line)
        if m != None:
            print(
                "ERROR: %s:%s: %s"
                % (filenames[int(m.group(1))], m.group(2), m.group(3).rstrip())
            )
        else:
            print(line.rstrip())
        pass
    sys.exit(1)
else:
    lines = []
    with open(input_path, "rb") as fp:
        lines = fp.readlines()
    if not lines[0].startswith(f"#version {check_version}".encode("utf-8")):
        print("Invalid version")
        sys.exit(1)
    with open(output_path, "wb+") as fp:
        fp.write(b"".join(lines[1:]))
    output = subprocess.run([compressor_path, output_path, output_path])
    if output.returncode != 0:
        print("Couldn't compress shader file")
        sys.exit(1)
