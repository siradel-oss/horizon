// SPDX-FileCopyrightText: Copyright 2021 Siradel
// SPDX-License-Identifier: MIT

#include "../crs_common.h"

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_22285)
{
    // Cape / Lo25

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 22285, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 146);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 22285, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 146);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=25 +k=1 +x_0=0 +y_0=0 +axis=wsu +a=6378249.145 "
        "+b=6356514.966398753 +towgs84=-136,-108,-292,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_22287)
{
    // Cape / Lo27

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 22287, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 146);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 22287, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 146);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=27 +k=1 +x_0=0 +y_0=0 +axis=wsu +a=6378249.145 "
        "+b=6356514.966398753 +towgs84=-136,-108,-292,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_22289)
{
    // Cape / Lo29

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 22289, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 146);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 22289, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 146);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=29 +k=1 +x_0=0 +y_0=0 +axis=wsu +a=6378249.145 "
        "+b=6356514.966398753 +towgs84=-136,-108,-292,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_22291)
{
    // Cape / Lo31

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 22291, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 146);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 22291, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 146);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=31 +k=1 +x_0=0 +y_0=0 +axis=wsu +a=6378249.145 "
        "+b=6356514.966398753 +towgs84=-136,-108,-292,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_22293)
{
    // Cape / Lo33

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 22293, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 146);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 22293, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 146);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=33 +k=1 +x_0=0 +y_0=0 +axis=wsu +a=6378249.145 "
        "+b=6356514.966398753 +towgs84=-136,-108,-292,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_22332)
{
    // Carthage / UTM zone 32N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 22332, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 53);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 22332, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 53);
    ASSERT_EQ(crs_string, "+proj=utm +zone=32 +datum=carthage +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_22391)
{
    // Carthage / Nord Tunisie

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 22391, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 116);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 22391, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 116);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=36 +lat_0=36 +lon_0=9.9 +k_0=0.999625544 +x_0=500000 +y_0=300000 "
        "+datum=carthage +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_22392)
{
    // Carthage / Sud Tunisie

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 22392, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 120);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 22392, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 120);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=33.3 +lat_0=33.3 +lon_0=9.9 +k_0=0.999625769 +x_0=500000 +y_0=300000 "
        "+datum=carthage +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_22521)
{
    // Corrego Alegre 1970-72 / UTM zone 21S

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 22521, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 85);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 22521, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 85);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=21 +south +ellps=intl +towgs84=-206,172,-6,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_22522)
{
    // Corrego Alegre 1970-72 / UTM zone 22S

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 22522, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 85);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 22522, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 85);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=22 +south +ellps=intl +towgs84=-206,172,-6,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_22523)
{
    // Corrego Alegre 1970-72 / UTM zone 23S

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 22523, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 85);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 22523, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 85);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=23 +south +ellps=intl +towgs84=-206,172,-6,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_22524)
{
    // Corrego Alegre 1970-72 / UTM zone 24S

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 22524, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 85);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 22524, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 85);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=24 +south +ellps=intl +towgs84=-206,172,-6,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_22525)
{
    // Corrego Alegre 1970-72 / UTM zone 25S

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 22525, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 85);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 22525, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 85);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=25 +south +ellps=intl +towgs84=-206,172,-6,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_22700)
{
    // Deir ez Zor / Levant Zone

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 22700, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 169);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 22700, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 169);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=34.65 +lat_0=34.65 +lon_0=37.35 +k_0=0.9996256 +x_0=300000 +y_0=300000 "
        "+a=6378249.2 +b=6356515 +towgs84=-190.421,8.532,238.69,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_22770)
{
    // Deir ez Zor / Syria Lambert

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 22770, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 169);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 22770, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 169);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=34.65 +lat_0=34.65 +lon_0=37.35 +k_0=0.9996256 +x_0=300000 +y_0=300000 "
        "+a=6378249.2 +b=6356515 +towgs84=-190.421,8.532,238.69,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_22780)
{
    // Deir ez Zor / Levant Stereographic

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 22780, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 146);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 22780, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 146);
    ASSERT_EQ(
        crs_string,
        "+proj=sterea +lat_0=34.2 +lon_0=39.15 +k=0.9995341 +x_0=0 +y_0=0 +a=6378249.2 +b=6356515 "
        "+towgs84=-190.421,8.532,238.69,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_22832)
{
    // Douala / UTM zone 32N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 22832, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 61);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 22832, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 61);
    ASSERT_EQ(crs_string, "+proj=utm +zone=32 +a=6378249.2 +b=6356515 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_22991)
{
    // Egypt 1907 / Blue Belt

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 22991, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 125);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 22991, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 125);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=30 +lon_0=35 +k=1 +x_0=300000 +y_0=1100000 +ellps=helmert "
        "+towgs84=-130,110,-13,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_22992)
{
    // Egypt 1907 / Red Belt

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 22992, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 124);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 22992, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 124);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=30 +lon_0=31 +k=1 +x_0=615000 +y_0=810000 +ellps=helmert "
        "+towgs84=-130,110,-13,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_22993)
{
    // Egypt 1907 / Purple Belt

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 22993, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 124);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 22993, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 124);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=30 +lon_0=27 +k=1 +x_0=700000 +y_0=200000 +ellps=helmert "
        "+towgs84=-130,110,-13,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_22994)
{
    // Egypt 1907 / Extended Purple Belt

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 22994, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 125);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 22994, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 125);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=30 +lon_0=27 +k=1 +x_0=700000 +y_0=1200000 +ellps=helmert "
        "+towgs84=-130,110,-13,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23028)
{
    // ED50 / UTM zone 28N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23028, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 79);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23028, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 79);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=28 +ellps=intl +towgs84=-87,-98,-121,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23029)
{
    // ED50 / UTM zone 29N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23029, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 79);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23029, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 79);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=29 +ellps=intl +towgs84=-87,-98,-121,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23030)
{
    // ED50 / UTM zone 30N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23030, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 79);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23030, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 79);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=30 +ellps=intl +towgs84=-87,-98,-121,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23031)
{
    // ED50 / UTM zone 31N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23031, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 79);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23031, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 79);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=31 +ellps=intl +towgs84=-87,-98,-121,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23032)
{
    // ED50 / UTM zone 32N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23032, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 79);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23032, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 79);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=32 +ellps=intl +towgs84=-87,-98,-121,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23033)
{
    // ED50 / UTM zone 33N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23033, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 79);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23033, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 79);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=33 +ellps=intl +towgs84=-87,-98,-121,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23034)
{
    // ED50 / UTM zone 34N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23034, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 79);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23034, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 79);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=34 +ellps=intl +towgs84=-87,-98,-121,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23035)
{
    // ED50 / UTM zone 35N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23035, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 79);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23035, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 79);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=35 +ellps=intl +towgs84=-87,-98,-121,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23036)
{
    // ED50 / UTM zone 36N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23036, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 79);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23036, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 79);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=36 +ellps=intl +towgs84=-87,-98,-121,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23037)
{
    // ED50 / UTM zone 37N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23037, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 79);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23037, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 79);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=37 +ellps=intl +towgs84=-87,-98,-121,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23038)
{
    // ED50 / UTM zone 38N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23038, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 79);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23038, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 79);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=38 +ellps=intl +towgs84=-87,-98,-121,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23090)
{
    // ED50 / TM 0 N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23090, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 119);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23090, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 119);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=0 +k=0.9996 +x_0=500000 +y_0=0 +ellps=intl "
        "+towgs84=-87,-98,-121,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23095)
{
    // ED50 / TM 5 NE

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23095, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 119);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23095, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 119);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=5 +k=0.9996 +x_0=500000 +y_0=0 +ellps=intl "
        "+towgs84=-87,-98,-121,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23239)
{
    // Fahud / UTM zone 39N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23239, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 80);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23239, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 80);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=39 +ellps=clrk80 +towgs84=-346,-1,224,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23240)
{
    // Fahud / UTM zone 40N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23240, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 80);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23240, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 80);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=40 +ellps=clrk80 +towgs84=-346,-1,224,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23433)
{
    // Garoua / UTM zone 33N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23433, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 61);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23433, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 61);
    ASSERT_EQ(crs_string, "+proj=utm +zone=33 +a=6378249.2 +b=6356515 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23700)
{
    // HD72 / EOV

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23700, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 167);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23700, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 167);
    ASSERT_EQ(
        crs_string,
        "+proj=somerc +lat_0=47.14439372222222 +lon_0=19.04857177777778 +k_0=0.99993 +x_0=650000 "
        "+y_0=200000 +ellps=GRS67 +towgs84=52.17,-71.82,-14.9,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23830)
{
    // DGN95 / Indonesia TM-3 zone 46.2

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23830, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 122);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23830, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 122);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=94.5 +k=0.9999 +x_0=200000 +y_0=1500000 +ellps=WGS84 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23831)
{
    // DGN95 / Indonesia TM-3 zone 47.1

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23831, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 122);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23831, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 122);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=97.5 +k=0.9999 +x_0=200000 +y_0=1500000 +ellps=WGS84 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23832)
{
    // DGN95 / Indonesia TM-3 zone 47.2

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23832, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 123);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23832, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 123);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=100.5 +k=0.9999 +x_0=200000 +y_0=1500000 +ellps=WGS84 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23833)
{
    // DGN95 / Indonesia TM-3 zone 48.1

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23833, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 123);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23833, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 123);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=103.5 +k=0.9999 +x_0=200000 +y_0=1500000 +ellps=WGS84 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23834)
{
    // DGN95 / Indonesia TM-3 zone 48.2

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23834, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 123);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23834, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 123);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=106.5 +k=0.9999 +x_0=200000 +y_0=1500000 +ellps=WGS84 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23835)
{
    // DGN95 / Indonesia TM-3 zone 49.1

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23835, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 123);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23835, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 123);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=109.5 +k=0.9999 +x_0=200000 +y_0=1500000 +ellps=WGS84 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23836)
{
    // DGN95 / Indonesia TM-3 zone 49.2

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23836, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 123);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23836, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 123);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=112.5 +k=0.9999 +x_0=200000 +y_0=1500000 +ellps=WGS84 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23837)
{
    // DGN95 / Indonesia TM-3 zone 50.1

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23837, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 123);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23837, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 123);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=115.5 +k=0.9999 +x_0=200000 +y_0=1500000 +ellps=WGS84 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23838)
{
    // DGN95 / Indonesia TM-3 zone 50.2

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23838, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 123);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23838, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 123);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=118.5 +k=0.9999 +x_0=200000 +y_0=1500000 +ellps=WGS84 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23839)
{
    // DGN95 / Indonesia TM-3 zone 51.1

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23839, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 123);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23839, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 123);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=121.5 +k=0.9999 +x_0=200000 +y_0=1500000 +ellps=WGS84 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23840)
{
    // DGN95 / Indonesia TM-3 zone 51.2

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23840, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 123);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23840, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 123);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=124.5 +k=0.9999 +x_0=200000 +y_0=1500000 +ellps=WGS84 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23841)
{
    // DGN95 / Indonesia TM-3 zone 52.1

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23841, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 123);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23841, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 123);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=127.5 +k=0.9999 +x_0=200000 +y_0=1500000 +ellps=WGS84 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23842)
{
    // DGN95 / Indonesia TM-3 zone 52.2

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23842, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 123);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23842, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 123);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=130.5 +k=0.9999 +x_0=200000 +y_0=1500000 +ellps=WGS84 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23843)
{
    // DGN95 / Indonesia TM-3 zone 53.1

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23843, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 123);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23843, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 123);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=133.5 +k=0.9999 +x_0=200000 +y_0=1500000 +ellps=WGS84 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23844)
{
    // DGN95 / Indonesia TM-3 zone 53.2

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23844, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 123);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23844, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 123);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=136.5 +k=0.9999 +x_0=200000 +y_0=1500000 +ellps=WGS84 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23845)
{
    // DGN95 / Indonesia TM-3 zone 54.1

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23845, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 123);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23845, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 123);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=139.5 +k=0.9999 +x_0=200000 +y_0=1500000 +ellps=WGS84 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23846)
{
    // ID74 / UTM zone 46N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23846, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 95);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23846, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 95);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=46 +a=6378160 +b=6356774.50408554 +towgs84=-24,-15,5,0,0,0,0 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23847)
{
    // ID74 / UTM zone 47N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23847, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 95);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23847, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 95);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=47 +a=6378160 +b=6356774.50408554 +towgs84=-24,-15,5,0,0,0,0 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23848)
{
    // ID74 / UTM zone 48N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23848, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 95);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23848, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 95);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=48 +a=6378160 +b=6356774.50408554 +towgs84=-24,-15,5,0,0,0,0 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23849)
{
    // ID74 / UTM zone 49N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23849, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 95);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23849, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 95);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=49 +a=6378160 +b=6356774.50408554 +towgs84=-24,-15,5,0,0,0,0 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23850)
{
    // ID74 / UTM zone 50N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23850, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 95);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23850, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 95);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=50 +a=6378160 +b=6356774.50408554 +towgs84=-24,-15,5,0,0,0,0 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23851)
{
    // ID74 / UTM zone 51N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23851, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 95);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23851, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 95);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=51 +a=6378160 +b=6356774.50408554 +towgs84=-24,-15,5,0,0,0,0 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23852)
{
    // ID74 / UTM zone 52N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23852, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 95);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23852, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 95);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=52 +a=6378160 +b=6356774.50408554 +towgs84=-24,-15,5,0,0,0,0 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23853)
{
    // ID74 / UTM zone 53N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23853, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 95);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23853, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 95);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=53 +a=6378160 +b=6356774.50408554 +towgs84=-24,-15,5,0,0,0,0 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23866)
{
    // DGN95 / UTM zone 46N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23866, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 73);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23866, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 73);
    ASSERT_EQ(
        crs_string, "+proj=utm +zone=46 +ellps=WGS84 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23867)
{
    // DGN95 / UTM zone 47N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23867, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 73);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23867, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 73);
    ASSERT_EQ(
        crs_string, "+proj=utm +zone=47 +ellps=WGS84 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23868)
{
    // DGN95 / UTM zone 48N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23868, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 73);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23868, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 73);
    ASSERT_EQ(
        crs_string, "+proj=utm +zone=48 +ellps=WGS84 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23869)
{
    // DGN95 / UTM zone 49N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23869, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 73);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23869, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 73);
    ASSERT_EQ(
        crs_string, "+proj=utm +zone=49 +ellps=WGS84 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23870)
{
    // DGN95 / UTM zone 50N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23870, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 73);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23870, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 73);
    ASSERT_EQ(
        crs_string, "+proj=utm +zone=50 +ellps=WGS84 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23871)
{
    // DGN95 / UTM zone 51N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23871, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 73);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23871, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 73);
    ASSERT_EQ(
        crs_string, "+proj=utm +zone=51 +ellps=WGS84 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23872)
{
    // DGN95 / UTM zone 52N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23872, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 73);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23872, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 73);
    ASSERT_EQ(
        crs_string, "+proj=utm +zone=52 +ellps=WGS84 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23877)
{
    // DGN95 / UTM zone 47S

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23877, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 80);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23877, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 80);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=47 +south +ellps=WGS84 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23878)
{
    // DGN95 / UTM zone 48S

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23878, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 80);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23878, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 80);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=48 +south +ellps=WGS84 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23879)
{
    // DGN95 / UTM zone 49S

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23879, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 80);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23879, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 80);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=49 +south +ellps=WGS84 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23880)
{
    // DGN95 / UTM zone 50S

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23880, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 80);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23880, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 80);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=50 +south +ellps=WGS84 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23881)
{
    // DGN95 / UTM zone 51S

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23881, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 80);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23881, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 80);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=51 +south +ellps=WGS84 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23882)
{
    // DGN95 / UTM zone 52S

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23882, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 80);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23882, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 80);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=52 +south +ellps=WGS84 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23883)
{
    // DGN95 / UTM zone 53S

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23883, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 80);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23883, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 80);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=53 +south +ellps=WGS84 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23884)
{
    // DGN95 / UTM zone 54S

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23884, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 80);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23884, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 80);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=54 +south +ellps=WGS84 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23886)
{
    // ID74 / UTM zone 46S

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23886, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 102);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23886, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 102);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=46 +south +a=6378160 +b=6356774.50408554 +towgs84=-24,-15,5,0,0,0,0 "
        "+units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23887)
{
    // ID74 / UTM zone 47S

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23887, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 102);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23887, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 102);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=47 +south +a=6378160 +b=6356774.50408554 +towgs84=-24,-15,5,0,0,0,0 "
        "+units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23888)
{
    // ID74 / UTM zone 48S

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23888, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 102);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23888, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 102);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=48 +south +a=6378160 +b=6356774.50408554 +towgs84=-24,-15,5,0,0,0,0 "
        "+units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23889)
{
    // ID74 / UTM zone 49S

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23889, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 102);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23889, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 102);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=49 +south +a=6378160 +b=6356774.50408554 +towgs84=-24,-15,5,0,0,0,0 "
        "+units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23890)
{
    // ID74 / UTM zone 50S

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23890, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 102);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23890, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 102);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=50 +south +a=6378160 +b=6356774.50408554 +towgs84=-24,-15,5,0,0,0,0 "
        "+units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23891)
{
    // ID74 / UTM zone 51S

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23891, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 102);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23891, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 102);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=51 +south +a=6378160 +b=6356774.50408554 +towgs84=-24,-15,5,0,0,0,0 "
        "+units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23892)
{
    // ID74 / UTM zone 52S

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23892, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 102);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23892, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 102);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=52 +south +a=6378160 +b=6356774.50408554 +towgs84=-24,-15,5,0,0,0,0 "
        "+units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23893)
{
    // ID74 / UTM zone 53S

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23893, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 102);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23893, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 102);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=53 +south +a=6378160 +b=6356774.50408554 +towgs84=-24,-15,5,0,0,0,0 "
        "+units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23894)
{
    // ID74 / UTM zone 54S

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23894, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 102);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23894, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 102);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=54 +south +a=6378160 +b=6356774.50408554 +towgs84=-24,-15,5,0,0,0,0 "
        "+units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23946)
{
    // Indian 1954 / UTM zone 46N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23946, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 101);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23946, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 101);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=46 +a=6377276.345 +b=6356075.41314024 +towgs84=217,823,299,0,0,0,0 "
        "+units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23947)
{
    // Indian 1954 / UTM zone 47N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23947, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 101);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23947, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 101);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=47 +a=6377276.345 +b=6356075.41314024 +towgs84=217,823,299,0,0,0,0 "
        "+units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_23948)
{
    // Indian 1954 / UTM zone 48N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 23948, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 101);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 23948, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 101);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=48 +a=6377276.345 +b=6356075.41314024 +towgs84=217,823,299,0,0,0,0 "
        "+units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_24047)
{
    // Indian 1975 / UTM zone 47N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 24047, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 101);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 24047, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 101);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=47 +a=6377276.345 +b=6356075.41314024 +towgs84=210,814,289,0,0,0,0 "
        "+units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_24048)
{
    // Indian 1975 / UTM zone 48N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 24048, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 101);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 24048, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 101);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=48 +a=6377276.345 +b=6356075.41314024 +towgs84=210,814,289,0,0,0,0 "
        "+units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_24100)
{
    // Jamaica 1875 / Jamaica (Old Grid)

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 24100, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 158);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 24100, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 158);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=18 +lat_0=18 +lon_0=-77 +k_0=1 +x_0=167638.49597 +y_0=121918.90616 "
        "+a=6378249.144808011 +b=6356514.966204134 +to_meter=0.3047972654 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_24200)
{
    // JAD69 / Jamaica National Grid

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 24200, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 134);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 24200, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 134);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=18 +lat_0=18 +lon_0=-77 +k_0=1 +x_0=250000 +y_0=150000 +ellps=clrk66 "
        "+towgs84=70,207,389.5,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_24305)
{
    // Kalianpur 1937 / UTM zone 45N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 24305, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 101);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 24305, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 101);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=45 +a=6377276.345 +b=6356075.41314024 +towgs84=282,726,254,0,0,0,0 "
        "+units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_24306)
{
    // Kalianpur 1937 / UTM zone 46N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 24306, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 101);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 24306, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 101);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=46 +a=6377276.345 +b=6356075.41314024 +towgs84=282,726,254,0,0,0,0 "
        "+units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_24311)
{
    // Kalianpur 1962 / UTM zone 41N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 24311, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 102);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 24311, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 102);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=41 +a=6377301.243 +b=6356100.230165384 +towgs84=283,682,231,0,0,0,0 "
        "+units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_24312)
{
    // Kalianpur 1962 / UTM zone 42N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 24312, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 102);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 24312, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 102);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=42 +a=6377301.243 +b=6356100.230165384 +towgs84=283,682,231,0,0,0,0 "
        "+units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_24313)
{
    // Kalianpur 1962 / UTM zone 43N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 24313, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 102);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 24313, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 102);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=43 +a=6377301.243 +b=6356100.230165384 +towgs84=283,682,231,0,0,0,0 "
        "+units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_24342)
{
    // Kalianpur 1975 / UTM zone 42N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 24342, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 102);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 24342, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 102);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=42 +a=6377299.151 +b=6356098.145120132 +towgs84=295,736,257,0,0,0,0 "
        "+units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_24343)
{
    // Kalianpur 1975 / UTM zone 43N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 24343, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 102);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 24343, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 102);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=43 +a=6377299.151 +b=6356098.145120132 +towgs84=295,736,257,0,0,0,0 "
        "+units=m +no_defs ");
}
