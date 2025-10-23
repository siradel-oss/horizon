#!/usr/bin/env python3
#-*- coding: utf-8 -*-

import argparse
import datetime
import json
import os
import os.path as path
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from xml.dom import minidom
import time

from copy import copy
from enum import IntEnum
from pathlib import Path
from threading import Thread

REPORT_FILE_NAME = "hrz-visual-test-report.json"

ROOT = Path(path.dirname(__file__)).parent.parent.absolute()
MANIFEST_PATH = ROOT / "tests/visual_tests/manifest.json"
TEST_DATA_DIR = ROOT / "tests/visual_tests/"

BRANCH = ""
COMMIT = ""

EXE_EXT = ".exe" if platform.system() == "Windows" else ""

VIEWER_EXE = ""
COMPARATOR_EXE = ""

FONT_FILE = ROOT / "third_party/fonts/Roboto-Regular.ttf"
MONO_FONT_FILE = ROOT / "third_party/fonts/RobotoMono-Regular.ttf"
MISSING_IMAGE_FILE = ROOT / "tools/visual_testing/missing_image.png"
IMAGE_SIZE = 512

MANIFEST = []

VERBOSE = False
STDOUT = subprocess.DEVNULL
STDERR = subprocess.DEVNULL

WHITE = (1.0, 1.0, 1.0, 1.0)
RED = (0.9, 0.3, 0.3, 1.)
GREEN = (0.4, 0.8, 0.4, 1.)
YELLOW = (0.9, 0.7, 0.2, 1.)
GRAY = (0.5, 0.5, 0.5, 1.)
HIGHLIGHT_COLOR = (0, 0.65, 0.75, 1.)

EDIT_POPUP_NAME = "Edit test"
REMOVE_POPUP_NAME = "Remove test"

# From https://stackoverflow.com/a/72965775
def ask_open_filename(*args, **kwargs):
    class Filedialog(tkinter.Tk):
        @classmethod
        def askopenfilename(cls, *args, **kwargs):
            root = cls()
            root.wm_withdraw()
            file = tkinter.filedialog.askopenfilename(*args, **kwargs)
            root.destroy()
            return file

    return Filedialog.askopenfilename(*args, **kwargs)

def ask_directory(*args, **kwargs):
    class Filedialog(tkinter.Tk):
        @classmethod
        def askdirectory(cls, *args, **kwargs):
            root = cls()
            root.wm_withdraw()
            dir = tkinter.filedialog.askdirectory(*args, **kwargs)
            root.destroy()
            return dir

    return Filedialog.askdirectory(*args, **kwargs)

def get_input_path(test_name, test_type):
    return path.join(Test.Type.directory(test_type), f"{test_name}{Test.Type.file_extension(test_type)}")

def get_ref_image_path(test_name, test_type):
    return path.join(Test.Type.directory(test_type), f"{test_name}.hrz_ref.png")

def get_capture_path(output_dir, test_name):
    return path.join(output_dir, f"{test_name}_capture.png")

def get_expected_path(output_dir, test_name):
    return path.join(output_dir, f"{test_name}_expected.png")

def get_diff_path(output_dir, test_name):
    return path.join(output_dir, f"{test_name}_diff.png")

class OutputDirectory:
    """
    Output directory which is either:
    - A temporary directory automatically deleted when the output directory input widget is empty.
    - A persistent directory.
    """
    def __init__(self, path):
        self.path = str(Path(path).absolute())
        self.preserve = self.path != ""

        if path:
            self.preserve = True
            os.makedirs(self.path, exist_ok=True)
        else:
            self.path = tempfile.mkdtemp(prefix="hrz-visual-tests-")
            self.preserve = False

    def has_capture(self, test):
        return path.exists(get_capture_path(self.path, test.name))

    def has_report(self):
        return path.exists(path.join(self.path, REPORT_FILE_NAME))

    def __del__(self):
        if not self.preserve:
            shutil.rmtree(self.path, ignore_errors=True)

class Test:
    class Type(IntEnum):
        HrzScene = 0,
        MapboxStyle = 1,

        def file_extension(type):
            return [
                ".hrz_scene.pbf",
                ".json"
            ][type]

        def directory(type):
            return [
                TEST_DATA_DIR / "hrz_scenes",
                TEST_DATA_DIR / "mapbox_styles"
            ][type]

        def name(type):
            return [
                "Horizon scene",
                "Mapbox style"
            ][type]

    class ErrorStrategy(IntEnum):
        Abort = 0,
        Message = 1,

        def name(strategy):
            return ["Abort", "Message"][strategy]


    ERROR_STRATEGY_NAMES = ["Abort", "Message"]

    def __init__(self, json=None):
        if json is not None:
            self.deserialize(json)
        else:
            self.new = True
            self.name = ""
            self.type = Test.Type.HrzScene
            self.input_path = ""
            self.error_threshold = 0.015
            self.timeout = 300
            self.error_strategy = Test.ErrorStrategy.Message
            self.error_message = ""
            # @Todo(enhancement): add subtests where other tests can be performed on a scene already
            # loaded (cf. RFC).

        self.check_input()

    def deserialize(self, json):
        default = Test()

        self.new = False
        self.name = json.get("name", default.name)
        self.type = json.get("type", default.type)
        self.input_path = get_input_path(self.name, self.type)
        self.error_threshold = json.get("errorThreshold", default.error_threshold)
        self.timeout = json.get("timeout", default.timeout)

        json_error = json.get("errorHandling", None)
        if json_error is not None:
            self.error_strategy = json_error.get("errorStrategy", default.error_strategy)
            self.error_message = json_error.get("errorMessage", default.error_threshold)
        else:
            self.error_strategy = default.error_strategy
            self.error_message = default.error_message

    def serialize(self):
        return {
            "name": self.name,
            "type": self.type,
            "errorThreshold": self.error_threshold,
            "timeout": self.timeout,
            "errorHandling": {
                "errorStrategy": self.error_strategy,
                "errorMessage": self.error_message,
            }
        }

    def check_input(self):
        self.input_info = None
        if not path.exists(self.input_path):
            self.input_error = "Invalid file path"
        else:
            self.input_error = None

    def input_must_be_copied(self):
        return (self.input_error is None
            and (Path(self.input_path).parent != Test.Type.directory(self.type)
            or path.basename(self.input_path) != self.name + Test.Type.file_extension(self.type)))

    def copy_input(self):
        self.check_input()
        if self.input_must_be_copied():
            dst_path = get_input_path(self.name, self.type)
            print(f"Copying {self.input_path} to {dst_path}...")
            Path(dst_path).absolute().parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(self.input_path, dst_path)
            self.input_path = str(dst_path)

    def matches_filters(self, name_filter, type_filter):
        if self.type not in type_filter:
            return False

        if name_filter == "":
            return True

        positive_subfilters = []
        negative_subfilters = []
        for subfilter in name_filter.split(" "):
            if subfilter.startswith("-"):
                if len(subfilter) >= 2:
                    negative_subfilters.append(subfilter[1:])
            else:
                positive_subfilters.append(subfilter)

        for subfilter in positive_subfilters:
            if not subfilter.casefold() in self.name.casefold():
                return False

        for subfilter in negative_subfilters:
            if subfilter.casefold() in self.name.casefold():
                return False

        return True

class Result:
    class ErrorType(IntEnum):
        None_ = 0,
        Viewer = 1,
        Timeout = 2,
        Threshold = 3,
        MissingRef = 4,
        MissingInput = 5,
        MigrationError = 6,
        Aborted = 7,
        Comparator = 8,

    def __init__(self, json=None):
        if json is not None:
            self.deserialize(json)
        else:
            self.name = ""
            self.type = ""
            self.success = False
            self.error_ratio = None
            self.error_type = Result.ErrorType.None_
            self.error_message = ""
            self.log = None
            self.duration = 0
            self.date = ""

    def deserialize(self, json):
        self.name = json.get("name", "")
        self.type = json.get("type", "")
        self.success = json.get("success", False)
        self.error_ratio = json.get("errorRatio", None)
        self.error_type = json.get("errorType", Result.ErrorType.None_)
        self.error_message = json.get("errorMessage", "")
        self.log = json.get("log", None)
        self.duration = json.get("duration", 0)
        self.date = json.get("date", "")

    def serialize(self):
        data = {
            "name": self.name,
            "type": self.type,
            "success": self.success,
            "errorRatio": self.error_ratio,
            "errorType": self.error_type,
            "duration": self.duration,
            "date": self.date,
        }
        if self.error_message != "":
            data["errorMessage"] = self.error_message
        if self.log:
            data["log"] = self.log
        return data

    def serialize_junit(self, parent_name):
        root = ET.Element("testcase")
        root.set("name", self.name)
        root.set("classname", parent_name)
        root.set("time", str(self.duration))
        root.set("timestamp", self.date)

        if self.error_type == Result.ErrorType.None_:
            pass
        elif self.error_type == Result.ErrorType.Aborted:
            skipped = ET.SubElement(root, "skipped")
            skipped.set("message", "Aborted because a previous test failed")
        else:
            failure = ET.SubElement(root, "failure")
            failure.set("message", f"Type: {self.error_type.name}, ratio: {self.error_ratio}, message: {self.error_message}")
            if self.log:
                failure.text = self.log
        return root

