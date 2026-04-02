import os
from pathlib import Path
import sys
import argparse
import itertools
from datetime import datetime
from typing import List

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

To customize this tool to your needs, you can edit the `SECTION_NAMES` array, and
the `Generator` class.
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
        self.content = content
        self.children = []

    def __repr__(self) -> str:
        return self.display(0)

    def display(self, indent: int) -> str:
        """
        Generates some Markdown code to display this node and its children.
        """
        repr = (" " * indent) + "* " + self.content + "\n"
        for child in self.children:
            repr += child.display(indent + 4)
        return repr

    def find_child(self, content: str) -> "Node":
        """
        Finds the child with the given content. Or None.
        """
        for child in self.children:
            if child.content == content:
                return child
        return None

    def find_or_create_child(self, content) -> "Node":
        """
        Finds the child with the given content, or create it if it does not exist.
        """
        child = self.find_child(content)
        if not child:
            child = Node(content)
            self.children.append(child)
        assert child != None
        return child

    def merge(self, other):
        """
        Merges the contents of another node in this one.
        Children and sub-children are merged based on their content.
        """
        assert other.content == self.content
        for other_child in other.children:
            this_child = self.find_or_create_child(other_child.content)
            this_child.merge(other_child)

    def sort_top_level_children(self, sections):
        """
        Sorts the children of a node so that they conform to the sections given as input.
        Note that any children that don't match one of the sections will be removed.
        """
        sorted_children = []
        for section_name in sections:
            child = self.find_child(section_name)
            if child:
                sorted_children.append(child)
        self.children = sorted_children


def parse_changelog_partial(lines: List[str]) -> Node:
    """
    Creates a nodes hierarchy from a list of lines read from a changelog file.
    This can be used on partial changelogs that don't contain metadata.
    """

    class NodeStackEntry:
        def __init__(self, node: Node, indent: int):
            self.node = node
            self.indent = indent

    root = Node("ROOT")
    stack = []

    for line in lines:
        line = line.rstrip()
        if len(line) == 0:
            continue

        indentation = len(line) - len(line.lstrip())

        if line.startswith("# "):
            section_name = line[2:].strip()
            if section_name not in SECTION_NAMES:
                print("ERROR: Invalid section name %s" % section_name)
                sys.exit(1)
            stack = [NodeStackEntry(root.find_or_create_child(section_name), -1)]

        elif line[indentation:].startswith(("- ", "* ")):
            content = line[indentation + 2 :].strip()

            assert len(stack) > 0

            while stack[len(stack) - 1].indent >= indentation:
                stack = stack[:-1]

            new_node = stack[len(stack) - 1].node.find_or_create_child(content)
            stack.append(NodeStackEntry(new_node, indentation))

    return root


class Changelog:
    """
    A changelog is a root node that contains the sections of the changelog,
    with a date and a version string.
    """

    def __init__(self, version: str, date: str, root: Node):
        self.version = version
        self.date = date
        self.root = root

    def write_to(self, fp):
        """
        Writes this changelog to a file given as input in a canonical format that
        can be parsed later on.
        """
        print("---", file=fp)
        print("Version: %s" % self.version, file=fp)
        print("Date: %s" % self.date, file=fp)
        print("---", file=fp)

        for section in self.root.children:
            print("", file=fp)
            print("# %s" % section.content, file=fp)
            print("", file=fp)
            for section_child in section.children:
                fp.write(section_child.display(0))


def parse_changelog(lines: List[str]) -> Changelog:
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


def list_input_files(patterns):
    files = []
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
    merged = Node("ROOT")

    for file_path in paths:
        with open(file_path, "r", encoding="utf-8") as fp:
            print("Parsing changelog %s" % file_path)
            partial = parse_changelog_partial(fp.readlines())
            merged.merge(partial)

    merged.sort_top_level_children(SECTION_NAMES)

    version = args.version
    date = datetime.strftime(datetime.now(), "%Y-%m-%d")

    changelog = Changelog(version, date, merged)
    with args.output_file as fp:
        changelog.write_to(fp)


class Generator:
    """
    The generator is used to transform a set of changelogs into a final file.
    You can customize how the versions and sections are written.
    """

    def __init__(self, fp):
        self.fp = fp

    def write_header(self):
        """
        Called once at the very beginning
        """
        print("---", file=self.fp)
        print("Title: Changelog", file=self.fp)
        print("TocDepth: 1-2", file=self.fp)
        print("Category: General", file=self.fp)
        print("---", file=self.fp)
        print("", file=self.fp)
        print(
            "The upgrade and integration notes sections aim to help API users port their application to the new version of Horizon by listing the changes needed to, respectively, maintain compatibility with the previous version, and integrate new or upgraded features optimally and efficiently. Please refer to [RFC 2119](https://datatracker.ietf.org/doc/html/rfc2119) for the meaning of the terms *must*, *should*, and *may*.",
            file=self.fp,
        )

    def write_footer(self):
        """
        Called once at the very end
        """
        pass

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


def generate_cmd(args):
    """
    This command generates a file containing all changelogs given as arguments, that are
    ordered based on their version name, from highest to lowest.
    The version is supposed to be numbers separated by dots.
    The output format can be customized by editing the `Generator` class above.
    """
    changelogs = []
    for file in list_input_files(args.input_file):
        with open(file, "r", encoding="utf-8") as fp:
            changelogs.append(parse_changelog(fp.readlines()))

    def parse_version_order(changelog):
        def accumulate_func(a, b):
            return a * 1000 + b

        version_parts = [int(p) for p in changelog.version.split(".")]
        order_iter = itertools.accumulate(version_parts, func=accumulate_func)
        *_, order = order_iter
        return order

    changelogs.sort(key=parse_version_order, reverse=True)

    with args.output_file as fp:
        generator = Generator(fp)
        generator.write_header()

        for changelog in changelogs:
            generator.write_version(changelog.version, changelog.date)

            for section_name in SECTION_NAMES:
                node = changelog.root.find_child(section_name)
                if node and len(node.children) > 0:
                    generator.write_section(section_name, node.children)

        generator.write_footer()


parser = argparse.ArgumentParser()
subparsers = parser.add_subparsers()

parser_merge_into = subparsers.add_parser("merge_into")
parser_merge_into.add_argument(
    "output_file", type=argparse.FileType("w+", encoding="utf-8")
)
parser_merge_into.add_argument("version")
parser_merge_into.add_argument("input_file", nargs="+")
parser_merge_into.set_defaults(func=merge_into_cmd)

parser_generate = subparsers.add_parser("generate")
parser_generate.add_argument(
    "output_file", type=argparse.FileType("w+", encoding="utf-8")
)
parser_generate.add_argument("input_file", nargs="+")
parser_generate.set_defaults(func=generate_cmd)

args = parser.parse_args(sys.argv[1:])
args.func(args)
