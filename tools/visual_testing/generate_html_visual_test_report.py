#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import argparse
import base64
import jinja2
import json
import os
import sys
from pathlib import Path


def get_root_path():
    return str(Path(__file__).parent.parent.parent)


def get_favicon_base64(size):
    icon_path = os.path.join(get_root_path(), f"hrz/branding/favicon-{size}x{size}.png")
    with open(icon_path, "rb") as icon_file:
        return base64.b64encode(icon_file.read()).decode("ascii")


def has_failed_tests(results):
    for test in results:
        if not test["success"]:
            return True
    return False


def has_tests_with_errors(results):
    for test in results:
        if (
            test["success"]
            and test["errorRatio"] is not None
            and test["errorRatio"] > 0
        ):
            return True
    return False


def format_error_ratio(error_ratio):
    if error_ratio is None:
        return "--"
    return "%.2f" % (error_ratio * 100)


def error_type_to_name(error_type):
    error_names = [
        "None",
        "Viewer",
        "Timeout",
        "Threshold",
        "Missing reference image",
        "Missing scene dump",
        "Migration error",
        "Aborted",
    ]
    return error_names[error_type]


def test_to_css_class(test):
    if test["errorType"] != 0:
        return "failure"
    if test["errorRatio"] and test["errorRatio"] > 0:
        return "error"
    return "success"


def make_template_env():
    template_path = os.path.join(get_root_path(), "tools/visual_testing/report")
    loader = jinja2.FileSystemLoader(template_path, encoding="utf-8")
    template_env = jinja2.Environment(loader=loader)
    template_env.filters["get_favicon_base64"] = get_favicon_base64
    template_env.filters["has_failed_tests"] = has_failed_tests
    template_env.filters["has_tests_with_errors"] = has_tests_with_errors
    template_env.filters["error_type_to_name"] = error_type_to_name
    template_env.filters["test_to_css_class"] = test_to_css_class
    template_env.filters["format_error_ratio"] = format_error_ratio
    return template_env


def render_template(template_env, template_name, params, output_file):
    template = template_env.get_template(template_name)
    output_file.write(template.render(params))


def generate_html_report(json_path, output_file_name):
    if not os.path.exists(json_path):
        print("Error: File not found:", json_path)
        return 1

    report = None
    with open(json_path, "r", encoding="utf-8") as json_file:
        report = json.load(json_file)
    if report is None:
        print("Could not load JSON report")
        return 2

    report_dir_path = os.path.dirname(json_path)
    html_path = os.path.join(report_dir_path, output_file_name)

    template_env = make_template_env()

    with open(html_path, "w", encoding="utf-8", newline="\n") as html_file:
        render_template(template_env, "report.tpl.html", report, html_file)

    return 0


def generate_html_index(output_file_path):
    template_env = make_template_env()

    with open(output_file_path, "w", encoding="utf-8", newline="\n") as html_file:
        render_template(template_env, "index.tpl.html", {}, html_file)

    return 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "report_path", nargs="?", default=None, type=str, help="Visual test report path"
    )
    parser.add_argument(
        "-o",
        nargs=1,
        default=["hrz-visual-test-report.html"],
        type=str,
        help="Output HTML file name",
    )
    parser.add_argument(
        "-t",
        nargs=1,
        choices=["report", "index"],
        default=["report"],
        type=str,
        help="Page type: report or index",
    )

    args = parser.parse_args(sys.argv[1:])

    if args.t[0] == "report":
        if args.report_path is None:
            print("Error: Missing required argument for report generation: report_path")
            sys.exit(1)
        res = generate_html_report(args.report_path, args.o[0])
    else:
        res = generate_html_index(args.o[0])

    sys.exit(res)
