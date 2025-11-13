import platform
import hashlib
import subprocess
import shutil
import sys
from pathlib import Path

from hrz.protocol.history.manifest import Manifest

BZL_CONFIG = "--config=" + platform.system().lower()

INITIAL_HASH = "0000000000000000000000000000000000000000000000000000000000000000"
DESCRIPTORS_PATH = "hrz/protocol/history"
MANIFEST_PATH = "hrz/protocol/history/versions_manifest.csv"

def compute_file_hash(path):
    file_content = None
    with open(path, "rb") as fp:
        file_content = fp.read()
    m = hashlib.sha3_256()
    m.update(file_content)
    return m.hexdigest()

def build_current_descriptor_set():
    ret = subprocess.run(
        ["bazel", "build", "//hrz/protocol:descriptor_set.pbf", BZL_CONFIG],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE)

    if ret.returncode != 0:
        print("Error when building protocol descriptor set:")
        print("")
        print(ret.stderr.decode("utf-8"))
        sys.exit(ret.returncode)

def save_current_descriptor_set(id):
    build_current_descriptor_set()
    src_file = "bazel-bin/hrz/protocol/descriptor_set.pbf"
    dst_file = f"{DESCRIPTORS_PATH}/{id}.pbf"
    shutil.copyfile(src_file, dst_file)

def gather_unused_descriptors(manifest: Manifest):
    files = set(Path(DESCRIPTORS_PATH).glob("*.pbf"))
    manifest_descriptor_files = [Path(DESCRIPTORS_PATH) / Path(f"{row.id}.pbf") for row in manifest.entries]
    for descriptor_file in manifest_descriptor_files:
        if descriptor_file in files:
            files.remove(descriptor_file)
        else:
            print(f"Missing descriptor file: {descriptor_file}.")
            sys.exit(1)
    return list(files)
