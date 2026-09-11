// SPDX-FileCopyrightText: Copyright 2022 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/scene_dump/utils.h"

#include <iomanip>
#include <iostream>

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        printf("Usage: %s <dump file>\n", argv[0]);
        return 1;
    }

    std::vector<std::byte> data = hrz::scene_dump::read_file(argv[1]);
    if (data.empty())
    {
        printf("Error when reading file %s\n", argv[1]);
        return 1;
    }

    uint32_t version = hrz::scene_dump::get_scene_dump_version(data);
    std::cout << std::setfill('0') << std::setw(8) << std::right << std::hex << version
              << std::endl;

    return 0;
}
