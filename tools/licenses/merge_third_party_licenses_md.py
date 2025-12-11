import sys

OUTPUT_PATH = sys.argv[1]
licenses = dict([arg.split("=", maxsplit=1) for arg in sys.argv[2:]])

full_str = """---
Title: Third-party licenses
Category: Resources
---
"""

for license_name, license_path in licenses.items():
    full_str += "\n## " + license_name + "\n\n"
    fp = open(license_path, "r", encoding="utf-8")
    full_str += "```\n"
    for line in fp.readlines():
        full_str += line
    full_str += "```\n"

fp = open(OUTPUT_PATH, mode="w+", encoding="utf-8")
fp.write(full_str)
