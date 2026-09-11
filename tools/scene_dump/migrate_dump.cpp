// SPDX-FileCopyrightText: Copyright 2022 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/scene_dump/migration.h"
#include "hrz/scene_dump/utils.h"

#include <stdio.h>

int main(int argc, char* argv[])
{
    if (argc < 3)
    {
        printf("Usage: %s <input file> <output file>\n", argv[0]);
        return 1;
    }

    const char* input_filename = argv[1];

    std::vector<std::byte> data = hrz::scene_dump::read_file(input_filename);
    if (data.empty())
    {
        printf("Error when opening file %s\n", input_filename);
        return 1;
    }

    data = hrz::migration::migrate(data);
    if (data.empty())
    {
        printf("Error when migrating file %s\n", input_filename);
        return 1;
    }

    if (!hrz::scene_dump::write_file(argv[2], data))
    {
        printf("Error when writing file %s\n", argv[2]);
        return 1;
    }

    return 0;
}
