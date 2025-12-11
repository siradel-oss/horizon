import sys


def chunks(l, n):
    for i in range(0, len(l), n):
        yield l[i : i + n]


content = None
with open(sys.argv[4], "rb") as fp:
    content = fp.read()

if not content:
    print("Couldn't read shader content")
    sys.exit(1)

out = open(sys.argv[5], "w+", encoding="utf-8")
out.write("#include <stddef.h>\n")
out.write("namespace %s {\n" % sys.argv[1])
out.write(
    "static constexpr const unsigned char %s_%s_compressed_str[] = {\n"
    % (sys.argv[2], sys.argv[3])
)

for chunk in chunks(content, 16):
    chunk = [hex(i) for i in chunk]
    out.write("    %s,\n" % ", ".join(chunk))

out.write("0 };\n")

out.write(
    "extern const size_t %s_%s_compressed_len = sizeof(%s_%s_compressed_str) - 1;\n"
    % (sys.argv[2], sys.argv[3], sys.argv[2], sys.argv[3])
)
out.write(
    "const unsigned char* %s_%s_compressed = %s_%s_compressed_str;\n"
    % (sys.argv[2], sys.argv[3], sys.argv[2], sys.argv[3])
)

out.write("size_t %s_%s_len = 0;\n" % (sys.argv[2], sys.argv[3]))
out.write("const char* %s_%s = nullptr;\n" % (sys.argv[2], sys.argv[3]))

out.write("}\n")
