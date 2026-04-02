import hashlib
import base64
from pathlib import Path
import urllib.request


def make_integrity_string(hash: bytes):
    return "sha256-" + base64.b64encode(hash).decode("utf-8")


# Bazel registry overlay files can contain a relative path
# to another file. In which case we need to read
def maybe_get_link_file_path(file_path: Path) -> Path:
    content = file_path.read_bytes().decode().strip()
    if len(content.splitlines()) == 1:
        linked_file_path = (file_path.parent / content).resolve()
        if linked_file_path.is_file():
            return linked_file_path
    return file_path


def compute_integrity_file(file_path: Path, can_be_indirect: bool = False) -> str:
    if can_be_indirect:
        file_path = maybe_get_link_file_path(file_path)
    hash_func = hashlib.sha256()
    with open(file_path, "rb") as f:
        while chunk := f.read(8192):
            hash_func.update(chunk)
    return make_integrity_string(hash_func.digest())


def compute_integrity_url_streaming(url):
    hash_func = hashlib.sha256()
    with urllib.request.urlopen(url) as response:
        while chunk := response.read(8192):
            hash_func.update(chunk)
    return make_integrity_string(hash_func.digest())
