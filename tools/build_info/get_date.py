# SPDX-FileCopyrightText: Copyright 2020 Siradel
# SPDX-License-Identifier: MIT

from datetime import datetime, timezone
import sys

# Write the date to the file passed whose path is
# passed as first parameter to this script.

if len(sys.argv) != 2:
    print("Usage: %s <date file>" % sys.argv[0])
    sys.exit(1)

date_string = datetime.now(timezone.utc).strftime("%Y%m%d%H%M%S")

with open(sys.argv[1], "w") as output_file:
    output_file.write(date_string)
