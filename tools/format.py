import argparse
import subprocess
import sys
import shutil
import os
import platform
from pathlib import Path
import json
import multiprocessing
import re


def retrieve_bazel_info(all, wanted):
    for line in all:
        if line.startswith(wanted + ":"):
            return line[(len(wanted) + 2) :]
    return ""


def run_command(cmd, name):
    ret = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if ret.returncode != 0:
        print("ERROR: %s failed" % name)
        print("Command:", " ".join([str(x) for x in cmd]))
        print("stdout:\n", ret.stdout.decode("utf-8"))
        print("stderr:\n", ret.stderr.decode("utf-8"))
        sys.exit(1)


parser = argparse.ArgumentParser(
    description="Format code files. If no operation is selected, all are performed."
)
parser.add_argument(
    "-s", "--staged", action="store_true", help="only operate on staged files"
)
parser.add_argument(
    "--jj", action="store_true", help="only operate on JJ working copy files"
)
parser.add_argument(
    "-c",
    "--clang-format",
    action="store_true",
    help="run Clang-Format (format C/C++ files)",
)
parser.add_argument(
    "-p",
    "--prettier",
    action="store_true",
    help="run Prettier (format TypeScript/JavaScript/CSS files)",
)
parser.add_argument(
    "-l", "--line-endings", action="store_true", help="fix line endings"
)
parser.add_argument(
    "-b",
    "--buildifier",
    action="store_true",
    help="run Buildifier (format Bazel files)",
)
parser.add_argument(
    "-k", "--black", action="store_true", help="run Black (format Python files)"
)
args = parser.parse_args()

mode = "staged" if args.staged else "all"
mode = "jj" if args.jj else mode

run_all = (
    not args.clang_format
    and not args.prettier
    and not args.line_endings
    and not args.buildifier
    and not args.black
)
run_clang_format = run_all or args.clang_format
run_prettier = run_all or args.prettier
run_buildifier = run_all or args.buildifier
run_black = run_all or args.black
fix_line_endings = run_all or args.line_endings

bazel_info = subprocess.check_output(["bazel", "info"]).decode("utf-8").splitlines()
repo_mapping = json.loads(
    subprocess.check_output(["bazel", "mod", "dump_repo_mapping", ""])
)
output_base = Path(retrieve_bazel_info(bazel_info, "output_base"))
bin_dir = Path(retrieve_bazel_info(bazel_info, "bazel-bin"))

platform_exe_extension = ".exe" if platform.system() == "Windows" else ""


