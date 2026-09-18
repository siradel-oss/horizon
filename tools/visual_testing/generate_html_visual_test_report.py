# SPDX-FileCopyrightText: Copyright 2022 Siradel
# SPDX-License-Identifier: MIT

#!/usr/bin/env python3

import argparse
import base64
import json
import os
import sys
from io import TextIOWrapper
from pathlib import Path
from typing import Any

import jinja2

sys.path.append(str(Path(__file__).parent.parent.parent.absolute()))

from tools.visual_testing import core
from tools.visual_testing.protocol import schema


def get_root_path() -> Path:
    return Path(__file__).parent.parent.parent


def get_favicon_base64(size: int) -> str:
    icon_path = get_root_path() / f"hrz/branding/favicon-{size}x{size}.png"
    with open(icon_path, "rb") as icon_file:
        return base64.b64encode(icon_file.read()).decode("ascii")


def has_failed_tests(results: list[schema.Result]) -> bool:
    return any(not test.success for test in results)


def has_tests_with_errors(results: list[schema.Result]) -> bool:
    return any(
        test.success and test.error_ratio is not None and test.error_ratio > 0
        for test in results
    )


def format_error_ratio(error_ratio: float | None) -> str:
    if error_ratio is None:
        return "--"
    return "%.2f" % (error_ratio * 100)


def test_to_css_class(test: schema.Result) -> str:
    if test.error_type != schema.ErrorType.NONE:
        return "failure"
    if test.error_ratio is not None and test.error_ratio > 0:
        return "error"
    return "success"


def make_template_env():
    template_path = os.path.join(get_root_path(), "tools/visual_testing/report")
    loader = jinja2.FileSystemLoader(template_path, encoding="utf-8")
    template_env = jinja2.Environment(loader=loader)
    template_env.filters["get_favicon_base64"] = get_favicon_base64
    template_env.filters["test_to_css_class"] = test_to_css_class
    template_env.filters["format_error_ratio"] = format_error_ratio
    return template_env


def render_template(
    template_env: jinja2.Environment,
    template_name: str,
    params: dict[str, Any],
    output_file: TextIOWrapper,
):
    template = template_env.get_template(template_name)
    _ = output_file.write(template.render(params))


def generate_html_report(json_path: Path, output_file_name: str) -> int:
    if not json_path.exists():
        print("Error: File not found:", json_path)
        return 1

    report: schema.Report | None = None
    with open(json_path, "r", encoding="utf-8") as json_file:
        report = core.deserialize_report(json.load(json_file))
    if report is None:
        print("Could not load JSON report")
        return 2

    report_dir_path = json_path.parent
    html_path = report_dir_path / output_file_name

    template_env = make_template_env()

    with open(html_path, "w", encoding="utf-8", newline="\n") as html_file:
        report_dict = {**report.__dict__}
        report_dict["has_failed_tests"] = has_failed_tests(report.results)
        report_dict["has_tests_with_errors"] = has_tests_with_errors(report.results)
        report_dict["passed_tests"] = sum(
            1 for test in report.results if test.error_type == schema.ErrorType.NONE
        )
        report_dict["failed_tests"] = sum(
            1
            for test in report.results
            if test.error_type not in (schema.ErrorType.NONE, schema.ErrorType.ABORTED)
        )
        report_dict["total_tests"] = len(report.results)
        render_template(template_env, "report.tpl.html", report_dict, html_file)

    return 0


def generate_html_index(output_file_path: Path) -> int:
    template_env = make_template_env()

    with open(output_file_path, "w", encoding="utf-8", newline="\n") as html_file:
        render_template(template_env, "index.tpl.html", {}, html_file)

    return 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    _ = parser.add_argument(
        "report_path", nargs="?", default=None, type=str, help="Visual test report path"
    )
    _ = parser.add_argument(
        "-o",
        nargs=1,
        default=["hrz-visual-test-report.html"],
        type=str,
        help="Output HTML file name",
    )
    _ = parser.add_argument(
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
        res = generate_html_report(Path(args.report_path), args.o[0])
    else:
        res = generate_html_index(args.o[0])

    sys.exit(res)
