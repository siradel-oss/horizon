import sys
from pathlib import Path
import shutil
import platform
import json
import requests

dependencies_dir_parent = (Path(__file__).parent / "../..").resolve()
sys.path.insert(0, str(dependencies_dir_parent))

from tools.dependencies.common.utils import create_temp_dir, create_temp_file, download_file, untar, run_command

if len(sys.argv) < 4:
    print("Usage: %s <version> <platform> <install_dir>" % sys.argv[0])
    sys.exit(1)

install_dir = Path(sys.argv[3])
version = sys.argv[1]

MODULE_URL = f"https://raw.githubusercontent.com/bazelbuild/bazel-central-registry/refs/heads/main/modules/curl/{version}"

if platform.system().lower() != sys.argv[2]:
    raise RuntimeError("Invalid platform")

print("Downloading curl bzlmod manifest")
SOURCE_URL = MODULE_URL + "/source.json"
source = json.loads(requests.get(SOURCE_URL).text)

print("Downloading package")
archive = create_temp_file()
if not download_file(source["url"], archive.path):
    raise RuntimeError("Couldn't download curl archive")

print("Extracting package")
unpack_dir = create_temp_dir()
untar(archive.path, unpack_dir.path)

source_dir = unpack_dir.path
if source.get("strip_prefix"):
    source_dir = unpack_dir.path / source["strip_prefix"]

patch_strip = source.get("patch_strip", 1)

print("Applying patches")
for patch in source.get("patches", []):
    print("    Applying patch", patch)
    patch_url = MODULE_URL + "/patches/" + patch
    patch_file = create_temp_file()
    if not download_file(patch_url, patch_file.path):
        raise RuntimeError(f"Couldn't download patch {patch}")
    if not run_command(["git", "apply", f"-p{patch_strip}", patch_file.path], directory=source_dir):
        raise RuntimeError(f"Failed to apply patch {patch}")

# Add zlib to the MODULE and BUILD files
# This might need to change when updating, so we assert on the version here
assert(version == "8.8.0.bcr.1")
patch = '''
diff --git a/BUILD.bazel b/BUILD.bazel
index a57440ceb..1c9e3e9f4 100644
--- a/BUILD.bazel
+++ b/BUILD.bazel
@@ -202,6 +202,7 @@ _BASE_CURL_COPTS = [
     "-DHAVE_VARIADIC_MACROS_GCC=1",
     "-DHAVE_WRITABLE_ARGV=1",
     "-DHAVE_WRITEV=1",
+    "-DHAVE_LIBZ=1",
     "-DRECV_TYPE_ARG1=int",
     "-DRECV_TYPE_ARG2=void*",
     "-DRECV_TYPE_ARG3=size_t",
@@ -252,6 +253,7 @@ cc_library(
             "/DUSE_IPV6",
             "/DUSE_WINDOWS_SSPI",
             "/DUSE_SCHANNEL",
+            "/DHAVE_LIBZ=1",
         ],
         "@platforms//os:macos": _BASE_CURL_COPTS,
         "//conditions:default": _BASE_CURL_COPTS + [
@@ -320,7 +322,9 @@ cc_library(
         ":ca_path",
     ],
     visibility = ["//visibility:public"],
-    deps = select({
+    deps = [
+        "@zlib//:zlib",
+    ] + select({
         ":use_mbedtls_setting": ["@mbedtls"],
         "//conditions:default": [],
     }) + select({
diff --git a/MODULE.bazel b/MODULE.bazel
index e1fcda8dc..e6a9816d6 100644
--- a/MODULE.bazel
+++ b/MODULE.bazel
@@ -8,3 +8,4 @@ bazel_dep(name = "bazel_skylib", version = "1.7.1")
 bazel_dep(name = "mbedtls", version = "3.6.0")
 bazel_dep(name = "platforms", version = "0.0.10")
 bazel_dep(name = "boringssl", version = "0.0.0-20230215-5c22014")
+bazel_dep(name = "zlib", version = "1.3.1.bcr.3")
'''
patch_file = create_temp_file()
patch_file.path.write_bytes(patch.encode())

if not run_command(["git", "apply", patch_file.path], directory=source_dir):
    raise RuntimeError(f"Failed to apply zlib patch")

lib_ext = ".a"
lib_prefix = "lib"
if platform.system() == "Windows":
    lib_ext = ".lib"
    lib_prefix = ""

for cfg in ["opt", "dbg"]:
    lib_dir = install_dir / "lib" / cfg
    lib_dir.mkdir(parents=True, exist_ok=True)

    print(f"Building boringssl, {cfg}")
    if not run_command(["bazel", "--batch", "build", "@boringssl//:crypto", "-c", cfg], directory=source_dir):
        raise RuntimeError("Failed to build curl")

    if not run_command(["bazel", "--batch", "build", "@boringssl//:ssl", "-c", cfg], directory=source_dir):
        raise RuntimeError("Failed to build curl")

    print(f"Building curl, {cfg}")
    if not run_command(["bazel", "--batch", "build", "//:curl", "-c", cfg], directory=source_dir):
        raise RuntimeError("Failed to build curl")

    shutil.copy(source_dir / f"bazel-bin/{lib_prefix}curl{lib_ext}", lib_dir)
    shutil.copy(source_dir / f"bazel-bin/external/boringssl+/{lib_prefix}ssl{lib_ext}", lib_dir)
    shutil.copy(source_dir / f"bazel-bin/external/boringssl+/{lib_prefix}crypto{lib_ext}", lib_dir)

print("Copying headers")
dst_include_dir = install_dir / "include/curl"
dst_include_dir.mkdir(parents=True, exist_ok=True)

src_include_dir = source_dir / "include/curl"
for f in src_include_dir.glob("*.h"):
    shutil.copy(f, dst_include_dir)

print("Downloading cacert.pem")
CACERT_URL = "https://curl.se/ca/cacert.pem"
download_file(CACERT_URL, install_dir / "cacert.pem")

print("Cleanup")
