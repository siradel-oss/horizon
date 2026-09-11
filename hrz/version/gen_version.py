# SPDX-FileCopyrightText: Copyright 2026 Siradel
# SPDX-License-Identifier: MIT

import sys, os
from pathlib import Path
from datetime import datetime

output_path = sys.argv[1]
version = sys.argv[2]


def read_status_file(env_var):
    path = os.environ.get(env_var)
    return Path(path).read_text().splitlines() if path else []


VOLATILE_LINES = read_status_file("BAZEL_VOLATILE_STATUS_FILE")
STABLE_LINES = read_status_file("BAZEL_STABLE_STATUS_FILE")


def parse_status_lines(lines):
    vars_map = {}
    for line in lines:
        line = line.strip()
        if not line:
            continue
        parts = line.split(maxsplit=1)
        key = parts[0]
        value = parts[1] if len(parts) > 1 else ""
        vars_map[key] = value
    return vars_map


VOLATILE_VARS = parse_status_lines(VOLATILE_LINES)
STABLE_VARS = parse_status_lines(STABLE_LINES)

BUILD_REVISION = STABLE_VARS.get("STABLE_GIT_COMMIT", "unknown")
BUILD_HOST = STABLE_VARS.get("BUILD_HOST", "unknown")

if not "SNAPSHOT" in version:
    BUILD_HOST = "redacted"

timestamp = int(VOLATILE_VARS.get("BUILD_TIMESTAMP", "0"))
BUILD_DATE = (
    datetime.fromtimestamp(timestamp).isoformat() if timestamp > 0 else "unknown"
)

output = ""
output += '#include "hrz/version/version.h"\n\n'
output += "namespace hrz\n{\n"
output += f'    const char* const Version = "{version}";\n'
output += f'    const char* const BuildRevision = "{BUILD_REVISION}";\n'
output += f'    const char* const BuildDate = "{BUILD_DATE}";\n'
output += f'    const char* const BuildHost = "{BUILD_HOST}";\n'
output += "}\n"

with open(output_path, "wb") as fp:
    fp.write(output.encode())
