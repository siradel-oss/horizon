from mkdocs.__main__ import cli

cli(["serve", "--config-file", "doc/mkdocs.yml", "-a", "127.0.0.1:8082"], standalone_mode=False)
