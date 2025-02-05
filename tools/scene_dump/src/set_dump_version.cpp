#include "hrz_scene_dump_utils.h"

#include <iomanip>
#include <iostream>
#include <stdlib.h>

int main(int argc, char* argv[])
{
    if (argc < 3)
    {
        printf("Usage: %s <dump file> <hex version>\n", argv[0]);
        return 1;
    }

    std::vector<std::byte> data = hrz::scene_dump::read_file(argv[1]);
    if (data.empty())
    {
        printf("Error when reading file %s\n", argv[1]);
        return 1;
    }

    uint32_t version = (uint32_t)strtoull(argv[2], nullptr, 16);
    data = hrz::scene_dump::set_scene_dump_version(data, version);

    if (!hrz::scene_dump::write_file(argv[1], data))
    {
        printf("Error when writing file %s\n", argv[1]);
        return 1;
    }

    return 0;
}
