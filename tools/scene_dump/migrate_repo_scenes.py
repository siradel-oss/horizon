# SPDX-FileCopyrightText: Copyright 2023 Siradel
# SPDX-License-Identifier: MIT

import requests
import base64
import subprocess
import tempfile
import os
from pathlib import Path
import json
import sys
import platform

BZL_CONFIG = "--config=" + platform.system().lower()

if len(sys.argv) != 2:
    print("Usage: " + sys.argv[0] + " <namespace>")
    print("    Migrate all scenes of namespace <namespace>.")
    sys.exit(1)

print("Building migration executable")
ret = subprocess.run(
    ["bazel", "build", "//tools/scene_dump:migrate_dump", "-c", "opt", BZL_CONFIG],
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
)

if ret.returncode != 0:
    raise RuntimeError("Couldn't build migration executable")

ENDPOINT = "https://redacted.localhost/scenes"


def migrate(id, name, namespace):
    print(f'Migrating scene "{name}"')
    r = requests.get(f"{ENDPOINT}/get/{id}")
    if r.status_code != 200:
        raise RuntimeError(f'Couldn\'t retrieve scene "{name}" ({id})')

    fd, path = tempfile.mkstemp()
    os.close(fd)
    with open(path, "wb") as fp:
        fp.write(base64.b64decode(r.text))

    ret = subprocess.run([Path("bazel-bin/tools/scene_dump/migrate_dump"), path, path])
    if ret.returncode != 0:
        print(f'Error when migrating scene "{name}", skipping')
        os.remove(path)
    else:
        migrated_content = ""
        with open(path, "rb") as fp:
            migrated_content = base64.b64encode(fp.read()).decode("utf-8")
        os.remove(path)

        r = requests.put(
            f"{ENDPOINT}/add/{namespace}",
            data=json.dumps(
                {
                    "name": name,
                    "content": migrated_content,
                }
            ),
        )
        if int(r.status_code / 100) != 2:
            print(f'Error when re-adding migrated scene "{name}", skipping')
            return

        r = requests.delete(f"{ENDPOINT}/delete/{id}")
        if int(r.status_code / 100) != 2:
            print(f'Error deleting old scene "{name}", woops')


namespace = sys.argv[1]
r = requests.get(f"{ENDPOINT}/list/{namespace}")
if r.status_code != 200:
    raise RuntimeError(f'Can\'t retrieve scenes for namespace "{namespace}"')

scenes = r.json()
print(f"Found {len(scenes)} scenes")
for it in r.json():
    migrate(it["id"], it["name"], namespace)
