import json
import re
import difflib

from utils import module_directory, download_text_file
from integrity import compute_integrity_url_streaming, compute_integrity_file

MODULE_DIRECTIVE_REGEX = (
    r'(?<!\w)module\s*\(\s*name\s*=\s*"([\w.-]+)"\s*,\s*version\s*=\s*"([\w.-]+)"'
)


def create_from_github_bazel(
    module_name: str,
    version: str,
    archive_url: str,
    module_dot_bazel_url: str,
    prefix: str,
):
    module_dot_bazel_content = download_text_file(module_dot_bazel_url)

    match = re.search(MODULE_DIRECTIVE_REGEX, module_dot_bazel_content)
    if not match:
        raise Exception(
            f"Could not parse module name and version from {module_dot_bazel_url}"
        )

    module_name_from_bazel = match.group(1)
    version_from_bazel = match.group(2)
    if module_name_from_bazel != module_name:
        raise Exception(
            f"Module name in MODULE.bazel ({module_name_from_bazel}) does not match the provided module name ({module_name})."
        )

    module_dir = module_directory(module_name, version)
    if module_dir.is_dir():
        raise Exception(
            f"Module {module_name} version {version} already exists in the local registry."
        )

    module_dir.mkdir(parents=True, exist_ok=False)

    integrity = compute_integrity_url_streaming(archive_url)
    source_json: dict = {
        "url": archive_url,
        "strip_prefix": prefix,
        "integrity": integrity,
    }

    if version_from_bazel != version:
        new_module_dot_bazel_content = re.sub(
            MODULE_DIRECTIVE_REGEX,
            f'module(name = "{module_name}", version = "{version}"',
            module_dot_bazel_content,
        )
        diff = difflib.unified_diff(
            module_dot_bazel_content.splitlines(keepends=True),
            new_module_dot_bazel_content.splitlines(keepends=True),
            fromfile="MODULE.bazel",
            tofile="MODULE.bazel",
        )
        module_dot_bazel_version_patch_content = "".join(diff)
        module_dot_bazel_content = new_module_dot_bazel_content

        module_dot_bazel_version_patch_path = (
            module_dir / "patches" / "module_dot_bazel_version.patch"
        )
        module_dot_bazel_version_patch_path.parent.mkdir(parents=True, exist_ok=True)
        module_dot_bazel_version_patch_path.write_bytes(
            module_dot_bazel_version_patch_content.encode("utf-8")
        )

        source_json["patches"] = {
            "module_dot_bazel_version.patch": compute_integrity_file(
                module_dot_bazel_version_patch_path
            ),
        }

    (module_dir / "MODULE.bazel").write_bytes(module_dot_bazel_content.encode("utf-8"))
    (module_dir / "source.json").write_bytes(json.dumps(source_json, indent=2).encode())
