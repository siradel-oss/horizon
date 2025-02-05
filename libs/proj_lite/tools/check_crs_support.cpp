#include <proj_lite.h>

#include <iostream>

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cout << "Usage: " << argv[0] << " <PROJ.4 string>" << std::endl;
        return 2;
    }

    pl_Crs crs;
    pl_Result res = pl_crs_from_proj_zstr(argv[1], &crs);

    return res == pl_Result_Ok ? 0 : 1;
}
