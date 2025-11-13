import sys
from pathlib import Path

sys.path.append("")
from hrz.protocol.history.manifest import read_manifest

from common import MANIFEST_PATH

if __name__ == "__main__":
    MANIFEST = read_manifest(Path(MANIFEST_PATH))
    print(MANIFEST.last_entry().id)