class Report:
    def __init__(self, json=None):
        if json is not None:
            self.deserialize(json)
        else:
            self.branch = ""
            self.commit = ""
            self.date = ""
            self.total_tests = 0
            self.passed_tests = 0
            self.failed_tests = 0
            self.aborted_tests = 0
            self.results = []
            self.duration = 0

    def deserialize(self, json):
        self.branch = json.get("branch", "")
        self.commit = json.get("commit", "")
        self.date = json.get("date", "")
        self.total_tests = json.get("totalTests", 0)
        self.passed_tests = json.get("passedTests", 0)
        self.failed_tests = json.get("failedTests", 0)
        self.aborted_tests = json.get("abortedTests", 0)
        self.results = [Result(r_json) for r_json in json.get("results", [])]
        self.duration = json.get("duration", 0)

    def serialize(self):
        return {
            "branch": self.branch,
            "commit": self.commit,
            "date": self.date,
            "totalTests": self.total_tests,
            "passedTests": self.passed_tests,
            "failedTests": self.failed_tests,
            "abortedTests": self.aborted_tests,
            "results": [r.serialize() for r in self.results],
            "duration": self.duration,
        }

    def serialize_junit(self):
        root = ET.Element("testsuites")
        root.set("name", "Visual tests")
        root.set("tests", str(self.total_tests))
        root.set("failures", str(self.failed_tests))
        root.set("skipped", str(self.aborted_tests))
        root.set("time", str(self.duration))
        root.set("timestamp", str(self.date))

        suite = ET.SubElement(root, "testsuite")
        suite.set("name", "Visual tests")
        suite.set("tests", str(self.total_tests))
        suite.set("failures", str(self.failed_tests))
        suite.set("skipped", str(self.aborted_tests))
        suite.set("time", str(self.duration))
        suite.set("timestamp", str(self.date))

        prps = ET.SubElement(suite, "properties")

        prp = ET.SubElement(prps, "property")
        prp.set("name", "os")
        prp.set("value", platform.system())

        prp = ET.SubElement(prps, "property")
        prp.set("name", "branch")
        prp.set("value", self.branch)

        prp = ET.SubElement(prps, "property")
        prp.set("name", "commit")
        prp.set("value", self.commit)

        for r in self.results:
            suite.append(r.serialize_junit("Visual tests"))

        return root

class ViewerExitCode(IntEnum):
    Ok = 0
    MissingArguments = 1
    FailedInitialization = 2
    MissingSceneDump = 3
    FailedMigration = 4
    Timeout = 5

def get_git_info():
    global BRANCH
    global COMMIT

    if BRANCH == "":
        ret = subprocess.run(["git", "branch", "--points-at", "HEAD"],
            cwd=ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL)
        BRANCH = ret.stdout.decode().splitlines()[0]
        match = re.match("\\* \\(HEAD detached at (.*)\\)", BRANCH)
        if bool(match):
            BRANCH = match.groups()[0]
        elif BRANCH.startswith("* "):
            BRANCH = BRANCH[2:]

    ret = subprocess.run(["git", "rev-parse", "HEAD"],
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL)
    COMMIT = ret.stdout.decode()[:-1]

def read_manifest(path):
    manifest_json = []
    with open(path, "r", encoding="utf8") as f:
        manifest_json = json.load(f)

    # Set default values when not present.
    manifest = [Test(test_json) for test_json in manifest_json]
    return sorted(manifest, key=lambda test: test.name)

def write_manifest():
    with open(MANIFEST_PATH, "w", encoding="utf8", newline="\n") as f:
        tests = sorted([test.serialize() for test in MANIFEST], key=lambda t: t["name"])
        content = json.dumps(tests, indent=4)
        f.write(content + "\n")

def write_json_report(report, path):
    print("Writing report at", path)
    with open(path, "w", encoding="utf8", newline="\n") as f:
        content = json.dumps(report, default=lambda o: o.serialize(), indent=4)
        f.write(content)

def read_report(path):
    print("Reading report at", path)
    report = Report()
    with open(path, "r", encoding="utf8") as f:
        report.deserialize(json.load(f))
    return report

def has_input(test):
    return path.exists(get_input_path(test.name, test.type))

def has_ref(test):
    return path.exists(get_ref_image_path(test.name, test.type))

def generate_ref_image(test, output_dir):
    if has_input(test):
        ret = subprocess.run(
            [
                VIEWER_EXE,
                "--input", get_input_path(test.name, test.type),
                "--output", get_capture_path(output_dir, test.name),
                "--width", str(IMAGE_SIZE),
                "--height", str(IMAGE_SIZE),
                "--timeout", str(test.timeout),
                "--no-show-window"
            ],
            cwd=ROOT,
            stdout=STDOUT,
            stderr=STDERR)
        if ret.returncode == ViewerExitCode.Ok:
            shutil.copyfile(get_capture_path(output_dir, test.name), get_ref_image_path(test.name, test.type))
        else:
            print(f"ERROR: Couldn't generate reference image for '{test.name}', return code: {ret.returncode}")
    else:
        print(f"ERROR: Couldn't generate reference image for '{test.name}', no input found.")

def generate_ref_images(tests, output_dir):
    for test_id in tests:
        generate_ref_image(MANIFEST[test_id], output_dir)

def can_append_to_report(report_to_append_to):
    return report_to_append_to is not None and report_to_append_to.commit == COMMIT

class TestExecutionContext:
    def __init__(self):
        self.tests = []
        self.output_dir = None
        self.report_to_append_to = None
        self.junit_output_file = None
        self.show_viewer_window = False
        self.process = None
        self.terminate = False

def run_tests(tests, report_to_append_to, output_dir, junit_output_file, show_viewer_window):
    ctx = TestExecutionContext()
    ctx.tests = tests
    ctx.report_to_append_to = report_to_append_to
    ctx.output_dir = output_dir
    ctx.junit_output_file = junit_output_file
    ctx.show_viewer_window = show_viewer_window

    return run_tests_in_context(ctx)

