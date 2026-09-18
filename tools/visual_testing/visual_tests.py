#!/usr/bin/env python3

# SPDX-FileCopyrightText: Copyright 2022 Siradel
# SPDX-License-Identifier: MIT

import argparse
import platform
import subprocess
import sys
from pathlib import Path

sys.path.append(str(Path(__file__).parent.parent.parent.absolute()))

from tools.visual_testing import core
from tools.visual_testing.server.run_manager import RunManager

EXE_EXT = ".exe" if platform.system() == "Windows" else ""
ROOT = Path(__file__).parent.parent.parent.absolute()
g_test_data_dir = ROOT / "tests/visual_tests/"
g_manifest: list[core.Test] = []
g_viewer = Path()
g_comparator = Path()
g_git_info = core.GitInfo(branch="<unknown>", commit="<unknown>")


def run_tests(args, tests: list[str]) -> int:
    ctx = core.TestExecutionContext(
        comparator=g_comparator,
        viewer=g_viewer,
        git_info=g_git_info,
        manifest=g_manifest,
        tests=tests,
        output_dir=core.OutputDirectory(
            Path(args.output_dir) if args.output_dir else None
        ),
        junit_output_file=Path(args.junit_report) if args.junit_report else None,
        show_viewer_window=args.show,
    )
    return core.run_tests_in_context(ctx)


def run_all(args) -> int:
    tests = [test.info.name for test in g_manifest]
    return run_tests(args, tests)


def run_exclude(args):
    tests = [test.info.name for test in g_manifest if test.info.name not in args.tests]
    return run_tests(args, tests)


def run_subset(args):
    tests = [test.info.name for test in g_manifest if test.info.name in args.tests]
    return run_tests(args, tests)


