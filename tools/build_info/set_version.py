import sys
import re

# Set the version for the Horizon project.

# Line ending handling when writing files is different
# between Python 2 and 3.
if sys.version_info[0] != 3:
    print("Error: Python 3 is required")
    sys.exit(1)

if len(sys.argv) != 2:
    print("Usage: %s <new_version>" % sys.argv[0])
    sys.exit(1)

VERSION = sys.argv[1]

fp = open("version.bzl", "w+", newline="\n")
fp.write('HRZ_VERSION = "%s"\n' % VERSION)
fp.close()

fp = open(".gitlab-ci.yml", "r")
content = fp.read().splitlines()
fp.close()

new_content = []
found_version = False

for l in content:
    if re.match('^[ \\t]*HRZ_VERSION:[ \\t]*"[0-9a-zA-Z-_.]+"\\s*$', l):
        tokens = l.split('"')
        tokens[1] = VERSION
        new_line = '"'.join(tokens)
        new_content.append(new_line)
        found_version = True
    else:
        new_content.append(l)

if not found_version:
    print("Couldn't find version line in .gitlab-cy.yml")
    sys.exit(1)

fp = open(".gitlab-ci.yml", "w+", newline="\n")
fp.write("\n".join(new_content) + "\n")
fp.close()
