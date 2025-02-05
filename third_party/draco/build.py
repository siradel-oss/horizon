import sys
from pathlib import Path
import shutil
import platform
import os

dependencies_dir_parent = (Path(__file__).parent / "../..").resolve()
sys.path.insert(0, str(dependencies_dir_parent))

from tools.dependencies.common.utils import create_temp_dir, create_temp_file, download_file, untar, run_command, apply_patch

if len(sys.argv) < 4:
    print("Usage: %s <version> <platform> <install_dir>" % sys.argv[0])
    sys.exit(1)

install_dir = Path(sys.argv[3])
version = sys.argv[1]
target_platform = sys.argv[2]

ARCHIVE_URL = f"https://github.com/google/draco/archive/refs/tags/{version}.tar.gz"
ROOT_FOLDER = f"draco-{version}"

if target_platform != "wasm" and platform.system().lower() != target_platform:
    raise RuntimeError("Invalid platform")

print("Downloading Draco")
draco_archive = create_temp_file()
if not download_file(ARCHIVE_URL, draco_archive.path):
    raise RuntimeError("Couldn't download Draco archive")

print("Extracting Draco")
draco_dir = create_temp_dir()
untar(draco_archive.path, draco_dir.path)

draco_src_dir = draco_dir.path / ROOT_FOLDER

print("Applying patches")
# Patches might need to change when changing version
assert(version == "1.5.7")
patch_files = Path("third_party/patches").glob("draco_*")
for patch_file in patch_files:
    print(f"    Applying patch {patch_file}")
    patch = patch_file.read_bytes().decode()
    apply_patch(patch, draco_src_dir)

extra_config_args = []
if target_platform == "wasm":
    if not "EMSDK" in os.environ:
        raise RuntimeError("EMSDK environment variable is not set")

    cmake_toolchain_file = Path(os.environ["EMSDK"]) / "upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake"
    extra_config_args.append(f"-DCMAKE_TOOLCHAIN_FILE={cmake_toolchain_file}")

    extra_config_args += ['-DCMAKE_CXX_FLAGS="-mbulk-memory"']

    if platform.system() == "Windows":
        extra_config_args += ["-G", "Ninja"]

def copy_file(from_path: Path, to_path: Path):
    if not shutil.copy(from_path, to_path):
        raise RuntimeError(f"Couldn't copy {from_path}")

print("Copying headers")
def ignore_all_but_headers(dir: str, names: list[str]) -> list[str]:
    return [name for name in names if not name.endswith(".h") and not (Path(dir) / name).is_dir()]
os.makedirs(install_dir / "include/", exist_ok=True)
shutil.copytree(draco_src_dir / "src/", install_dir / "include/", ignore=ignore_all_but_headers, dirs_exist_ok=True)

for build_type in ["Release", "Debug"]:
    print(f"Configure CMake {build_type} build")
    build_dir = create_temp_dir()
    if not run_command(["cmake", "-S", draco_src_dir, "-B", build_dir.path, "-DCMAKE_BUILD_TYPE=" + build_type, "-DDRACO_JS_GLUE=OFF"] + extra_config_args):
        raise RuntimeError("Couldn't configure CMake build")

    print("Building Draco")
    if not run_command(["cmake", "--build", build_dir.path, "--config", build_type]):
        raise RuntimeError("Failed to build Draco")

    print("Copying artifacts")
    os.makedirs(install_dir / "include/draco", exist_ok=True)
    copy_file(build_dir.path / "draco/draco_features.h", install_dir / "include/draco/draco_features.h")

    os.makedirs(install_dir / f"lib/{build_type}", exist_ok=True)
    if target_platform == "windows":
        copy_file(build_dir.path / f"{build_type}/draco.lib", install_dir / f"lib/{build_type}/draco.lib")
    else:
        copy_file(build_dir.path / "libdraco.a", install_dir / f"lib/{build_type}/libdraco.a")

print("Cleanup")

