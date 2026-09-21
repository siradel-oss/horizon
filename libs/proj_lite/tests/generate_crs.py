# SPDX-FileCopyrightText: Copyright 2020 Siradel
# SPDX-License-Identifier: MIT

import os
import sys
import sqlite3

DB_PATH = sys.argv[1]
OUTPUT_DIR = sys.argv[2]

conn = sqlite3.connect(DB_PATH)
resp = conn.execute("SELECT auth, code, name, proj_string FROM crs")

file_index = 0
file_name = os.path.join(OUTPUT_DIR, str(file_index).zfill(2) + ".cpp")

FP_OUTPUT = open(file_name, "w")
FP_OUTPUT.write('#include "../crs_common.h"\n')

for i, r in enumerate(resp):
    auth = r[0]
    srid = r[1]
    name = r[2]
    proj_str = r[3]

    if auth != "EPSG":
        continue

    str_length = len(proj_str)

    FP_OUTPUT.write("""TEST_F(CrsDatabaseTest, pl_get_crs_string_%s_%s)
{
    // %s

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "%s", %s, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, %s);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(crs_db, "%s", %s, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, %s);
    ASSERT_EQ(crs_string, "%s");
}
""" % (auth, srid, name, auth, srid, str_length, auth, srid, str_length, proj_str))

    if i % 100 == 0:
        FP_OUTPUT.close()
        file_index += 1
        file_name = os.path.join(OUTPUT_DIR, str(file_index).zfill(2) + ".cpp")
        FP_OUTPUT = open(file_name, "w")
        FP_OUTPUT.write('#include "../crs_common.h"\n')

FP_OUTPUT.close()
