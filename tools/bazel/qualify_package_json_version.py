import json
import re
import sys


def fix_version(version):
    version_pattern = re.compile(r"HRZ_VERSION\((.+)\)")
    match = version_pattern.match(version)

    if match:
        version = match.group(1)
        if "SNAPSHOT" in version:
            return version + "." + qualifier_string
        else:
            return version
    else:
        return version


if len(sys.argv) != 7:
    print(
        "Usage: %s <in package.json> <VERSION> <LICENSE> <qualifier file> <build type> <out package.json>"
        % sys.argv[0]
    )
    sys.exit(1)

with open(sys.argv[1], "r") as in_file, open(sys.argv[4], "r") as qualifier_file:
    package_json = json.load(in_file)
    qualifier_string = qualifier_file.read()

build_type = sys.argv[5]
if build_type != "opt":
    qualifier_string += "-" + build_type

version = fix_version(sys.argv[2])
package_json["version"] = version

if len(sys.argv[3]) > 0:
    package_json["license"] = sys.argv[3]

dependencies = package_json["dependencies"]
for package_name in dependencies:
    if dependencies[package_name].startswith("workspace:"):
        dependencies[package_name] = version

with open(sys.argv[6], "w") as out_file:
    json.dump(package_json, out_file)
