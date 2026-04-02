import argparse

from create_from_github_bazel import create_from_github_bazel
from utils import is_executed_in_registry


def create_from_github_commit_bazel(
    module_name: str, version: str, repo: str, commit: str, additional_prefix: str
):
    archive_url = f"https://github.com/{repo}/archive/{commit}.tar.gz"
    module_dot_bazel_url = f"https://raw.githubusercontent.com/{repo}/{commit}/{additional_prefix}MODULE.bazel"
    prefix = f"{repo.split('/')[-1]}-{commit}/{additional_prefix}"
    create_from_github_bazel(
        module_name, version, archive_url, module_dot_bazel_url, prefix
    )


if __name__ == "__main__":
    if not is_executed_in_registry():
        raise Exception(
            "This script should be executed from the root of the registry directory."
        )

    parser = argparse.ArgumentParser(
        description="Create a new module from an HTTP archive."
    )
    parser.add_argument("module_name", help="The name of the module to create.")
    parser.add_argument("version", help="The version of the module to create.")
    parser.add_argument(
        "repo", help="The GitHub repository to use as source (e.g. rxi/microui)."
    )
    parser.add_argument("commit", help="The commit to use as source (e.g. a1b2c3d...).")
    parser.add_argument(
        "--additional-prefix",
        help="An additional prefix to strip from the archive files (e.g. src). This is useful if the archive contains a single top-level directory and the files are inside it.",
    )
    args = parser.parse_args()

    module_name = args.module_name
    version = args.version
    repo = args.repo
    commit = args.commit
    additional_prefix = (
        args.additional_prefix if isinstance(args.additional_prefix, str) else ""
    )

    additional_prefix = additional_prefix.strip("/") + "/"

    create_from_github_commit_bazel(
        module_name, version, repo, commit, additional_prefix
    )
