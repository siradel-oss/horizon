import sys
import sqlite3
import subprocess

DB_PATH = sys.argv[1]
OUTPUT = sys.argv[2]
CHECK_TOOL_PATH = sys.argv[3]

conn = sqlite3.connect(DB_PATH)
resp = conn.execute("SELECT auth, code, name, proj_string FROM crs")

FP_OUTPUT = open(OUTPUT, "w+")

for r in resp:
    auth = r[0]
    srid = r[1]
    name = r[2]
    proj_str = r[3]

    res = subprocess.call([CHECK_TOOL_PATH, proj_str])

    if res == 0:
        FP_OUTPUT.write("%s\t%s\t%s\t%s\n" % (auth, srid, name, proj_str))

FP_OUTPUT.close()
