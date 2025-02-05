import subprocess
from subprocess import PIPE
import re
import os
import sys

# First argument is the absolute path to the "test_suites" folder
# Second argument is the absolute path to the cpp file to generate

path = sys.argv[1]
OUTPUT = sys.argv[2]

def generate_case(suite_name, case_id, in_proj, out_proj, in_x, in_y, in_z, eps):
    print(in_proj)
    print(out_proj)
    print("")
    is_3d = (in_z != None)
    in_x = float(in_x)
    in_y = float(in_y)
    in_z = 0 if not is_3d else float(in_z)
    eps_f = float(eps)
    p = subprocess.Popen("cs2cs -f \"%.16f\" " + in_proj + " +to " + out_proj, stdin=PIPE, stdout=PIPE)

    offsets = [
        [0, 0],
        [-100 * eps_f, -100 * eps_f],
        [100 * eps_f, 100 * eps_f]
    ]
    x = [0, 0, 0]
    y = [0, 0, 0]
    z = [0, 0, 0]

    for i in range(0, 3):
        p.stdin.write(bytes("%f %f %f\n" % (in_x + offsets[i][0], in_y + offsets[i][1], in_z), "utf-8"))

    lines = p.communicate()[0].decode().split("\n")
    for i in range(0, 3):
        output = lines[i]
        split = re.split("\s+", output)
        x[i] = split[0]
        y[i] = split[1]
        z[i] = split[2]

    if not is_3d:
        FP_OUTPUT.write("""TEST(%s, generated_line_%d)
{
    double src_x[3] = {%f, %f, %f};
    double src_y[3] = {%f, %f, %f};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {%s, %s, %s};
    double dst_y[3] = {%s, %s, %s};
    run_single_case(
        "%s",
        "%s",
        3,
        src_x, src_y, src_z,
        dst_x, dst_y,
        %s);
}
""" % (suite_name, case_id,
            in_x + offsets[0][0], in_x + offsets[1][0], in_x + offsets[2][0],
            in_y + offsets[0][1], in_y + offsets[1][1], in_y + offsets[2][1],
            x[0], x[1], x[2],
            y[0], y[1], y[2],
            in_proj, out_proj,
            eps
        ))
    else:
        FP_OUTPUT.write("""TEST(%s, generated_line_%d)
{
    double src_x[3] = {%f, %f, %f};
    double src_y[3] = {%f, %f, %f};
    double src_z[3] = {%f, %f, %f};
    double dst_x[3] = {%s, %s, %s};
    double dst_y[3] = {%s, %s, %s};
    double dst_z[3] = {%s, %s, %s};
    run_single_case_3d(
        "%s",
        "%s",
        3,
        src_x, src_y, src_z,
        dst_x, dst_y, dst_z,
        %s);
}
""" % (suite_name, case_id,
            in_x + offsets[0][0], in_x + offsets[1][0], in_x + offsets[2][0],
            in_y + offsets[0][1], in_y + offsets[1][1], in_y + offsets[2][1],
            in_z, in_z, in_z,
            x[0], x[1], x[2],
            y[0], y[1], y[2],
            z[0], z[1], z[2],
            in_proj, out_proj,
            eps
        ))

def generate_suite(suite_path):
    suite_name = os.path.basename(suite_path)[:-4]
    print(suite_name)

    input_x = ""
    input_y = ""
    input_z = None
    epsilon = ""
    input_proj = ""

    fp = open(suite_path, "r")
    for line_n, line in enumerate(fp.readlines()):
        line = line.strip()
        if len(line) < 1:
            continue

        if line[0] == "=":
            input_proj = line[1:].strip()
        elif line[0] == "<":
            tokens = re.split("\s+", line[1:].strip())
            if len(tokens) == 3:
                input_x = tokens[0]
                input_y = tokens[1]
                input_z = None
                epsilon = tokens[2]
            elif len(tokens) == 4:
                input_x = tokens[0]
                input_y = tokens[1]
                input_z = tokens[2]
                epsilon = tokens[3]
            else:
                print("Unhandled number of tokens on line %d" % (line_n + 1))
        elif line[0] == ">":
            output_proj = line[1:].strip()
            generate_case(suite_name, line_n + 1, input_proj, output_proj, input_x, input_y, input_z, epsilon)

    fp.close()

files = []
for r, d, f in os.walk(path):
    for file in f:
        if file[-4:] == ".txt":
            files.append(os.path.join(r, file))

FP_OUTPUT = open(OUTPUT, "w+")
FP_OUTPUT.write("#include \"common.h\"\n")

for f in files:
    generate_suite(f)

FP_OUTPUT.close()