if __name__ == "__main__":
    manifest_path = ROOT / "tests/visual_tests/manifest.json"

    parser = argparse.ArgumentParser()
    _ = parser.add_argument(
        "-m", "--manifest", help="Manifest path", default=manifest_path
    )
    _ = parser.add_argument(
        "-o", "--output_dir", help="Output directory", nargs="?", type=str, default=""
    )
    _ = parser.add_argument(
        "-v", "--verbose", help="Verbose", action="store_true", default=False
    )
    _ = parser.add_argument(
        "-b", "--branch", help="Branch name", nargs="?", type=str, default=""
    )
    _ = parser.add_argument(
        "-c",
        "--compilation_mode",
        help="Compilation mode",
        nargs="?",
        type=str,
        default="",
    )
    _ = parser.add_argument(
        "--stamp", help="Stamp the build", action="store_true", default=False
    )
    _ = parser.add_argument(
        "--bazelrc", help="Path to bazelrc file", nargs="?", type=str, default=""
    )
    _ = parser.add_argument(
        "--output_base",
        help="Path Bazel output base directory",
        nargs="?",
        type=str,
        default="",
    )
    _ = parser.add_argument(
        "--batch",
        help="Execute Bazel in batch mode",
        action="store_true",
        default=False,
    )
    _ = parser.add_argument(
        "--gui",
        help="Run the visual tests web GUI tool",
        action="store_true",
        default=False,
    )
    _ = parser.add_argument(
        "--port", help="Port for the web GUI server", type=int, default=5000
    )
    _ = parser.add_argument(
        "--no-open-browser",
        help="Don't automatically open a browser tab for the web GUI",
        dest="open_browser",
        action="store_false",
        default=True,
    )
    _ = parser.add_argument(
        "--client-dev",
        help="Don't build/serve the web client; assume its Vite dev server is running separately",
        action="store_true",
        default=False,
    )
    _ = parser.add_argument(
        "--show", help="Show Horizon window", action="store_true", default=False
    )
    _ = parser.add_argument(
        "--sync",
        help="Sync the manifest entries with the available input files",
        action="store_true",
        default=False,
    )
    _ = parser.add_argument(
        "--junit_report", help="JUnit report output file", type=str, default=None
    )
    _ = parser.add_argument(
        "--viewer", help="Viewer executable", type=str, default=None
    )
    _ = parser.add_argument(
        "--comparator", help="Comparator executable", type=str, default=None
    )

    if platform.system() == "Linux":
        _ = parser.add_argument(
            "--wsi",
            help="Choose the windowing system integration",
            type=str,
            default="x11",
            choices=["x11", "headless_egl"],
        )

    subparsers = parser.add_subparsers()

    subparser = subparsers.add_parser("subset", help="Runs a subset of tests")
    _ = subparser.add_argument("tests", nargs="+", help="List of tests to run")
    subparser.set_defaults(func=run_subset)

    subparser = subparsers.add_parser(
        "exclude", help="Runs all the tests except those specified"
    )
    _ = subparser.add_argument("tests", nargs="+", help="List of tests to skip")
    subparser.set_defaults(func=run_exclude)

    args = parser.parse_args(sys.argv[1:])

    if args.verbose:
        print("Verbose mode enabled")
        core.g_verbose = True
        core.g_stdout = None
        core.g_stderr = subprocess.STDOUT

    manifest_path = Path(args.manifest)
    g_test_data_dir = manifest_path.parent.absolute()
    g_manifest = core.read_manifest(manifest_path, g_test_data_dir)

    print("Root:", ROOT)
    print("Test data:", g_test_data_dir)

    g_git_info = core.fetch_git_info(ROOT)
    if args.branch != "":
        g_git_info.branch = args.branch
    print("Branch name:", g_git_info.branch)
    print("Commit:", g_git_info.commit)

    bazel_startup_options: list[str] = []
    bazel_build_options: list[str] = []

    if args.bazelrc != "":
        bazel_startup_options.append("--bazelrc=" + args.bazelrc)
        print("bazelrc:", args.bazelrc)
    if args.output_base != "":
        bazel_startup_options.append("--output_base=" + args.output_base)
        print("output_base:", args.output_base)
    if args.batch:
        bazel_startup_options.append("--batch")
    if args.compilation_mode != "":
        bazel_build_options += ["-c", args.compilation_mode]
    if args.stamp:
        bazel_build_options.append("--stamp")

    if args.bazelrc == "":
        bazel_build_options += ["--config=" + platform.system().lower()]
    else:
        print("Relying on the bazelrc file for platform-specific configuration.")

    if args.sync:
        print("Synchronising manifest file.")
        g_manifest = core.sync_manifest(g_manifest, g_test_data_dir, manifest_path)

    if args.viewer:
        g_viewer = args.viewer
    else:
        print("Building viewer executable...", end="", flush=True)
        if core.g_verbose:
            print("")
        viewer_build_command = (
            ["bazel"]
            + bazel_startup_options
            + ["build", "//tools/visual_testing:viewer"]
            + bazel_build_options
        )
        if platform.system() == "Linux" and args.wsi == "headless_egl":
            viewer_build_command.append("--platforms=//:linux_headless_gl_platform")
        build_error = (
            subprocess.run(
                viewer_build_command,
                cwd=ROOT,
                stdout=core.g_stdout,
                stderr=core.g_stderr,
                check=False,
            ).returncode
            != 0
        )
        g_viewer = ROOT / "bazel-bin/tools/visual_testing" / f"viewer{EXE_EXT}"
        g_viewer = g_viewer.resolve()
        if build_error or not g_viewer.exists():
            print(" [FAILED]")
            sys.exit(1)
        else:
            print(" [OK]")

    if args.comparator:
        g_comparator = args.comparator
    else:
        print("Building comparator executable...", end="", flush=True)
        if core.g_verbose:
            print("")
        build_error = (
            subprocess.run(
                ["bazel"]
                + bazel_startup_options
                + ["build", "//tools/visual_testing:comparator"]
                + bazel_build_options,
                cwd=ROOT,
                stdout=core.g_stdout,
                stderr=core.g_stderr,
                check=False,
            ).returncode
            != 0
        )
        g_comparator = ROOT / "bazel-bin/tools/visual_testing" / f"comparator{EXE_EXT}"
        g_comparator = g_comparator.resolve()
        if build_error or not g_comparator.exists():
            print(" [FAILED]")
            sys.exit(1)
        else:
            print(" [OK]")

    if core.g_verbose:
        print(f"Using viewer {g_viewer}")
        print(f"Using comparator {g_comparator}")

    if args.gui:
        from tools.visual_testing.server import app
        from tools.visual_testing.server.context import ServerContext

        test_execution_ctx = core.TestExecutionContext(
            comparator=g_comparator,
            viewer=g_viewer,
            git_info=g_git_info,
            manifest=g_manifest,
            tests=[],
            output_dir=core.OutputDirectory(
                Path(args.output_dir) if args.output_dir else None
            ),
        )

        server_ctx = ServerContext(
            root=ROOT,
            manifest_path=manifest_path,
            suite_path=g_test_data_dir,
            execution_ctx=test_execution_ctx,
            run_manager=RunManager(test_execution_ctx),
        )

        app.run(server_ctx, args.port, args.open_browser, not args.client_dev)
    else:
        failed_tests = 0
        if hasattr(args, "func"):
            failed_tests = args.func(args)
        else:
            failed_tests = run_all(args)
        sys.exit(0 if failed_tests == 0 else 1)
