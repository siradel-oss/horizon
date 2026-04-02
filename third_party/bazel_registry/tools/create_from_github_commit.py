import argparse

from utils import is_executed_in_registry
from create_from_archive import create_from_archive

if __name__ == "__main__":
    if not is_executed_in_registry():
        raise Exception(
            "This script should be executed from the root of the registry directory."
        )

    parser = argparse.ArgumentParser(
        description="Create a new module from a Github repository and a commit id."
    )
    parser.add_argument("module_name", help="The name of the module to create.")
    parser.add_argument("version", help="The version of the module to create.")
    parser.add_argument(
        "repo", help="The GitHub repository to use as source (e.g. rxi/microui)."
    )
    parser.add_argument("commit", help="The commit id to use as source.")
    parser.add_argument(
        "--additional-prefix",
        help="An additional prefix to strip from the archive files (e.g. src). This is useful if the archive contains a single top-level directory and the files are inside it.",
    )
    args = parser.parse_args()

    module_name = args.module_name
    version = args.version
    repo = args.repo
    commit = args.commit
    prefix = "/" + args.additional_prefix.strip("/") if args.additional_prefix else ""

    url = f"https://github.com/{repo}/archive/{commit}.tar.gz"
    prefix = f"{repo.split('/')[-1]}-{commit}" + prefix

    create_from_archive(module_name, version, url, prefix)
