from pathlib import Path
import tempfile
import shutil
import os
import requests
import tarfile
import subprocess

class TempFile:
    def __init__(self, path):
        self.path = Path(path)

    def __del__(self):
        try:
            os.remove(self.path)
        except Exception:
            print(f"Failed to remove {self.path}")

class TempDir:
    def __init__(self, path):
        self.path = Path(path)

    def __del__(self):
        try:
            shutil.rmtree(self.path)
        except Exception:
            print(f"Failed to remove {self.path}")

def create_temp_file(suffix: str | None = None):
    fd, path = tempfile.mkstemp(suffix=suffix)
    os.close(fd)
    return TempFile(path)

def create_temp_dir():
    path = tempfile.mkdtemp()
    return TempDir(path)

def download_file(url, dst):
    with requests.get(url, stream=True) as r:
        r.raise_for_status()
        with open(dst, "wb") as f:
            for chunk in r.iter_content(chunk_size=1024 * 1024):
                f.write(chunk)
    return True

def untar(archive, dst):
    tar = tarfile.open(archive)
    output = Path(dst)
    if not output.exists():
        output.mkdir(parents=True)
    tar.extractall(path = output)
    tar.close()

def run_command(cmd, directory=None):
    res = subprocess.run(cmd, cwd=directory)
    return res.returncode == 0

def apply_patch(patch: str, in_dir: Path):
    patch_file = create_temp_file()
    patch_file.path.write_bytes(patch.encode())
    if not run_command(["git", "apply", patch_file.path], directory=in_dir):
        raise RuntimeError(f"Failed to apply patch {patch}")

