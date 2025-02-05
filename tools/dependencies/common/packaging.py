import requests
import requests.auth
from pathlib import Path
import hashlib
import tarfile
import fnmatch
from .utils import TempFile, create_temp_file

def compute_file_digest(file_path: str | Path) -> str:
    sha256 = hashlib.sha256()
    BUF_SIZE = 8192
    with open(file_path, "rb") as f:
        while True:
            data = f.read(BUF_SIZE)
            if not data:
                break
            sha256.update(data)
    return sha256.hexdigest()

def register_nexus_auth(auth: requests.auth.HTTPBasicAuth):
    global NEXUS_AUTH
    NEXUS_AUTH = auth

def download_file_with_digest(url: str, dst: str) -> str:
    print("Downloading", url)
    sha256 = hashlib.sha256()
    with requests.get(url, stream=True) as r:
        r.raise_for_status()
        with open(dst, "wb") as f:
            for chunk in r.iter_content(chunk_size=8192):
                f.write(chunk)
                sha256.update(chunk)
    dgst = sha256.hexdigest()
    print("Computed SHA-256 digest:", dgst)
    return dgst

def upload_file(local_file: Path, url: str):
    global NEXUS_AUTH
    print(f"Uploading to {url}")
    with open(local_file, "rb") as f:
        r = requests.put(url, data=f, auth=NEXUS_AUTH)
        if not r.ok:
            print("Error: ", r.status_code)

def upload_file_with_digest(local_file: Path, digest: str, dst_dir_url: str, dst_filename: str) -> str:
    url = dst_dir_url + digest[:8] + "_" + dst_filename
    upload_file(local_file, url)
    return url

def filter_tar(tar_path: Path, prefix: str, includes: list[str], excludes: list[str]) -> TempFile:
    if len(includes) == 0:
        includes = ["*"]

    includes = [f"{prefix}/{i}" for i in includes]
    excludes = [f"{prefix}/{e}" for e in excludes]

    new_tar = create_temp_file()

    with tarfile.open(tar_path, "r") as in_tar, tarfile.open(new_tar.path, "w:gz") as out_tar:
        for member in in_tar.getmembers():
            if member.isfile():
                if any(fnmatch.fnmatch(member.name, i) for i in includes) and not any(fnmatch.fnmatch(member.name, e) for e in excludes):
                    out_tar.addfile(member, in_tar.extractfile(member))

    return new_tar


def normalize_version(version: str) -> str:
    return version.replace("/", "-")