def run_tests_in_context(ctx):
    abort_tests = False
    total_tests = len(ctx.tests)
    passed_tests = 0
    failed_tests = 0
    aborted_tests = 0
    total_duration = 0

    test_names = set()

    error_type = Result.ErrorType.None_

    if VERBOSE:
        print(f"(Output dir: \"{ctx.output_dir.path}\")")

    results = []
    for test_index, test_id in enumerate(ctx.tests):
        test = MANIFEST[test_id]

        different_pixels = None
        log = None
        error_type = Result.ErrorType.None_

        if abort_tests:
            error_type = Result.ErrorType.Aborted
        elif not has_input(test):
            error_type = Result.ErrorType.MissingInput
        elif not has_ref(test):
            error_type = Result.ErrorType.MissingRef

        print("Running test", end="")
        if len(ctx.tests) > 1:
            print(f" \"{test.name}\" ({test_index + 1} of {len(ctx.tests)})", end="")
        print("...", end="", flush=True)

        input_path = get_input_path(test.name, test.type)
        capture_path = get_capture_path(ctx.output_dir.path, test.name)

        if VERBOSE:
            print(f"\n  (Timeout: {str(test.timeout)}s)")
            print(f"  (Input: \"{input_path}\")")
            print(f"  (Output: \"{capture_path}\")")

        start_time = time.time_ns() / 1_000_000_000
        if error_type == Result.ErrorType.None_ and not abort_tests:
            arguments = [
                VIEWER_EXE,
                "--input", input_path,
                "--output", capture_path,
                "--width", str(IMAGE_SIZE),
                "--height", str(IMAGE_SIZE),
                "--timeout", str(test.timeout)
            ]
            if not ctx.show_viewer_window:
                arguments.append("--no-show-window")
            ctx.process = subprocess.Popen(
                arguments,
                cwd=ROOT,
                stdout=subprocess.PIPE,
                stderr=STDERR)
            process_stdout, _ = ctx.process.communicate()
            return_code = ctx.process.wait()

            if ctx.terminate:
                return failed_tests

            log = process_stdout.decode("utf-8")

            if return_code == ViewerExitCode.Timeout:
                error_type = Result.ErrorType.Timeout
            elif return_code != ViewerExitCode.Ok:
                error_type = Result.ErrorType.Viewer

                if VERBOSE:
                    print(f"Viewer error, return code: {return_code}")

        duration = time.time_ns() / 1_000_000_000 - start_time
        total_duration += duration

        ref_image_path = get_ref_image_path(test.name, test.type)
        diff_path = get_diff_path(ctx.output_dir.path, test.name)

        if error_type == Result.ErrorType.None_ and not abort_tests:
            if VERBOSE:
                print(f"Running comparator: {COMPARATOR_EXE} {ref_image_path} {capture_path} {diff_path}")

            ret = subprocess.run([COMPARATOR_EXE, ref_image_path, capture_path, diff_path],
                cwd=ROOT,
                stdout=subprocess.PIPE,
                stderr=STDERR)
            if ret.returncode != 0:
                error_type = Result.ErrorType.Comparator
            else:
                different_pixels = int(ret.stdout.decode("utf-8").strip())

        if different_pixels is not None:
            diff_ratio = (different_pixels / (IMAGE_SIZE * IMAGE_SIZE))

            if diff_ratio > test.error_threshold or different_pixels > IMAGE_SIZE*IMAGE_SIZE:
                error_type = Result.ErrorType.Threshold

            shutil.copyfile(ref_image_path, get_expected_path(ctx.output_dir.path, test.name))
        else:
            diff_ratio = None

        if error_type == Result.ErrorType.None_:
            print(" [OK]")
            passed_tests += 1
        elif error_type == Result.ErrorType.Aborted:
            print(" [ABORTED]")
            aborted_tests += 1
        else:
            print(" [FAILED]")
            failed_tests += 1

        entry = Result()
        entry.name = test.name
        entry.type = test.type
        entry.success = error_type == Result.ErrorType.None_
        entry.error_ratio = diff_ratio
        entry.error_type = error_type
        entry.error_message = test.error_message
        entry.log = None if entry.success else log
        entry.date = datetime.datetime.now(datetime.timezone.utc).isoformat()
        entry.duration = duration
        results.append(entry)

        test_names.add(test.name)

        if error_type != Result.ErrorType.None_ and test.error_strategy == Test.ErrorStrategy.Abort:
            abort_tests = True

    report = Report()
    report.branch = BRANCH
    report.commit = COMMIT
    report.date = datetime.datetime.now(datetime.timezone.utc).isoformat()
    report.total_tests = total_tests
    report.passed_tests = passed_tests
    report.failed_tests = failed_tests
    report.aborted_tests = aborted_tests
    report.results = results
    report.duration = total_duration

    if ctx.report_to_append_to is not None and ctx.report_to_append_to.commit == COMMIT:
        previous_results = ctx.report_to_append_to.results
        for entry in previous_results:
            if entry.name not in test_names:
                report.results.append(entry)
                report.total_tests += 1
                if entry.error_type == Result.ErrorType.None_:
                    report.passed_tests += 1
                elif entry.error_type == Result.ErrorType.Aborted:
                    report.aborted_tests += 1
                else:
                    report.failed_tests += 1

    write_json_report(report, path.join(ctx.output_dir.path, REPORT_FILE_NAME))

    if ctx.junit_output_file:
        print(f"Writing JUnit report file to {ctx.junit_output_file}")
        junit = report.serialize_junit()
        junit = minidom.parseString(ET.tostring(junit)).toprettyxml(indent="   ", encoding="utf8")
        with open(ctx.junit_output_file, "wb+") as fp:
            fp.write(junit)

    print(f"Passed: {passed_tests}, failed: {failed_tests}, aborted: {aborted_tests}")
    return failed_tests

def create_test(args):
    test = Test()

    if args.scene_dump_path != None:
        if not path.exists(args.scene_dump_path):
            print("Couldn't find scene dump path at", args.scene_dump_path)
            return 1
        if not args.scene_dump_path.endswith(".hrz_scene.pbf"):
            print("Wrong file format for scene dump file at", args.scene_dump_path)
            return 2

        test.type = Test.Type.HrzScene
        test.input_path = args.scene_dump_path

    elif args.mapbox_style_path != None:
        if not path.exists(args.mapbox_style_path):
            print("Couldn't find mapbox style path at", args.mapbox_style_path)
            return 1
        if not args.mapbox_style_path.endswith(".json"):
            print("Wrong file format for mapbox file at", args.mapbox_style_path)
            return 2

        test.type = Test.Type.MapboxStyle
        test.input_path = args.mapbox_style_path

    else:
        print("Unhandled input type")
        return 3

    test.name = args.test_name
    test.error_threshold = args.threshold

    test.copy_input()

    MANIFEST.append(test)
    write_manifest()

    return 0

def remove_test(args):
    for i, test in enumerate(MANIFEST):
        if test.name == args.test_name:
            MANIFEST.pop(i)
            break
    write_manifest()

def run_all(args):
    tests = list(range(len(MANIFEST)))
    return run_tests(tests, None, OutputDirectory(args.o), args.junit_report, args.show)

def run_exclude(args):
    tests = list(range(len(MANIFEST)))
    return run_tests([i for i, test in enumerate(MANIFEST) if test.name not in args.tests],
        None, OutputDirectory(args.o), args.junit_report, args.show)

def run_subset(args):
    return run_tests([i for i, test in enumerate(MANIFEST) if test.name in args.tests],
        None, OutputDirectory(args.o), args.junit_report, args.show)

def sync_manifest():
    """
    Synchronises the manifest file with what is contained inside the visual tests data directory.
    Tests written in the manifest that don't have an associated input file are removed from the
    manifest.
    Input files that don't have an associated entry in the manifest are created with default
    parameters.
    """
    global MANIFEST
    MANIFEST = [test for test in MANIFEST if has_input(test)]

    tests_name = set([test.name for test in MANIFEST])

    for type in list(Test.Type):
        ext = Test.Type.file_extension(type)
        dumps_filename = [f[:-len(ext)] for f in os.listdir(Test.Type.directory(type)) if f.endswith(ext)]
        for filename in dumps_filename:
            if not (filename in tests_name):
                test = Test()
                test.name = filename
                test.type = type
                MANIFEST.append(test)

    write_manifest()

class Texture:
    def __init__(self):
        self.id = -1
        self.has_data = False
        self.size = (0, 0)

    def __del__(self):
        if self.id >= 0:
            gl.glDeleteTextures([self.id])

    def upload_image_on_gpu(self, path):
        w, h, data = 0, 0, b""
        img = IMG_Load(path.encode("utf-8"))
        if not img:
            print("Couldn't load {}: {}".format(path, SDL_GetError().decode("utf-8")))
            img = IMG_Load(str(MISSING_IMAGE_FILE).encode("utf-8"))

        w, h = img.contents.w, img.contents.h
        data = ctypes.cast(img.contents.pixels, ctypes.POINTER(ctypes.c_uint8))

        if not self.has_data:
            self.id = gl.glGenTextures(1)
            gl.glBindTexture(gl.GL_TEXTURE_2D, self.id)
            gl.glPixelStorei(gl.GL_UNPACK_ALIGNMENT, 1)
            gl.glTexParameteri(gl.GL_TEXTURE_2D, gl.GL_TEXTURE_WRAP_S, gl.GL_REPEAT)
            gl.glTexParameteri(gl.GL_TEXTURE_2D, gl.GL_TEXTURE_WRAP_T, gl.GL_REPEAT)
            gl.glTexParameteri(gl.GL_TEXTURE_2D, gl.GL_TEXTURE_MIN_FILTER, gl.GL_LINEAR)
            gl.glTexParameteri(gl.GL_TEXTURE_2D, gl.GL_TEXTURE_MAG_FILTER, gl.GL_LINEAR)
            gl.glTexImage2D(gl.GL_TEXTURE_2D, 0, gl.GL_RGBA, w, h, 0, gl.GL_RGBA, gl.GL_UNSIGNED_BYTE, data)
            gl.glBindTexture(gl.GL_TEXTURE_2D, 0)
            self.has_data = True
            self.size = (w, h)
        else:
            assert w == self.size[0] and h == self.size[1]
            gl.glBindTexture(gl.GL_TEXTURE_2D, self.id)
            gl.glTexSubImage2D(gl.GL_TEXTURE_2D, 0, 0, 0, w, h, gl.GL_RGBA, gl.GL_UNSIGNED_BYTE, data)
            gl.glBindTexture(gl.GL_TEXTURE_2D, 0)

        SDL_FreeSurface(img)

