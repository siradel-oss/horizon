import sys

OUTPUT_PATH = sys.argv[1]
licenses = dict([arg.split('=', maxsplit=1) for arg in sys.argv[2:]])

license_split_str = "#############################################################################\n\n"

full_str = ""
i = 0
for license_name, license_path in licenses.items():
    if i != 0:
        full_str += license_split_str
    full_str += license_name
    full_str += ":\n\n"
    fp = open(license_path, "r", encoding="utf-8")
    for line in fp.readlines():
        full_str += line
    full_str += "\n\n"
    i += 1

fp = open(OUTPUT_PATH, mode = "w+", encoding="utf-8")
fp.write(full_str)
