import sys
from pathlib import Path

sys.path.append("")
from hrz.proto.history.manifest import read_manifest, compute_hash

from common import MANIFEST_PATH, INITIAL_HASH, save_current_descriptor_set, compute_file_hash

if __name__ == "__main__":
    MANIFEST = read_manifest(MANIFEST_PATH)

    id = MANIFEST.last_entry().id
    print(f"Updating version {id}...")

    print("WARNING: You are about to overwrite the latest registered scene model descriptor set with the current one.")
    print("While this is mandatory, it should only be done if you are sure those versions are compatible (aka no migration is needed).")
    print("Otherwise please use new_version.py to create a new version & migration")
    print("")
    resp = input("Are you sure you want to do this? Type \"Yes\" if so: ")

    if resp.lower() == "yes":
        print("Updating the descriptor set")
        save_current_descriptor_set(id)
        descriptor_hash = compute_file_hash(f"hrz/proto/history/{id}.pbf")
        last_hash = INITIAL_HASH
        if len(MANIFEST) >= 2:
            last_hash = MANIFEST.entries[-2].chain_hash
        chain_hash = compute_hash(last_hash, id, descriptor_hash)
        MANIFEST.last_entry().descriptor_hash = descriptor_hash
        MANIFEST.last_entry().chain_hash = chain_hash
        print("Writing manifest...")
        MANIFEST.write(Path(MANIFEST_PATH))
        print("Done")
    else:
        print("Canceling")
