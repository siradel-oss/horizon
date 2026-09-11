# SPDX-FileCopyrightText: Copyright 2026 Siradel
# SPDX-License-Identifier: MIT

import itertools
import sys
from pathlib import Path
from typing import List

from python.runfiles import Runfiles

from changelogs.changelog import (
    SECTION_NAMES,
    Changelog,
    Node,
    merge_partial_changelogs,
    parse_changelog,
)

"""
Turns the released changelogs (`changelogs/*.md`) and the unreleased changelog
fragments (`changelogs/unreleased/*.md`) into the single Markdown page shown in
the documentation, merging all unreleased fragments into an "Unreleased"
section displayed above every released version.
"""


class Generator:
    """
    The generator is used to transform a set of changelogs into the final doc page.
    You can customize how the versions and sections are written.
    """

    def __init__(self, fp):
        self.fp = fp

    def write_header(self):
        """
        Called once at the very beginning.
        """
        print("+++", file=self.fp)
        print('title = "Changelog"', file=self.fp)
        print("toc_end_level = 2", file=self.fp)
        print("allow_broken_links = true", file=self.fp)
        print("+++", file=self.fp)
        print("", file=self.fp)
        print("# Changelog", file=self.fp)
        print("", file=self.fp)
        print(
            "The upgrade and integration notes sections aim to help API users port their application to the new version of Horizon by listing the changes needed to, respectively, maintain compatibility with the previous version, and integrate new or upgraded features optimally and efficiently. Please refer to [RFC 2119](https://datatracker.ietf.org/doc/html/rfc2119) for the meaning of the terms *must*, *should*, and *may*.",
            file=self.fp,
        )

    def write_footer(self):
        """
        Called once at the very end.
        """
        pass

    def write_unreleased(self):
        """
        Called once, before the first version, if there are unreleased changes.
        """
        print("", file=self.fp)
        print("## Unreleased", file=self.fp)

    def write_version(self, version: str, date: str):
        """
        Called at the beginning of every version.
        """
        print("", file=self.fp)
        print("## Version %s - %s" % (version, date), file=self.fp)

    def write_section(self, section: str, content: List[Node]):
        """
        Called for every section.
        """
        print("", file=self.fp)
        print("### %s" % section, file=self.fp)
        print("", file=self.fp)
        for child in content:
            self.fp.write(child.display(0))


def write_sections(generator: Generator, root: Node):
    """
    Writes every non-empty section of the given root node, in the canonical
    section order, using the given generator.
    """
    for section_name in SECTION_NAMES:
        node = root.find_child(section_name)
        if node and len(node.children) > 0:
            generator.write_section(section_name, node.children)


def parse_released_changelogs(paths) -> List[Changelog]:
    """
    Parses every given released changelog file, and sorts them by version,
    from highest to lowest. The version is supposed to be numbers separated by dots.
    """
    changelogs = []
    for file_path in paths:
        with open(file_path, "r", encoding="utf-8") as fp:
            changelogs.append(parse_changelog(fp.readlines()))

    def parse_version_order(changelog: Changelog):
        def accumulate_func(a, b):
            return a * 1000 + b

        version_parts = [int(p) for p in changelog.version.split(".")]
        order_iter = itertools.accumulate(version_parts, func=accumulate_func)
        *_, order = order_iter
        return order

    changelogs.sort(key=parse_version_order, reverse=True)
    return changelogs


def generate_changelog_md(changelogs_dir: Path, output_path: Path):
    released = parse_released_changelogs(sorted(changelogs_dir.glob("*.md")))
    unreleased = merge_partial_changelogs(
        sorted((changelogs_dir / "unreleased").glob("*.md"))
    )

    with open(output_path, "w+", encoding="utf-8") as fp:
        generator = Generator(fp)
        generator.write_header()

        if len(unreleased.children) > 0:
            generator.write_unreleased()
            write_sections(generator, unreleased)

        for changelog in released:
            generator.write_version(changelog.version, changelog.date)
            write_sections(generator, changelog.root)

        generator.write_footer()


def main():
    r = Runfiles.Create()
    if r is None:
        raise Exception("Failed to create Runfiles instance")

    # This file always exists (regardless of how many unreleased fragments are
    # pending), so it acts as a stable anchor to locate the `changelogs/`
    # directory, whose released and unreleased contents are declared as `data`
    # dependencies of this binary.
    anchor = r.Rlocation("horizon/changelogs/unreleased/CHANGELOG_TEMPLATE.md.tpl")
    if anchor is None:
        raise Exception("Failed to locate the changelogs directory")
    changelogs_dir = Path(anchor).parent.parent

    output_dir = Path(sys.argv[1])
    output_dir.mkdir(parents=True, exist_ok=True)

    generate_changelog_md(changelogs_dir, output_dir / "changelog.md")


if __name__ == "__main__":
    main()
