# SPDX-FileCopyrightText: Copyright 2025 Siradel
# SPDX-License-Identifier: MIT

import sys
from pathlib import Path
import re

# We don't want this do be redacted lmao
DASH = "-"
BEGIN_TAG = f"BEGIN{DASH}INTERNAL"
END_TAG = f"END{DASH}INTERNAL"

input = sys.argv[1]
output = sys.argv[2]

data = Path(input).read_bytes()
regex = f"^.*{BEGIN_TAG}[\\w\\W]*?{END_TAG}.*$\\n"
data = re.sub(regex, "", data.decode(), flags=re.MULTILINE)
Path(output).write_bytes(data.encode())
