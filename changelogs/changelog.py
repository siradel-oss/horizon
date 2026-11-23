# SPDX-FileCopyrightText: Copyright 2021 Siradel
# SPDX-License-Identifier: MIT

import argparse
import sys
import textwrap
from collections.abc import Iterator
from dataclasses import dataclass
from datetime import datetime
from enum import Enum
from pathlib import Path

from typing import TextIO

"""
Partial markdown changelogs must conform to the following form:
A list of section given by H1 titles (prefixed by "#").
In each section, an unordered list of items, that may have subitems if
properly indented. For example:

# Added

* Added X to system Y
* **System Z**
    * Added A to B.
    * C can now D.

# Fixed

* Fixed issue E.

To customize this tool to your needs, you can edit the `SECTION_NAMES` array.

This module is both a library (import it to reuse its parsing/merging utilities,
for instance to generate a changelog page) and a script that can be invoked
directly to merge unreleased changelog fragments when cutting a release:

    python3 changelogs/changelog.py merge_into changelogs/a.b.c.md a.b.c changelogs/unreleased/*.md
"""

"""
These are the section lines, in the same order as they will be written in the final changelog.
Feel free to customize this as necessary.
"""
SECTION_NAMES = [
    "Added",
    "Changed",
    "Deprecated",
    "Removed",
    "Fixed",
    "Upgrade notes",
    "Integration notes",
]


class Node:
    """
    Represents a node in a changelog hierarchy and its children.
    This can be a section, in which case the content is its name, or any list item.
    For example here is a markdown document and how it would be represented:

    # Section A

    * **A title**
        * Item 1
        * Item 2
    * Another item

    # Section B

    * A final item

    ---

    - Section A
        - **A title**
            - Item 1
            - Item 2
        - Another item
    - Section B
        - A final item
    """

    def __init__(self, content: str):
        self.content: str = content
        self.children: list[Node] = []

    def __repr__(self) -> str:
        return self.display(0)

    def display(self, indent: int) -> str:
        """
        Generates some Markdown code to display this node and its children.
        """
        content = textwrap.wrap(
            self.content,
            width=80,
            initial_indent=" " * indent + "* ",
            subsequent_indent=" " * (indent + 2),
            break_long_words=False,
            break_on_hyphens=False,
        )
        repr = "\n".join(content) + "\n"
        for child in self.children:
            repr += child.display(indent + 2)
        return repr

    def find_child(self, content: str) -> "Node | None":
        """
        Finds the child with the given content. Or None.
        """
        for child in self.children:
            if child.content == content:
                return child
        return None

    def find_or_create_child(self, content: str) -> "Node":
        """
        Finds the child with the given content, or create it if it does not exist.
        """
        child = self.find_child(content)
        if not child:
            child = Node(content)
            self.children.append(child)
        assert child != None
        return child

    def merge(self, other: "Node"):
        """
        Merges the contents of another node in this one.
        Children and sub-children are merged based on their content.
        """
        assert other.content == self.content
        for other_child in other.children:
            this_child = self.find_or_create_child(other_child.content)
            this_child.merge(other_child)

    def sort_top_level_children(self, sections: list[str]):
        """
        Sorts the children of a node so that they conform to the sections given as input.
        Note that any children that don't match one of the sections will be removed.
        """
        sorted_children: list[Node] = []
        for section_name in sections:
            child = self.find_child(section_name)
            if child:
                sorted_children.append(child)
        self.children = sorted_children


class LogicalLineType(Enum):
    SECTION = "section"
    ITEM = "item"


@dataclass
class LogicalLine:
    type: LogicalLineType
    indent: int
    content: str


def logical_lines(lines: list[str]) -> Iterator[LogicalLine]:
    """
    Iterates over logical lines. Those are lines after we merge multi-lines items.
    """

    pending_item: LogicalLine | None = None

    def flush() -> Iterator[LogicalLine]:
        nonlocal pending_item
        if pending_item is not None:
            yield pending_item
            pending_item = None

    for line in lines:
        line = line.rstrip()
        if len(line) == 0:
            continue

        if "\t" in line:
            raise Exception("Tabs are not allowed in changelog files")

        indent = len(line) - len(line.lstrip())
        stripped = line[indent:]

        if line.startswith("# "):
            yield from flush()
            yield LogicalLine(LogicalLineType.SECTION, indent, line[2:].strip())
        elif stripped.startswith(("- ", "* ")):
            yield from flush()
            pending_item = LogicalLine(
                LogicalLineType.ITEM, indent, stripped[2:].strip()
            )
        elif pending_item is not None and indent == pending_item.indent + 2:
            pending_item.content += " " + stripped
        else:
            raise Exception(f"Invalid line in changelog: {line}")

    yield from flush()


