import sys
import sqlite3
import subprocess

DB_PATH = sys.argv[1]
OUTPUT = sys.argv[2]

conn = sqlite3.connect(DB_PATH)
resp = conn.execute("SELECT auth, code, name, proj_string FROM crs")

with open(OUTPUT, "w+") as fp:
    for r in resp:
        auth = r[0]
        srid = r[1]
        name = r[2]
        proj_str = r[3]

        fp.write("%s\t%s\t%s\t%s\n" % (auth, srid, name, proj_str))