def fetch_clang_format():
    output = subprocess.run(
        ["bazel", "build", "@clang_format_prebuilt//clang-format"],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    if output.returncode != 0:
        raise RuntimeError("Failed to build clang_format")
    return (
        bin_dir
        / "external"
        / repo_mapping["clang_format_prebuilt"]
        / "clang-format"
        / ("clang-format" + platform_exe_extension)
    )


clang_format_exe = fetch_clang_format()


def fetch_file(workspace: str, file: str) -> Path:
    workspace = repo_mapping[workspace]
    target = "@@" + workspace + "//:" + file
    output = subprocess.run(
        ["bazel", "fetch", target],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    if output.returncode != 0:
        raise RuntimeError(f"Failed to fetch {target}")
    return output_base / "external" / workspace / file


buildifier_exe = fetch_file(
    f"buildifier_{platform.system().lower()}", f"buildifier{platform_exe_extension}"
)

if not shutil.which("git"):
    print("\033[93mERROR: Git required (really? :thinking:)\033[0m")
    sys.exit(1)

# Check that git settings are OK
output = (
    subprocess.check_output(["git", "config", "--get", "core.autocrlf"])
    .decode("utf-8")
    .strip()
)
if output != "false":
    print("\033[93mWARNING: git config core.autocrlf should be false\033[0m")

all_extensions = [
    "txt",
    "bazel",
    "cpp",
    "h",
    "c",
    "cc",
    "hpp",
    "inl",
    "proto",
    "bzl",
    "py",
    "js",
    "json",
    "md",
    "Config",
    "tpl",
    "css",
    "ts",
    "html",
    "cs",
    "bat",
    "sh",
    "tpl",
    "frag",
    "vert",
    "glsl",
    "patch",
    "csv",
    "php",
    "yaml",
    "yml",
    "vue",
]
all_files_names = [".gitlab-ci.yml", "Dockerfile"]
cpp_extensions = ["cpp", "h", "c", "cc", "hpp", "inl", "proto"]
bazel_extensions = ["bzl", "bazel"]

all_files = []
cpp_files = []
bazel_files = []

print("Discovering files....")

if mode == "all":
    cmd = ["git", "ls-files", "--cached", "--others", "--exclude-standard"]
    for line in subprocess.check_output(cmd).decode("utf-8").splitlines():
        ext = os.path.splitext(line)[1][1:]
        basename = os.path.basename(line)
        if ext in all_extensions or basename in all_files_names:
            all_files.append(line)
elif mode == "staged":
    files = (
        subprocess.check_output(
            [
                "git",
                "diff-index",
                "--cached",
                "--diff-filter=ACMRTUXB",
                "--name-only",
                "HEAD",
            ]
        )
        .decode("utf-8")
        .splitlines()
    )
    for f in files:
        ext = os.path.splitext(f)[1][1:]
        basename = os.path.basename(f)
        if ext in all_extensions:
            all_files.append(f)
        elif basename in all_files_names:
            all_files.append(f)
elif mode == "jj":
    files = (
        subprocess.check_output(
            [
                "jj",
                "show",
                "-r",
                "@",
                "-s",
                "-T",
                "''",
                "--no-pager",
                "--color",
                "never",
            ]
        )
        .decode("utf-8")
        .splitlines()
    )
    use_ops = ["A", "M", "R", "C"]
    skip_ops = ["D"]
    for f in files:
        op = f[0]
        if op in skip_ops:
            continue
        if op not in use_ops:
            raise RuntimeError(
                f"Unexpected operation '{op}' in JJ output, expected one of {use_ops}"
            )

        filename = f[2:]
        if op in ("R", "C"):
            # Change hrz\doc\{doc_internal => doc}\img\impostors.png
            # to hrz\doc\doc\img\impostors.png
            filename = re.sub(r"\{[^{}]*=>\s*([^{}]*)\}", r"\1", filename)
            filename = str(Path(filename))

        ext = os.path.splitext(filename)[1][1:]
        basename = os.path.basename(filename)

        if ext in all_extensions:
            all_files.append(filename)
        elif basename in all_files_names:
            all_files.append(filename)

print("Excluding bazel registry module files...")

# Remove files that are in third_party/bazel_registry/modules/
bazel_registry_modules_path = Path("third_party/bazel_registry/modules")
all_files = [
    f for f in all_files if not Path(f).is_relative_to(bazel_registry_modules_path)
]

print("Filtering C++ files...")
for f in all_files:
    ext = os.path.splitext(f)[1][1:]
    if ext in cpp_extensions:
        cpp_files.append(f)

print("Filtering Bazel files...")
for f in all_files:
    ext = os.path.splitext(f)[1][1:]
    if ext in bazel_extensions:
        bazel_files.append(f)

print("Excluding third_party files from C++ files...")
cpp_files = [f for f in cpp_files if not f.startswith("third_party")]

print("Excluding template files from C++ files...")
cpp_files = [f for f in cpp_files if not ".tpl." in f]

if run_clang_format:
    # Run Clang Format for c & cpp files in parallel
    import concurrent.futures

    print("Running Clang Format...")

    def format_file(f):
        run_command([clang_format_exe, "-i", f], "Clang Format")
        return f

    num_workers = multiprocessing.cpu_count()
    total = len(cpp_files)
    with concurrent.futures.ThreadPoolExecutor(max_workers=num_workers) as executor:
        futures = {executor.submit(format_file, f): i for i, f in enumerate(cpp_files)}
        for i, future in enumerate(concurrent.futures.as_completed(futures)):
            idx = futures[future]
            print(f"\r    Formatting file {idx + 1}/{total}", end="")
            # Raise exception if any
            future.result()
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
    print("Running Buildifier...")
    i = 0
    while i < len(bazel_files):
        cmd = [
            str(buildifier_exe),
            "-lint",
            "fix",
        ]
        cmd_len = sum(len(x) + 1 for x in cmd)
        MAX_CMD_LEN = 8000
        while i < len(bazel_files) and cmd_len + len(bazel_files[i]) + 1 < MAX_CMD_LEN:
            cmd.append(bazel_files[i])
            cmd_len += len(bazel_files[i]) + 1
            i += 1
        print(f"\r    Formatting files {i}/{len(bazel_files)}", end="")
        run_command(cmd, "Buildifier")
    print("\n    Done")

if run_black:
    # Run Black for Python files
    print("Running Black...")
    black_cmd = ["bazel", "run", "//third_party:black", "--"]
    should_run = True
    if mode != "all":
        py_files = [f for f in all_files if f.endswith(".py")]
        if len(py_files) == 0:
            should_run = False
        else:
            black_cmd += [Path(f).resolve() for f in all_files if f.endswith(".py")]
    else:
        black_cmd += [os.getcwd()]

    if should_run:
        run_command(black_cmd, "Black")

if fix_line_endings:
    # Fix line endings for all text files
    print("Fixing line endings...")

    for i, f in enumerate(all_files):
        print("\r    Fixing file %d/%d" % (i + 1, len(all_files)), end="")
        lines = Path(f).read_bytes().splitlines()
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
            Path(f).write_bytes(fixed_output.encode("utf-8"))
    print("\n    Done")

if mode == "staged":
    print("Re-staging files...")
    for f in all_files:
        run_command(["git", "add", f], "Git Add")
