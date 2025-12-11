import subprocess
import sys
from os import path
import platform


def retrieve_bazel_info(all, wanted):
    for line in all:
        if line.startswith(wanted + ":"):
            return line[(len(wanted) + 2) :]
    return ""


config = platform.system().lower() + "_clang"

additional_args = []
if "--" in sys.argv:
    additional_args = sys.argv[sys.argv.index("--") + 1 :]

print("Running bazel info")
bazel_info = (
    subprocess.check_output(["bazel", "info"] + additional_args)
    .decode("utf-8")
    .splitlines()
)
execution_root = retrieve_bazel_info(bazel_info, "execution_root")
bazel_bin = retrieve_bazel_info(bazel_info, "bazel-bin")

print("    execution_root: %s" % execution_root)
print("    bazel-bin:      %s" % bazel_bin)

print("Generating compile_commands.json")

subprocess.call(
    ["bazel", "build", "//:compilation_database", "--config=" + config]
    + additional_args
)

print("Patching compile_commands.json")

content = ""
with open(path.join(bazel_bin, "compile_commands.json"), "r") as fp:
    content = fp.read()

content = content.replace("__EXEC_ROOT__", execution_root)

with open("compile_commands.json", "w") as fp:
    fp.write(content)
