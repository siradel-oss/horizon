#include <proj_lite.h>

#include <fstream>
#include <iostream>
#include <string>

namespace
{

bool is_supported(const char* proj_str)
{
    pl_Crs crs;
    const pl_Result res = pl_crs_from_proj_zstr(proj_str, &crs);
    return res == pl_Result_Ok;
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc != 3)
    {
        std::cerr << "Usage: check_crs_support <input_csv> <output_csv>\n";
        return 1;
    }

    const char* input_csv = argv[1];
    const char* output_csv = argv[2];

    std::ifstream input(input_csv);
    std::ofstream output(output_csv);

    std::string line;
    while (std::getline(input, line))
    {
        const size_t pos1 = line.find('\t');
        const size_t pos2 = line.find('\t', pos1 + 1);
        const size_t pos3 = line.find('\t', pos2 + 1);

        const std::string auth = line.substr(0, pos1);
        const std::string srid = line.substr(pos1 + 1, pos2 - pos1 - 1);
        const std::string name = line.substr(pos2 + 1, pos3 - pos2 - 1);
        const std::string proj_str = line.substr(pos3 + 1);

        if (is_supported(proj_str.c_str()))
        {
            output << auth << "\t" << srid << "\t" << name << "\t" << proj_str << "\n";
        }
    }

    return 0;
}
