from dataclasses import asdict, is_dataclass
import typing
import jinja2

from pathlib import Path
from python.runfiles import Runfiles
from hrz.generator.filters import FILTERS


def prepare_env(templates_path: str) -> jinja2.Environment:
    r = Runfiles.Create()
    if r is None:
        raise Exception("Failed to create Runfiles instance")

    def rlocation_load_function(template_name: str) -> str | None:
        template_path = templates_path.rstrip("/") + "/" + template_name
        template_path = r.Rlocation(template_path)
        if template_path is None or not Path(template_path).exists():
            return None
        return Path(template_path).read_text(encoding="utf-8")

    loader = jinja2.FunctionLoader(rlocation_load_function)
    tpl_env = jinja2.Environment(loader=loader)
    tpl_env.filters.update(FILTERS)
    tpl_env.trim_blocks = True
    tpl_env.lstrip_blocks = True
    return tpl_env


def remove_prefix(value: str, prefix: str) -> str:
    if value.startswith(prefix):
        return value[len(prefix) :]
    return value


def output_template(
    value: dict[str, typing.Any],
    tpl: jinja2.Template,
    output_path: str | Path,
    name: str | Path,
):
    output_file_path = Path(output_path) / name
    output_dir = output_file_path.parent

    if not output_dir.exists():
        output_dir.mkdir(parents=True)

    content = tpl.render(asdict(value) if is_dataclass(value) else value)  # type: ignore
    output_file_path.write_bytes(content.encode("utf-8"))
