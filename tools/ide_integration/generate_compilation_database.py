import subprocess
import os
import sys
from os import path

PLATFORM_TO_TARGET = {
    "windows": ["//:compilation_database_windows", "compile_commands.json", "--config=windows_clang"],
}

def retrieve_bazel_info(all, wanted):
    for line in all:
        if line.startswith(wanted + ":"):
            return line[(len(wanted) + 2):]
    return ""

if len(sys.argv) < 2 or sys.argv[1] not in PLATFORM_TO_TARGET:
    print("Select a platform:")
    print("    - windows")
    sys.exit(1)

additional_args = []
if "--" in sys.argv:
    additional_args = sys.argv[sys.argv.index("--") + 1:]

target = PLATFORM_TO_TARGET[sys.argv[1]]

print("Running bazel info")
bazel_info = subprocess.check_output(["bazel", "info"] + additional_args).decode("utf-8").splitlines()
execution_root = retrieve_bazel_info(bazel_info, "execution_root")
bazel_bin = retrieve_bazel_info(bazel_info, "bazel-bin")

print("    execution_root: %s" % execution_root)
print("    bazel-bin:      %s" % bazel_bin)

print("Generating compile_commands.json")

subprocess.call(["bazel", "build", target[0], target[2]] + additional_args)

print("Patching compile_commands.json")

content = ""
with open(path.join(bazel_bin, target[1]), "r") as fp:
    content = fp.read()

content = content.replace("__EXEC_ROOT__", execution_root)

with open("compile_commands.json", "w") as fp:
    fp.write(content)
