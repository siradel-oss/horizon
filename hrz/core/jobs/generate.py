# SPDX-FileCopyrightText: Copyright 2026 Siradel
# SPDX-License-Identifier: MIT

import sys
import json
from dataclasses import dataclass
from pathlib import Path
from python.runfiles import Runfiles

from hrz.generator.common import (
    prepare_env,
    output_template,
)


@dataclass
class JobsManifest:
    name: str
    documentation: str
    params_type: str
    response_type: str
    includes: list[str]


def parse_jobs_manifest(file_path: Path) -> dict:
    jobs_json = json.loads(file_path.read_bytes())
    jobs: list[JobsManifest] = []
    for name, v in jobs_json.items():
        jobs.append(
            JobsManifest(
                name=name,
                documentation=v.get("documentation", ""),
                params_type=v["params_type"],
                response_type=v["response_type"],
                includes=v.get("includes", []),
            )
        )

    includes = set()
    for job in jobs:
        for include in job.includes:
            includes.add(include)
    includes = list(includes)

    return {"jobs": jobs, "all_params_responses_includes": includes}


if __name__ == "__main__":
    r = Runfiles.Create()

    if r is None:
        raise Exception("Failed to create Runfiles instance")

    manifest_path = r.Rlocation("horizon/hrz/core/jobs/manifest.json")
    if manifest_path is None:
        raise Exception("Failed to locate jobs manifest file")

    jobs_manifest = parse_jobs_manifest(Path(manifest_path))
    tpl_env = prepare_env("horizon/hrz/core/jobs/templates")
    output_dir = Path(sys.argv[1])

    tpl_data = jobs_manifest

    tpl = tpl_env.get_template("declarations.tpl.cpp")
    output_template(tpl_data, tpl, output_dir, "jobs_declarations.cpp")
    tpl = tpl_env.get_template("declarations.tpl.h")
    output_template(tpl_data, tpl, output_dir, "jobs_declarations.h")
    tpl = tpl_env.get_template("tickets.tpl.cpp")
    output_template(tpl_data, tpl, output_dir, "jobs_tickets.cpp")
    tpl = tpl_env.get_template("tickets.tpl.h")
    output_template(tpl_data, tpl, output_dir, "jobs_tickets.h")
    tpl = tpl_env.get_template("enum_names.tpl.h")
    output_template(tpl_data, tpl, output_dir, "jobs_enum_names.h")
    tpl = tpl_env.get_template("type.tpl.h")
    output_template(tpl_data, tpl, output_dir, "jobs_type.h")
    tpl = tpl_env.get_template("all_params_responses.tpl.h")
    output_template(tpl_data, tpl, output_dir, "all_params_responses.h")
