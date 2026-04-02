import os
from pathlib import Path
import urllib.request
import json

BCR_ADDRESS = "https://bcr.bazel.build/"
MODULES_DIR = Path("modules")


def is_executed_in_registry() -> bool:
    current_dir = Path(os.getcwd())
    if (
        not (current_dir / "modules").is_dir()
        or not (current_dir / "bazel_registry.json").is_file()
    ):
        return False
    return True


def module_directory(module_name: str, version: str) -> Path:
    return MODULES_DIR / module_name / version


def download_text_file(url: str) -> str:
    with urllib.request.urlopen(url) as response:
        return response.read().decode("utf-8")


def download_json_file(url: str) -> dict:
    with urllib.request.urlopen(url) as response:
        return json.load(response)


def registry_file_url(module_name: str, version: str, file_path: str) -> str:
    return f"{BCR_ADDRESS}modules/{module_name}/{version}/{file_path}"
