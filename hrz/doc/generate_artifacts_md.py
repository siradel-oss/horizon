# SPDX-FileCopyrightText: Copyright 2026 Siradel
# SPDX-License-Identifier: MIT

import sys
from pathlib import Path

import jinja2
from python.runfiles import Runfiles

from hrz.generator.common import (
    prepare_env,
    output_template,
)


def generate_artifacts_md(
    version: str,
    is_internal: bool,
    build_date: str,
    tpl_env: jinja2.Environment,
    output_dir: Path,
):
    npm_version = version
    is_snapshot = "SNAPSHOT" in version

    values: dict[str, str] = {}

    if is_snapshot:
        # npm snapshot package names have an extra qualifier appended to them (the date of the build).
        # See `tools/bazel/qualify_package_json_version.py`.
        npm_qualifier = build_date
        npm_version += "." + npm_qualifier

    if is_internal:
        npm_base = ""
        raw_base = ""

        values["core_windows"] = f"{raw_base}core-windows-{version}.tar.gz"
        values["api_windows"] = f"{raw_base}cpp-api-windows-{version}.tar.gz"
        values["protocol_windows"] = f"{raw_base}cpp-protocol-windows-{version}.tar.gz"

        values["core_linux"] = f"{raw_base}core-linux-{version}.tar.gz"
        values["api_linux"] = f"{raw_base}cpp-api-linux-{version}.tar.gz"
        values["protocol_linux"] = f"{raw_base}cpp-protocol-linux-{version}.tar.gz"

        values["core_npm"] = (
            f"{npm_base}@siradel-oss/horizon-core/-/horizon-core-{npm_version}.tgz"
        )
        values["api_npm"] = (
            f"{npm_base}@siradel-oss/horizon-api/-/horizon-api-{npm_version}.tgz"
        )
        values["protocol_npm"] = (
            f"{npm_base}@siradel-oss/horizon-protocol/-/horizon-protocol-{npm_version}.tgz"
        )

        values["scene_dump_npm"] = (
            f"{npm_base}@siradel-oss/horizon-scene-dump/-/horizon-scene-dump-{npm_version}.tgz"
        )
        values["monitoring_protocol_npm"] = (
            f"{npm_base}@siradel-oss/horizon-monitoring-protocol/-/horizon-monitoring-protocol-{npm_version}.tgz"
        )

        values["monitoring_app_windows"] = (
            f"{raw_base}monitoring-client-windows-{version}.exe"
        )
        values["monitoring_app_linux"] = f"{raw_base}monitoring-client-linux-{version}"

        values["testing_kit_linux_x11"] = (
            f"{raw_base}testing-kit-linux-x11-{version}.tar.gz"
        )
        values["testing_kit_linux_headless"] = (
            f"{raw_base}testing-kit-linux-headless-{version}.tar.gz"
        )
        values["testing_kit_windows"] = (
            f"{raw_base}testing-kit-windows-{version}.tar.gz"
        )

        values["documentation"] = f"{raw_base}documentation-{version}.tar.gz"
        values["gallery"] = f"{raw_base}gallery-{version}.tar.gz"
    else:
        base = f"https://github.com/siradel-oss/Horizon/releases/download/v{version}/"

        values["core_windows"] = f"{base}horizon-core-{version}-cpp-windows.tar.gz"
        values["api_windows"] = f"{base}horizon-api-{version}-cpp-windows.tar.gz"
        values["protocol_windows"] = (
            f"{base}horizon-protocol-{version}-cpp-windows.tar.gz"
        )

        values["core_linux"] = f"{base}horizon-core-{version}-cpp-linux.tar.gz"
        values["api_linux"] = f"{base}horizon-api-{version}-cpp-linux.tar.gz"
        values["protocol_linux"] = f"{base}horizon-protocol-{version}-cpp-linux.tar.gz"

        values["core_npm"] = f"{base}horizon-core-{version}-ts-npm.tgz"
        values["api_npm"] = f"{base}horizon-api-{version}-ts-npm.tgz"
        values["protocol_npm"] = f"{base}horizon-protocol-{version}-ts-npm.tgz"

        values["scene_dump_npm"] = f"{base}horizon-scene-dump-{version}-ts-npm.tgz"
        values["monitoring_protocol_npm"] = (
            f"{base}horizon-monitoring-protocol-{version}-ts-npm.tgz"
        )

        values["monitoring_app_windows"] = (
            f"{base}horizon-monitoring-client-{version}-windows.exe"
        )
        values["monitoring_app_linux"] = (
            f"{base}horizon-monitoring-client-{version}-linux"
        )

        values["testing_kit_linux_x11"] = (
            f"{base}horizon-testing-kit-{version}-linux-x11.tar.gz"
        )
        values["testing_kit_linux_headless"] = (
            f"{base}horizon-testing-kit-{version}-linux-headless.tar.gz"
        )
        values["testing_kit_windows"] = (
            f"{base}horizon-testing-kit-{version}-windows.tar.gz"
        )

        values["documentation"] = f"{base}horizon-documentation-{version}.tar.gz"
        values["gallery"] = f"{base}horizon-gallery-{version}.tar.gz"

    tpl = tpl_env.get_template("artifacts.tpl.md")
    output_template(values, tpl, output_dir, "artifacts.md")


def main():
    r = Runfiles.Create()

    if r is None:
        raise Exception("Failed to create Runfiles instance")

    build_date_path = r.Rlocation("horizon/build_date.txt")
    if build_date_path is None:
        raise Exception("Failed to locate build date file")
    build_date = Path(build_date_path).read_text().strip()

    tpl_env = prepare_env("horizon/hrz/doc/templates")
    output_dir = Path(sys.argv[1])

    version = sys.argv[2]
    is_internal = "SNAPSHOT" in version

    generate_artifacts_md(version, is_internal, build_date, tpl_env, output_dir)


if __name__ == "__main__":
    main()
