#include "crs_common.h"

TEST_F(CrsDatabaseTest, pl_get_crs_proj_str_length_unknown_crs)
{
    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 9999999, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_CrsNotFound);

    res = pl_get_crs_proj_str_length(crs_db, "_invalid_", 4326, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_CrsNotFound);
}

TEST_F(CrsDatabaseTest, pl_get_crs_proj_str_unknown_crs)
{
    std::vector<char> crs_chars;
    crs_chars.resize(100);
    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str(
        crs_db, "EPSG", 9999999, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_CrsNotFound);

    res = pl_get_crs_proj_str(
        crs_db, "_invalid_", 4326, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_CrsNotFound);
}

TEST_F(CrsDatabaseTest, pl_get_crs_proj_str_too_small_buffer)
{
    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3164, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 97);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length - 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3164, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_BufferTooSmall);
}

TEST_F(CrsDatabaseTest, pl_get_crs)
{
    pl_Crs crs;
    pl_CrsDatabaseResult res1 = pl_get_crs(crs_db, "EPSG", 3857, &crs);
    ASSERT_EQ(res1, pl_CrsDatabaseResult_Ok);

    pl_Crs expected_crs;
    pl_Result res2 = pl_crs_from_proj_zstr(
        "+proj=merc +a=6378137 +b=6378137 +lat_ts=0.0 +lon_0=0.0 +x_0=0.0 +y_0=0 +k=1.0 +units=m "
        "+nadgrids=@null +wktext +no_defs",
        &expected_crs);
    ASSERT_EQ(res2, pl_Result_Ok);

    int equal = memcmp(&crs, &expected_crs, sizeof(pl_Crs));
    ASSERT_EQ(equal, 0);
}
