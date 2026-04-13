from dataclasses import asdict, dataclass
import sys
from pathlib import Path

import jinja2
from python.runfiles import Runfiles

from hrz.generator.protocol_model import HrzProtocol
from hrz.generator import protocol_parser
from hrz.generator.common import (
    prepare_env,
    output_template,
)


def generate_artifacts_md(
    version: str,
    is_opensource: bool,
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

    if not is_opensource:
        npm_base = ""
        raw_base = ""

        values["core_windows"] = f"{raw_base}core-windows-{version}.tar.gz"
        values["api_windows"] = f"{raw_base}cpp-api-windows-{version}.tar.gz"
        values["protocol_windows"] = f"{raw_base}cpp-protocol-windows-{version}.tar.gz"

        values["core_linux"] = f"{raw_base}core-linux-{version}.tar.gz"
        values["api_linux"] = f"{raw_base}cpp-api-linux-{version}.tar.gz"
        values["protocol_linux"] = f"{raw_base}cpp-protocol-linux-{version}.tar.gz"

        values["core_npm"] = (
            f"{npm_base}@siradel/horizon-core/-/horizon-core-{npm_version}.tgz"
        )
        values["api_npm"] = (
            f"{npm_base}@siradel/horizon-api/-/horizon-api-{npm_version}.tgz"
        )
        values["protocol_npm"] = (
            f"{npm_base}@siradel/horizon-protocol/-/horizon-protocol-{npm_version}.tgz"
        )

        values["scene_dump_npm"] = (
            f"{npm_base}@siradel/horizon-scene-dump/-/horizon-scene-dump-{npm_version}.tgz"
        )
        values["monitoring_protocol_npm"] = (
            f"{npm_base}@siradel/horizon-monitoring-protocol/-/horizon-monitoring-protocol-{npm_version}.tgz"
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

        values["documentation"] = f"{raw_base}api-doc-{version}.tar.gz"
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

    tpl = tpl_env.get_template("artifacts.tpl.md")
    output_template(values, tpl, output_dir, "artifacts.md")


def generate_reference(
    protocol: HrzProtocol, tpl_env: jinja2.Environment, output_dir: Path
):
    scene_model_roots = [
        {"type": msg.full_name, "root_field": msg.path_root}
        for msg in protocol.messages
        if msg.is_path_root
    ]

    values = asdict(protocol)
    values["scene_model_roots"] = scene_model_roots

    tpl = tpl_env.get_template("reference.tpl.md")
    output_template(values, tpl, output_dir, "reference.md")


def generate_style_enums(
    protocol: HrzProtocol, tpl_env: jinja2.Environment, output_dir: Path
):
    tpl = tpl_env.get_template("style_enums.tpl.md")
    output_template(asdict(protocol), tpl, output_dir, "style_enums.md")


@dataclass
class Crs:
    srid: int
    name: str
    proj_str: str


def read_crs_database(crs_database_path: str) -> dict[str, list[Crs]]:
    crs_list: dict[str, list[Crs]] = {}

    with open(crs_database_path) as in_file:
        for line in in_file:
            parts = line.split("\t")
            auth = parts[0]
            l = crs_list.get(auth, [])
            l.append(Crs(int(parts[1]), parts[2], parts[3]))
            crs_list[auth] = l

    for _, l in crs_list.items():
        l.sort(key=lambda crs: crs.srid)

    return crs_list


def generate_crs_database_md(
    crs: dict[str, list[Crs]], tpl_env: jinja2.Environment, output_dir: Path
):
    values = {"crs_db": crs}

    tpl = tpl_env.get_template("crs_database.tpl.md")
    output_template(values, tpl, output_dir, "crs_database.md")


def main():
    r = Runfiles.Create()

    if r is None:
        raise Exception("Failed to create Runfiles instance")

    build_date_path = r.Rlocation("horizon/build_date.txt")
    if build_date_path is None:
        raise Exception("Failed to locate build date file")
    build_date = Path(build_date_path).read_text().strip()

    protocol_path = r.Rlocation("horizon/hrz/hrz_protocol.xml")
    if protocol_path is None:
        raise Exception("Failed to locate protocol XML file")
    protocol = protocol_parser.parse(protocol_path)

    db_path = r.Rlocation("horizon/libs/proj_lite/db/crs_database.csv")
    if db_path is None:
        raise Exception("Failed to locate CRS database file")
    crs = read_crs_database(db_path)

    tpl_env = prepare_env("horizon/hrz/doc/templates")
    output_dir = Path(sys.argv[1])

    version = sys.argv[2]

    is_opensource = True

    generate_artifacts_md(version, is_opensource, build_date, tpl_env, output_dir)
    generate_reference(protocol, tpl_env, output_dir)
    generate_style_enums(protocol, tpl_env, output_dir)
    generate_crs_database_md(crs, tpl_env, output_dir)


if __name__ == "__main__":
    main()