def parse_changelog_partial(lines: list[str]) -> Node:
    """
    Creates a nodes hierarchy from a list of lines read from a changelog file.
    This can be used on partial changelogs that don't contain metadata.
    """

    class NodeStackEntry:
        def __init__(self, node: Node, indent: int):
            self.node: Node = node
            self.indent: int = indent

    root = Node("ROOT")
    stack = []

    for line in logical_lines(lines):
        if line.type == LogicalLineType.SECTION:
            section_name = line.content
            if section_name not in SECTION_NAMES:
                print(f"ERROR: Invalid section name {section_name}")
                sys.exit(1)
            stack = [NodeStackEntry(root.find_or_create_child(section_name), -1)]

        elif line.type == LogicalLineType.ITEM:
            assert len(stack) > 0

            while stack[len(stack) - 1].indent >= line.indent:
                stack = stack[:-1]

            new_node = stack[len(stack) - 1].node.find_or_create_child(line.content)
            stack.append(NodeStackEntry(new_node, line.indent))

    return root


def merge_partial_changelogs(paths: list[Path]) -> Node:
    """
    Reads and merges all the partial changelogs found at the given paths into a
    single root node, with its top-level children sorted according to `SECTION_NAMES`.
    """
    merged = Node("ROOT")
    for file_path in paths:
        with open(file_path, "r", encoding="utf-8") as fp:
            merged.merge(parse_changelog_partial(fp.readlines()))
    merged.sort_top_level_children(SECTION_NAMES)
    return merged


class Changelog:
    """
    A changelog is a root node that contains the sections of the changelog,
    with a date and a version string.
    """

    def __init__(self, version: str, date: str, root: Node):
        self.version: str = version
        self.date: str = date
        self.root: Node = root

    def write_to(self, fp: "TextIO"):
        """
        Writes this changelog to a file given as input in a canonical format that
        can be parsed later on.
        """
        print("---", file=fp)
        print(f"Version: {self.version}", file=fp)
        print(f"Date: {self.date}", file=fp)
        print("---", file=fp)

        for section in self.root.children:
            print(file=fp)
            print(f"# {section.content}", file=fp)
            print(file=fp)
            for section_child in section.children:
                _ = fp.write(section_child.display(0))


def parse_changelog(lines: list[str]) -> Changelog:
    """
    Parses a changelog file (with metadata) into a changelog object.
    This can be used on canonical changelogs (that contain metadata).
    """
    cursor = 0
    while not lines[cursor].startswith("---"):
        cursor += 1
    cursor += 1

    version = None
    date = None

    while not lines[cursor].startswith("---"):
        line = lines[cursor]
        if line.startswith("Version:"):
            version = line[8:].strip()
        elif line.startswith("Date:"):
            date = line[5:].strip()
        cursor += 1
    cursor += 1

    if not version or not date:
        raise Exception("Version or date missing")

    root = parse_changelog_partial(lines[cursor:])
    return Changelog(version, date, root)


def list_input_files(patterns: list[str]) -> list[Path]:
    files: list[Path] = []
    for pattern in patterns:
        files += Path(".").glob(pattern)
    return list(set(files))


def merge_into_cmd(args):
    """
    This command reads all the partial changelogs from a folder and merged them
    together into a canonical changelog.
    The section names are validated.
    The current date is written.
    Of course it can later be edited manually to, for instance, change the date, or
    reorder the items.
    """
    paths = list_input_files(args.input_file)
    for file_path in paths:
        print(f"Parsing changelog {file_path}")
    merged = merge_partial_changelogs(paths)

    version = args.version
    date = datetime.strftime(datetime.now(), "%Y-%m-%d")

    changelog = Changelog(version, date, merged)
    with open(args.output_file, "w+", encoding="utf-8", newline="\n") as fp:
        changelog.write_to(fp)


def main():
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers()

    parser_merge_into = subparsers.add_parser("merge_into")
    parser_merge_into.add_argument("output_file")
    parser_merge_into.add_argument("version")
    parser_merge_into.add_argument("input_file", nargs="+")
    parser_merge_into.set_defaults(func=merge_into_cmd)

    args = parser.parse_args(sys.argv[1:])
    args.func(args)


if __name__ == "__main__":
    main()
