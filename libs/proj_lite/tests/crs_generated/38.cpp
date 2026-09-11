// SPDX-FileCopyrightText: Copyright 2021 Siradel
// SPDX-License-Identifier: MIT

#include "../crs_common.h"

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32054)
{
    // NAD27 / Wisconsin South

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32054, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 146);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32054, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 146);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=42.73333333333333 +lat_2=44.06666666666667 +lat_0=42 +lon_0=-90 "
        "+x_0=609601.2192024384 +y_0=0 +datum=NAD27 +units=us-ft +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32055)
{
    // NAD27 / Wyoming East

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32055, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 143);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32055, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 143);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=40.66666666666666 +lon_0=-105.1666666666667 +k=0.999941177 "
        "+x_0=152400.3048006096 +y_0=0 +datum=NAD27 +units=us-ft +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32056)
{
    // NAD27 / Wyoming East Central

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32056, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 143);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32056, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 143);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=40.66666666666666 +lon_0=-107.3333333333333 +k=0.999941177 "
        "+x_0=152400.3048006096 +y_0=0 +datum=NAD27 +units=us-ft +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32057)
{
    // NAD27 / Wyoming West Central

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32057, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 132);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32057, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 132);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=40.66666666666666 +lon_0=-108.75 +k=0.999941177 +x_0=152400.3048006096 "
        "+y_0=0 +datum=NAD27 +units=us-ft +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32058)
{
    // NAD27 / Wyoming West

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32058, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 143);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32058, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 143);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=40.66666666666666 +lon_0=-110.0833333333333 +k=0.999941177 "
        "+x_0=152400.3048006096 +y_0=0 +datum=NAD27 +units=us-ft +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32061)
{
    // NAD27 / Guatemala Norte

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32061, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 161);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32061, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 161);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=16.81666666666667 +lat_0=16.81666666666667 +lon_0=-90.33333333333333 "
        "+k_0=0.99992226 +x_0=500000 +y_0=292209.579 +datum=NAD27 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32062)
{
    // NAD27 / Guatemala Sur

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32062, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 135);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32062, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 135);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=14.9 +lat_0=14.9 +lon_0=-90.33333333333333 +k_0=0.99989906 +x_0=500000 "
        "+y_0=325992.681 +datum=NAD27 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32064)
{
    // NAD27 / BLM 14N (ftUS)

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32064, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 106);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32064, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 106);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=-99 +k=0.9996 +x_0=500000.001016002 +y_0=0 +datum=NAD27 "
        "+units=us-ft +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32065)
{
    // NAD27 / BLM 15N (ftUS)

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32065, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 106);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32065, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 106);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=-93 +k=0.9996 +x_0=500000.001016002 +y_0=0 +datum=NAD27 "
        "+units=us-ft +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32066)
{
    // NAD27 / BLM 16N (ftUS)

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32066, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 106);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32066, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 106);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=-87 +k=0.9996 +x_0=500000.001016002 +y_0=0 +datum=NAD27 "
        "+units=us-ft +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32067)
{
    // NAD27 / BLM 17N (ftUS)

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32067, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 106);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32067, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 106);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=-81 +k=0.9996 +x_0=500000.001016002 +y_0=0 +datum=NAD27 "
        "+units=us-ft +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32074)
{
    // NAD27 / BLM 14N (feet)

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32074, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 106);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32074, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 106);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=-99 +k=0.9996 +x_0=500000.001016002 +y_0=0 +datum=NAD27 "
        "+units=us-ft +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32075)
{
    // NAD27 / BLM 15N (feet)

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32075, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 106);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32075, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 106);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=-93 +k=0.9996 +x_0=500000.001016002 +y_0=0 +datum=NAD27 "
        "+units=us-ft +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32076)
{
    // NAD27 / BLM 16N (feet)

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32076, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 106);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32076, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 106);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=-87 +k=0.9996 +x_0=500000.001016002 +y_0=0 +datum=NAD27 "
        "+units=us-ft +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32077)
{
    // NAD27 / BLM 17N (feet)

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32077, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 106);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32077, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 106);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=-81 +k=0.9996 +x_0=500000.001016002 +y_0=0 +datum=NAD27 "
        "+units=us-ft +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32081)
{
    // NAD27 / MTM zone 1

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32081, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 92);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32081, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 92);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=-53 +k=0.9999 +x_0=304800 +y_0=0 +datum=NAD27 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32082)
{
    // NAD27 / MTM zone 2

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32082, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 92);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32082, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 92);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=-56 +k=0.9999 +x_0=304800 +y_0=0 +datum=NAD27 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32083)
{
    // NAD27 / MTM zone 3

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32083, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 94);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32083, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 94);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=-58.5 +k=0.9999 +x_0=304800 +y_0=0 +datum=NAD27 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32084)
{
    // NAD27 / MTM zone 4

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32084, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 94);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32084, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 94);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=-61.5 +k=0.9999 +x_0=304800 +y_0=0 +datum=NAD27 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32085)
{
    // NAD27 / MTM zone 5

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32085, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 94);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32085, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 94);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=-64.5 +k=0.9999 +x_0=304800 +y_0=0 +datum=NAD27 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32086)
{
    // NAD27 / MTM zone 6

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32086, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 94);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32086, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 94);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=-67.5 +k=0.9999 +x_0=304800 +y_0=0 +datum=NAD27 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32098)
{
    // NAD27 / Quebec Lambert

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32098, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 98);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32098, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 98);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=60 +lat_2=46 +lat_0=44 +lon_0=-68.5 +x_0=0 +y_0=0 +datum=NAD27 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32099)
{
    // NAD27 / Louisiana Offshore

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32099, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 176);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32099, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 176);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=27.83333333333333 +lat_2=26.16666666666667 +lat_0=25.66666666666667 "
        "+lon_0=-91.33333333333333 +x_0=609601.2192024384 +y_0=0 +datum=NAD27 +units=us-ft "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32100)
{
    // NAD83 / Montana

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32100, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 107);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32100, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 107);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=49 +lat_2=45 +lat_0=44.25 +lon_0=-109.5 +x_0=600000 +y_0=0 +datum=NAD83 "
        "+units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32104)
{
    // NAD83 / Nebraska

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32104, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 117);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32104, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 117);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=43 +lat_2=40 +lat_0=39.83333333333334 +lon_0=-100 +x_0=500000 +y_0=0 "
        "+datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32107)
{
    // NAD83 / Nevada East

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32107, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 117);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32107, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 117);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=34.75 +lon_0=-115.5833333333333 +k=0.9999 +x_0=200000 +y_0=8000000 "
        "+datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32108)
{
    // NAD83 / Nevada Central

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32108, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 117);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32108, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 117);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=34.75 +lon_0=-116.6666666666667 +k=0.9999 +x_0=500000 +y_0=6000000 "
        "+datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32109)
{
    // NAD83 / Nevada West

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32109, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 117);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32109, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 117);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=34.75 +lon_0=-118.5833333333333 +k=0.9999 +x_0=800000 +y_0=4000000 "
        "+datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32110)
{
    // NAD83 / New Hampshire

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32110, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 115);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32110, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 115);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=42.5 +lon_0=-71.66666666666667 +k=0.999966667 +x_0=300000 +y_0=0 "
        "+datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32111)
{
    // NAD83 / New Jersey

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32111, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 110);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32111, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 110);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=38.83333333333334 +lon_0=-74.5 +k=0.9999 +x_0=150000 +y_0=0 "
        "+datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32112)
{
    // NAD83 / New Mexico East

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32112, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 113);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32112, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 113);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=31 +lon_0=-104.3333333333333 +k=0.999909091 +x_0=165000 +y_0=0 "
        "+datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32113)
{
    // NAD83 / New Mexico Central

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32113, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 97);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32113, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 97);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=31 +lon_0=-106.25 +k=0.9999 +x_0=500000 +y_0=0 +datum=NAD83 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32114)
{
    // NAD83 / New Mexico West

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32114, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 113);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32114, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 113);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=31 +lon_0=-107.8333333333333 +k=0.999916667 +x_0=830000 +y_0=0 "
        "+datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32115)
{
    // NAD83 / New York East

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32115, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 110);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32115, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 110);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=38.83333333333334 +lon_0=-74.5 +k=0.9999 +x_0=150000 +y_0=0 "
        "+datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32116)
{
    // NAD83 / New York Central

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32116, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 111);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32116, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 111);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=40 +lon_0=-76.58333333333333 +k=0.9999375 +x_0=250000 +y_0=0 "
        "+datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32117)
{
    // NAD83 / New York West

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32117, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 111);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32117, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 111);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=40 +lon_0=-78.58333333333333 +k=0.9999375 +x_0=350000 +y_0=0 "
        "+datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32118)
{
    // NAD83 / New York Long Island

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32118, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 146);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32118, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 146);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=41.03333333333333 +lat_2=40.66666666666666 +lat_0=40.16666666666666 "
        "+lon_0=-74 +x_0=300000 +y_0=0 +datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32119)
{
    // NAD83 / North Carolina

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32119, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 137);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32119, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 137);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=36.16666666666666 +lat_2=34.33333333333334 +lat_0=33.75 +lon_0=-79 "
        "+x_0=609601.22 +y_0=0 +datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32120)
{
    // NAD83 / North Dakota North

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32120, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 134);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32120, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 134);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=48.73333333333333 +lat_2=47.43333333333333 +lat_0=47 +lon_0=-100.5 "
        "+x_0=600000 +y_0=0 +datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32121)
{
    // NAD83 / North Dakota South

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32121, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 149);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32121, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 149);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=47.48333333333333 +lat_2=46.18333333333333 +lat_0=45.66666666666666 "
        "+lon_0=-100.5 +x_0=600000 +y_0=0 +datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32122)
{
    // NAD83 / Ohio North

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32122, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 135);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32122, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 135);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=41.7 +lat_2=40.43333333333333 +lat_0=39.66666666666666 +lon_0=-82.5 "
        "+x_0=600000 +y_0=0 +datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32123)
{
    // NAD83 / Ohio South

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32123, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 133);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32123, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 133);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=40.03333333333333 +lat_2=38.73333333333333 +lat_0=38 +lon_0=-82.5 "
        "+x_0=600000 +y_0=0 +datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32124)
{
    // NAD83 / Oklahoma North

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32124, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 131);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32124, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 131);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=36.76666666666667 +lat_2=35.56666666666667 +lat_0=35 +lon_0=-98 "
        "+x_0=600000 +y_0=0 +datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32125)
{
    // NAD83 / Oklahoma South

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32125, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 146);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32125, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 146);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=35.23333333333333 +lat_2=33.93333333333333 +lat_0=33.33333333333334 "
        "+lon_0=-98 +x_0=600000 +y_0=0 +datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32126)
{
    // NAD83 / Oregon North

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32126, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 135);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32126, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 135);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=46 +lat_2=44.33333333333334 +lat_0=43.66666666666666 +lon_0=-120.5 "
        "+x_0=2500000 +y_0=0 +datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32127)
{
    // NAD83 / Oregon South

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32127, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 135);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32127, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 135);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=44 +lat_2=42.33333333333334 +lat_0=41.66666666666666 +lon_0=-120.5 "
        "+x_0=1500000 +y_0=0 +datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32128)
{
    // NAD83 / Pennsylvania North

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32128, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 137);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32128, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 137);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=41.95 +lat_2=40.88333333333333 +lat_0=40.16666666666666 +lon_0=-77.75 "
        "+x_0=600000 +y_0=0 +datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32129)
{
    // NAD83 / Pennsylvania South

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32129, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 149);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32129, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 149);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=40.96666666666667 +lat_2=39.93333333333333 +lat_0=39.33333333333334 "
        "+lon_0=-77.75 +x_0=600000 +y_0=0 +datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32130)
{
    // NAD83 / Rhode Island

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32130, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 114);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32130, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 114);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=41.08333333333334 +lon_0=-71.5 +k=0.99999375 +x_0=100000 +y_0=0 "
        "+datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32133)
{
    // NAD83 / South Carolina

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32133, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 133);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32133, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 133);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=34.83333333333334 +lat_2=32.5 +lat_0=31.83333333333333 +lon_0=-81 "
        "+x_0=609600 +y_0=0 +datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32134)
{
    // NAD83 / South Dakota North

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32134, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 147);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32134, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 147);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=45.68333333333333 +lat_2=44.41666666666666 +lat_0=43.83333333333334 "
        "+lon_0=-100 +x_0=600000 +y_0=0 +datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32135)
{
    // NAD83 / South Dakota South

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32135, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 148);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32135, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 148);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=44.4 +lat_2=42.83333333333334 +lat_0=42.33333333333334 "
        "+lon_0=-100.3333333333333 +x_0=600000 +y_0=0 +datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32136)
{
    // NAD83 / Tennessee

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32136, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 134);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32136, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 134);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=36.41666666666666 +lat_2=35.25 +lat_0=34.33333333333334 +lon_0=-86 "
        "+x_0=600000 +y_0=0 +datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32137)
{
    // NAD83 / Texas North

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32137, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 128);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32137, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 128);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=36.18333333333333 +lat_2=34.65 +lat_0=34 +lon_0=-101.5 +x_0=200000 "
        "+y_0=1000000 +datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32138)
{
    // NAD83 / Texas North Central

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32138, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 154);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32138, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 154);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=33.96666666666667 +lat_2=32.13333333333333 +lat_0=31.66666666666667 "
        "+lon_0=-98.5 +x_0=600000 +y_0=2000000 +datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32139)
{
    // NAD83 / Texas Central

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32139, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 167);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32139, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 167);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=31.88333333333333 +lat_2=30.11666666666667 +lat_0=29.66666666666667 "
        "+lon_0=-100.3333333333333 +x_0=700000 +y_0=3000000 +datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32140)
{
    // NAD83 / Texas South Central

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32140, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 152);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32140, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 152);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=30.28333333333333 +lat_2=28.38333333333333 +lat_0=27.83333333333333 "
        "+lon_0=-99 +x_0=600000 +y_0=4000000 +datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32141)
{
    // NAD83 / Texas South

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32141, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 154);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32141, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 154);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=27.83333333333333 +lat_2=26.16666666666667 +lat_0=25.66666666666667 "
        "+lon_0=-98.5 +x_0=300000 +y_0=5000000 +datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32142)
{
    // NAD83 / Utah North

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32142, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 155);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32142, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 155);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=41.78333333333333 +lat_2=40.71666666666667 +lat_0=40.33333333333334 "
        "+lon_0=-111.5 +x_0=500000 +y_0=1000000 +datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32143)
{
    // NAD83 / Utah Central

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32143, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 143);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32143, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 143);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=40.65 +lat_2=39.01666666666667 +lat_0=38.33333333333334 +lon_0=-111.5 "
        "+x_0=500000 +y_0=2000000 +datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32144)
{
    // NAD83 / Utah South

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32144, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 143);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32144, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 143);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=38.35 +lat_2=37.21666666666667 +lat_0=36.66666666666666 +lon_0=-111.5 "
        "+x_0=500000 +y_0=3000000 +datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32145)
{
    // NAD83 / Vermont

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32145, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 102);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32145, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 102);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=42.5 +lon_0=-72.5 +k=0.999964286 +x_0=500000 +y_0=0 +datum=NAD83 "
        "+units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32146)
{
    // NAD83 / Virginia North

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32146, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 142);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32146, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 142);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=39.2 +lat_2=38.03333333333333 +lat_0=37.66666666666666 +lon_0=-78.5 "
        "+x_0=3500000 +y_0=2000000 +datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32147)
{
    // NAD83 / Virginia South

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32147, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 155);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32147, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 155);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=37.96666666666667 +lat_2=36.76666666666667 +lat_0=36.33333333333334 "
        "+lon_0=-78.5 +x_0=3500000 +y_0=1000000 +datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32148)
{
    // NAD83 / Washington North

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32148, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 133);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32148, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 133);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=48.73333333333333 +lat_2=47.5 +lat_0=47 +lon_0=-120.8333333333333 "
        "+x_0=500000 +y_0=0 +datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32149)
{
    // NAD83 / Washington South

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32149, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 149);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32149, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 149);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=47.33333333333334 +lat_2=45.83333333333334 +lat_0=45.33333333333334 "
        "+lon_0=-120.5 +x_0=500000 +y_0=0 +datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32150)
{
    // NAD83 / West Virginia North

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32150, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 108);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32150, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 108);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=40.25 +lat_2=39 +lat_0=38.5 +lon_0=-79.5 +x_0=600000 +y_0=0 +datum=NAD83 "
        "+units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32151)
{
    // NAD83 / West Virginia South

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32151, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 131);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32151, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 131);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=38.88333333333333 +lat_2=37.48333333333333 +lat_0=37 +lon_0=-81 "
        "+x_0=600000 +y_0=0 +datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32152)
{
    // NAD83 / Wisconsin North

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32152, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 146);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32152, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 146);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=46.76666666666667 +lat_2=45.56666666666667 +lat_0=45.16666666666666 "
        "+lon_0=-90 +x_0=600000 +y_0=0 +datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32153)
{
    // NAD83 / Wisconsin Central

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32153, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 121);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32153, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 121);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=45.5 +lat_2=44.25 +lat_0=43.83333333333334 +lon_0=-90 +x_0=600000 +y_0=0 "
        "+datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32154)
{
    // NAD83 / Wisconsin South

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32154, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 131);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32154, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 131);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=44.06666666666667 +lat_2=42.73333333333333 +lat_0=42 +lon_0=-90 "
        "+x_0=600000 +y_0=0 +datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32155)
{
    // NAD83 / Wyoming East

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32155, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 113);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32155, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 113);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=40.5 +lon_0=-105.1666666666667 +k=0.9999375 +x_0=200000 +y_0=0 "
        "+datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32156)
{
    // NAD83 / Wyoming East Central

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32156, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 118);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32156, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 118);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=40.5 +lon_0=-107.3333333333333 +k=0.9999375 +x_0=400000 +y_0=100000 "
        "+datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32157)
{
    // NAD83 / Wyoming West Central

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32157, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 102);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32157, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 102);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=40.5 +lon_0=-108.75 +k=0.9999375 +x_0=600000 +y_0=0 +datum=NAD83 "
        "+units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32158)
{
    // NAD83 / Wyoming West

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32158, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 118);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32158, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 118);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=40.5 +lon_0=-110.0833333333333 +k=0.9999375 +x_0=800000 +y_0=100000 "
        "+datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32161)
{
    // NAD83 / Puerto Rico & Virgin Is.

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32161, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 166);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32161, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 166);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=18.43333333333333 +lat_2=18.03333333333333 +lat_0=17.83333333333333 "
        "+lon_0=-66.43333333333334 +x_0=200000 +y_0=200000 +datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32164)
{
    // NAD83 / BLM 14N (ftUS)

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32164, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 106);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32164, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 106);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=-99 +k=0.9996 +x_0=500000.001016002 +y_0=0 +datum=NAD83 "
        "+units=us-ft +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32165)
{
    // NAD83 / BLM 15N (ftUS)

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32165, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 106);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32165, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 106);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=-93 +k=0.9996 +x_0=500000.001016002 +y_0=0 +datum=NAD83 "
        "+units=us-ft +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32166)
{
    // NAD83 / BLM 16N (ftUS)

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32166, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 106);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32166, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 106);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=-87 +k=0.9996 +x_0=500000.001016002 +y_0=0 +datum=NAD83 "
        "+units=us-ft +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32167)
{
    // NAD83 / BLM 17N (ftUS)

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32167, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 106);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32167, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 106);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=-81 +k=0.9996 +x_0=500000.001016002 +y_0=0 +datum=NAD83 "
        "+units=us-ft +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32180)
{
    // NAD83 / SCoPQ zone 2

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32180, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 94);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32180, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 94);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=-55.5 +k=0.9999 +x_0=304800 +y_0=0 +datum=NAD83 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32181)
{
    // NAD83 / MTM zone 1

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32181, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 92);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32181, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 92);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=-53 +k=0.9999 +x_0=304800 +y_0=0 +datum=NAD83 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32182)
{
    // NAD83 / MTM zone 2

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32182, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 92);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32182, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 92);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=-56 +k=0.9999 +x_0=304800 +y_0=0 +datum=NAD83 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32183)
{
    // NAD83 / MTM zone 3

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32183, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 94);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32183, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 94);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=-58.5 +k=0.9999 +x_0=304800 +y_0=0 +datum=NAD83 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32184)
{
    // NAD83 / MTM zone 4

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32184, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 94);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32184, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 94);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=-61.5 +k=0.9999 +x_0=304800 +y_0=0 +datum=NAD83 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32185)
{
    // NAD83 / MTM zone 5

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32185, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 94);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32185, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 94);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=-64.5 +k=0.9999 +x_0=304800 +y_0=0 +datum=NAD83 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32186)
{
    // NAD83 / MTM zone 6

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32186, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 94);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32186, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 94);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=-67.5 +k=0.9999 +x_0=304800 +y_0=0 +datum=NAD83 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32187)
{
    // NAD83 / MTM zone 7

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32187, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 94);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32187, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 94);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=-70.5 +k=0.9999 +x_0=304800 +y_0=0 +datum=NAD83 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32188)
{
    // NAD83 / MTM zone 8

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32188, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 94);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32188, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 94);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=-73.5 +k=0.9999 +x_0=304800 +y_0=0 +datum=NAD83 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32189)
{
    // NAD83 / MTM zone 9

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32189, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 94);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32189, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 94);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=-76.5 +k=0.9999 +x_0=304800 +y_0=0 +datum=NAD83 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32190)
{
    // NAD83 / MTM zone 10

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32190, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 94);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32190, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 94);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=-79.5 +k=0.9999 +x_0=304800 +y_0=0 +datum=NAD83 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32191)
{
    // NAD83 / MTM zone 11

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32191, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 94);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32191, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 94);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=-82.5 +k=0.9999 +x_0=304800 +y_0=0 +datum=NAD83 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32192)
{
    // NAD83 / MTM zone 12

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32192, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 92);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32192, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 92);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=-81 +k=0.9999 +x_0=304800 +y_0=0 +datum=NAD83 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32193)
{
    // NAD83 / MTM zone 13

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32193, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 92);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32193, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 92);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=-84 +k=0.9999 +x_0=304800 +y_0=0 +datum=NAD83 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32194)
{
    // NAD83 / MTM zone 14

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32194, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 92);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32194, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 92);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=-87 +k=0.9999 +x_0=304800 +y_0=0 +datum=NAD83 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32195)
{
    // NAD83 / MTM zone 15

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32195, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 92);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32195, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 92);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=-90 +k=0.9999 +x_0=304800 +y_0=0 +datum=NAD83 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32196)
{
    // NAD83 / MTM zone 16

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32196, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 92);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32196, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 92);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=-93 +k=0.9999 +x_0=304800 +y_0=0 +datum=NAD83 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32197)
{
    // NAD83 / MTM zone 17

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32197, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 92);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32197, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 92);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=-96 +k=0.9999 +x_0=304800 +y_0=0 +datum=NAD83 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32198)
{
    // NAD83 / Quebec Lambert

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32198, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 98);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32198, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 98);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=60 +lat_2=46 +lat_0=44 +lon_0=-68.5 +x_0=0 +y_0=0 +datum=NAD83 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_32199)
{
    // NAD83 / Louisiana Offshore

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 32199, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 149);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 32199, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 149);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=27.83333333333333 +lat_2=26.16666666666667 +lat_0=25.5 "
        "+lon_0=-91.33333333333333 +x_0=1000000 +y_0=0 +datum=NAD83 +units=m +no_defs ");
}
