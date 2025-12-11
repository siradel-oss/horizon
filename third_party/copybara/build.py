import sys
from pathlib import Path
import shutil

dependencies_dir_parent = (Path(__file__).parent / "../..").resolve()
sys.path.insert(0, str(dependencies_dir_parent))

from tools.dependencies.common.utils import (
    create_temp_dir,
    create_temp_file,
    download_file,
    untar,
    run_command,
    apply_patch,
)

if len(sys.argv) < 4:
    print("Usage: %s <version> <platform> <install_dir>" % sys.argv[0])
    sys.exit(1)

install_dir = Path(sys.argv[3])
version_with_suffix = sys.argv[1]
version_without_suffix = version_with_suffix.split("/")[0]
target_platform = sys.argv[2]

ARCHIVE_URL = (
    f"https://github.com/google/copybara/archive/{version_without_suffix}.tar.gz"
)
ROOT_FOLDER = f"copybara-{version_without_suffix}"

print("Downloading copybara")
copybara_archive = create_temp_file()
if not download_file(ARCHIVE_URL, copybara_archive.path):
    raise RuntimeError("Couldn't download copybara archive")

print("Extracting copybara")
copybara_dir = create_temp_dir()
untar(copybara_archive.path, copybara_dir.path)

source_dir = copybara_dir.path / ROOT_FOLDER

if not run_command(
    [
        "bazel",
        "--batch",
        "build",
        "--java_language_version=17",
        "//java/com/google/copybara:copybara_deploy.jar",
        "-c",
        "opt",
    ],
    directory=source_dir,
):
    raise RuntimeError("Failed to build copybara")


def copy_file(from_path: Path, to_path: Path):
    if not shutil.copy(from_path, to_path):
        raise RuntimeError(f"Couldn't copy {from_path}")


copy_file(
    source_dir / "bazel-bin/java/com/google/copybara/copybara_deploy.jar",
    install_dir / "copybara.jar",
)

print("Cleanup")
