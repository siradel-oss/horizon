from mkdocs.__main__ import cli
import sys

cli(sys.argv[1:], standalone_mode=False)
