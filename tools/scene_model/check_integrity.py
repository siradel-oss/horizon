# SPDX-FileCopyrightText: Copyright 2025 Siradel
# SPDX-License-Identifier: MIT

import sys

sys.path.append("")
from hrz.protocol.history.manifest import read_manifest, compute_hash

from common import (
    MANIFEST_PATH,
    INITIAL_HASH,
    DESCRIPTORS_PATH,
    compute_file_hash,
    build_current_descriptor_set,
)

if __name__ == "__main__":
    MANIFEST = read_manifest(MANIFEST_PATH)

    last_hash = INITIAL_HASH
    for entry in MANIFEST.entries:
        descriptor_hash = compute_file_hash(f"{DESCRIPTORS_PATH}/{entry.id}.pbf")
        if descriptor_hash != entry.descriptor_hash:
            print(
                f"Migration manifest integrity error at version {entry.id}: {entry.description}"
            )
            print(
                "The descriptor hash does not match the one declared in the manifest file"
            )
            print("This might be due to an error during a merge or rebase")
            sys.exit(1)
        hash = entry.chain_hash
        test_hash = compute_hash(last_hash, entry.id, descriptor_hash)
        if hash != test_hash:
            print(
                f"Migration manifest integrity error at version {entry.id}: {entry.description}"
            )
            print("The chain hash doesn't match the one declared in the manifest file")
            print("This might be due to an error during a merge or rebase")
            sys.exit(1)
        last_hash = hash
        print(f"{entry.id} OK")

    build_current_descriptor_set()

    hash1 = compute_file_hash(f"{DESCRIPTORS_PATH}/{MANIFEST.last_entry().id}.pbf")
    hash2 = compute_file_hash("bazel-bin/hrz/protocol/descriptor_set.pbf")

    if hash1 != hash2:
        print(
            "The current latest version of the descriptor set in the manifest doesn't match the current scene model."
        )
        print(
            "Please update the latest descriptor set with update_latest_version.py if it is compatible."
        )
        print("Please create a new version and migration with new_version.py otherwise")
        sys.exit(1)

    print("OK")
