import sys
from pathlib import Path
import shutil
import platform

dependencies_dir_parent = (Path(__file__).parent / "../..").resolve()
sys.path.insert(0, str(dependencies_dir_parent))

from tools.dependencies.common.utils import (
    create_temp_dir,
    create_temp_file,
    download_file,
    untar,
    run_command,
)

if len(sys.argv) < 4:
    print("Usage: %s <version> <platform> <install_dir>" % sys.argv[0])
    sys.exit(1)

install_dir = Path(sys.argv[3])
version = sys.argv[1]

ARTIFACT_URL = (
    f"https://github.com/KhronosGroup/glslang/archive/refs/tags/{version}.tar.gz"
)
ROOT_DIR_NAME = f"glslang-{version}"

if platform.system().lower() != sys.argv[2]:
    raise RuntimeError("Invalid platform")

print("Downloading glslang")
archive = create_temp_file()
if not download_file(ARTIFACT_URL, archive.path):
    raise RuntimeError("Couldn't download glslang archive")

print("Extracting glslang")
source_dir = create_temp_dir()
untar(archive.path, source_dir.path)

print("Updating glslang sources")
source_root_dir = source_dir.path / ROOT_DIR_NAME
if not run_command(
    [sys.executable, "update_glslang_sources.py"], directory=source_root_dir
):
    raise RuntimeError("Failed to update glslang sources")

print("Configure CMake build")
build_dir = create_temp_dir()
if not run_command(
    [
        "cmake",
        "-DCMAKE_BUILD_TYPE=Release",
        "-S",
        source_root_dir,
        "-B",
        build_dir.path,
        "-DENABLE_HLSL=OFF",
    ]
):
    raise RuntimeError("Couldn't configure CMake build")

print("Building")
if not run_command(
    [
        "cmake",
        "--build",
        build_dir.path,
        "--config",
        "Release",
        "--target",
        "glslang-standalone",
    ]
):
    raise RuntimeError("Failed to build glslang")

bin_path = None
if platform.system() == "Windows":
    bin_path = build_dir.path / "StandAlone/Release/glslang.exe"
elif platform.system() == "Linux":
    bin_path = build_dir.path / "StandAlone/glslang"
else:
    raise RuntimeError("Unsupported platform")

# We can't use cmake install because it can't install partial builds :(
print("Copying artifacts")
if not shutil.copy(bin_path, install_dir):
    raise RuntimeError("Couldn't copy glslang executable")

print("Cleanup")
