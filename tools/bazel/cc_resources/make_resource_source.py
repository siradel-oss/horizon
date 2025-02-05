import sys

def chunks(l, n):
    for i in range(0, len(l), n):
        yield l[i:i + n]

RES_NAME = sys.argv[1]
RES_PATH = sys.argv[2]
MODE = sys.argv[3]
OUTPUT_PATH = sys.argv[4]

assert MODE == "cpp" or MODE == "c", "Mode must be either \"cpp\" or \"c\""

fp = open(RES_PATH, mode = "rb")
content = bytes(fp.read())

if MODE == "cpp":
    full_str = "#include <cstdint>\n"
    full_str = "#include <cstddef>\n"
    full_str += "#include <gsl/gsl-lite.hpp>\n"
    full_str += "extern const size_t _embedded_resource_%s_len;\n" % RES_NAME
    full_str += "extern const std::byte* _embedded_resource_%s;\n" % RES_NAME
    full_str += "const size_t _embedded_resource_%s_len = %d;\n" % (RES_NAME, len(content))
    full_str += "static const char _embedded_resource_%s_array[] =\n" % RES_NAME
elif MODE == "c":
    full_str = "#include <stddef.h>\n"
    full_str += "extern const size_t _embedded_resource_%s_len;\n" % RES_NAME
    full_str += "extern const char _embedded_resource_%s[];\n" % RES_NAME
    full_str += "const size_t _embedded_resource_%s_len = %d;\n" % (RES_NAME, len(content))
    full_str += "const char _embedded_resource_%s[] =\n" % RES_NAME

full_str += "{\n"

for chunk in chunks(content, 16):
    line_str = ""

    for b in chunk:
        if sys.version[0] == "2":
            b = ord(b)
        line_str += "(char)%d," % b

    line_str += "\n"
    full_str += line_str
full_str += "};\n"

if MODE == "cpp":
    full_str += "const std::byte* _embedded_resource_%s = (const std::byte*)&_embedded_resource_%s_array[0];\n" % (RES_NAME, RES_NAME)

fp = open(OUTPUT_PATH, mode = "w+")
fp.write(full_str)
