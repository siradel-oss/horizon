#include <hrz_common_proj.h>
#include <hrz_core.h>

#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    hrz_proj::init_global_common_transforms();
    return RUN_ALL_TESTS();
}
