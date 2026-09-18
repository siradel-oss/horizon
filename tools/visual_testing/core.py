# SPDX-FileCopyrightText: Copyright 2026 Siradel
# SPDX-License-Identifier: MIT

import datetime
import json
import os
import platform
import re
import shutil
import subprocess
import tempfile
import time
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from enum import IntEnum
from pathlib import Path
from typing import Any
from xml.dom import minidom

from tools.visual_testing.protocol import schema

REPORT_FILE_NAME = "hrz-visual-test-report.json"


@dataclass
class TestTypeProperties:
    file_extension: str
    directory_name: str
    name: str


TEST_TYPE_PROPERTIES = {
    schema.TestType.HRZ_SCENE: TestTypeProperties(
        file_extension=".hrz_scene.pbf",
        directory_name="hrz_scenes",
        name="Horizon scene",
    ),
    schema.TestType.MAPBOX_STYLE: TestTypeProperties(
        file_extension=".json",
        directory_name="mapbox_styles",
        name="Mapbox style",
    ),
}

ERROR_STRATEGY_NAME = {
    schema.ErrorStrategy.ABORT: "Abort",
    schema.ErrorStrategy.MESSAGE: "Message",
}


def default_test_definition() -> schema.TestDefinition:
    return schema.TestDefinition(
        name="",
        type=schema.TestType.HRZ_SCENE,
        error_threshold=0.005,
        timeout=180,
        error_strategy=schema.ErrorStrategy.MESSAGE,
        error_message="",
    )


def deserialize_test_definition(json: dict[str, Any]) -> schema.TestDefinition:
    td = default_test_definition()
    td.name = json.get("name", td.name)
    td.type = json.get("type", td.type)
    td.error_threshold = json.get("errorThreshold", td.error_threshold)
    td.timeout = json.get("timeout", td.timeout)

    errorHandlingJson = json.get("errorHandling", {})
    td.error_message = errorHandlingJson.get("errorMessage", td.error_message)
    td.error_strategy = errorHandlingJson.get("errorStrategy", td.error_strategy)

    return td


def serialize_test_definition(test_def: schema.TestDefinition) -> dict[str, Any]:
    return {
        "name": test_def.name,
        "type": test_def.type,
        "errorThreshold": test_def.error_threshold,
        "timeout": test_def.timeout,
        "errorHandling": {
            "errorStrategy": test_def.error_strategy,
            "errorMessage": test_def.error_message,
        },
    }


class Test:
    suite_path: Path
    info: schema.TestDefinition

    def __init__(self, suite_path: Path, json: dict[str, Any] | None = None):
        self.suite_path = suite_path
        if json is not None:
            self.info = deserialize_test_definition(json)
        else:
            self.info = default_test_definition()

    def input_path(self) -> Path:
        return (
            self.suite_path
            / (TEST_TYPE_PROPERTIES[self.info.type].directory_name)
            / f"{self.info.name}{TEST_TYPE_PROPERTIES[self.info.type].file_extension}"
        )

    def ref_image_path(self) -> Path:
        return (
            self.suite_path
            / (TEST_TYPE_PROPERTIES[self.info.type].directory_name)
            / f"{self.info.name}.hrz_ref.png"
        )

    def serialize(self) -> dict[str, Any]:
        return serialize_test_definition(self.info)


class OutputDirectory:
    path: Path
    preserve: bool

    def __init__(self, path: str | Path | None = None):
        if path:
            self.preserve = True
            self.path = Path(path).absolute()
            os.makedirs(self.path, exist_ok=True)
        else:
            self.preserve = False
            self.path = Path(tempfile.mkdtemp(prefix="hrz-visual-tests-"))

    def capture_path(self, test: Test) -> Path:
        return self.path / f"{test.info.name}_capture.png"

    def expected_path(self, test: Test) -> Path:
        return self.path / f"{test.info.name}_expected.png"

    def diff_path(self, test: Test) -> Path:
        return self.path / f"{test.info.name}_diff.png"

    def report_path(self) -> Path:
        return self.path / REPORT_FILE_NAME

    def has_capture(self, test: Test) -> bool:
        return self.capture_path(test).exists()

    def has_report(self) -> bool:
        return self.report_path().exists()

    def __del__(self):
        if not self.preserve:
            shutil.rmtree(self.path, ignore_errors=True)


