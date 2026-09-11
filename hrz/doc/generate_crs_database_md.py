# SPDX-FileCopyrightText: Copyright 2026 Siradel
# SPDX-License-Identifier: MIT

from dataclasses import dataclass
import sys
from pathlib import Path

import jinja2
from python.runfiles import Runfiles

from hrz.generator.common import (
    prepare_env,
    output_template,
)


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

    db_path = r.Rlocation("horizon/libs/proj_lite/db/crs_database.csv")
    if db_path is None:
        raise Exception("Failed to locate CRS database file")
    crs = read_crs_database(db_path)

    tpl_env = prepare_env("horizon/hrz/doc/templates")
    output_dir = Path(sys.argv[1])

    generate_crs_database_md(crs, tpl_env, output_dir)


if __name__ == "__main__":
    main()
