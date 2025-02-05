import argparse
import sys
import random

from pathlib import Path

sys.path.append("")
from hrz.proto.history.manifest import ManifestEntry, read_manifest, compute_hash
from common import MANIFEST_PATH, save_current_descriptor_set, compute_file_hash

def make_id():
    id = random.getrandbits(32)
    id = f"{id:08x}"
    return id

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("description")

    args = parser.parse_args(sys.argv[1:])
    MANIFEST = read_manifest(MANIFEST_PATH)

    while True:
        id = make_id()
        if not MANIFEST.is_id_in(id):
            break

    print(f"New version id: {id}")

    print("Building protocol descriptor set...")
    save_current_descriptor_set(id)
    descriptor_hash = compute_file_hash(f"hrz/proto/history/{id}.pbf")

    print("Registering version...")
    print("")
    print("Please implement the following function in the hrz::migration namespace in the //hrz/scene_dump:migration library:")
    print(f"bool migration_{MANIFEST.last_entry().id}_to_{id}(const DynamicMessage& src, DynamicMessage* dst);")
    print("")

    previous_hash = MANIFEST.last_entry().chain_hash
    hash = compute_hash(previous_hash, id, descriptor_hash)
    description = args.description
    MANIFEST.entries.append(ManifestEntry(id, descriptor_hash, hash, description))

    print("Writing manifest...")
    MANIFEST.write(Path(MANIFEST_PATH))

    print("Done")
