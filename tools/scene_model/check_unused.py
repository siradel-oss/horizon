import sys

sys.path.append("")
from hrz.protocol.history.manifest import read_manifest

from common import MANIFEST_PATH, gather_unused_descriptors

if __name__ == "__main__":
    MANIFEST = read_manifest(MANIFEST_PATH)

    unused_descriptors = gather_unused_descriptors(MANIFEST)
    if unused_descriptors:
        for f in unused_descriptors:
            print(f"Unused descriptor \"{str(f)}\"")
        sys.exit(1)

    print("OK")
