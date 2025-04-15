import argparse
import subprocess
import sys
from os import path

def retrieve_bazel_info(all, wanted):
    for line in all:
        if line.startswith(wanted + ":"):
            return line[(len(wanted) + 2):]
    return ""

parser = argparse.ArgumentParser(description="Generates a CMakeLists.txt file for the project.")
parser.add_argument("-n", "--no-build", action='store_true', help="do not compile the targets")
parser.add_argument("--bazelrc", help="Path to bazelrc file")
parser.add_argument("--asan", action='store_true', help="Enable address sanitizer")
parser.add_argument("remaining", nargs = argparse.REMAINDER, help="Bazel arguments, must start with \"--\"")

args = parser.parse_args()
bazel_args = []
if len(args.remaining) >= 1:
    if args.remaining[0] == "--":
        bazel_args = args.remaining[1:]
    else:
        parser.print_usage()
        print("Error: Prepend Bazel argument list with \"--\"")
        parser.exit()

bazelrc = []
if args.bazelrc is not None:
    bazelrc = ["--bazelrc=" + args.bazelrc]
    print("Using bazelrc file at " + args.bazelrc)

print("Running bazel info...")
bazel_info = subprocess.check_output(["bazel"] + bazelrc + ["info"] + bazel_args).decode("utf-8").splitlines()
execution_root = retrieve_bazel_info(bazel_info, "execution_root")
bazel_bin = retrieve_bazel_info(bazel_info, "bazel-bin")
workspace = retrieve_bazel_info(bazel_info, "workspace")

print("    execution_root: %s" % execution_root)
print("    bazel-bin:      %s" % bazel_bin)

print("Generating CMakeLists.txt...")

target = "//:CMakeLists.txt" if args.no_build else "//:cmakelists"
subprocess.call(["bazel"] + bazelrc + ["build", target] + bazel_args)

print("Patching CMakeLists.txt...")

content = ""
with open(path.join(bazel_bin, "CMakeLists.txt"), "r") as fp:
    content = fp.read()

content = content.replace("__EXEC_ROOT__", execution_root)
content = content.replace("__PROJ_NAME__", "Horizon-" + workspace.split("/")[-1])

if args.asan:
    content = content.replace(
        "#__GLOBAL_OPTIONS__",
        "add_compile_options(-fsanitize=address)\nadd_link_options(-fsanitize=address)")

with open("CMakeLists.txt", "w") as fp:
    fp.write(content)

print("Done")