class TestState(IntEnum):
    Skip = 0,
    Fail = 1,
    Success = 2,
    UnderThreshold = 3,
    Updated = 4,
    Aborted = 5,

class Gui:
    class Context:
        def __init__(self, output_dir):
            self.name_to_id = {test.name: test_id for test_id, test in enumerate(MANIFEST)}
            self.id_to_result_id = {}
            self.selection = [True] * len(MANIFEST)
            self.states = [TestState.Skip] * len(MANIFEST)

            self.textures = (Texture(), Texture(), Texture())
            self.uploaded_textures_test_id = None
            self.uploaded_textures_report_date = None
            self.compare_texture_id = 0
            self.compare_last_time = 0

            self.view_queue = set()
            self.viewed_test_id = None

            self.output_dir_input = output_dir
            self.output_dir = OutputDirectory(self.output_dir_input)

            self.name_filter = ""
            self.type_filter = {type for type in Test.Type}

            self.report = None
            self.has_report = self.output_dir.has_report()

            self.test_id = -1
            self.test = None
            self.generate_ref = False

            self.show_viewer_window = False

            self.test_run_plan = []
            self.remaining_tests_in_plan = set()
            self.currently_running_test_in_plan = None
            self.cancel_test_plan = False
            self.test_plan_thread = None
            self.test_plan_execution_ctx = TestExecutionContext()

    def center_next_widget_h(widget_width):
        offset_x = (imgui.get_content_region_available()[0] - widget_width) * 0.5
        imgui.set_cursor_pos((offset_x, imgui.get_cursor_pos()[1]))

    def display_texture(texture, width, height):
        if texture.id != -1:
            imgui.image(texture.id, width, height)
        else:
            start_cursor = imgui.get_cursor_pos()
            imgui.invisible_button("", width + 2, height + 2)
            end_cursor = imgui.get_cursor_pos()
            start_cursor = (start_cursor[0] + 1, start_cursor[1] + 1)
            imgui.set_cursor_pos(start_cursor)
            imgui.push_style_color(imgui.COLOR_BUTTON, 1.0, 0.0, 0.0)
            imgui.push_style_color(imgui.COLOR_BUTTON_ACTIVE, 1.0, 0.0, 0.0)
            imgui.push_style_color(imgui.COLOR_BUTTON_HOVERED, 1.0, 0.0, 0.0)
            imgui.button("Error: Could not load image", width, height)
            imgui.pop_style_color(3)
            imgui.set_cursor_pos(end_cursor)

    def __init__(self):
        pass

    def pysdl_init(self, width: int, height: int) -> bool:
        if SDL_Init(SDL_INIT_EVERYTHING) < 0:
            print("Couldn't initialize SDL:", SDL_GetError().decode("utf-8"))
            exit(1)

        if IMG_Init(IMG_INIT_PNG) != IMG_INIT_PNG:
            print("Couldn't initialize SDL_Image:", IMG_GetError().decode("utf-8"))
            exit(1)

        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1)
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24)
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8)
        SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1)
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1)
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4)
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG)
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4)
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1)
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE)

        window = SDL_CreateWindow(
            "Horizon – Visual test tool".encode("utf-8"),
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            width, height,
            SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE)

        if window is None:
            print("Coudln't create window:", SDL_GetError().decode("utf-8"))
            SDL_Quit()
            exit(1)

        gl_context = SDL_GL_CreateContext(window)
        if gl_context is None:
            print("Couldn't create OpenGL context:", SDL_GetError().decode("utf-8"))
            SDL_DestroyWindow(window)
            SDL_Quit()
            exit(1)

        SDL_GL_MakeCurrent(window, gl_context)

        if SDL_GL_SetSwapInterval(1) < 0:
            print("Unable to set VSync:", SDL_GetError().decode("utf-8"))

        icon = IMG_Load(f"{ROOT}/hrz/branding/favicon-32x32.png".encode("utf-8"))
        if icon:
            SDL_SetWindowIcon(window, icon)
            SDL_FreeSurface(icon)

        return window, gl_context

    def pysdl_shutdown(self, window, gl_context):
        SDL_GL_DeleteContext(gl_context)
        SDL_DestroyWindow(window)
        SDL_Quit()

    def load_report(self, ctx, go_to_first_in_view_queue = False):
        ctx.has_report = ctx.output_dir.has_report()
        if not ctx.has_report:
            ctx.report = None
            ctx.states = [TestState.Skip] * len(MANIFEST)
            return

        ctx.report = read_report(path.join(ctx.output_dir.path, REPORT_FILE_NAME))
        ctx.states = [TestState.Skip] * len(MANIFEST)
        ctx.view_queue = set()
        ctx.id_to_result_id = {}
        for result_id, result in enumerate(ctx.report.results):
            test_id = ctx.name_to_id.get(result.name, -1)
            if test_id == -1:
                print(f"WARNING: result of '{result.name}' was ignored because it wasn't found in the manifest.")
                continue

            if result.error_type == Result.ErrorType.Aborted:
                ctx.states[test_id] = TestState.Aborted
            elif not result.success:
                ctx.states[test_id] = TestState.Fail
            elif result.error_ratio is not None and result.error_ratio > 0:
                ctx.states[test_id] = TestState.UnderThreshold
            else:
                ctx.states[test_id] = TestState.Success

            ctx.id_to_result_id[test_id] = result_id
            if ctx.states[test_id] == TestState.Fail or ctx.states[test_id] == TestState.UnderThreshold:
                ctx.view_queue.add(test_id)

        if go_to_first_in_view_queue and len(ctx.view_queue) > 0:
            ctx.viewed_test_id = next(iter(ctx.view_queue))

    def run_test_plan(self, ctx, show_viewer_window):
        ctx.show_viewer_window = show_viewer_window
        ctx.test_run_plan = [i for i, test in enumerate(MANIFEST) if ctx.selection[i] and test.matches_filters(ctx.name_filter, ctx.type_filter)]
        ctx.remaining_tests_in_plan = {i for i in ctx.test_run_plan}
        ctx.cancel_test_plan = False
        ctx.test_plan_thread = Thread(target=self.test_plan_worker_thread_func, args=[ctx])
        ctx.test_plan_thread.start()

    def test_plan_worker_thread_func(self, ctx):
        ctx.currently_running_test_in_plan = 0

        while ctx.currently_running_test_in_plan < len(ctx.test_run_plan) and not ctx.cancel_test_plan:
            if len(ctx.test_run_plan) > 1:
                print(f"Running test {ctx.currently_running_test_in_plan + 1} of {len(ctx.test_run_plan)}...")

            ctx.test_plan_execution_ctx.tests = [ctx.test_run_plan[ctx.currently_running_test_in_plan]]
            ctx.test_plan_execution_ctx.report_to_append_to = ctx.report
            ctx.test_plan_execution_ctx.output_dir = ctx.output_dir
            ctx.test_plan_execution_ctx.junit_output_file = None
            ctx.test_plan_execution_ctx.show_viewer_window = ctx.show_viewer_window
            ctx.test_plan_execution_ctx.terminate = False
            run_tests_in_context(ctx.test_plan_execution_ctx)

            self.load_report(ctx, False)

            ctx.remaining_tests_in_plan.remove(ctx.test_run_plan[ctx.currently_running_test_in_plan])
            ctx.currently_running_test_in_plan += 1

        ctx.test_plan_thread = None
        ctx.test_run_plan = []
        ctx.remaining_tests_in_plan = set()
        ctx.currently_running_test_in_plan = None

    def cancel_test_plan(self, ctx):
        if ctx.test_plan_thread is None:
            return

        ctx.cancel_test_plan = True

        if ctx.test_plan_execution_ctx.process:
            ctx.test_plan_execution_ctx.terminate = True
            ctx.test_plan_execution_ctx.process.terminate()

        if ctx.test_plan_thread is not None:
            ctx.test_plan_thread.join()
            ctx.test_plan_thread = None

    # @Todo: overly complicated trying to keep all GUI elements in sync. Test order isn't maintained
    # in the selection list, but whatever...
    def remove_test(self, ctx, to_remove_id):
        to_move_id = len(MANIFEST) - 1

        to_remove = MANIFEST[to_remove_id]
        to_move = MANIFEST[to_move_id]

        ctx.name_to_id[to_move.name] = to_remove_id
        if to_remove_id in ctx.id_to_result_id:
            del ctx.id_to_result_id[to_remove_id]
        if to_move_id in ctx.id_to_result_id:
            ctx.id_to_result_id[to_remove_id] = ctx.id_to_result_id[to_move_id]

        MANIFEST[to_remove_id],MANIFEST[to_move_id] = MANIFEST[to_move_id],MANIFEST[to_remove_id]
        MANIFEST.pop()

        ctx.selection[to_remove_id],ctx.selection[to_move_id] = ctx.selection[to_move_id],ctx.selection[to_remove_id]
        ctx.selection.pop()

        ctx.states[to_remove_id],ctx.states[to_move_id] = ctx.states[to_move_id],ctx.states[to_remove_id]
        ctx.states.pop()

        if ctx.viewed_test_id == to_remove_id:
            ctx.viewed_test_id = None
        elif ctx.viewed_test_id == to_move_id:
            ctx.viewed_test_id = to_remove_id

        ctx.view_queue.discard(to_move_id)

        write_manifest()

    def work_gui_remove(self, ctx):
        if ctx.test is None: return

        style = imgui.get_style()
        text = f"Are you sure you want to remove '{ctx.test.name}'?"
        width = imgui.calc_text_size(text)[0] + 2*style.item_spacing[0]
        imgui.push_style_var(imgui.STYLE_WINDOW_MIN_SIZE, (width, 1))
        popup_open = imgui.begin_popup_modal(REMOVE_POPUP_NAME, flags=imgui.WINDOW_NO_MOVE | imgui.WINDOW_NO_RESIZE)[0]
        imgui.pop_style_var()

        if not popup_open: return

        imgui.text(text)

        available = imgui.get_window_width() - 3 * style.item_spacing[0]
        close_popup = False
        if imgui.button("No", width=available * 0.5):
            close_popup = True
        imgui.same_line()
        if imgui.button("Yes", width=available * 0.5):
            self.remove_test(ctx, ctx.test_id)
            close_popup = True

        if close_popup:
            ctx.test_id = -1
            ctx.test = None
            imgui.close_current_popup()

        imgui.end_popup()

    def work_gui_edit(self, ctx):
        imgui.set_next_window_size(640, 274)
        if not imgui.begin_popup_modal(EDIT_POPUP_NAME, flags=imgui.WINDOW_NO_RESIZE)[0]:
            return

        style = imgui.get_style()

        if ctx.test is None:
            ctx.test = Test()

        input_path = ctx.test.input_path

        _, ctx.test.name = imgui.input_text("Test name", ctx.test.name, 512)
        _, ctx.test.type = imgui.combo("Test type", ctx.test.type, [Test.Type.name(x) for x in list(Test.Type)])

        filetype = ('Horizon scene', '*.hrz_scene.pbf')
        if ctx.test.type == Test.Type.MapboxStyle:
            filetype = ('Mapbox style', '*.json')

        if imgui.button(f"Load {filetype[0]}..."):
            filepath = ask_open_filename(filetypes = [filetype])
            if filepath is not None and filepath != ():
                input_path = filepath
        if ctx.test.input_error is not None:
            imgui.same_line()
            imgui.text_colored(ctx.test.input_error, *RED)
        elif ctx.test.input_info is not None:
            imgui.same_line()
            imgui.text(ctx.test.input_info)
        _, input_path = imgui.input_text(f"{filetype[0]} path", input_path, 512)
        _, ctx.test.error_threshold = imgui.input_float("Error threshold (%)", ctx.test.error_threshold * 100, format="%.6f")
        ctx.test.error_threshold = max(0, min(1, ctx.test.error_threshold / 100))
        _, ctx.test.timeout = imgui.input_int("Timeout (sec)", ctx.test.timeout)
        _, ctx.test.error_strategy = imgui.combo("Error strategy", ctx.test.error_strategy, [Test.ErrorStrategy.name(x) for x in list(Test.ErrorStrategy)])
        if ctx.test.error_strategy == Test.ErrorStrategy.Message:
            _, ctx.test.error_message = imgui.input_text("Error message", ctx.test.error_message, 512)
        _, ctx.generate_ref = imgui.checkbox(("G" if ctx.test.new else "Reg") + "enerate reference image", ctx.generate_ref)

        ctx.test.timeout = max(ctx.test.timeout, 0)

        if input_path != ctx.test.input_path:
            ctx.test.input_path = input_path
            suffix = filetype[1].removeprefix("*")
            if not input_path.endswith(suffix) or not path.exists(input_path):
                ctx.test.input_error = "Invalid file path"
            else:
                ctx.test.input_error = None
                if ctx.test.name == "":
                    ctx.test.name = path.basename(input_path)[0:-len(Test.Type.file_extension(ctx.test.type))]
                if ctx.test.input_must_be_copied():
                    ctx.test.input_info = f"File will be copied to \"{Test.Type.directory(ctx.test.type).relative_to(ROOT)}\""
                else:
                    ctx.test.input_info = None

        available_width = imgui.get_window_width() - 3 * style.item_spacing[0]
        if imgui.get_content_region_available()[1] > 30:
            imgui.set_cursor_pos((imgui.get_cursor_pos()[0], imgui.get_window_height() - 30))

        if imgui.button("Cancel", available_width * 0.5):
            ctx.test_id = -1
            ctx.test = None
            ctx.generate_ref = False
            imgui.close_current_popup()
        imgui.same_line()
        if imgui.button("Create" if ctx.test and ctx.test.new else "Update", available_width * 0.5):
            if ctx.test.input_must_be_copied():
                ctx.test.copy_input()
                ctx.test.input_info = None

            if ctx.test_id != -1:
                MANIFEST[ctx.test_id] = ctx.test
                ctx.name_to_id[ctx.test.name] = ctx.test_id
                if ctx.output_dir.has_capture(ctx.test) and has_input(ctx.test):
                    ctx.states[ctx.name_to_id[ctx.test.name]] = TestState.Updated
            else:
                MANIFEST.append(ctx.test)
                # Update GUI context
                ctx.selection.append(True)
                ctx.states.append(TestState.Skip)
                ctx.name_to_id[ctx.test.name] = len(MANIFEST) - 1

            write_manifest()

            if ctx.generate_ref:
                generate_ref_image(ctx.test, ctx.output_dir.path)

            ctx.test.new = False

            ctx.test_id = -1
            ctx.test = None
            ctx.generate_ref = False
            imgui.close_current_popup()

        imgui.end_popup()

    def work_gui_selection(self, ctx):
        style = imgui.get_style()

        imgui.begin("window-selection", False, imgui.WINDOW_NO_RESIZE | imgui.WINDOW_NO_MOVE | imgui.WINDOW_NO_TITLE_BAR | imgui.WINDOW_NO_SAVED_SETTINGS)

        text = "Working directory"
        Gui.center_next_widget_h(imgui.calc_text_size(text)[0])
        imgui.text(text)
        imgui.spacing()

        if imgui.button("Select directory..."):
            dirpath = ask_directory()
            if dirpath is not None and dirpath != ():
                ctx.output_dir = OutputDirectory(dirpath)
                ctx.output_dir = OutputDirectory(ctx.output_dir_input)
                self.load_report(ctx)

        changed, value = imgui.input_text("Path", str(ctx.output_dir_input), 512, imgui.INPUT_TEXT_ENTER_RETURNS_TRUE)
        if changed:
            if value:
                ctx.output_dir_input = Path(value.replace("\\", "\\\\")).absolute()
            else:
                ctx.output_dir_input = value
            ctx.output_dir = OutputDirectory(ctx.output_dir_input)
            self.load_report(ctx)
        imgui.same_line()
        imgui.text("(?)")
        if imgui.is_item_hovered():
            imgui.begin_tooltip()
            imgui.text("Results are generated in a temporary directory when the path is empty.")
            imgui.end_tooltip()

        imgui.separator()

        text = "Test creation"
        Gui.center_next_widget_h(imgui.calc_text_size(text)[0])
        imgui.text(text)
        imgui.spacing()

        if imgui.button("Create new test...", -1):
            imgui.open_popup(EDIT_POPUP_NAME)
        self.work_gui_edit(ctx)

        imgui.separator()

        text = "Test selection"
        Gui.center_next_widget_h(imgui.calc_text_size(text)[0])
        imgui.text(text)
        imgui.spacing()

        _, ctx.name_filter = imgui.input_text("Filter", ctx.name_filter, 512)
        imgui.same_line()
        if imgui.button("Clear"):
            ctx.name_filter = ""

        imgui.text("Test types:")
        imgui.same_line()
        for type in list(Test.Type):
            clicked, enabled = imgui.checkbox(Test.Type.name(type) + "s", type in ctx.type_filter)
            if clicked:
                if enabled:
                    ctx.type_filter.add(type)
                else:
                    ctx.type_filter.remove(type)
            imgui.same_line()

        imgui.new_line()

        available = imgui.get_window_width() - 3 * style.item_spacing[0]
        if imgui.button("Select all", available * 0.5):
            for test_id, test in enumerate(MANIFEST):
                if test.matches_filters(ctx.name_filter, ctx.type_filter):
                    ctx.selection[test_id] = True

        imgui.same_line()
        if imgui.button("Deselect all", available * 0.5):
            for test_id, test in enumerate(MANIFEST):
                if test.matches_filters(ctx.name_filter, ctx.type_filter):
                    ctx.selection[test_id] = False

        if imgui.button("Select skipped", available * 0.5):
            for test_id, test in enumerate(MANIFEST):
                if test.matches_filters(ctx.name_filter, ctx.type_filter):
                    state = ctx.states[test_id]
                    ctx.selection[test_id] = state == TestState.Skip

        imgui.same_line()
        if imgui.button("Select failed", available * 0.5):
            for test_id, test in enumerate(MANIFEST):
                if test.matches_filters(ctx.name_filter, ctx.type_filter):
                    state = ctx.states[test_id]
                    ctx.selection[test_id] = state == TestState.Fail

        imgui.begin_child("Region", -1, -2 * style.item_spacing[1] - 50, border=True)
        available = imgui.get_content_region_available()[0]
        col_width_remove = imgui.calc_text_size("Remove")[0] + 2 * style.item_spacing[0]
        col_width_edit = imgui.calc_text_size("Edit")[0] + 2 * style.item_spacing[0]
        col_width_go = imgui.calc_text_size("View")[0] + 2 * style.item_spacing[0]
        available -= (col_width_remove + col_width_edit + col_width_go)

        imgui.columns(5, "selectioncolumns", False)
        imgui.set_column_width(4, col_width_remove)
        imgui.set_column_width(3, col_width_edit)
        imgui.set_column_width(2, col_width_go)
        imgui.set_column_width(1, available*0.2)
        imgui.set_column_width(0, available*0.8)

        test_state_label_data = {}
        test_state_label_data[TestState.Skip] = ("skipped", GRAY)
        test_state_label_data[TestState.Fail] = ("failed", RED)
        test_state_label_data[TestState.Success] = ("passed", GREEN)
        test_state_label_data[TestState.UnderThreshold] = ("passed", YELLOW)
        test_state_label_data[TestState.Updated] = ("updated", GREEN)
        test_state_label_data[TestState.Aborted] = ("aborted", GRAY)

        for test_id, test in enumerate(MANIFEST):
            if not test.matches_filters(ctx.name_filter, ctx.type_filter):
                continue

            state = ctx.states[test_id]

            cur_begin = imgui.get_cursor_screen_pos()
            if test_id == ctx.viewed_test_id:
                width = imgui.get_window_width()
                height = imgui.get_frame_height()
                draw_list = imgui.get_window_draw_list()
                draw_list.add_rect_filled(
                    cur_begin[0], cur_begin[1],
                    cur_begin[0] + width, cur_begin[1] + height,
                    imgui.get_color_u32_rgba(*HIGHLIGHT_COLOR))

            _, ctx.selection[test_id] = imgui.checkbox(test.name, ctx.selection[test_id])
            if imgui.is_item_hovered():
                imgui.set_tooltip(test.name)
            imgui.next_column()
            if test_id in ctx.remaining_tests_in_plan:
                imgui.text_colored("queued", *WHITE)
            else:
                imgui.text_colored(test_state_label_data[state][0], *test_state_label_data[state][1])
            imgui.next_column()
            imgui.button("View")
            if imgui.is_item_clicked():
                ctx.viewed_test_id = test_id
            imgui.next_column()
            imgui.button("Edit")
            if imgui.is_item_clicked():
                imgui.open_popup(EDIT_POPUP_NAME)
                ctx.test_id = test_id
                ctx.test = copy(MANIFEST[test_id])
            imgui.next_column()
            imgui.button("Remove")
            if imgui.is_item_clicked() and ctx.test_plan_thread is None:
                ctx.test_id = test_id
                ctx.test = MANIFEST[test_id]
                imgui.open_popup(REMOVE_POPUP_NAME)
            imgui.next_column()

            if ctx.viewed_test_id is None and ctx.selection[test_id]:
                ctx.viewed_test_id = test_id

        self.work_gui_edit(ctx)
        self.work_gui_remove(ctx)

        imgui.columns(1)
        imgui.end_child()

        if ctx.test_plan_thread is None:
            style = imgui.get_style()
            available_width = imgui.get_window_width() - 3 * style.item_spacing[0]
            button_width = available_width * 0.5
            if imgui.button("Run in background", button_width, 30):
                self.run_test_plan(ctx, False)
            if imgui.is_item_hovered():
                tooltip = "Run selected tests, no Horizon window is visible.\n"
                if can_append_to_report(ctx.report):
                    tooltip += "Their results will be added to the current report."
                else:
                    tooltip += "A new report will be generated."
                imgui.set_tooltip(tooltip)
            imgui.same_line()
            if imgui.button("Run in foreground", button_width, 30):
                self.run_test_plan(ctx, True)
            if imgui.is_item_hovered():
                tooltip = "Run selected tests, the Horizon window is visible.\n"
                if can_append_to_report(ctx.report):
                    tooltip += "Their results will be added to the current report."
                else:
                    tooltip += "A new report will be generated."
                imgui.set_tooltip(tooltip)
        else:
            button_name = "Cancel test run queue"
            if ctx.currently_running_test_in_plan is not None:
                button_name += f" ({ctx.currently_running_test_in_plan + 1}/{len(ctx.test_run_plan)})"
            if imgui.button(button_name, -1, 30):
                self.cancel_test_plan(ctx)

        if imgui.button("(Re)generate reference images", -1, 20):
            tests = [i for i, test in enumerate(MANIFEST) if ctx.selection[i] and test.matches_filters(ctx.name_filter, ctx.type_filter)]
            generate_ref_images(tests, ctx.output_dir.path)
            if ctx.report is not None:
                for result_id, result in enumerate(ctx.report.results):
                    test_id = ctx.name_to_id.get(result.name, -1)
                    if test_id in tests and has_ref(MANIFEST[test_id]):
                        ctx.states[test_id] = TestState.Updated
        if imgui.is_item_hovered():
            imgui.set_tooltip("(Re)generate reference images for selected tests.")

        imgui.end()

    def work_gui_test(self, ctx):
        style = imgui.get_style()

        test = MANIFEST[ctx.viewed_test_id]

        result = None
        if ctx.viewed_test_id in ctx.id_to_result_id:
            result = ctx.report.results[ctx.id_to_result_id[ctx.viewed_test_id]]

        text = "Test results"
        Gui.center_next_widget_h(imgui.calc_text_size(text)[0])
        imgui.text(text)
        imgui.spacing()

        imgui.columns(2, "report-result", False)
        imgui.set_column_width(0, min(imgui.get_column_width(), 150))
        imgui.text("Test name")
        imgui.text("Test type")
        imgui.text("Status")
        if result is not None:
            imgui.text("Error")
            if result.error_message != "":
                imgui.text("Error message")
        else:
            imgui.text("Error threshold")
        imgui.next_column()
        imgui.text(test.name)
        imgui.text(Test.Type.name(test.type))
        if result is not None:
            if result.success:
                color = GREEN if result.error_ratio is None or result.error_ratio == 0 else YELLOW
                imgui.text_colored("Passed", *color)
            else:
                imgui.text_colored("Failure", *RED)
            if result.error_ratio is not None:
                imgui.text(f"{result.error_ratio * 100:.5f}%")
            else:
                imgui.text(f"--%")
            imgui.same_line()
            imgui.text(f"(threshold: {test.error_threshold * 100:.5f}%)")
            if result.error_message != "":
                imgui.text(result.error_message)
        else:
            imgui.text_colored("Not run", *GRAY)
            imgui.text(f"{test.error_threshold * 100:.5f}%")
        imgui.columns(1)
        imgui.spacing()

        imgui.begin_child("region-images", -1, -style.item_spacing[1] - 30, border=False)

        if result is not None:
            if result.error_type == Result.ErrorType.MissingInput:
                imgui.text_colored(f"Couldn't load input at {get_input_path(result.name, result.type)}", *RED)
            elif result.error_type == Result.ErrorType.MissingRef:
                imgui.text_colored(f"Couldn't load reference image at {get_ref_image_path(result.name, result.type)}", *RED)
            if result.error_type == Result.ErrorType.MigrationError:
                imgui.text_colored(f"Couldn't migrate scene dump at {get_input_path(result.name, result.type)}", *RED)
            elif result.error_type == Result.ErrorType.Timeout:
                imgui.text_colored(f"The test has timed out after {test.timeout} seconds.", *RED)
            elif result.error_type == Result.ErrorType.Viewer:
                imgui.text_colored("The scene couldn't be captured due to a viewer error", *RED)
            elif result.error_type == Result.ErrorType.Aborted:
                imgui.text_colored("Test was aborted due to a previous error.", *RED)
            elif result.error_type == Result.ErrorType.Comparator:
                imgui.text_colored("There was an error during capture comparison", *RED)

            has_capture_and_diff = result.error_type == Result.ErrorType.None_ or result.error_type == Result.ErrorType.Threshold

            if ctx.uploaded_textures_test_id != ctx.viewed_test_id or ctx.uploaded_textures_report_date != ctx.report.date:
                ctx.textures[0].upload_image_on_gpu(get_ref_image_path(result.name, result.type))
                if has_capture_and_diff:
                    ctx.textures[1].upload_image_on_gpu(get_capture_path(ctx.output_dir.path, result.name))
                    ctx.textures[2].upload_image_on_gpu(get_diff_path(ctx.output_dir.path, result.name))
                ctx.uploaded_textures_test_id = ctx.viewed_test_id
                ctx.uploaded_textures_report_date = ctx.report.date

            if has_capture_and_diff:
                expanded, _ = imgui.collapsing_header("Images", flags=imgui.TREE_NODE_DEFAULT_OPEN)
                if expanded:
                    Gui.center_next_widget_h(IMAGE_SIZE*1.5 + style.item_spacing[0])
                    Gui.display_texture(ctx.textures[2], IMAGE_SIZE, IMAGE_SIZE)
                    imgui.same_line()
                    imgui.begin_group()
                    Gui.display_texture(ctx.textures[0], IMAGE_SIZE*0.5, IMAGE_SIZE*0.5)
                    Gui.display_texture(ctx.textures[1], IMAGE_SIZE*0.5, IMAGE_SIZE*0.5)
                    imgui.end_group()

                    now = int(time.time_ns() / 1_000_000)
                    if imgui.is_item_hovered():
                        imgui.begin_tooltip()
                        Gui.display_texture(ctx.textures[ctx.compare_texture_id], IMAGE_SIZE, IMAGE_SIZE)
                        text = ["Reference", "Capture"]
                        Gui.center_next_widget_h(imgui.calc_text_size(text[ctx.compare_texture_id])[0])
                        imgui.text(text[ctx.compare_texture_id])
                        imgui.end_tooltip()

                        if now - ctx.compare_last_time > 500:
                            ctx.compare_texture_id = (ctx.compare_texture_id + 1) & 1
                            ctx.compare_last_time = now
            else:
                expanded, _ = imgui.collapsing_header("Reference image", flags=imgui.TREE_NODE_DEFAULT_OPEN)
                if expanded:
                    Gui.center_next_widget_h(IMAGE_SIZE + style.item_spacing[0])
                    Gui.display_texture(ctx.textures[0], IMAGE_SIZE, IMAGE_SIZE)

            if result.log:
                expanded, _ = imgui.collapsing_header("Logs")
                if expanded:
                    imgui.begin_child("logs_content", height=250, border=True, flags=imgui.WINDOW_HORIZONTAL_SCROLLING_BAR)
                    imgui.push_font(self.mono_font)
                    imgui.text_unformatted(result.log)
                    imgui.pop_font()
                    imgui.end_child()
        else:
            if ctx.uploaded_textures_test_id != ctx.viewed_test_id:
                ctx.textures[0].upload_image_on_gpu(get_ref_image_path(test.name, test.type))
                ctx.uploaded_textures_test_id = ctx.viewed_test_id

            expanded, _ = imgui.collapsing_header("Reference image", flags=imgui.TREE_NODE_DEFAULT_OPEN)
            if expanded:
                    Gui.center_next_widget_h(IMAGE_SIZE + style.item_spacing[0])
                    Gui.display_texture(ctx.textures[0], IMAGE_SIZE, IMAGE_SIZE)

        imgui.end_child()

        available = imgui.get_content_region_available()[0] - 2*style.item_spacing[0]
        go_to_next_in_queue = False
        if imgui.button("Regenerate reference image", available * 0.5, 30):
            if result is not None and ctx.output_dir.has_capture(test) and has_input(test):
                shutil.copyfile(get_capture_path(ctx.output_dir.path, result.name), get_ref_image_path(result.name, result.type))
            else:
                generate_ref_image(MANIFEST[ctx.viewed_test_id], ctx.output_dir.path)
            ctx.states[ctx.name_to_id[test.name]] = TestState.Updated
            go_to_next_in_queue = True
        imgui.same_line()
        if imgui.button("Edit", available * 0.4, 30):
            imgui.open_popup(EDIT_POPUP_NAME)
            ctx.test_id = ctx.name_to_id[test.name]
            ctx.test = copy(MANIFEST[ctx.test_id])
        self.work_gui_edit(ctx)
        imgui.same_line()
        go_to_next_in_queue |= imgui.button("Review next", available * 0.1, 30)
        if imgui.is_item_hovered():
            remaining = len(ctx.view_queue)
            if ctx.viewed_test_id in ctx.view_queue:
                remaining -= 1
            imgui.set_tooltip(f"Go to next unreviewed failed test\n{remaining} remaining")

        if go_to_next_in_queue:
            ctx.view_queue.discard(ctx.viewed_test_id)
            next_in_queue = next(iter(ctx.view_queue), None)
            if next_in_queue is not None:
                ctx.viewed_test_id = next_in_queue

    def work_gui_report(self, ctx):
        imgui.begin("window-report", False, imgui.WINDOW_NO_RESIZE | imgui.WINDOW_NO_MOVE | imgui.WINDOW_NO_TITLE_BAR | imgui.WINDOW_NO_SAVED_SETTINGS)

        if ctx.has_report:
            text = "Report"
            Gui.center_next_widget_h(imgui.calc_text_size(text)[0])
            imgui.text(text)
            imgui.spacing()

            imgui.columns(2, "report-info", False)
            imgui.set_column_width(0, min(imgui.get_column_width(), 150))
            imgui.text("Report path")
            imgui.text("Date")
            imgui.text("Branch")
            imgui.text("Commit")
            imgui.next_column()
            imgui.text(path.join(ctx.output_dir.path, REPORT_FILE_NAME))
            show_tooltip = imgui.is_item_hovered()
            use_report_dir = imgui.is_item_clicked()
            imgui.text(str(datetime.datetime.fromisoformat(ctx.report.date).astimezone()))
            imgui.text(ctx.report.branch)
            imgui.text(ctx.report.commit)
            imgui.columns(1)

            if show_tooltip:
                imgui.set_tooltip("Click to use as the working directory")
            if use_report_dir:
                ctx.output_dir.preserve = True
                ctx.output_dir_input = ctx.output_dir.path

            imgui.separator()

            def center_text_colored(text, color):
                cursor = imgui.get_cursor_pos()
                offset_x = (imgui.get_column_width() - imgui.calc_text_size(text)[0]) * 0.5
                imgui.set_cursor_pos((cursor[0] + offset_x, cursor[1]))
                imgui.text_colored(text, *color)

            passed_ratio_str = "--"
            failed_ratio_str = "--"
            aborted_ratio_str = "--"
            if ctx.report.total_tests > 0:
                passed_ratio = (ctx.report.passed_tests / ctx.report.total_tests) * 100
                failed_ratio = (ctx.report.failed_tests / ctx.report.total_tests) * 100
                aborted_ratio = (ctx.report.aborted_tests / ctx.report.total_tests) * 100

                passed_ratio_str = f"{passed_ratio:.2f}"
                failed_ratio_str = f"{failed_ratio:.2f}"
                aborted_ratio_str = f"{aborted_ratio:.2f}"

            imgui.columns(4, "testcount", False)
            center_text_colored("Total", WHITE)
            center_text_colored(f"{str(ctx.report.total_tests)}", WHITE)
            imgui.next_column()
            center_text_colored("Passed", GREEN)
            center_text_colored(f"{str(ctx.report.passed_tests)} ({passed_ratio_str}%)", GREEN)
            imgui.next_column()
            center_text_colored("Failed", RED)
            center_text_colored(f"{str(ctx.report.failed_tests)} ({failed_ratio_str}%)", RED)
            imgui.next_column()
            center_text_colored("Aborted", GRAY)
            center_text_colored(f"{str(ctx.report.aborted_tests)} ({aborted_ratio_str}%)", GRAY)
            imgui.columns(1)
            imgui.separator()
            imgui.spacing()

        if ctx.viewed_test_id != None:
            self.work_gui_test(ctx)

        imgui.end()

    def run(self, output_dir):
        window_size = (1280, 720)
        window, gl_context = self.pysdl_init(*window_size)

        imgui.create_context()
        imgui.want_save_ini_settings = False
        renderer = SDL2Renderer(window)

        io = imgui.get_io()
        io.fonts.clear()
        self.normal_font = io.fonts.add_font_from_file_ttf(str(FONT_FILE), 14.0)
        self.mono_font = io.fonts.add_font_from_file_ttf(str(MONO_FONT_FILE), 16.0)
        renderer.refresh_font_texture()

        style = imgui.get_style()
        style.window_rounding = 0

        ctx = Gui.Context(output_dir)
        self.load_report(ctx)

        event = SDL_Event()
        running = True
        while running:
            while SDL_PollEvent(ctypes.byref(event)):
                if event.type == SDL_QUIT:
                    running = False
                    break
                if event.type == SDL_WINDOWEVENT:
                    if event.window.event == SDL_WINDOWEVENT_RESIZED or\
                        event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED:
                        window_size = (event.window.data1, event.window.data2)
                renderer.process_event(event)
            renderer.process_inputs()

            imgui.new_frame()

            imgui.set_next_window_size(window_size[0] / 3, window_size[1])
            imgui.set_next_window_position(0, 0)
            self.work_gui_selection(ctx)

            imgui.set_next_window_size(2 * window_size[0] / 3 + 1, window_size[1])
            imgui.set_next_window_position(window_size[0] / 3, 0)
            self.work_gui_report(ctx)

            imgui.render()
            gl.glClearColor(1., 1., 1., 1.)
            gl.glClear(gl.GL_COLOR_BUFFER_BIT)
            gl.glClear(gl.GL_DEPTH_BUFFER_BIT)
            renderer.render(imgui.get_draw_data())
            SDL_GL_SwapWindow(window)

        self.cancel_test_plan(ctx)

        del ctx.textures
        ctx.uploaded_textures_test_id = None
        ctx.uploaded_textures_report_date = None
        renderer.shutdown()
        self.pysdl_shutdown(window, gl_context)

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("-m", "--manifest", help="Manifest path", default=MANIFEST_PATH)
    parser.add_argument("-o", help="Output directory", nargs="?", type=str, default="")
    parser.add_argument("-v", help="Verbose", action="store_true", default=False)
    parser.add_argument("-b", help="Branch name", nargs="?", type=str, default="")
    parser.add_argument("-c", "--compilation_mode", help="Compilation mode", nargs="?", type=str, default="")
    parser.add_argument("--bazelrc", help="Path to bazelrc file", nargs="?", type=str, default="")
    parser.add_argument("--output_base", help="Path Bazel output base directory", nargs="?", type=str, default="")
    parser.add_argument("--batch", help="Execute Bazel in batch mode", action="store_true", default=False)
    parser.add_argument("--gui", help="Run the visual tests GUI tool", action="store_true", default=False)
    parser.add_argument("--show", help="Show Horizon window", action="store_true", default=False)
    parser.add_argument("--sync", help="Sync the manifest entries with the available input files", action="store_true", default=False)
    parser.add_argument("--junit_report", help="JUnit report output file", type=str, default=None)
    parser.add_argument("--viewer", help="Viewer executable", type=str, default=None)
    parser.add_argument("--comparator", help="Comparator executable", type=str, default=None)

    if platform.system() == "Linux":
        parser.add_argument("--wsi", help="Choose the windowing system integration", type=str, default="x11", choices=["x11", "headless_egl"])

    subparsers = parser.add_subparsers()

    subparser = subparsers.add_parser("subset", help="Runs a subset of tests")
    subparser.add_argument("tests", nargs="+", help="List of tests to run")
    subparser.set_defaults(func=run_subset)

    subparser = subparsers.add_parser("exclude", help="Runs all the tests except those specified")
    subparser.add_argument("tests", nargs="+", help="List of tests to skip")
    subparser.set_defaults(func=run_exclude)

    subparser = subparsers.add_parser("create", help="Create a new test")
    subparser.add_argument("test_name", help="Test name")
    subparser.add_argument("-t", "--threshold", type=float, help="Error threshold", default=0.015)
    group = subparser.add_mutually_exclusive_group(required=True)
    group.add_argument("--scene_dump_path", help="Scene dump file")
    group.add_argument("--mapbox_style_path", help="Mapbox style file")
    subparser.set_defaults(func=create_test)

    subparser = subparsers.add_parser("remove", help="Remove a test")
    subparser.add_argument("test_name", help="Test name")
    subparser.set_defaults(func=remove_test)

    args = parser.parse_args(sys.argv[1:])

    if args.v:
        print("Verbose mode enabled")
        VERBOSE = True
        STDOUT = None
        STDERR = subprocess.STDOUT

    MANIFEST_PATH = args.manifest
    MANIFEST = read_manifest(MANIFEST_PATH)
    TEST_DATA_DIR = Path(MANIFEST_PATH).parent.absolute()

    print("Root:", ROOT)
    print("Test data:", TEST_DATA_DIR)

    if args.b != "":
        BRANCH = args.b
        print("Branch name:", BRANCH)

    bazel_startup_options = []
    if args.bazelrc != "":
        bazel_startup_options.append("--bazelrc=" + args.bazelrc)
        print("bazelrc:", args.bazelrc)
    if args.output_base != "":
        bazel_startup_options.append("--output_base=" + args.output_base)
        print("output_base:", args.output_base)
    if args.batch:
        bazel_startup_options.append("--batch")

    bazel_build_options = []
    if args.compilation_mode != "":
        bazel_build_options += ["-c", args.compilation_mode]

    if args.bazelrc == "":
        bazel_build_options += ["--config=" + platform.system().lower()]
    else:
        print("Relying on the bazelrc file for platform-specific configuration.")


    if args.sync:
        print("Synchronising manifest file.")
        sync_manifest()

    if args.viewer:
        VIEWER_EXE = args.viewer
    else:
        print("Building viewer executable...", end="", flush=True)
        if VERBOSE:
            print("")
        viewer_build_command = ["bazel"] + bazel_startup_options + ["build", "//tools/visual_testing:viewer"] + bazel_build_options
        if platform.system() == "Linux" and args.wsi == "headless_egl":
            viewer_build_command.append("--//:linux_wsi=headless_egl")
        build_error = subprocess.run(viewer_build_command,
            cwd=ROOT,
            stdout=STDOUT,
            stderr=STDERR).returncode != 0
        VIEWER_EXE = f"{ROOT}/bazel-bin/tools/visual_testing/viewer{EXE_EXT}"
        if build_error:
            print(" [FAILED]")
            sys.exit(1)
        else:
            print(" [OK]")

    if args.comparator:
        COMPARATOR_EXE = args.comparator
    else:
        print("Building comparator executable...", end="", flush=True)
        if VERBOSE:
            print("")
        build_error = subprocess.run(["bazel"] + bazel_startup_options + ["build", "//tools/visual_testing:comparator"] + bazel_build_options,
            cwd=ROOT,
            stdout=STDOUT,
            stderr=STDERR).returncode != 0
        COMPARATOR_EXE = f"{ROOT}/bazel-bin/tools/visual_testing/comparator{EXE_EXT}"
        if build_error:
            print(" [FAILED]")
            sys.exit(1)
        else:
            print(" [OK]")

    if VERBOSE:
        print(f"Using viewer {VIEWER_EXE}")
        print(f"Using comparator {COMPARATOR_EXE}")

    get_git_info()

    if args.gui:
        # Include libraries for the GUI only when needed so that extra dependencies don't need to
        # be installed when using the tool in CLI only. This is particularily useful in a CI
        # environment.
        import ctypes
        import imgui
        import OpenGL.GL as gl
        import tkinter
        import tkinter.filedialog

        from sdl2 import *
        from sdl2.sdlimage import *
        from imgui.integrations.sdl2 import SDL2Renderer

        # Include platform again because otherwise it's overriden by sdl2.platform
        import platform

        Gui().run(args.o)
    else:
        failed_tests = 0
        if hasattr(args, "func"):
            failed_tests = args.func(args)
        else:
            failed_tests = run_all(args)
        sys.exit(0 if failed_tests == 0 else 1)
