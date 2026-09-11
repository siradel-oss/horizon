// SPDX-FileCopyrightText: Copyright 2021 Siradel
// SPDX-License-Identifier: MIT

#include "../crs_common.h"

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4645)
{
    // RGNC 1991

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4645, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 58);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4645, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 58);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=intl +towgs84=0,0,0,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4646)
{
    // Grand Comoros

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4646, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 66);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4646, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 66);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=intl +towgs84=-963,510,-359,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4657)
{
    // Reykjavik 1900

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4657, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 80);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4657, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 80);
    ASSERT_EQ(
        crs_string,
        "+proj=longlat +a=6377019.27 +b=6355762.5391 +towgs84=-28,199,5,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4658)
{
    // Hjorsey 1955

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4658, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 63);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4658, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 63);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=intl +towgs84=-73,46,-86,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4659)
{
    // ISN93

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4659, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 59);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4659, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 59);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4660)
{
    // Helle 1954

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4660, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 102);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4660, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 102);
    ASSERT_EQ(
        crs_string,
        "+proj=longlat +ellps=intl "
        "+towgs84=982.609,552.753,-540.873,6.68163,-31.6115,-19.8482,16.805 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4661)
{
    // LKS92

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4661, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 59);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4661, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 59);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4662)
{
    // IGN72 Grande Terre

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4662, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 73);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4662, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 73);
    ASSERT_EQ(
        crs_string, "+proj=longlat +ellps=intl +towgs84=-11.64,-348.6,291.98,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4663)
{
    // Porto Santo 1995

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4663, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 78);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4663, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 78);
    ASSERT_EQ(
        crs_string,
        "+proj=longlat +ellps=intl +towgs84=-502.862,-247.438,312.724,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4664)
{
    // Azores Oriental 1995

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4664, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 76);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4664, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 76);
    ASSERT_EQ(
        crs_string, "+proj=longlat +ellps=intl +towgs84=-204.619,140.176,55.226,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4665)
{
    // Azores Central 1995

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4665, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 77);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4665, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 77);
    ASSERT_EQ(
        crs_string,
        "+proj=longlat +ellps=intl +towgs84=-106.226,166.366,-37.893,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4666)
{
    // Lisbon 1890

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4666, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 79);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4666, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 79);
    ASSERT_EQ(
        crs_string,
        "+proj=longlat +ellps=bessel +towgs84=508.088,-191.042,565.223,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4667)
{
    // IKBD-92

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4667, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 59);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4667, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 59);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=WGS84 +towgs84=0,0,0,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4668)
{
    // ED79

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4668, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 65);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4668, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 65);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=intl +towgs84=-86,-98,-119,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4669)
{
    // LKS94

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4669, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 59);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4669, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 59);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4670)
{
    // IGM95

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4670, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 59);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4670, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 59);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=WGS84 +towgs84=0,0,0,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4671)
{
    // Voirol 1879

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4671, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 47);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4671, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 47);
    ASSERT_EQ(crs_string, "+proj=longlat +a=6378249.2 +b=6356515 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4672)
{
    // Chatham Islands 1971

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4672, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 64);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4672, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 64);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=intl +towgs84=175,-38,113,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4673)
{
    // Chatham Islands 1979

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4673, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 84);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4673, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 84);
    ASSERT_EQ(
        crs_string,
        "+proj=longlat +ellps=intl +towgs84=174.05,-25.49,112.57,-0,-0,0.554,0.2263 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4674)
{
    // SIRGAS 2000

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4674, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 59);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4674, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 59);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4675)
{
    // Guam 1963

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4675, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 68);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4675, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 68);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=clrk66 +towgs84=-100,-248,259,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4676)
{
    // Vientiane 1982

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4676, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 36);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4676, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 36);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=krass +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4677)
{
    // Lao 1993

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4677, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 36);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4677, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 36);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=krass +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4678)
{
    // Lao 1997

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4678, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 77);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4678, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 77);
    ASSERT_EQ(
        crs_string,
        "+proj=longlat +ellps=krass +towgs84=44.585,-131.212,-39.544,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4679)
{
    // Jouik 1961

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4679, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 75);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4679, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 75);
    ASSERT_EQ(
        crs_string, "+proj=longlat +ellps=clrk80 +towgs84=-80.01,253.26,291.19,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4680)
{
    // Nouakchott 1965

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4680, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 71);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4680, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 71);
    ASSERT_EQ(
        crs_string, "+proj=longlat +ellps=clrk80 +towgs84=124.5,-63.5,-281,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4681)
{
    // Mauritania 1999

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4681, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 37);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4681, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 37);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=clrk80 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4682)
{
    // Gulshan 303

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4682, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 93);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4682, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 93);
    ASSERT_EQ(
        crs_string,
        "+proj=longlat +a=6377276.345 +b=6356075.41314024 +towgs84=283.7,735.9,261.1,0,0,0,0 "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4683)
{
    // PRS92

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4683, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 93);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4683, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 93);
    ASSERT_EQ(
        crs_string,
        "+proj=longlat +ellps=clrk66 +towgs84=-127.62,-67.24,-47.04,-3.068,4.903,1.578,-1.06 "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4684)
{
    // Gan 1970

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4684, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 65);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4684, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 65);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=intl +towgs84=-133,-321,50,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4685)
{
    // Gandajika

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4685, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 35);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4685, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 35);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=intl +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4686)
{
    // MAGNA-SIRGAS

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4686, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 59);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4686, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 59);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4687)
{
    // RGPF

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4687, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 95);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4687, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 95);
    ASSERT_EQ(
        crs_string,
        "+proj=longlat +ellps=GRS80 +towgs84=0.072,-0.507,-0.245,-0.0183,0.0003,-0.007,-0.0093 "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4688)
{
    // Fatu Iva 72

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4688, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 101);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4688, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 101);
    ASSERT_EQ(
        crs_string,
        "+proj=longlat +ellps=intl "
        "+towgs84=347.103,1078.12,2623.92,-33.8875,70.6773,-9.3943,186.074 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4689)
{
    // IGN63 Hiva Oa

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4689, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 95);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4689, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 95);
    ASSERT_EQ(
        crs_string,
        "+proj=longlat +ellps=intl +towgs84=410.721,55.049,80.746,2.5779,2.3514,0.6664,17.3311 "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4690)
{
    // Tahiti 79

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4690, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 99);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4690, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 99);
    ASSERT_EQ(
        crs_string,
        "+proj=longlat +ellps=intl +towgs84=221.525,152.948,176.768,-2.3847,-1.3896,-0.877,11.4741 "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4691)
{
    // Moorea 87

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4691, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 99);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4691, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 99);
    ASSERT_EQ(
        crs_string,
        "+proj=longlat +ellps=intl +towgs84=215.525,149.593,176.229,-3.2624,-1.692,-1.1571,10.4773 "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4692)
{
    // Maupiti 83

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4692, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 74);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4692, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 74);
    ASSERT_EQ(
        crs_string, "+proj=longlat +ellps=intl +towgs84=217.037,86.959,23.956,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4693)
{
    // Nakhl-e Ghanem

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4693, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 66);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4693, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 66);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=WGS84 +towgs84=0,-0.15,0.68,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4694)
{
    // POSGAR 94

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4694, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 59);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4694, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 59);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=WGS84 +towgs84=0,0,0,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4695)
{
    // Katanga 1955

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4695, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 78);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4695, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 78);
    ASSERT_EQ(
        crs_string,
        "+proj=longlat +ellps=clrk66 +towgs84=-103.746,-9.614,-255.95,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4696)
{
    // Kasai 1953

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4696, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 37);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4696, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 37);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=clrk80 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4697)
{
    // IGC 1962 6th Parallel South

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4697, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 37);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4697, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 37);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=clrk80 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4698)
{
    // IGN 1962 Kerguelen

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4698, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 65);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4698, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 65);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=intl +towgs84=145,-187,103,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4699)
{
    // Le Pouce 1934

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4699, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 74);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4699, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 74);
    ASSERT_EQ(
        crs_string, "+proj=longlat +ellps=clrk80 +towgs84=-770.1,158.4,-498.2,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4700)
{
    // IGN Astro 1960

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4700, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 37);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4700, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 37);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=clrk80 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4701)
{
    // IGCB 1955

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4701, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 72);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4701, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 72);
    ASSERT_EQ(
        crs_string, "+proj=longlat +ellps=clrk80 +towgs84=-79.9,-158,-168.9,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4702)
{
    // Mauritania 1999

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4702, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 59);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4702, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 59);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4703)
{
    // Mhast 1951

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4703, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 37);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4703, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 37);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=clrk80 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4704)
{
    // Mhast (onshore)

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4704, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 35);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4704, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 35);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=intl +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4705)
{
    // Mhast (offshore)

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4705, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 35);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4705, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 35);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=intl +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4706)
{
    // Egypt Gulf of Suez S-650 TL

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4706, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 75);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4706, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 75);
    ASSERT_EQ(
        crs_string, "+proj=longlat +ellps=helmert +towgs84=-146.21,112.63,4.05,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4707)
{
    // Tern Island 1961

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4707, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 66);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4707, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 66);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=intl +towgs84=114,-116,-333,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4708)
{
    // Cocos Islands 1965

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4708, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 68);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4708, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 68);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=aust_SA +towgs84=-491,-22,435,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4709)
{
    // Iwo Jima 1945

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4709, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 64);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4709, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 64);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=intl +towgs84=145,75,-272,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4710)
{
    // Astro DOS 71

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4710, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 66);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4710, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 66);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=intl +towgs84=-320,550,-494,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4711)
{
    // Marcus Island 1952

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4711, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 65);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4711, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 65);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=intl +towgs84=124,-234,-25,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4712)
{
    // Ascension Island 1958

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4712, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 64);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4712, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 64);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=intl +towgs84=-205,107,53,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4713)
{
    // Ayabelle Lighthouse

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4713, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 67);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4713, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 67);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=clrk80 +towgs84=-79,-129,145,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4714)
{
    // Bellevue

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4714, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 66);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4714, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 66);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=intl +towgs84=-127,-769,472,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4715)
{
    // Camp Area Astro

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4715, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 66);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4715, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 66);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=intl +towgs84=-104,-129,239,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4716)
{
    // Phoenix Islands 1966

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4716, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 66);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4716, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 66);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=intl +towgs84=298,-304,-375,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4717)
{
    // Cape Canaveral

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4717, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 65);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4717, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 65);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=clrk66 +towgs84=-2,151,181,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4718)
{
    // Solomon 1968

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4718, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 66);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4718, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 66);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=intl +towgs84=252,-209,-751,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4719)
{
    // Easter Island 1967

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4719, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 64);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4719, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 64);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=intl +towgs84=211,147,111,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4720)
{
    // Fiji 1986

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4720, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 70);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4720, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 70);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=WGS72 +towgs84=0,0,4.5,0,0,0.554,0.2263 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4721)
{
    // Fiji 1956

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4721, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 77);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4721, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 77);
    ASSERT_EQ(
        crs_string,
        "+proj=longlat +ellps=intl +towgs84=265.025,384.929,-194.046,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4722)
{
    // South Georgia 1968

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4722, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 66);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4722, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 66);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=intl +towgs84=-794,119,-298,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4723)
{
    // GCGD59

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4723, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 71);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4723, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 71);
    ASSERT_EQ(
        crs_string, "+proj=longlat +ellps=clrk66 +towgs84=67.8,106.1,138.8,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4724)
{
    // Diego Garcia 1969

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4724, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 66);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4724, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 66);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=intl +towgs84=208,-435,-229,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4725)
{
    // Johnston Island 1961

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4725, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 65);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4725, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 65);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=intl +towgs84=189,-79,-202,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4726)
{
    // SIGD61

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4726, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 65);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4726, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 65);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=clrk66 +towgs84=42,124,147,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4727)
{
    // Midway 1961

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4727, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 64);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4727, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 64);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=intl +towgs84=403,-81,277,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4728)
{
    // Pico de las Nieves 1984

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4728, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 65);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4728, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 65);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=intl +towgs84=-307,-92,127,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4729)
{
    // Pitcairn 1967

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4729, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 63);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4729, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 63);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=intl +towgs84=185,165,42,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4730)
{
    // Santo 1965

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4730, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 62);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4730, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 62);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=intl +towgs84=170,42,84,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4731)
{
    // Viti Levu 1916

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4731, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 65);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4731, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 65);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=clrk80 +towgs84=51,391,-36,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4732)
{
    // Marshall Islands 1960

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4732, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 83);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4732, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 83);
    ASSERT_EQ(
        crs_string,
        "+proj=longlat +a=6378270 +b=6356794.343434343 +towgs84=102,52,-38,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4733)
{
    // Wake Island 1952

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4733, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 64);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4733, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 64);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=intl +towgs84=276,-57,149,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4734)
{
    // Tristan 1968

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4734, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 66);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4734, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 66);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=intl +towgs84=-632,438,-609,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4735)
{
    // Kusaie 1951

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4735, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 67);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4735, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 67);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=intl +towgs84=647,1777,-1124,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4736)
{
    // Deception Island

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4736, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 66);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4736, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 66);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=clrk80 +towgs84=260,12,-147,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4737)
{
    // Korea 2000

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4737, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 59);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4737, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 59);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4738)
{
    // Hong Kong 1963

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4738, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 65);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4738, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 65);
    ASSERT_EQ(crs_string, "+proj=longlat +a=6378293.645208759 +b=6356617.987679838 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4739)
{
    // Hong Kong 1963(67)

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4739, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 67);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4739, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 67);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=intl +towgs84=-156,-271,-189,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4740)
{
    // PZ-90

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4740, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 86);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4740, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 86);
    ASSERT_EQ(
        crs_string,
        "+proj=longlat +a=6378136 +b=6356751.361745712 +towgs84=0,0,1.5,-0,-0,0.076,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4741)
{
    // FD54

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4741, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 35);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4741, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 35);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=intl +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4742)
{
    // GDM2000

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4742, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 36);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4742, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 36);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=GRS80 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4743)
{
    // Karbala 1979

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4743, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 78);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4743, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 78);
    ASSERT_EQ(
        crs_string,
        "+proj=longlat +ellps=clrk80 +towgs84=70.995,-335.916,262.898,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4744)
{
    // Nahrwan 1934

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4744, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 37);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4744, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 37);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=clrk80 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4745)
{
    // RD/83

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4745, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 37);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4745, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 37);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=bessel +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4746)
{
    // PD/83

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4746, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 37);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4746, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 37);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=bessel +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4747)
{
    // GR96

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4747, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 59);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4747, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 59);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4748)
{
    // Vanua Levu 1915

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4748, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 82);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4748, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 82);
    ASSERT_EQ(
        crs_string,
        "+proj=longlat +a=6378306.3696 +b=6356571.996 +towgs84=51,391,-36,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4749)
{
    // RGNC91-93

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4749, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 59);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4749, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 59);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4750)
{
    // ST87 Ouvea

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4750, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 76);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4750, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 76);
    ASSERT_EQ(
        crs_string, "+proj=longlat +ellps=WGS84 +towgs84=-56.263,16.136,-22.856,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4751)
{
    // Kertau (RSO)

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4751, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 59);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4751, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 59);
    ASSERT_EQ(crs_string, "+proj=longlat +a=6377295.664 +b=6356094.667915204 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4752)
{
    // Viti Levu 1912

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4752, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 82);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4752, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 82);
    ASSERT_EQ(
        crs_string,
        "+proj=longlat +a=6378306.3696 +b=6356571.996 +towgs84=51,391,-36,0,0,0,0 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4753)
{
    // fk89

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4753, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 35);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4753, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 35);
    ASSERT_EQ(crs_string, "+proj=longlat +ellps=intl +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_4754)
{
    // LGD2006

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 4754, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 78);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 4754, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 78);
    ASSERT_EQ(
        crs_string,
        "+proj=longlat +ellps=intl +towgs84=-208.406,-109.878,-2.5764,0,0,0,0 +no_defs ");
}