def default_result() -> schema.Result:
    return schema.Result(
        name="",
        type=schema.TestType.HRZ_SCENE,
        success=False,
        error_type=schema.ErrorType.NONE,
        duration=0,
        date="",
    )


def deserialize_result(json: dict[str, Any]) -> schema.Result:
    r = default_result()
    r.name = json.get("name", r.name)
    r.type = json.get("type", r.type)
    r.success = json.get("success", r.success)
    r.error_ratio = json.get("errorRatio", r.error_ratio)
    r.error_type = json.get("errorType", r.error_type)
    r.error_message = json.get("errorMessage", r.error_message)
    r.log = json.get("log", r.log)
    r.duration = json.get("duration", r.duration)
    r.date = json.get("date", r.date)
    return r


def serialize_result(result: schema.Result) -> dict[str, Any]:
    data = {
        "name": result.name,
        "type": result.type,
        "success": result.success,
        "errorRatio": result.error_ratio,
        "errorType": result.error_type,
        "duration": result.duration,
        "date": result.date,
    }
    if result.error_message != "":
        data["errorMessage"] = result.error_message
    if result.log:
        data["log"] = result.log
    return data


def serialize_result_junit(result: schema.Result, parent_name: str) -> ET.Element:
    root = ET.Element("testcase")
    root.set("name", result.name)
    root.set("classname", parent_name)
    root.set("time", str(result.duration))
    root.set("timestamp", result.date)

    if result.error_type == schema.ErrorType.NONE:
        pass
    elif result.error_type == schema.ErrorType.ABORTED:
        skipped = ET.SubElement(root, "skipped")
        skipped.set("message", "Aborted because a previous test failed")
    else:
        failure = ET.SubElement(root, "failure")
        failure.set(
            "message",
            f"Type: {result.error_type.name}, ratio: {result.error_ratio}, message: {result.error_message}",
        )
        if result.log:
            failure.text = result.log
    return root


def default_report() -> schema.Report:
    return schema.Report(
        branch="",
        commit="",
        date="",
        results=[],
        duration=0,
    )


def deserialize_report(json: dict[str, Any]) -> schema.Report:
    r = default_report()
    r.branch = json.get("branch", r.branch)
    r.commit = json.get("commit", r.commit)
    r.date = json.get("date", r.date)
    r.results = [deserialize_result(r_json) for r_json in json.get("results", [])]
    r.duration = json.get("duration", r.duration)
    return r


def serialize_report(report: schema.Report) -> dict[str, Any]:
    return {
        "branch": report.branch,
        "commit": report.commit,
        "date": report.date,
        "results": [serialize_result(r) for r in report.results],
        "duration": report.duration,
    }


def serialize_report_junit(report: schema.Report) -> ET.Element:
    total_tests = len(report.results)
    aborted_tests = sum(
        1 for r in report.results if r.error_type == schema.ErrorType.ABORTED
    )
    passed_tests = sum(
        1 for r in report.results if r.error_type == schema.ErrorType.NONE
    )
    failed_tests = total_tests - passed_tests - aborted_tests

    root = ET.Element("testsuites")
    root.set("name", "Visual tests")
    root.set("tests", str(total_tests))
    root.set("failures", str(failed_tests))
    root.set("skipped", str(aborted_tests))
    root.set("time", str(report.duration))
    root.set("timestamp", str(report.date))

    suite = ET.SubElement(root, "testsuite")
    suite.set("name", "Visual tests")
    suite.set("tests", str(total_tests))
    suite.set("failures", str(failed_tests))
    suite.set("skipped", str(aborted_tests))
    suite.set("time", str(report.duration))
    suite.set("timestamp", str(report.date))

    prps = ET.SubElement(suite, "properties")

    prp = ET.SubElement(prps, "property")
    prp.set("name", "os")
    prp.set("value", platform.system())

    prp = ET.SubElement(prps, "property")
    prp.set("name", "branch")
    prp.set("value", report.branch)

    prp = ET.SubElement(prps, "property")
    prp.set("name", "commit")
    prp.set("value", report.commit)

    for r in report.results:
        suite.append(serialize_result_junit(r, "Visual tests"))

    return root


