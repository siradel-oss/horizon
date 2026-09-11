// SPDX-FileCopyrightText: Copyright 2021 Siradel
// SPDX-License-Identifier: MIT

#include "../crs_common.h"

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2139)
{
    // NAD83(CSRS98) / SCoPQ zone 2

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2139, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 117);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2139, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 117);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=-55.5 +k=0.9999 +x_0=304800 +y_0=0 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2140)
{
    // NAD83(CSRS98) / MTM zone 3

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2140, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 117);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2140, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 117);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=-58.5 +k=0.9999 +x_0=304800 +y_0=0 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2141)
{
    // NAD83(CSRS98) / MTM zone 4

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2141, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 117);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2141, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 117);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=-61.5 +k=0.9999 +x_0=304800 +y_0=0 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2142)
{
    // NAD83(CSRS98) / MTM zone 5

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2142, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 117);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2142, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 117);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=-64.5 +k=0.9999 +x_0=304800 +y_0=0 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2143)
{
    // NAD83(CSRS98) / MTM zone 6

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2143, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 117);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2143, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 117);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=-67.5 +k=0.9999 +x_0=304800 +y_0=0 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2144)
{
    // NAD83(CSRS98) / MTM zone 7

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2144, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 117);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2144, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 117);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=-70.5 +k=0.9999 +x_0=304800 +y_0=0 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2145)
{
    // NAD83(CSRS98) / MTM zone 8

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2145, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 117);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2145, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 117);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=-73.5 +k=0.9999 +x_0=304800 +y_0=0 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2146)
{
    // NAD83(CSRS98) / MTM zone 9

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2146, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 117);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2146, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 117);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=-76.5 +k=0.9999 +x_0=304800 +y_0=0 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2147)
{
    // NAD83(CSRS98) / MTM zone 10

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2147, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 117);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2147, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 117);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=-79.5 +k=0.9999 +x_0=304800 +y_0=0 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2148)
{
    // NAD83(CSRS98) / UTM zone 21N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2148, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 73);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2148, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 73);
    ASSERT_EQ(
        crs_string, "+proj=utm +zone=21 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2149)
{
    // NAD83(CSRS98) / UTM zone 18N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2149, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 73);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2149, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 73);
    ASSERT_EQ(
        crs_string, "+proj=utm +zone=18 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2150)
{
    // NAD83(CSRS98) / UTM zone 17N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2150, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 73);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2150, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 73);
    ASSERT_EQ(
        crs_string, "+proj=utm +zone=17 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2151)
{
    // NAD83(CSRS98) / UTM zone 13N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2151, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 73);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2151, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 73);
    ASSERT_EQ(
        crs_string, "+proj=utm +zone=13 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2152)
{
    // NAD83(CSRS98) / UTM zone 12N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2152, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 73);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2152, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 73);
    ASSERT_EQ(
        crs_string, "+proj=utm +zone=12 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2153)
{
    // NAD83(CSRS98) / UTM zone 11N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2153, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 73);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2153, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 73);
    ASSERT_EQ(
        crs_string, "+proj=utm +zone=11 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2154)
{
    // RGF93 / Lambert-93

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2154, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 130);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2154, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 130);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=49 +lat_2=44 +lat_0=46.5 +lon_0=3 +x_0=700000 +y_0=6600000 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2155)
{
    // American Samoa 1962 / American Samoa Lambert

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2155, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 176);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2155, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 176);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=-14.26666666666667 +lat_0=-14.26666666666667 +lon_0=170 +k_0=1 "
        "+x_0=152400.3048006096 +y_0=0 +ellps=clrk66 +towgs84=-115,118,426,0,0,0,0 +units=us-ft "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2156)
{
    // NAD83(HARN) / UTM zone 59S

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2156, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 80);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2156, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 80);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=59 +south +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2157)
{
    // IRENET95 / Irish Transverse Mercator

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2157, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 123);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2157, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 123);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=53.5 +lon_0=-8 +k=0.99982 +x_0=600000 +y_0=750000 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2158)
{
    // IRENET95 / UTM zone 29N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2158, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 73);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2158, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 73);
    ASSERT_EQ(
        crs_string, "+proj=utm +zone=29 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2159)
{
    // Sierra Leone 1924 / New Colony Grid

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2159, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 153);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2159, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 153);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=6.666666666666667 +lon_0=-12 +k=1 +x_0=152399.8550907544 +y_0=0 "
        "+a=6378300 +b=6356751.689189189 +to_meter=0.3047997101815088 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2160)
{
    // Sierra Leone 1924 / New War Office Grid

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2160, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 169);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2160, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 169);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=6.666666666666667 +lon_0=-12 +k=1 +x_0=243839.7681452071 "
        "+y_0=182879.8261089053 +a=6378300 +b=6356751.689189189 +to_meter=0.3047997101815088 "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2161)
{
    // Sierra Leone 1968 / UTM zone 28N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2161, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 78);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2161, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 78);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=28 +ellps=clrk80 +towgs84=-88,4,101,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2162)
{
    // Sierra Leone 1968 / UTM zone 29N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2162, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 78);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2162, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 78);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=29 +ellps=clrk80 +towgs84=-88,4,101,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2163)
{
    // US National Atlas Equal Area

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2163, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 87);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2163, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 87);
    ASSERT_EQ(
        crs_string,
        "+proj=laea +lat_0=45 +lon_0=-100 +x_0=0 +y_0=0 +a=6370997 +b=6370997 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2164)
{
    // Locodjo 1965 / TM 5 NW

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2164, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 121);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2164, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 121);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=-5 +k=0.9996 +x_0=500000 +y_0=0 +ellps=clrk80 "
        "+towgs84=-125,53,467,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2165)
{
    // Abidjan 1987 / TM 5 NW

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2165, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 127);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2165, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 127);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=-5 +k=0.9996 +x_0=500000 +y_0=0 +ellps=clrk80 "
        "+towgs84=-124.76,53,466.79,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2166)
{
    // Pulkovo 1942(83) / Gauss Kruger zone 3

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2166, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 115);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2166, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 115);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=9 +k=1 +x_0=3500000 +y_0=0 +ellps=krass "
        "+towgs84=26,-121,-78,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2167)
{
    // Pulkovo 1942(83) / Gauss Kruger zone 4

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2167, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 116);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2167, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 116);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=12 +k=1 +x_0=4500000 +y_0=0 +ellps=krass "
        "+towgs84=26,-121,-78,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2168)
{
    // Pulkovo 1942(83) / Gauss Kruger zone 5

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2168, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 116);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2168, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 116);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=15 +k=1 +x_0=5500000 +y_0=0 +ellps=krass "
        "+towgs84=26,-121,-78,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2169)
{
    // Luxembourg 1930 / Gauss

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2169, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 188);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2169, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 188);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=49.83333333333334 +lon_0=6.166666666666667 +k=1 +x_0=80000 +y_0=100000 "
        "+ellps=intl +towgs84=-189.681,18.3463,-42.7695,-0.33746,-3.09264,2.53861,0.4598 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2170)
{
    // MGI / Slovenia Grid

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2170, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 99);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2170, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 99);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=15 +k=0.9999 +x_0=500000 +y_0=0 +datum=hermannskogel +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2171)
{
    // Pulkovo 1942(58) / Poland zone I

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2171, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 172);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2171, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 172);
    ASSERT_EQ(
        crs_string,
        "+proj=sterea +lat_0=50.625 +lon_0=21.08333333333333 +k=0.9998 +x_0=4637000 +y_0=5647000 "
        "+ellps=krass +towgs84=33.4,-146.6,-76.3,-0.359,-0.053,0.844,-0.84 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2172)
{
    // Pulkovo 1942(58) / Poland zone II

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2172, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 183);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2172, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 183);
    ASSERT_EQ(
        crs_string,
        "+proj=sterea +lat_0=53.00194444444445 +lon_0=21.50277777777778 +k=0.9998 +x_0=4603000 "
        "+y_0=5806000 +ellps=krass +towgs84=33.4,-146.6,-76.3,-0.359,-0.053,0.844,-0.84 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2173)
{
    // Pulkovo 1942(58) / Poland zone III

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2173, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 183);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2173, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 183);
    ASSERT_EQ(
        crs_string,
        "+proj=sterea +lat_0=53.58333333333334 +lon_0=17.00833333333333 +k=0.9998 +x_0=3501000 "
        "+y_0=5999000 +ellps=krass +towgs84=33.4,-146.6,-76.3,-0.359,-0.053,0.844,-0.84 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2174)
{
    // Pulkovo 1942(58) / Poland zone IV

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2174, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 183);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2174, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 183);
    ASSERT_EQ(
        crs_string,
        "+proj=sterea +lat_0=51.67083333333333 +lon_0=16.67222222222222 +k=0.9998 +x_0=3703000 "
        "+y_0=5627000 +ellps=krass +towgs84=33.4,-146.6,-76.3,-0.359,-0.053,0.844,-0.84 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2175)
{
    // Pulkovo 1942(58) / Poland zone V

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2175, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 168);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2175, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 168);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=18.95833333333333 +k=0.999983 +x_0=237000 +y_0=-4700000 "
        "+ellps=krass +towgs84=33.4,-146.6,-76.3,-0.359,-0.053,0.844,-0.84 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2176)
{
    // ETRS89 / Poland CS2000 zone 5

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2176, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 117);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2176, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 117);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=15 +k=0.999923 +x_0=5500000 +y_0=0 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2177)
{
    // ETRS89 / Poland CS2000 zone 6

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2177, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 117);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2177, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 117);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=18 +k=0.999923 +x_0=6500000 +y_0=0 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2178)
{
    // ETRS89 / Poland CS2000 zone 7

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2178, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 117);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2178, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 117);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=21 +k=0.999923 +x_0=7500000 +y_0=0 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2179)
{
    // ETRS89 / Poland CS2000 zone 8

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2179, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 117);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2179, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 117);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=24 +k=0.999923 +x_0=8500000 +y_0=0 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2180)
{
    // ETRS89 / Poland CS92

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2180, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 121);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2180, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 121);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=19 +k=0.9993 +x_0=500000 +y_0=-5300000 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2188)
{
    // Azores Occidental 1939 / UTM zone 25N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2188, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 79);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2188, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 79);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=25 +ellps=intl +towgs84=-425,-169,81,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2189)
{
    // Azores Central 1948 / UTM zone 26N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2189, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 79);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2189, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 79);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=26 +ellps=intl +towgs84=-104,167,-38,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2190)
{
    // Azores Oriental 1940 / UTM zone 26N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2190, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 78);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2190, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 78);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=26 +ellps=intl +towgs84=-203,141,53,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2191)
{
    // Madeira 1936 / UTM zone 28N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2191, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 49);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2191, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 49);
    ASSERT_EQ(crs_string, "+proj=utm +zone=28 +ellps=intl +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2192)
{
    // ED50 / France EuroLambert

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2192, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 160);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2192, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 160);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=46.8 +lat_0=46.8 +lon_0=2.337229166666667 +k_0=0.99987742 +x_0=600000 "
        "+y_0=2200000 +ellps=intl +towgs84=-87,-98,-121,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2193)
{
    // NZGD2000 / New Zealand Transverse Mercator 2000

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2193, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 123);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2193, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 123);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=173 +k=0.9996 +x_0=1600000 +y_0=10000000 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2194)
{
    // American Samoa 1962 / American Samoa Lambert

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2194, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 177);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2194, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 177);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=-14.26666666666667 +lat_0=-14.26666666666667 +lon_0=-170 +k_0=1 "
        "+x_0=152400.3048006096 +y_0=0 +ellps=clrk66 +towgs84=-115,118,426,0,0,0,0 +units=us-ft "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2195)
{
    // NAD83(HARN) / UTM zone 2S

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2195, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 79);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2195, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 79);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=2 +south +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2196)
{
    // ETRS89 / Kp2000 Jutland

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2196, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 116);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2196, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 116);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=9.5 +k=0.99995 +x_0=200000 +y_0=0 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2197)
{
    // ETRS89 / Kp2000 Zealand

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2197, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 115);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2197, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 115);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=12 +k=0.99995 +x_0=500000 +y_0=0 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2198)
{
    // ETRS89 / Kp2000 Bornholm

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2198, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 109);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2198, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 109);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=15 +k=1 +x_0=900000 +y_0=0 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2199)
{
    // Albanian 1987 / Gauss Kruger zone 4

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2199, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 87);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2199, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 87);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=21 +k=1 +x_0=4500000 +y_0=0 +ellps=krass +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2200)
{
    // ATS77 / New Brunswick Stereographic (ATS77)

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2200, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 124);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2200, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 124);
    ASSERT_EQ(
        crs_string,
        "+proj=sterea +lat_0=46.5 +lon_0=-66.5 +k=0.999912 +x_0=300000 +y_0=800000 +a=6378135 "
        "+b=6356750.304921594 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2201)
{
    // REGVEN / UTM zone 18N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2201, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 73);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2201, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 73);
    ASSERT_EQ(
        crs_string, "+proj=utm +zone=18 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2202)
{
    // REGVEN / UTM zone 19N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2202, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 73);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2202, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 73);
    ASSERT_EQ(
        crs_string, "+proj=utm +zone=19 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2203)
{
    // REGVEN / UTM zone 20N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2203, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 73);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2203, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 73);
    ASSERT_EQ(
        crs_string, "+proj=utm +zone=20 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2204)
{
    // NAD27 / Tennessee

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2204, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 165);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2204, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 165);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=35.25 +lat_2=36.41666666666666 +lat_0=34.66666666666666 +lon_0=-86 "
        "+x_0=609601.2192024384 +y_0=30480.06096012192 +datum=NAD27 +units=us-ft +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2205)
{
    // NAD83 / Kentucky North

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2205, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 136);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2205, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 136);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=37.96666666666667 +lat_2=38.96666666666667 +lat_0=37.5 +lon_0=-84.25 "
        "+x_0=500000 +y_0=0 +datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2206)
{
    // ED50 / 3-degree Gauss-Kruger zone 9

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2206, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 116);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2206, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 116);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=27 +k=1 +x_0=9500000 +y_0=0 +ellps=intl "
        "+towgs84=-87,-98,-121,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2207)
{
    // ED50 / 3-degree Gauss-Kruger zone 10

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2207, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 117);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2207, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 117);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=30 +k=1 +x_0=10500000 +y_0=0 +ellps=intl "
        "+towgs84=-87,-98,-121,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2208)
{
    // ED50 / 3-degree Gauss-Kruger zone 11

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2208, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 117);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2208, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 117);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=33 +k=1 +x_0=11500000 +y_0=0 +ellps=intl "
        "+towgs84=-87,-98,-121,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2209)
{
    // ED50 / 3-degree Gauss-Kruger zone 12

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2209, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 117);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2209, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 117);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=36 +k=1 +x_0=12500000 +y_0=0 +ellps=intl "
        "+towgs84=-87,-98,-121,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2210)
{
    // ED50 / 3-degree Gauss-Kruger zone 13

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2210, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 117);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2210, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 117);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=39 +k=1 +x_0=13500000 +y_0=0 +ellps=intl "
        "+towgs84=-87,-98,-121,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2211)
{
    // ED50 / 3-degree Gauss-Kruger zone 14

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2211, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 117);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2211, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 117);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=42 +k=1 +x_0=14500000 +y_0=0 +ellps=intl "
        "+towgs84=-87,-98,-121,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2212)
{
    // ED50 / 3-degree Gauss-Kruger zone 15

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2212, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 117);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2212, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 117);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=45 +k=1 +x_0=15500000 +y_0=0 +ellps=intl "
        "+towgs84=-87,-98,-121,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2213)
{
    // ETRS89 / TM 30 NE

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2213, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 114);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2213, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 114);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=30 +k=0.9996 +x_0=500000 +y_0=0 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2214)
{
    // Douala 1948 / AOF west

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2214, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 135);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2214, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 135);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=10.5 +k=0.999 +x_0=1000000 +y_0=1000000 +ellps=intl "
        "+towgs84=-206.1,-174.7,-87.7,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2215)
{
    // Manoca 1962 / UTM zone 32N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2215, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 97);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2215, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 97);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=32 +a=6378249.2 +b=6356515 +towgs84=-70.9,-151.8,-41.4,0,0,0,0 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2216)
{
    // Qornoq 1927 / UTM zone 22N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2216, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 79);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2216, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 79);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=22 +ellps=intl +towgs84=164,138,-189,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2217)
{
    // Qornoq 1927 / UTM zone 23N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2217, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 79);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2217, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 79);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=23 +ellps=intl +towgs84=164,138,-189,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2219)
{
    // ATS77 / UTM zone 19N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2219, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 69);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2219, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 69);
    ASSERT_EQ(crs_string, "+proj=utm +zone=19 +a=6378135 +b=6356750.304921594 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2220)
{
    // ATS77 / UTM zone 20N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2220, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 69);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2220, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 69);
    ASSERT_EQ(crs_string, "+proj=utm +zone=20 +a=6378135 +b=6356750.304921594 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2222)
{
    // NAD83 / Arizona East (ft)

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2222, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 109);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2222, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 109);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=31 +lon_0=-110.1666666666667 +k=0.9999 +x_0=213360 +y_0=0 +datum=NAD83 "
        "+units=ft +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2223)
{
    // NAD83 / Arizona Central (ft)

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2223, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 109);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2223, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 109);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=31 +lon_0=-111.9166666666667 +k=0.9999 +x_0=213360 +y_0=0 +datum=NAD83 "
        "+units=ft +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2224)
{
    // NAD83 / Arizona West (ft)

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2224, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 103);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2224, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 103);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=31 +lon_0=-113.75 +k=0.999933333 +x_0=213360 +y_0=0 +datum=NAD83 "
        "+units=ft +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2225)
{
    // NAD83 / California zone 1 (ftUS)

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2225, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 161);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2225, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 161);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=41.66666666666666 +lat_2=40 +lat_0=39.33333333333334 +lon_0=-122 "
        "+x_0=2000000.0001016 +y_0=500000.0001016001 +datum=NAD83 +units=us-ft +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2226)
{
    // NAD83 / California zone 2 (ftUS)

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2226, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 176);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2226, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 176);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=39.83333333333334 +lat_2=38.33333333333334 +lat_0=37.66666666666666 "
        "+lon_0=-122 +x_0=2000000.0001016 +y_0=500000.0001016001 +datum=NAD83 +units=us-ft "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2227)
{
    // NAD83 / California zone 3 (ftUS)

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2227, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 165);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2227, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 165);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=38.43333333333333 +lat_2=37.06666666666667 +lat_0=36.5 +lon_0=-120.5 "
        "+x_0=2000000.0001016 +y_0=500000.0001016001 +datum=NAD83 +units=us-ft +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2228)
{
    // NAD83 / California zone 4 (ftUS)

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2228, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 149);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2228, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 149);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=37.25 +lat_2=36 +lat_0=35.33333333333334 +lon_0=-119 "
        "+x_0=2000000.0001016 +y_0=500000.0001016001 +datum=NAD83 +units=us-ft +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2229)
{
    // NAD83 / California zone 5 (ftUS)

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2229, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 163);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2229, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 163);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=35.46666666666667 +lat_2=34.03333333333333 +lat_0=33.5 +lon_0=-118 "
        "+x_0=2000000.0001016 +y_0=500000.0001016001 +datum=NAD83 +units=us-ft +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2230)
{
    // NAD83 / California zone 6 (ftUS)

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2230, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 179);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2230, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 179);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=33.88333333333333 +lat_2=32.78333333333333 +lat_0=32.16666666666666 "
        "+lon_0=-116.25 +x_0=2000000.0001016 +y_0=500000.0001016001 +datum=NAD83 +units=us-ft "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2231)
{
    // NAD83 / Colorado North (ftUS)

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2231, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 180);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2231, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 180);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=40.78333333333333 +lat_2=39.71666666666667 +lat_0=39.33333333333334 "
        "+lon_0=-105.5 +x_0=914401.8288036576 +y_0=304800.6096012192 +datum=NAD83 +units=us-ft "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2232)
{
    // NAD83 / Colorado Central (ftUS)

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2232, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 156);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2232, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 156);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=39.75 +lat_2=38.45 +lat_0=37.83333333333334 +lon_0=-105.5 "
        "+x_0=914401.8288036576 +y_0=304800.6096012192 +datum=NAD83 +units=us-ft +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2233)
{
    // NAD83 / Colorado South (ftUS)

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2233, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 180);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2233, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 180);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=38.43333333333333 +lat_2=37.23333333333333 +lat_0=36.66666666666666 "
        "+lon_0=-105.5 +x_0=914401.8288036576 +y_0=304800.6096012192 +datum=NAD83 +units=us-ft "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2234)
{
    // NAD83 / Connecticut (ftUS)

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2234, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 167);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2234, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 167);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=41.86666666666667 +lat_2=41.2 +lat_0=40.83333333333334 +lon_0=-72.75 "
        "+x_0=304800.6096012192 +y_0=152400.3048006096 +datum=NAD83 +units=us-ft +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2235)
{
    // NAD83 / Delaware (ftUS)

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2235, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 125);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2235, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 125);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=38 +lon_0=-75.41666666666667 +k=0.999995 +x_0=200000.0001016002 +y_0=0 "
        "+datum=NAD83 +units=us-ft +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2236)
{
    // NAD83 / Florida East (ftUS)

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2236, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 128);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2236, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 128);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=24.33333333333333 +lon_0=-81 +k=0.999941177 +x_0=200000.0001016002 "
        "+y_0=0 +datum=NAD83 +units=us-ft +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2237)
{
    // NAD83 / Florida West (ftUS)

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2237, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 128);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2237, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 128);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=24.33333333333333 +lon_0=-82 +k=0.999941177 +x_0=200000.0001016002 "
        "+y_0=0 +datum=NAD83 +units=us-ft +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2238)
{
    // NAD83 / Florida North (ftUS)

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2238, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 125);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2238, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 125);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=30.75 +lat_2=29.58333333333333 +lat_0=29 +lon_0=-84.5 +x_0=600000 +y_0=0 "
        "+datum=NAD83 +units=us-ft +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2239)
{
    // NAD83 / Georgia East (ftUS)

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2239, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 123);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2239, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 123);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=30 +lon_0=-82.16666666666667 +k=0.9999 +x_0=200000.0001016002 +y_0=0 "
        "+datum=NAD83 +units=us-ft +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2240)
{
    // NAD83 / Georgia West (ftUS)

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2240, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 123);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2240, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 123);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=30 +lon_0=-84.16666666666667 +k=0.9999 +x_0=699999.9998983998 +y_0=0 "
        "+datum=NAD83 +units=us-ft +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2241)
{
    // NAD83 / Idaho East (ftUS)

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2241, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 150);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2241, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 150);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=41.66666666666666 +lon_0=-112.1666666666667 +k=0.9999473679999999 "
        "+x_0=200000.0001016002 +y_0=0 +datum=NAD83 +units=us-ft +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2242)
{
    // NAD83 / Idaho Central (ftUS)

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2242, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 136);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2242, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 136);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=41.66666666666666 +lon_0=-114 +k=0.9999473679999999 "
        "+x_0=500000.0001016001 +y_0=0 +datum=NAD83 +units=us-ft +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2243)
{
    // NAD83 / Idaho West (ftUS)

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2243, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 132);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2243, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 132);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=41.66666666666666 +lon_0=-115.75 +k=0.999933333 +x_0=800000.0001016001 "
        "+y_0=0 +datum=NAD83 +units=us-ft +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2244)
{
    // NAD83 / Indiana East (ftUS)

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2244, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 146);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2244, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 146);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=37.5 +lon_0=-85.66666666666667 +k=0.999966667 +x_0=99999.99989839978 "
        "+y_0=249364.9987299975 +datum=NAD83 +units=us-ft +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2245)
{
    // NAD83 / Indiana West (ftUS)

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2245, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 135);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2245, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 135);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=37.5 +lon_0=-87.08333333333333 +k=0.999966667 +x_0=900000 "
        "+y_0=249364.9987299975 +datum=NAD83 +units=us-ft +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2246)
{
    // NAD83 / Kentucky North (ftUS)

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2246, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 151);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2246, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 151);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=37.96666666666667 +lat_2=38.96666666666667 +lat_0=37.5 +lon_0=-84.25 "
        "+x_0=500000.0001016001 +y_0=0 +datum=NAD83 +units=us-ft +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_2247)
{
    // NAD83 / Kentucky South (ftUS)

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 2247, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 180);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 2247, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 180);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=37.93333333333333 +lat_2=36.73333333333333 +lat_0=36.33333333333334 "
        "+lon_0=-85.75 +x_0=500000.0001016001 +y_0=500000.0001016001 +datum=NAD83 +units=us-ft "
        "+no_defs ");
}
