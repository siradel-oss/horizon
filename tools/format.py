import argparse
import subprocess
import sys
import shutil
import os
import platform
from pathlib import Path
import json

def retrieve_bazel_info(all, wanted):
    for line in all:
        if line.startswith(wanted + ":"):
            return line[(len(wanted) + 2):]
    return ""

def run_command(cmd, name):
    ret = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if ret.returncode != 0:
        print("ERROR: %s failed" % name)
        print("Command:", " ".join([str(x) for x in cmd]))
        print("stdout:\n", ret.stdout.decode("utf-8"))
        print("stderr:\n", ret.stderr.decode("utf-8"))
        sys.exit(1)

parser = argparse.ArgumentParser(description="Format code files. If no operation is selected, all are performed.")
parser.add_argument("-s", "--staged", action='store_true', help="only operate on staged files")
parser.add_argument("-c", "--clang-format", action='store_true', help="run Clang-Format (format C/C++ files)")
parser.add_argument("-p", "--prettier", action='store_true', help="run Prettier (format TypeScript/JavaScript/CSS files)")
parser.add_argument("-l", "--line-endings", action='store_true', help="fix line endings")
parser.add_argument("-b", "--buildifier", action='store_true', help="run Buildifier (format Bazel files)")
args = parser.parse_args()

mode = "staged" if args.staged else "all"

run_all = not args.clang_format and not args.prettier and not args.line_endings and not args.buildifier
run_clang_format = run_all or args.clang_format
run_prettier = run_all or args.prettier
run_buildifier = run_all or args.buildifier
fix_line_endings = run_all or args.line_endings

fd = "fd"
if mode == "all":
    if not shutil.which("fd"):
        if shutil.which("fdfind"):
            fd = "fdfind"
        else:
            print("\033[93mERROR: fd required (install package 'fd-find' (cargo, apt, etc) or https://github.com/sharkdp/fd/releases)\033[0m")
            sys.exit(1)

bazel_info = subprocess.check_output(["bazel", "info"]).decode("utf-8").splitlines()
repo_mapping = json.loads(subprocess.check_output(["bazel", "mod", "dump_repo_mapping", "_main"]))
output_base = Path(retrieve_bazel_info(bazel_info, "output_base"))

clang_format_config = {
    "Windows": {
        "workspace": repo_mapping["clang-format_windows"],
        "file": "clang-format.exe",
    },
    "Linux": {
        "workspace": repo_mapping["clang-format_linux"],
        "file": "clang-format",
    },
}[platform.system()]

clang_format_target = "@@" + clang_format_config["workspace"] + "//:" + clang_format_config["file"]
clang_format_exe = output_base / "external" / clang_format_config["workspace"] / clang_format_config["file"]

subprocess.run(["bazel", "build", clang_format_target])
subprocess.run([clang_format_exe, "--version"])

if not shutil.which("git"):
    print("\033[93mERROR: Git required (really? :thinking:)\033[0m")
    sys.exit(1)

# Check that git settings are OK
output = subprocess.check_output(["git", "config", "--get", "core.autocrlf"]).decode("utf-8").strip()
if output != "false":
    print("\033[93mWARNING: git config core.autocrlf should be false\033[0m")

all_extensions = ["txt", "bazel", "cpp", "h", "c", "cc", "hpp", "inl", "proto", "bzl", "py", "js", "json", "md", "Config", "tpl", "css", "ts", "html", "cs", "bat", "sh", "tpl", "frag", "vert", "glsl", "patch", "csv", "php", "yaml", "yml", "vue"]
all_files_names = [".gitlab-ci.yml", "Dockerfile"]
cpp_extensions = ["cpp", "h", "c", "cc", "hpp", "inl", "proto"]

all_files = []
cpp_files = []

print("Discovering files....")

if mode == "all":
    extensions_args = " ".join(["-e %s" % e for e in all_extensions])
    all_files = subprocess.check_output((fd + " " + extensions_args).split(" ")).decode("utf-8").splitlines()
    all_files += subprocess.check_output([fd, "\"" + "|".join(all_files_names) + "\""]).decode("utf-8").splitlines()
elif mode == "staged":
    files = subprocess.check_output(["git", "diff-index", "--cached", "--diff-filter=ACMRTUXB", "--name-only", "HEAD"]).decode("utf-8").splitlines()
    for f in files:
        ext = os.path.splitext(f)[1][1:]
        basename = os.path.basename(f)
        if ext in all_extensions:
            all_files.append(f)
        elif basename in all_files_names:
            all_files.append(f)

print("Filtering C++ files...")
for f in all_files:
    ext = os.path.splitext(f)[1][1:]
    if ext in cpp_extensions:
        cpp_files.append(f)

print("Excluding third_party files from C++ files...")
cpp_files = [f for f in cpp_files if not f.startswith("third_party")]

print("Excluding template files from C++ files...")
cpp_files = [f for f in cpp_files if not ".tpl." in f]

if run_clang_format:
    # Run Clang Format for c & cpp files
    print("Running Clang Format...")

    for (i, f) in enumerate(cpp_files):
        print("\r    Formatting file %d/%d" % (i + 1, len(cpp_files)), end = "")
        run_command([clang_format_exe, "-i", f], "Clang Format")
    print("\n    Done")

if run_prettier:
    # Run Prettier for TS, JS, and CSS files
    print("Running Prettier...")
    prettier_cmd = ["bazel", "run", "//:prettier", "--"]
    if mode == "staged":
        prettier_cmd += [Path(f).resolve() for f in all_files]
    else:
        prettier_cmd += [os.getcwd()]
    run_command(prettier_cmd, "Prettier")

if run_buildifier:
    # Run Buildifier for Bazel files
    print("Running Buildifier...")
    buildifier_cmd = ["bazel", "run", "//third_party:buildifier", "--", "-lint", "fix", "-r", os.getcwd()]
    run_command(buildifier_cmd, "Buildifier")

if fix_line_endings:
    # Fix line endings for all text files
    print("Fixing line endings...")

    for (i, f) in enumerate(all_files):
        print("\r    Fixing file %d/%d" % (i + 1, len(all_files)), end = "")
        lines = open(f, "rb").readlines()
        fixed_output = ""
        has_differences = False
        for l in lines:
            original_line = l.decode("utf-8")
            fixed_line = original_line.rstrip() + "\n"
            fixed_output += fixed_line
            if fixed_line != original_line:
                has_differences = True
        if has_differences:
            # Only write the file if there are differences,
            # to avoid messing with the last-modified date.
            open(f, "wb+").write(fixed_output.encode("utf-8"))
    print("\n    Done")

if mode == "staged":
    print("Re-staging files...")
    for f in all_files:
        run_command(["git", "add", f], "Git Add")