# Unknown exit codes can happen when the viewer crashes.
class ViewerExitCode(IntEnum):
    Ok = 0
    MissingArguments = 1
    InvalidArguments = 2
    FailedInitialization = 3
    MissingInput = 4
    InvalidInput = 5
    FailedMigration = 6
    Timeout = 7
    Unknown = -1


IMAGE_SIZE = 512

g_verbose = False
g_stdout = subprocess.DEVNULL
g_stderr = subprocess.DEVNULL


@dataclass
class GitInfo:
    branch: str
    commit: str


def fetch_git_info(root_path: Path) -> GitInfo:
    info = GitInfo(branch="", commit="")

    ret = subprocess.run(
        ["git", "branch", "--points-at", "HEAD"],
        cwd=root_path,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    if ret.returncode == 0:
        info.branch = ret.stdout.decode().splitlines()[0]
        match = re.match("\\* \\(HEAD detached at (.*)\\)", info.branch)
        if bool(match):
            info.branch = match.groups()[0]
        elif info.branch.startswith("* "):
            info.branch = info.branch[2:]

    ret = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=root_path,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    if ret.returncode == 0:
        info.commit = ret.stdout.decode()[:-1]

    return info


def read_manifest(path: Path, test_data_dir: Path) -> list[Test]:
    manifest_json = None
    with open(path, "r", encoding="utf8") as f:
        manifest_json = json.load(f)

    if not isinstance(manifest_json, list):
        raise TypeError(f"Manifest file {path} is not a list of tests.")

    manifest = [Test(test_data_dir, test_json) for test_json in manifest_json]
    return sorted(manifest, key=lambda test: test.info.name)


def write_manifest(manifest: list[Test], path: Path):
    with open(path, "w", encoding="utf8", newline="\n") as f:
        tests = [
            test.serialize()
            for test in sorted(manifest, key=lambda test: test.info.name)
        ]
        content = json.dumps(tests, indent=4)
        _ = f.write(content + "\n")


def write_json_report(report: schema.Report, path: Path):
    print("Writing report at", path)
    with open(path, "w", encoding="utf8", newline="\n") as f:
        content = json.dumps(serialize_report(report), indent=4)
        _ = f.write(content)


def remove_results(report: schema.Report, names: set[str]):
    report.results = [r for r in report.results if r.name not in names]


def read_report(path: Path) -> schema.Report:
    print("Reading report at", path)
    with open(path, "r", encoding="utf8") as f:
        return deserialize_report(json.load(f))


def refresh_ref_image(viewer: Path, test: Test, output_dir: OutputDirectory) -> bool:
    """
    Brings a test's reference image up to date, returning whether it was rewritten.

    Promotes the capture from the last run when there is one, which is just a file copy — versus
    re-rendering the scene through the viewer, which costs seconds per test. Both the single-test
    and the bulk RPC go through here so they behave (and cost) the same.
    """
    if output_dir.has_capture(test) and test.input_path().exists():
        _ = shutil.copyfile(output_dir.capture_path(test), test.ref_image_path())
        return True

    return generate_ref_image(viewer, test, output_dir)


def generate_ref_image(viewer: Path, test: Test, output_dir: OutputDirectory) -> bool:
    """Renders a fresh reference image with the viewer. Returns whether it was written."""
    if not test.input_path().exists():
        print(
            f"ERROR: Couldn't generate reference image for '{test.info.name}', input file not found at '{test.input_path()}'"
        )
        return False

    ret = subprocess.run(
        [
            viewer,
            "--input",
            test.input_path().absolute(),
            "--output",
            output_dir.capture_path(test).absolute(),
            "--width",
            str(IMAGE_SIZE),
            "--height",
            str(IMAGE_SIZE),
            "--timeout",
            str(test.info.timeout),
            "--no-show-window",
            "--log-filter-level",
            "0" if g_verbose else "3",
        ],
        stdout=g_stdout,
        stderr=g_stderr,
        check=False,
    )

    return_code = ViewerExitCode(ret.returncode)
    if return_code == ViewerExitCode.Ok:
        _ = shutil.copyfile(
            output_dir.capture_path(test),
            test.ref_image_path(),
        )
        return True

    print(
        f"ERROR: Couldn't generate reference image for '{test.info.name}', return code: {return_code.name} ({return_code.value})"
    )
    return False


@dataclass
class TestExecutionContext:
    manifest: list[Test]
    tests: list[str]
    output_dir: OutputDirectory
    git_info: GitInfo
    viewer: Path
    comparator: Path
    report_to_append_to: schema.Report | None = None
    junit_output_file: Path | None = None
    process: subprocess.Popen[str] | None = None
    show_viewer_window: bool = False
    terminate: bool = False

    def for_run(
        self,
        tests: list[str],
        output_dir: OutputDirectory,
        show_viewer_window: bool,
    ) -> "TestExecutionContext":
        return TestExecutionContext(
            manifest=self.manifest,
            tests=tests,
            output_dir=output_dir,
            git_info=self.git_info,
            viewer=self.viewer,
            comparator=self.comparator,
            show_viewer_window=show_viewer_window,
        )


def run_tests_in_context(ctx: TestExecutionContext):
    abort_tests = False
    total_tests = len(ctx.tests)
    passed_tests = 0
    failed_tests = 0
    aborted_tests = 0
    total_duration = 0

    error_type = schema.ErrorType.NONE

    if g_verbose:
        print(f'(Output dir: "{ctx.output_dir}")')

    results: list[schema.Result] = []
    for test_index, test_name in enumerate(ctx.tests):
        test = next((t for t in ctx.manifest if t.info.name == test_name), None)
        if test is None:
            raise ValueError(f"Test '{test_name}' not found in manifest.")

        different_pixels = None
        log = None
        duration = 0
        diff_ratio = 0
        error_type = schema.ErrorType.NONE

        if abort_tests:
            error_type = schema.ErrorType.ABORTED
        elif not test.input_path().exists():
            error_type = schema.ErrorType.MISSING_INPUT
        elif not test.ref_image_path().exists():
            error_type = schema.ErrorType.MISSING_REF
        else:
            print("Running test", end="")
            if len(ctx.tests) > 1:
                print(
                    f' "{test.info.name}" ({test_index + 1} of {total_tests})', end=""
                )
            print("...", end="", flush=True)

            input_path = test.input_path()
            capture_path = ctx.output_dir.capture_path(test)

            if g_verbose:
                print(f"\n  (Timeout: {test.info.timeout!s}s)")
                print(f'  (Input: "{input_path}")')
                print(f'  (Output: "{capture_path}")')

            start_time = time.time_ns() / 1_000_000_000

            arguments = [
                ctx.viewer,
                "--input",
                input_path.absolute(),
                "--output",
                capture_path.absolute(),
                "--width",
                str(IMAGE_SIZE),
                "--height",
                str(IMAGE_SIZE),
                "--timeout",
                str(test.info.timeout),
                "--log-filter-level",
                "0" if g_verbose else "3",
            ]
            if not ctx.show_viewer_window:
                arguments.append("--no-show-window")

            ctx.process = subprocess.Popen(
                arguments,
                stdout=subprocess.PIPE,
                stderr=g_stderr,
                text=True,
                bufsize=1,
            )

            assert ctx.process.stdout is not None

            output_lines: list[str] = []
            for line in ctx.process.stdout:
                if g_verbose:
                    print(line, end="")
                output_lines.append(line)

            return_code = ctx.process.wait()

            if ctx.terminate:
                return failed_tests

            log = "".join(output_lines)

            return_code = ViewerExitCode(return_code)

            if return_code == ViewerExitCode.Timeout:
                error_type = schema.ErrorType.TIMEOUT
            elif return_code != ViewerExitCode.Ok:
                error_type = schema.ErrorType.VIEWER

                if g_verbose:
                    print(
                        f"Viewer error, return code: {return_code.name} ({return_code.value})"
                    )

            duration = time.time_ns() / 1_000_000_000 - start_time
            total_duration += duration

            ref_image_path = test.ref_image_path()
            diff_path = ctx.output_dir.diff_path(test)

            if error_type == schema.ErrorType.NONE:
                if g_verbose:
                    print(
                        f"Running comparator: {ctx.comparator} {ref_image_path} {capture_path} {diff_path}"
                    )

                ret = subprocess.run(
                    [
                        ctx.comparator,
                        ref_image_path.absolute(),
                        capture_path.absolute(),
                        diff_path.absolute(),
                    ],
                    stdout=subprocess.PIPE,
                    stderr=g_stderr,
                    check=False,
                )
                if ret.returncode != 0:
                    error_type = schema.ErrorType.COMPARATOR
                else:
                    different_pixels = int(ret.stdout.decode("utf-8").strip())

            if different_pixels is not None:
                diff_ratio = different_pixels / (IMAGE_SIZE * IMAGE_SIZE)

                if (
                    diff_ratio > test.info.error_threshold
                    or different_pixels > IMAGE_SIZE * IMAGE_SIZE
                ):
                    error_type = schema.ErrorType.THRESHOLD

                _ = shutil.copyfile(ref_image_path, ctx.output_dir.expected_path(test))
            else:
                diff_ratio = None

        if error_type == schema.ErrorType.NONE:
            print(" [OK]")
            passed_tests += 1
        elif error_type == schema.ErrorType.ABORTED:
            print(" [ABORTED]")
            aborted_tests += 1
        else:
            print(" [FAILED]")
            failed_tests += 1

        entry = schema.Result(
            name=test.info.name,
            type=test.info.type,
            success=error_type == schema.ErrorType.NONE,
            error_ratio=diff_ratio,
            error_type=error_type,
            error_message=test.info.error_message,
            log=None if error_type == schema.ErrorType.NONE else log,
            date=datetime.datetime.now(datetime.timezone.utc).isoformat(),
            duration=duration,
        )
        results.append(entry)

        if (
            error_type != schema.ErrorType.NONE
            and test.info.error_strategy == schema.ErrorStrategy.ABORT
        ):
            abort_tests = True

    report = schema.Report(
        branch=ctx.git_info.branch,
        commit=ctx.git_info.commit,
        date=datetime.datetime.now(datetime.timezone.utc).isoformat(),
        results=results,
        duration=total_duration,
    )

    if (
        ctx.report_to_append_to is not None
        and ctx.report_to_append_to.commit == ctx.git_info.commit
    ):
        previous_results = ctx.report_to_append_to.results
        for entry in previous_results:
            if entry.name not in ctx.tests:
                report.results.append(entry)

    write_json_report(report, ctx.output_dir.report_path())

    if ctx.junit_output_file:
        print(f"Writing JUnit report file to {ctx.junit_output_file}")
        junit = serialize_report_junit(report)
        junit = minidom.parseString(ET.tostring(junit)).toprettyxml(
            indent="   ", encoding="utf8"
        )
        with open(ctx.junit_output_file, "wb+") as fp:
            _ = fp.write(junit)

    print(f"Passed: {passed_tests}, failed: {failed_tests}, aborted: {aborted_tests}")
    return failed_tests


def sync_manifest(
    manifest: list[Test], test_data_dir: Path, manifest_path: Path
) -> list[Test]:
    """
    Synchronises the manifest file with what is contained inside the visual tests data directory.
    Tests written in the manifest that don't have an associated input file are removed from the
    manifest.
    Input files that don't have an associated entry in the manifest are created with default
    parameters.
    """
    manifest = [test for test in manifest if test.input_path().exists()]
    test_names = {test.info.name for test in manifest}
    added = 0

    for type in schema.TestType.__members__.values():
        extension = TEST_TYPE_PROPERTIES[type].file_extension
        path = test_data_dir / TEST_TYPE_PROPERTIES[type].directory_name
        new_test_names = [
            f.name.removesuffix(extension) for f in path.glob(f"*{extension}")
        ]

        for filename in new_test_names:
            if not (filename in test_names):
                test = Test(test_data_dir)
                test.info.name = filename
                test.info.type = type
                manifest.append(test)
                added += 1

    write_manifest(manifest, manifest_path)
    return manifest
