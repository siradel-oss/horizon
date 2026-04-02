import sys
from pathlib import Path
import os
import re

input_path = Path(sys.argv[1])
output_path = Path(sys.argv[2])
version = sys.argv[3]
profile = sys.argv[4]
search_paths = [Path(""), Path(input_path.parent)]
do_not_visit_again = {}
file_ids = {}

for arg in sys.argv:
    if arg.startswith("-I"):
        search_paths.append(Path(arg[2:]))

find_include_re = re.compile(r"^\s*#\s*include\s*[\"<]([a-zA-Z0-9_\.\/]+)[\">]")
find_pragma_once_re = re.compile(r"^\s*#\s*pragma\s*once")


def error(s):
    print(s)
    sys.exit(1)


def get_file_id(f):
    global file_ids
    if not f in file_ids:
        file_ids[f] = len(file_ids)
        return len(file_ids) - 1
    else:
        return file_ids[f]


def process_include(l):
    m = find_include_re.search(l)
    if m == None:
        raise RuntimeError("Couldn't parse include for line " + l)
    return process_file(Path(m.group(1)))


def process_pragma(l, file):
    if find_pragma_once_re.match(l):
        do_not_visit_again[file] = True
        return ""
    else:
        return l


def is_pound(l, keyword):
    l = l.strip()
    return l.startswith("#") and l[1:].strip().startswith(keyword)


def process_file(file):
    if file in do_not_visit_again:
        return ""
    for search_path in search_paths:
        if not os.path.exists(search_path / file):
            continue
        with open(search_path / file, "rb") as fp:
            input_lines = fp.read().split(b"\n")
            output_lines = []
            line = 1
            output_lines += [f"#line {line} {get_file_id(file)}"]
            for l in input_lines:
                l = l.decode("utf-8")
                if is_pound(l, "include"):
                    output_lines.append(process_include(l))
                    output_lines += [f"#line {line + 1} {get_file_id(file)}"]
                elif is_pound(l, "pragma"):
                    output_lines.append(process_pragma(l, file))
                else:
                    output_lines.append(l)
                line += 1
            return "\n".join([l.rstrip() for l in output_lines])
    raise RuntimeError("Couldn't find file " + str(file))


content = process_file(input_path)
output = "#version " + version + " " + profile + "\n"

for f in file_ids:
    s = str(f).replace("\\", "/")
    output += f"// {file_ids[f]}: {s}\n"

output += "precision highp float;\n"
output += "precision highp int;\n"

for arg in sys.argv:
    if arg.startswith("-D"):
        define = arg[2:]
        output += "#define " + " ".join(define.split("=")) + "\n"
        search_paths.append(Path(arg[2:]))

output += content
output += "\n"

with open(output_path, "wb+") as fp:
    fp.write(output.encode("utf-8"))
