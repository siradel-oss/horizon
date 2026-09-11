# SPDX-FileCopyrightText: Copyright 2026 Siradel
# SPDX-License-Identifier: MIT

import sys
import json
import shutil
from pathlib import Path

manifest_file = Path(sys.argv[1])

# Each entry is : ['f' | 'd', src_path, dest_path]
# where 'f' means file and 'd' means directory.
manifest = json.loads(manifest_file.read_bytes())

for entry in manifest:
    entry_type, src, dest = entry
    src_path = Path(src)
    dest_path = Path(dest)
    if entry_type == "f":
        dest_path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src_path, dest_path)
    elif entry_type == "d":
        shutil.copytree(src_path, dest_path, dirs_exist_ok=True)
