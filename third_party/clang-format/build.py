import sys
from pathlib import Path
import shutil
import platform

dependencies_dir_parent = (Path(__file__).parent / "../..").resolve()
sys.path.insert(0, str(dependencies_dir_parent))

from tools.dependencies.common.utils import create_temp_dir, create_temp_file, download_file, untar, run_command

if len(sys.argv) < 4:
    print("Usage: %s <version> <platform> <install_dir>" % sys.argv[0])
    sys.exit(1)

install_dir = Path(sys.argv[3])
version = sys.argv[1]

LLVM_URL = f"https://github.com/llvm/llvm-project/releases/download/llvmorg-{version}/llvm-project-{version}.src.tar.xz"
ROOT_FOLDER = f"llvm-project-{version}.src"

if platform.system().lower() != sys.argv[2]:
    raise RuntimeError("Invalid platform")

print("Downloading LLVM")
llvm_archive = create_temp_file()
if not download_file(LLVM_URL, llvm_archive.path):
    raise RuntimeError("Couldn't download LLVM archive")

print("Extracting LLVM")
llvm_dir = create_temp_dir()
untar(llvm_archive.path, llvm_dir.path)

print("Configure CMake build")
build_dir = create_temp_dir()
if not run_command(["cmake", "-S", llvm_dir.path / ROOT_FOLDER / "llvm", "-B", build_dir.path, "-DLLVM_ENABLE_PROJECTS=clang", "-DCMAKE_BUILD_TYPE=Release"]):
    raise RuntimeError("Couldn't configure CMake build")

print("Build clang-format")
if not run_command(["cmake", "--build", build_dir.path, "--config", "Release", "--target", "clang-format"]):
    raise RuntimeError("Failed to build clang-format")

bin_path = None
if platform.system() == "Windows":
    bin_path = build_dir.path / "Release/bin/clang-format.exe"
elif platform.system() == "Linux":
    bin_path = build_dir.path / "bin/clang-format"
else:
    raise RuntimeError("Unsupported platform")

# We can't use cmake install because it can't install partial builds :(
print("Copying artifacts")
if not shutil.copy(bin_path, install_dir):
    raise RuntimeError("Couldn't copy clang-format executable")

print("Cleanup")

