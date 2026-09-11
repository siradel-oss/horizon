// SPDX-FileCopyrightText: Copyright 2021 Siradel
// SPDX-License-Identifier: MIT

#include "../crs_common.h"

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2039)
{
    // Israel 1993 / Israeli TM Grid

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2039, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 164);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2039, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 164);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=31.73439361111111 +lon_0=35.20451694444445 +k=1.0000067 "
        "+x_0=219529.584 +y_0=626907.39 +ellps=GRS80 +towgs84=-48,55,52,0,0,0,0 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2040)
{
    // Locodjo 1965 / UTM zone 30N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2040, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 80);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2040, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 80);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=30 +ellps=clrk80 +towgs84=-125,53,467,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2041)
{
    // Abidjan 1987 / UTM zone 30N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2041, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 86);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2041, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 86);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=30 +ellps=clrk80 +towgs84=-124.76,53,466.79,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2042)
{
    // Locodjo 1965 / UTM zone 29N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2042, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 80);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2042, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 80);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=29 +ellps=clrk80 +towgs84=-125,53,467,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2043)
{
    // Abidjan 1987 / UTM zone 29N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2043, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 86);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2043, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 86);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=29 +ellps=clrk80 +towgs84=-124.76,53,466.79,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2044)
{
    // Hanoi 1972 / Gauss-Kruger zone 18

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2044, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 128);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2044, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 128);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=105 +k=1 +x_0=18500000 +y_0=0 +ellps=krass "
        "+towgs84=-17.51,-108.32,-62.39,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2045)
{
    // Hanoi 1972 / Gauss-Kruger zone 19

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2045, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 128);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2045, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 128);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=111 +k=1 +x_0=19500000 +y_0=0 +ellps=krass "
        "+towgs84=-17.51,-108.32,-62.39,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2046)
{
    // Hartebeesthoek94 / Lo15

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2046, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 114);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2046, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 114);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=15 +k=1 +x_0=0 +y_0=0 +axis=wsu +ellps=WGS84 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2047)
{
    // Hartebeesthoek94 / Lo17

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2047, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 114);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2047, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 114);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=17 +k=1 +x_0=0 +y_0=0 +axis=wsu +ellps=WGS84 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2048)
{
    // Hartebeesthoek94 / Lo19

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2048, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 114);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2048, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 114);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=19 +k=1 +x_0=0 +y_0=0 +axis=wsu +ellps=WGS84 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2049)
{
    // Hartebeesthoek94 / Lo21

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2049, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 114);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2049, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 114);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=21 +k=1 +x_0=0 +y_0=0 +axis=wsu +ellps=WGS84 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2050)
{
    // Hartebeesthoek94 / Lo23

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2050, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 114);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2050, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 114);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=23 +k=1 +x_0=0 +y_0=0 +axis=wsu +ellps=WGS84 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2051)
{
    // Hartebeesthoek94 / Lo25

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2051, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 114);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2051, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 114);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=25 +k=1 +x_0=0 +y_0=0 +axis=wsu +ellps=WGS84 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2052)
{
    // Hartebeesthoek94 / Lo27

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2052, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 114);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2052, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 114);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=27 +k=1 +x_0=0 +y_0=0 +axis=wsu +ellps=WGS84 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2053)
{
    // Hartebeesthoek94 / Lo29

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2053, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 114);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2053, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 114);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=29 +k=1 +x_0=0 +y_0=0 +axis=wsu +ellps=WGS84 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2054)
{
    // Hartebeesthoek94 / Lo31

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2054, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 114);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2054, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 114);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=31 +k=1 +x_0=0 +y_0=0 +axis=wsu +ellps=WGS84 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2055)
{
    // Hartebeesthoek94 / Lo33

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2055, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 114);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2055, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 114);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=33 +k=1 +x_0=0 +y_0=0 +axis=wsu +ellps=WGS84 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2056)
{
    // CH1903+ / LV95

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2056, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 168);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2056, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 168);
    ASSERT_EQ(
        crs_string,
        "+proj=somerc +lat_0=46.95240555555556 +lon_0=7.439583333333333 +k_0=1 +x_0=2600000 "
        "+y_0=1200000 +ellps=bessel +towgs84=674.374,15.056,405.346,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2057)
{
    // Rassadiran / Nakhl e Taqi

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2057, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 231);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2057, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 231);
    ASSERT_EQ(
        crs_string,
        "+proj=omerc +lat_0=27.51882880555555 +lonc=52.60353916666667 +alpha=0.5716611944444444 "
        "+k=0.999895934 +x_0=658377.437 +y_0=3044969.194 +gamma=0.5716611944444444 +ellps=intl "
        "+towgs84=-133.63,-157.5,-158.62,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2058)
{
    // ED50(ED77) / UTM zone 38N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2058, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 81);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2058, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 81);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=38 +ellps=intl +towgs84=-117,-132,-164,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2059)
{
    // ED50(ED77) / UTM zone 39N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2059, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 81);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2059, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 81);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=39 +ellps=intl +towgs84=-117,-132,-164,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2060)
{
    // ED50(ED77) / UTM zone 40N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2060, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 81);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2060, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 81);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=40 +ellps=intl +towgs84=-117,-132,-164,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2061)
{
    // ED50(ED77) / UTM zone 41N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2061, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 81);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2061, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 81);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=41 +ellps=intl +towgs84=-117,-132,-164,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2062)
{
    // Madrid 1870 (Madrid) / Spain

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2062, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 144);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2062, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 144);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=40 +lat_0=40 +lon_0=0 +k_0=0.9988085293 +x_0=600000 +y_0=600000 "
        "+a=6378298.3 +b=6356657.142669561 +pm=madrid +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2063)
{
    // Dabola 1981 / UTM zone 28N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2063, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 89);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2063, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 89);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=28 +a=6378249.2 +b=6356515 +towgs84=-23,259,-9,0,0,0,0 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2064)
{
    // Dabola 1981 / UTM zone 29N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2064, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 89);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2064, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 89);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=29 +a=6378249.2 +b=6356515 +towgs84=-23,259,-9,0,0,0,0 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2065)
{
    // S-JTSK (Ferro) / Krovak

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2065, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 156);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2065, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 156);
    ASSERT_EQ(
        crs_string,
        "+proj=krovak +lat_0=49.5 +lon_0=42.5 +alpha=30.28813972222222 +k=0.9999 +x_0=0 +y_0=0 "
        "+ellps=bessel +towgs84=589,76,480,0,0,0,0 +pm=ferro +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2066)
{
    // Mount Dillon / Tobago Grid

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2066, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 178);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2066, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 178);
    ASSERT_EQ(
        crs_string,
        "+proj=cass +lat_0=11.25217861111111 +lon_0=-60.68600888888889 +x_0=37718.66159325 "
        "+y_0=36209.91512952 +a=6378293.645208759 +b=6356617.987679838 +to_meter=0.201166195164 "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2067)
{
    // Naparima 1955 / UTM zone 20N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2067, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 89);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2067, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 89);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=20 +ellps=intl +towgs84=-0.465,372.095,171.736,0,0,0,0 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2068)
{
    // ELD79 / Libya zone 5

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2068, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 133);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2068, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 133);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=9 +k=0.9999 +x_0=200000 +y_0=0 +ellps=intl "
        "+towgs84=-115.854,-99.0583,-152.462,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2069)
{
    // ELD79 / Libya zone 6

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2069, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 134);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2069, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 134);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=11 +k=0.9999 +x_0=200000 +y_0=0 +ellps=intl "
        "+towgs84=-115.854,-99.0583,-152.462,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2070)
{
    // ELD79 / Libya zone 7

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2070, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 134);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2070, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 134);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=13 +k=0.9999 +x_0=200000 +y_0=0 +ellps=intl "
        "+towgs84=-115.854,-99.0583,-152.462,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2071)
{
    // ELD79 / Libya zone 8

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2071, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 134);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2071, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 134);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=15 +k=0.9999 +x_0=200000 +y_0=0 +ellps=intl "
        "+towgs84=-115.854,-99.0583,-152.462,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2072)
{
    // ELD79 / Libya zone 9

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2072, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 134);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2072, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 134);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=17 +k=0.9999 +x_0=200000 +y_0=0 +ellps=intl "
        "+towgs84=-115.854,-99.0583,-152.462,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2073)
{
    // ELD79 / Libya zone 10

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2073, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 134);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2073, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 134);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=19 +k=0.9999 +x_0=200000 +y_0=0 +ellps=intl "
        "+towgs84=-115.854,-99.0583,-152.462,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2074)
{
    // ELD79 / Libya zone 11

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2074, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 134);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2074, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 134);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=21 +k=0.9999 +x_0=200000 +y_0=0 +ellps=intl "
        "+towgs84=-115.854,-99.0583,-152.462,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2075)
{
    // ELD79 / Libya zone 12

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2075, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 134);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2075, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 134);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=23 +k=0.9999 +x_0=200000 +y_0=0 +ellps=intl "
        "+towgs84=-115.854,-99.0583,-152.462,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2076)
{
    // ELD79 / Libya zone 13

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2076, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 134);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2076, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 134);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=25 +k=0.9999 +x_0=200000 +y_0=0 +ellps=intl "
        "+towgs84=-115.854,-99.0583,-152.462,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2077)
{
    // ELD79 / UTM zone 32N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2077, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 93);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2077, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 93);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=32 +ellps=intl +towgs84=-115.854,-99.0583,-152.462,0,0,0,0 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2078)
{
    // ELD79 / UTM zone 33N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2078, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 93);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2078, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 93);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=33 +ellps=intl +towgs84=-115.854,-99.0583,-152.462,0,0,0,0 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2079)
{
    // ELD79 / UTM zone 34N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2079, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 93);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2079, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 93);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=34 +ellps=intl +towgs84=-115.854,-99.0583,-152.462,0,0,0,0 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2080)
{
    // ELD79 / UTM zone 35N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2080, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 93);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2080, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 93);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=35 +ellps=intl +towgs84=-115.854,-99.0583,-152.462,0,0,0,0 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2081)
{
    // Chos Malal 1914 / Argentina 2

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2081, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 89);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2081, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 89);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=-90 +lon_0=-69 +k=1 +x_0=2500000 +y_0=0 +ellps=intl +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2082)
{
    // Pampa del Castillo / Argentina 2

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2082, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 120);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2082, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 120);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=-90 +lon_0=-69 +k=1 +x_0=2500000 +y_0=0 +ellps=intl "
        "+towgs84=27.5,14,186.4,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2083)
{
    // Hito XVIII 1963 / Argentina 2

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2083, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 116);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2083, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 116);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=-90 +lon_0=-69 +k=1 +x_0=2500000 +y_0=0 +ellps=intl "
        "+towgs84=16,196,93,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2084)
{
    // Hito XVIII 1963 / UTM zone 19S

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2084, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 83);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2084, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 83);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=19 +south +ellps=intl +towgs84=16,196,93,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2085)
{
    // NAD27 / Cuba Norte

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2085, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 122);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2085, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 122);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=22.35 +lat_0=22.35 +lon_0=-81 +k_0=0.99993602 +x_0=500000 "
        "+y_0=280296.016 +datum=NAD27 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2086)
{
    // NAD27 / Cuba Sur

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2086, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 161);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2086, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 161);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=20.71666666666667 +lat_0=20.71666666666667 +lon_0=-76.83333333333333 "
        "+k_0=0.99994848 +x_0=500000 +y_0=229126.939 +datum=NAD27 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2087)
{
    // ELD79 / TM 12 NE

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2087, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 134);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2087, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 134);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=12 +k=0.9996 +x_0=500000 +y_0=0 +ellps=intl "
        "+towgs84=-115.854,-99.0583,-152.462,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2088)
{
    // Carthage / TM 11 NE

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2088, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 94);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2088, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 94);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=11 +k=0.9996 +x_0=500000 +y_0=0 +datum=carthage +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2089)
{
    // Yemen NGN96 / UTM zone 38N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2089, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 73);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2089, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 73);
    ASSERT_EQ(
        crs_string, "+proj=utm +zone=38 +ellps=WGS84 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2090)
{
    // Yemen NGN96 / UTM zone 39N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2090, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 73);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2090, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 73);
    ASSERT_EQ(
        crs_string, "+proj=utm +zone=39 +ellps=WGS84 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2091)
{
    // South Yemen / Gauss Kruger zone 8

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2091, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 116);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2091, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 116);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=45 +k=1 +x_0=8500000 +y_0=0 +ellps=krass "
        "+towgs84=-76,-138,67,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2092)
{
    // South Yemen / Gauss Kruger zone 9

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2092, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 116);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2092, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 116);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=51 +k=1 +x_0=9500000 +y_0=0 +ellps=krass "
        "+towgs84=-76,-138,67,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2093)
{
    // Hanoi 1972 / GK 106 NE

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2093, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 126);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2093, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 126);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=106 +k=1 +x_0=500000 +y_0=0 +ellps=krass "
        "+towgs84=-17.51,-108.32,-62.39,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2094)
{
    // WGS 72BE / TM 106 NE

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2094, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 125);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2094, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 125);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=106 +k=0.9996 +x_0=500000 +y_0=0 +ellps=WGS72 "
        "+towgs84=0,0,1.9,0,0,0.814,-0.38 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2095)
{
    // Bissau / UTM zone 28N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2095, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 78);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2095, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 78);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=28 +ellps=intl +towgs84=-173,253,27,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2096)
{
    // Korean 1985 / East Belt

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2096, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 94);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2096, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 94);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=38 +lon_0=129 +k=1 +x_0=200000 +y_0=500000 +ellps=bessel +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2097)
{
    // Korean 1985 / Central Belt

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2097, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 94);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2097, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 94);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=38 +lon_0=127 +k=1 +x_0=200000 +y_0=500000 +ellps=bessel +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2098)
{
    // Korean 1985 / West Belt

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2098, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 94);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2098, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 94);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=38 +lon_0=125 +k=1 +x_0=200000 +y_0=500000 +ellps=bessel +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2099)
{
    // Qatar 1948 / Qatar Grid

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2099, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 118);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2099, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 118);
    ASSERT_EQ(
        crs_string,
        "+proj=cass +lat_0=25.38236111111111 +lon_0=50.76138888888889 +x_0=100000 +y_0=100000 "
        "+ellps=helmert +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2100)
{
    // GGRS87 / Greek Grid

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2100, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 92);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2100, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 92);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=24 +k=0.9996 +x_0=500000 +y_0=0 +datum=GGRS87 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2101)
{
    // Lake / Maracaibo Grid M1

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2101, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 146);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2101, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 146);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=10.16666666666667 +lat_0=10.16666666666667 +lon_0=-71.60561777777777 "
        "+k_0=1 +x_0=0 +y_0=-52684.972 +ellps=intl +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2102)
{
    // Lake / Maracaibo Grid

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2102, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 151);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2102, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 151);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=10.16666666666667 +lat_0=10.16666666666667 +lon_0=-71.60561777777777 "
        "+k_0=1 +x_0=200000 +y_0=147315.028 +ellps=intl +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2103)
{
    // Lake / Maracaibo Grid M3

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2103, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 151);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2103, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 151);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=10.16666666666667 +lat_0=10.16666666666667 +lon_0=-71.60561777777777 "
        "+k_0=1 +x_0=500000 +y_0=447315.028 +ellps=intl +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2104)
{
    // Lake / Maracaibo La Rosa Grid

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2104, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 150);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2104, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 150);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=10.16666666666667 +lat_0=10.16666666666667 +lon_0=-71.60561777777777 "
        "+k_0=1 +x_0=-17044 +y_0=-23139.97 +ellps=intl +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2105)
{
    // NZGD2000 / Mount Eden 2000

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2105, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 151);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2105, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 151);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=-36.87972222222222 +lon_0=174.7641666666667 +k=0.9999 +x_0=400000 "
        "+y_0=800000 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2106)
{
    // NZGD2000 / Bay of Plenty 2000

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2106, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 146);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2106, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 146);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=-37.76111111111111 +lon_0=176.4661111111111 +k=1 +x_0=400000 "
        "+y_0=800000 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2107)
{
    // NZGD2000 / Poverty Bay 2000

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2107, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 146);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2107, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 146);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=-38.62444444444444 +lon_0=177.8855555555556 +k=1 +x_0=400000 "
        "+y_0=800000 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2108)
{
    // NZGD2000 / Hawkes Bay 2000

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2108, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 146);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2108, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 146);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=-39.65083333333333 +lon_0=176.6736111111111 +k=1 +x_0=400000 "
        "+y_0=800000 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2109)
{
    // NZGD2000 / Taranaki 2000

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2109, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 146);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2109, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 146);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=-39.13555555555556 +lon_0=174.2277777777778 +k=1 +x_0=400000 "
        "+y_0=800000 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2110)
{
    // NZGD2000 / Tuhirangi 2000

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2110, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 135);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2110, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 135);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=-39.51222222222222 +lon_0=175.64 +k=1 +x_0=400000 +y_0=800000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2111)
{
    // NZGD2000 / Wanganui 2000

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2111, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 146);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2111, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 146);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=-40.24194444444444 +lon_0=175.4880555555555 +k=1 +x_0=400000 "
        "+y_0=800000 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2112)
{
    // NZGD2000 / Wairarapa 2000

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2112, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 146);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2112, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 146);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=-40.92527777777777 +lon_0=175.6472222222222 +k=1 +x_0=400000 "
        "+y_0=800000 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2113)
{
    // NZGD2000 / Wellington 2000

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2113, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 145);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2113, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 145);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=-41.3011111111111 +lon_0=174.7763888888889 +k=1 +x_0=400000 "
        "+y_0=800000 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2114)
{
    // NZGD2000 / Collingwood 2000

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2114, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 146);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2114, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 146);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=-40.71472222222223 +lon_0=172.6719444444444 +k=1 +x_0=400000 "
        "+y_0=800000 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2115)
{
    // NZGD2000 / Nelson 2000

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2115, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 146);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2115, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 146);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=-41.27444444444444 +lon_0=173.2991666666667 +k=1 +x_0=400000 "
        "+y_0=800000 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2116)
{
    // NZGD2000 / Karamea 2000

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2116, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 146);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2116, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 146);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=-41.28972222222222 +lon_0=172.1088888888889 +k=1 +x_0=400000 "
        "+y_0=800000 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2117)
{
    // NZGD2000 / Buller 2000

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2117, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 146);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2117, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 146);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=-41.81055555555555 +lon_0=171.5811111111111 +k=1 +x_0=400000 "
        "+y_0=800000 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2118)
{
    // NZGD2000 / Grey 2000

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2118, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 146);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2118, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 146);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=-42.33361111111111 +lon_0=171.5497222222222 +k=1 +x_0=400000 "
        "+y_0=800000 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2119)
{
    // NZGD2000 / Amuri 2000

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2119, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 135);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2119, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 135);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=-42.68888888888888 +lon_0=173.01 +k=1 +x_0=400000 +y_0=800000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2120)
{
    // NZGD2000 / Marlborough 2000

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2120, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 146);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2120, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 146);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=-41.54444444444444 +lon_0=173.8019444444444 +k=1 +x_0=400000 "
        "+y_0=800000 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2121)
{
    // NZGD2000 / Hokitika 2000

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2121, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 146);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2121, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 146);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=-42.88611111111111 +lon_0=170.9797222222222 +k=1 +x_0=400000 "
        "+y_0=800000 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2122)
{
    // NZGD2000 / Okarito 2000

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2122, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 134);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2122, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 134);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=-43.11 +lon_0=170.2608333333333 +k=1 +x_0=400000 +y_0=800000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2123)
{
    // NZGD2000 / Jacksons Bay 2000

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2123, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 146);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2123, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 146);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=-43.97777777777778 +lon_0=168.6061111111111 +k=1 +x_0=400000 "
        "+y_0=800000 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2124)
{
    // NZGD2000 / Mount Pleasant 2000

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2124, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 146);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2124, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 146);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=-43.59055555555556 +lon_0=172.7269444444445 +k=1 +x_0=400000 "
        "+y_0=800000 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2125)
{
    // NZGD2000 / Gawler 2000

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2125, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 146);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2125, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 146);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=-43.74861111111111 +lon_0=171.3605555555555 +k=1 +x_0=400000 "
        "+y_0=800000 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2126)
{
    // NZGD2000 / Timaru 2000

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2126, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 146);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2126, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 146);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=-44.40194444444445 +lon_0=171.0572222222222 +k=1 +x_0=400000 "
        "+y_0=800000 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2127)
{
    // NZGD2000 / Lindis Peak 2000

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2127, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 126);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2127, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 126);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=-44.735 +lon_0=169.4675 +k=1 +x_0=400000 +y_0=800000 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2128)
{
    // NZGD2000 / Mount Nicholas 2000

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2128, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 146);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2128, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 146);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=-45.13277777777778 +lon_0=168.3986111111111 +k=1 +x_0=400000 "
        "+y_0=800000 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2129)
{
    // NZGD2000 / Mount York 2000

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2129, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 146);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2129, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 146);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=-45.56361111111111 +lon_0=167.7386111111111 +k=1 +x_0=400000 "
        "+y_0=800000 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2130)
{
    // NZGD2000 / Observation Point 2000

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2130, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 146);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2130, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 146);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=-45.81611111111111 +lon_0=170.6283333333333 +k=1 +x_0=400000 "
        "+y_0=800000 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2131)
{
    // NZGD2000 / North Taieri 2000

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2131, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 143);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2131, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 143);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=-45.86138888888889 +lon_0=170.2825 +k=0.99996 +x_0=400000 +y_0=800000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2132)
{
    // NZGD2000 / Bluff 2000

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2132, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 133);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2132, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 133);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=-46.6 +lon_0=168.3427777777778 +k=1 +x_0=400000 +y_0=800000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2133)
{
    // NZGD2000 / UTM zone 58S

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2133, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 80);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2133, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 80);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=58 +south +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2134)
{
    // NZGD2000 / UTM zone 59S

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2134, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 80);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2134, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 80);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=59 +south +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2135)
{
    // NZGD2000 / UTM zone 60S

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2135, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 80);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2135, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 80);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=60 +south +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2136)
{
    // Accra / Ghana National Grid

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2136, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 187);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2136, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 187);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=4.666666666666667 +lon_0=-1 +k=0.99975 +x_0=274319.7391633579 +y_0=0 "
        "+a=6378300 +b=6356751.689189189 +towgs84=-199,32,322,0,0,0,0 +to_meter=0.3047997101815088 "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2137)
{
    // Accra / TM 1 NW

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2137, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 139);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2137, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 139);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=-1 +k=0.9996 +x_0=500000 +y_0=0 +a=6378300 "
        "+b=6356751.689189189 +towgs84=-199,32,322,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2138)
{
    // NAD27(CGQ77) / Quebec Lambert

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2138, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 99);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2138, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 99);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=60 +lat_2=46 +lat_0=44 +lon_0=-68.5 +x_0=0 +y_0=0 +ellps=clrk66 +units=m "
        "+no_defs ");
}
