// SPDX-FileCopyrightText: Copyright 2021 Siradel
// SPDX-License-Identifier: MIT

#include "../crs_common.h"

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3067)
{
    // ETRS89 / TM35FIN(E,N)

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3067, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 73);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3067, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 73);
    ASSERT_EQ(
        crs_string, "+proj=utm +zone=35 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3068)
{
    // DHDN / Soldner Berlin

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3068, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 116);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3068, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 116);
    ASSERT_EQ(
        crs_string,
        "+proj=cass +lat_0=52.41864827777778 +lon_0=13.62720366666667 +x_0=40000 +y_0=10000 "
        "+datum=potsdam +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3069)
{
    // NAD27 / Wisconsin Transverse Mercator

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3069, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 99);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3069, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 99);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=-90 +k=0.9996 +x_0=500000 +y_0=-4500000 +datum=NAD27 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3070)
{
    // NAD83 / Wisconsin Transverse Mercator

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3070, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 99);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3070, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 99);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=-90 +k=0.9996 +x_0=520000 +y_0=-4480000 +datum=NAD83 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3071)
{
    // NAD83(HARN) / Wisconsin Transverse Mercator

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3071, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 122);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3071, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 122);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=-90 +k=0.9996 +x_0=520000 +y_0=-4480000 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3072)
{
    // NAD83 / Maine CS2000 East

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3072, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 113);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3072, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 113);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=43.83333333333334 +lon_0=-67.875 +k=0.99998 +x_0=700000 +y_0=0 "
        "+datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3073)
{
    // NAD83 / Maine CS2000 Central

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3073, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 98);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3073, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 98);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=43 +lon_0=-69.125 +k=0.99998 +x_0=500000 +y_0=0 +datum=NAD83 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3074)
{
    // NAD83 / Maine CS2000 West

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3074, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 113);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3074, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 113);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=42.83333333333334 +lon_0=-70.375 +k=0.99998 +x_0=300000 +y_0=0 "
        "+datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3075)
{
    // NAD83(HARN) / Maine CS2000 East

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3075, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 136);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3075, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 136);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=43.83333333333334 +lon_0=-67.875 +k=0.99998 +x_0=700000 +y_0=0 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3076)
{
    // NAD83(HARN) / Maine CS2000 Central

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3076, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 121);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3076, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 121);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=43 +lon_0=-69.125 +k=0.99998 +x_0=500000 +y_0=0 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3077)
{
    // NAD83(HARN) / Maine CS2000 West

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3077, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 136);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3077, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 136);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=42.83333333333334 +lon_0=-70.375 +k=0.99998 +x_0=300000 +y_0=0 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3078)
{
    // NAD83 / Michigan Oblique Mercator

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3078, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 166);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3078, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 166);
    ASSERT_EQ(
        crs_string,
        "+proj=omerc +lat_0=45.30916666666666 +lonc=-86 +alpha=337.25556 +k=0.9996 "
        "+x_0=2546731.496 +y_0=-4354009.816 +no_uoff +gamma=337.25556 +datum=NAD83 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3079)
{
    // NAD83(HARN) / Michigan Oblique Mercator

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3079, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 189);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3079, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 189);
    ASSERT_EQ(
        crs_string,
        "+proj=omerc +lat_0=45.30916666666666 +lonc=-86 +alpha=337.25556 +k=0.9996 "
        "+x_0=2546731.496 +y_0=-4354009.816 +no_uoff +gamma=337.25556 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3080)
{
    // NAD27 / Shackleford

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3080, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 153);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3080, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 153);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=27.41666666666667 +lat_2=34.91666666666666 +lat_0=31.16666666666667 "
        "+lon_0=-100 +x_0=914400 +y_0=914400 +datum=NAD27 +units=ft +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3081)
{
    // NAD83 / Texas State Mapping System

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3081, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 154);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3081, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 154);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=27.41666666666667 +lat_2=34.91666666666666 +lat_0=31.16666666666667 "
        "+lon_0=-100 +x_0=1000000 +y_0=1000000 +datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3082)
{
    // NAD83 / Texas Centric Lambert Conformal

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3082, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 111);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3082, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 111);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=27.5 +lat_2=35 +lat_0=18 +lon_0=-100 +x_0=1500000 +y_0=5000000 "
        "+datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3083)
{
    // NAD83 / Texas Centric Albers Equal Area

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3083, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 111);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3083, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 111);
    ASSERT_EQ(
        crs_string,
        "+proj=aea +lat_1=27.5 +lat_2=35 +lat_0=18 +lon_0=-100 +x_0=1500000 +y_0=6000000 "
        "+datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3084)
{
    // NAD83(HARN) / Texas Centric Lambert Conformal

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3084, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 134);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3084, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 134);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=27.5 +lat_2=35 +lat_0=18 +lon_0=-100 +x_0=1500000 +y_0=5000000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3085)
{
    // NAD83(HARN) / Texas Centric Albers Equal Area

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3085, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 134);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3085, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 134);
    ASSERT_EQ(
        crs_string,
        "+proj=aea +lat_1=27.5 +lat_2=35 +lat_0=18 +lon_0=-100 +x_0=1500000 +y_0=6000000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3086)
{
    // NAD83 / Florida GDL Albers

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3086, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 103);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3086, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 103);
    ASSERT_EQ(
        crs_string,
        "+proj=aea +lat_1=24 +lat_2=31.5 +lat_0=24 +lon_0=-84 +x_0=400000 +y_0=0 +datum=NAD83 "
        "+units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3087)
{
    // NAD83(HARN) / Florida GDL Albers

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3087, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 126);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3087, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 126);
    ASSERT_EQ(
        crs_string,
        "+proj=aea +lat_1=24 +lat_2=31.5 +lat_0=24 +lon_0=-84 +x_0=400000 +y_0=0 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3088)
{
    // NAD83 / Kentucky Single Zone

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3088, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 156);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3088, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 156);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=37.08333333333334 +lat_2=38.66666666666666 +lat_0=36.33333333333334 "
        "+lon_0=-85.75 +x_0=1500000 +y_0=1000000 +datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3089)
{
    // NAD83 / Kentucky Single Zone (ftUS)

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3089, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 170);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3089, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 170);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=37.08333333333334 +lat_2=38.66666666666666 +lat_0=36.33333333333334 "
        "+lon_0=-85.75 +x_0=1500000 +y_0=999999.9998983998 +datum=NAD83 +units=us-ft +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3090)
{
    // NAD83(HARN) / Kentucky Single Zone

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3090, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 179);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3090, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 179);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=37.08333333333334 +lat_2=38.66666666666666 +lat_0=36.33333333333334 "
        "+lon_0=-85.75 +x_0=1500000 +y_0=1000000 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3091)
{
    // NAD83(HARN) / Kentucky Single Zone (ftUS)

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3091, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 193);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3091, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 193);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=37.08333333333334 +lat_2=38.66666666666666 +lat_0=36.33333333333334 "
        "+lon_0=-85.75 +x_0=1500000 +y_0=999999.9998983998 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 "
        "+units=us-ft +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3092)
{
    // Tokyo / UTM zone 51N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3092, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 93);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3092, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 93);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=51 +ellps=bessel +towgs84=-146.414,507.337,680.507,0,0,0,0 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3093)
{
    // Tokyo / UTM zone 52N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3093, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 93);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3093, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 93);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=52 +ellps=bessel +towgs84=-146.414,507.337,680.507,0,0,0,0 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3094)
{
    // Tokyo / UTM zone 53N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3094, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 93);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3094, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 93);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=53 +ellps=bessel +towgs84=-146.414,507.337,680.507,0,0,0,0 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3095)
{
    // Tokyo / UTM zone 54N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3095, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 93);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3095, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 93);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=54 +ellps=bessel +towgs84=-146.414,507.337,680.507,0,0,0,0 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3096)
{
    // Tokyo / UTM zone 55N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3096, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 93);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3096, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 93);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=55 +ellps=bessel +towgs84=-146.414,507.337,680.507,0,0,0,0 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3097)
{
    // JGD2000 / UTM zone 51N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3097, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 73);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3097, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 73);
    ASSERT_EQ(
        crs_string, "+proj=utm +zone=51 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3098)
{
    // JGD2000 / UTM zone 52N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3098, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 73);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3098, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 73);
    ASSERT_EQ(
        crs_string, "+proj=utm +zone=52 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3099)
{
    // JGD2000 / UTM zone 53N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3099, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 73);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3099, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 73);
    ASSERT_EQ(
        crs_string, "+proj=utm +zone=53 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3100)
{
    // JGD2000 / UTM zone 54N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3100, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 73);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3100, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 73);
    ASSERT_EQ(
        crs_string, "+proj=utm +zone=54 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3101)
{
    // JGD2000 / UTM zone 55N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3101, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 73);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3101, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 73);
    ASSERT_EQ(
        crs_string, "+proj=utm +zone=55 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3102)
{
    // American Samoa 1962 / American Samoa Lambert

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3102, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 193);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3102, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 193);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=-14.26666666666667 +lat_0=-14.26666666666667 +lon_0=-170 +k_0=1 "
        "+x_0=152400.3048006096 +y_0=95169.31165862332 +ellps=clrk66 +towgs84=-115,118,426,0,0,0,0 "
        "+units=us-ft +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3103)
{
    // Mauritania 1999 / UTM zone 28N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3103, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 51);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3103, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 51);
    ASSERT_EQ(crs_string, "+proj=utm +zone=28 +ellps=clrk80 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3104)
{
    // Mauritania 1999 / UTM zone 29N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3104, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 51);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3104, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 51);
    ASSERT_EQ(crs_string, "+proj=utm +zone=29 +ellps=clrk80 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3105)
{
    // Mauritania 1999 / UTM zone 30N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3105, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 51);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3105, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 51);
    ASSERT_EQ(crs_string, "+proj=utm +zone=30 +ellps=clrk80 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3106)
{
    // Gulshan 303 / Bangladesh Transverse Mercator

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3106, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 148);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3106, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 148);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=90 +k=0.9996 +x_0=500000 +y_0=0 +a=6377276.345 "
        "+b=6356075.41314024 +towgs84=283.7,735.9,261.1,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3107)
{
    // GDA94 / SA Lambert

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3107, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 134);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3107, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 134);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=-28 +lat_2=-36 +lat_0=-32 +lon_0=135 +x_0=1000000 +y_0=2000000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3108)
{
    // ETRS89 / Guernsey Grid

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3108, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 138);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3108, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 138);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=49.5 +lon_0=-2.416666666666667 +k=0.999997 +x_0=47000 +y_0=50000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3109)
{
    // ETRS89 / Jersey Transverse Mercator

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3109, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 138);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3109, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 138);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=49.225 +lon_0=-2.135 +k=0.9999999000000001 +x_0=40000 +y_0=70000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3110)
{
    // AGD66 / Vicgrid66

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3110, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 171);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3110, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 171);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=-36 +lat_2=-38 +lat_0=-37 +lon_0=145 +x_0=2500000 +y_0=4500000 "
        "+ellps=aust_SA +towgs84=-117.808,-51.536,137.784,0.303,0.446,0.234,-0.29 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3111)
{
    // GDA94 / Vicgrid

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3111, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 134);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3111, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 134);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=-36 +lat_2=-38 +lat_0=-37 +lon_0=145 +x_0=2500000 +y_0=2500000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3112)
{
    // GDA94 / Geoscience Australia Lambert

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3112, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 120);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3112, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 120);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=-18 +lat_2=-36 +lat_0=0 +lon_0=134 +x_0=0 +y_0=0 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3113)
{
    // GDA94 / BCSG02

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3113, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 122);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3113, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 122);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=-28 +lon_0=153 +k=0.99999 +x_0=50000 +y_0=100000 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3114)
{
    // MAGNA-SIRGAS / Colombia Far West zone

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3114, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 148);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3114, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 148);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=4.596200416666666 +lon_0=-80.07750791666666 +k=1 +x_0=1000000 "
        "+y_0=1000000 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3115)
{
    // MAGNA-SIRGAS / Colombia West zone

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3115, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 148);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3115, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 148);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=4.596200416666666 +lon_0=-77.07750791666666 +k=1 +x_0=1000000 "
        "+y_0=1000000 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3116)
{
    // MAGNA-SIRGAS / Colombia Bogota zone

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3116, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 148);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3116, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 148);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=4.596200416666666 +lon_0=-74.07750791666666 +k=1 +x_0=1000000 "
        "+y_0=1000000 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3117)
{
    // MAGNA-SIRGAS / Colombia East Central zone

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3117, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 148);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3117, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 148);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=4.596200416666666 +lon_0=-71.07750791666666 +k=1 +x_0=1000000 "
        "+y_0=1000000 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3118)
{
    // MAGNA-SIRGAS / Colombia East zone

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3118, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 148);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3118, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 148);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=4.596200416666666 +lon_0=-68.07750791666666 +k=1 +x_0=1000000 "
        "+y_0=1000000 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3119)
{
    // Douala 1948 / AEF west

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3119, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 135);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3119, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 135);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=10.5 +k=0.999 +x_0=1000000 +y_0=1000000 +ellps=intl "
        "+towgs84=-206.1,-174.7,-87.7,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3120)
{
    // Pulkovo 1942(58) / Poland zone I

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3120, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 172);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3120, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 172);
    ASSERT_EQ(
        crs_string,
        "+proj=sterea +lat_0=50.625 +lon_0=21.08333333333333 +k=0.9998 +x_0=4637000 +y_0=5467000 "
        "+ellps=krass +towgs84=33.4,-146.6,-76.3,-0.359,-0.053,0.844,-0.84 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3121)
{
    // PRS92 / Philippines zone 1

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3121, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 150);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3121, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 150);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=117 +k=0.99995 +x_0=500000 +y_0=0 +ellps=clrk66 "
        "+towgs84=-127.62,-67.24,-47.04,-3.068,4.903,1.578,-1.06 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3122)
{
    // PRS92 / Philippines zone 2

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3122, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 150);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3122, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 150);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=119 +k=0.99995 +x_0=500000 +y_0=0 +ellps=clrk66 "
        "+towgs84=-127.62,-67.24,-47.04,-3.068,4.903,1.578,-1.06 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3123)
{
    // PRS92 / Philippines zone 3

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3123, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 150);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3123, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 150);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=121 +k=0.99995 +x_0=500000 +y_0=0 +ellps=clrk66 "
        "+towgs84=-127.62,-67.24,-47.04,-3.068,4.903,1.578,-1.06 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3124)
{
    // PRS92 / Philippines zone 4

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3124, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 150);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3124, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 150);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=123 +k=0.99995 +x_0=500000 +y_0=0 +ellps=clrk66 "
        "+towgs84=-127.62,-67.24,-47.04,-3.068,4.903,1.578,-1.06 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3125)
{
    // PRS92 / Philippines zone 5

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3125, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 150);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3125, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 150);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=125 +k=0.99995 +x_0=500000 +y_0=0 +ellps=clrk66 "
        "+towgs84=-127.62,-67.24,-47.04,-3.068,4.903,1.578,-1.06 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3126)
{
    // ETRS89 / ETRS-GK19FIN

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3126, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 109);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3126, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 109);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=19 +k=1 +x_0=500000 +y_0=0 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3127)
{
    // ETRS89 / ETRS-GK20FIN

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3127, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 109);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3127, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 109);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=20 +k=1 +x_0=500000 +y_0=0 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3128)
{
    // ETRS89 / ETRS-GK21FIN

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3128, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 109);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3128, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 109);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=21 +k=1 +x_0=500000 +y_0=0 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3129)
{
    // ETRS89 / ETRS-GK22FIN

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3129, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 109);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3129, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 109);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=22 +k=1 +x_0=500000 +y_0=0 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3130)
{
    // ETRS89 / ETRS-GK23FIN

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3130, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 109);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3130, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 109);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=23 +k=1 +x_0=500000 +y_0=0 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3131)
{
    // ETRS89 / ETRS-GK24FIN

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3131, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 109);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3131, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 109);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=24 +k=1 +x_0=500000 +y_0=0 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3132)
{
    // ETRS89 / ETRS-GK25FIN

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3132, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 109);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3132, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 109);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=25 +k=1 +x_0=500000 +y_0=0 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3133)
{
    // ETRS89 / ETRS-GK26FIN

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3133, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 109);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3133, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 109);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=26 +k=1 +x_0=500000 +y_0=0 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3134)
{
    // ETRS89 / ETRS-GK27FIN

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3134, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 109);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3134, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 109);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=27 +k=1 +x_0=500000 +y_0=0 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3135)
{
    // ETRS89 / ETRS-GK28FIN

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3135, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 109);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3135, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 109);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=28 +k=1 +x_0=500000 +y_0=0 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3136)
{
    // ETRS89 / ETRS-GK29FIN

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3136, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 109);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3136, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 109);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=29 +k=1 +x_0=500000 +y_0=0 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3137)
{
    // ETRS89 / ETRS-GK30FIN

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3137, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 109);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3137, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 109);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=30 +k=1 +x_0=500000 +y_0=0 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3138)
{
    // ETRS89 / ETRS-GK31FIN

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3138, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 109);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3138, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 109);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=31 +k=1 +x_0=500000 +y_0=0 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3140)
{
    // Viti Levu 1912 / Viti Levu Grid

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3140, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 152);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3140, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 152);
    ASSERT_EQ(
        crs_string,
        "+proj=cass +lat_0=-18 +lon_0=178 +x_0=109435.392 +y_0=141622.272 +a=6378306.3696 "
        "+b=6356571.996 +towgs84=51,391,-36,0,0,0,0 +to_meter=0.201168 +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3141)
{
    // Fiji 1956 / UTM zone 60S

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3141, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 98);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3141, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 98);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=60 +south +ellps=intl +towgs84=265.025,384.929,-194.046,0,0,0,0 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3142)
{
    // Fiji 1956 / UTM zone 1S

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3142, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 97);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3142, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 97);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=1 +south +ellps=intl +towgs84=265.025,384.929,-194.046,0,0,0,0 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3143)
{
    // Fiji 1986 / Fiji Map Grid

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3143, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 139);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3143, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 139);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=-17 +lon_0=178.75 +k=0.99985 +x_0=2000000 +y_0=4000000 +ellps=WGS72 "
        "+towgs84=0,0,4.5,0,0,0.554,0.2263 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3146)
{
    // Pulkovo 1942 / 3-degree Gauss-Kruger zone 6

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3146, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 135);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3146, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 135);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=18 +k=1 +x_0=6500000 +y_0=0 +ellps=krass "
        "+towgs84=23.92,-141.27,-80.9,-0,0.35,0.82,-0.12 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3147)
{
    // Pulkovo 1942 / 3-degree Gauss-Kruger CM 18E

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3147, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 134);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3147, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 134);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=18 +k=1 +x_0=500000 +y_0=0 +ellps=krass "
        "+towgs84=23.92,-141.27,-80.9,-0,0.35,0.82,-0.12 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3148)
{
    // Indian 1960 / UTM zone 48N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3148, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 101);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3148, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 101);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=48 +a=6377276.345 +b=6356075.41314024 +towgs84=198,881,317,0,0,0,0 "
        "+units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3149)
{
    // Indian 1960 / UTM zone 49N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3149, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 101);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3149, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 101);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=49 +a=6377276.345 +b=6356075.41314024 +towgs84=198,881,317,0,0,0,0 "
        "+units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3150)
{
    // Pulkovo 1995 / 3-degree Gauss-Kruger zone 6

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3150, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 134);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3150, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 134);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=18 +k=1 +x_0=6500000 +y_0=0 +ellps=krass "
        "+towgs84=24.47,-130.89,-81.56,-0,-0,0.13,-0.22 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3151)
{
    // Pulkovo 1995 / 3-degree Gauss-Kruger CM 18E

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3151, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 133);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3151, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 133);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=18 +k=1 +x_0=500000 +y_0=0 +ellps=krass "
        "+towgs84=24.47,-130.89,-81.56,-0,-0,0.13,-0.22 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3152)
{
    // ST74

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3152, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 141);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3152, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 141);
    ASSERT_EQ(
        crs_string,
        "+proj=tmerc +lat_0=0 +lon_0=18.05779 +k=0.99999425 +x_0=100178.1808 +y_0=-6500614.7836 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3153)
{
    // NAD83(CSRS) / BC Albers

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3153, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 128);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3153, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 128);
    ASSERT_EQ(
        crs_string,
        "+proj=aea +lat_1=50 +lat_2=58.5 +lat_0=45 +lon_0=-126 +x_0=1000000 +y_0=0 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3154)
{
    // NAD83(CSRS) / UTM zone 7N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3154, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 72);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3154, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 72);
    ASSERT_EQ(
        crs_string, "+proj=utm +zone=7 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3155)
{
    // NAD83(CSRS) / UTM zone 8N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3155, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 72);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3155, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 72);
    ASSERT_EQ(
        crs_string, "+proj=utm +zone=8 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3156)
{
    // NAD83(CSRS) / UTM zone 9N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3156, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 72);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3156, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 72);
    ASSERT_EQ(
        crs_string, "+proj=utm +zone=9 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3157)
{
    // NAD83(CSRS) / UTM zone 10N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3157, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 73);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3157, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 73);
    ASSERT_EQ(
        crs_string, "+proj=utm +zone=10 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3158)
{
    // NAD83(CSRS) / UTM zone 14N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3158, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 73);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3158, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 73);
    ASSERT_EQ(
        crs_string, "+proj=utm +zone=14 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3159)
{
    // NAD83(CSRS) / UTM zone 15N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3159, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 73);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3159, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 73);
    ASSERT_EQ(
        crs_string, "+proj=utm +zone=15 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3160)
{
    // NAD83(CSRS) / UTM zone 16N

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3160, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 73);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3160, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 73);
    ASSERT_EQ(
        crs_string, "+proj=utm +zone=16 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3161)
{
    // NAD83 / Ontario MNR Lambert

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3161, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 110);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3161, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 110);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=44.5 +lat_2=53.5 +lat_0=0 +lon_0=-85 +x_0=930000 +y_0=6430000 "
        "+datum=NAD83 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3162)
{
    // NAD83(CSRS) / Ontario MNR Lambert

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3162, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 133);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3162, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 133);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=44.5 +lat_2=53.5 +lat_0=0 +lon_0=-85 +x_0=930000 +y_0=6430000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3163)
{
    // RGNC91-93 / Lambert New Caledonia

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3163, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 164);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3163, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 164);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=-20.66666666666667 +lat_2=-22.33333333333333 +lat_0=-21.5 +lon_0=166 "
        "+x_0=400000 +y_0=300000 +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3164)
{
    // ST87 Ouvea / UTM zone 58S

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3164, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 97);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3164, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 97);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=58 +south +ellps=WGS84 +towgs84=-56.263,16.136,-22.856,0,0,0,0 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3165)
{
    // NEA74 Noumea / Noumea Lambert

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3165, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 179);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3165, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 179);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=-22.24469175 +lat_2=-22.29469175 +lat_0=-22.26969175 +lon_0=166.44242575 "
        "+x_0=0.66 +y_0=1.02 +ellps=intl +towgs84=-10.18,-350.43,291.37,0,0,0,0 +units=m "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3166)
{
    // NEA74 Noumea / Noumea Lambert 2

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3166, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 208);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3166, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 208);
    ASSERT_EQ(
        crs_string,
        "+proj=lcc +lat_1=-22.24472222222222 +lat_2=-22.29472222222222 +lat_0=-22.26972222222222 "
        "+lon_0=166.4425 +x_0=8.313000000000001 +y_0=-2.354 +ellps=intl "
        "+towgs84=-10.18,-350.43,291.37,0,0,0,0 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3167)
{
    // Kertau (RSO) / RSO Malaya (ch)

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3167, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 181);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3167, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 181);
    ASSERT_EQ(
        crs_string,
        "+proj=omerc +lat_0=4 +lonc=102.25 +alpha=323.0257905 +k=0.99984 +x_0=40000 +y_0=0 "
        "+no_uoff +gamma=323.1301023611111 +a=6377295.664 +b=6356094.667915204 +to_meter=20.116756 "
        "+no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3168)
{
    // Kertau (RSO) / RSO Malaya (m)

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3168, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 174);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3168, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 174);
    ASSERT_EQ(
        crs_string,
        "+proj=omerc +lat_0=4 +lonc=102.25 +alpha=323.0257905 +k=0.99984 +x_0=804670.24 +y_0=0 "
        "+no_uoff +gamma=323.1301023611111 +a=6377295.664 +b=6356094.667915204 +units=m +no_defs ");
}

TEST_F(CrsDatabaseTest, pl_get_crs_string_EPSG_3169)
{
    // RGNC91-93 / UTM zone 57S

    size_t string_length;
    pl_CrsDatabaseResult res = pl_get_crs_proj_str_length(crs_db, "EPSG", 3169, &string_length);
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    ASSERT_EQ(string_length, 80);

    std::vector<char> crs_chars;
    crs_chars.resize(string_length + 1);
    res = pl_get_crs_proj_str(
        crs_db, "EPSG", 3169, crs_chars.data(), &string_length, crs_chars.size());
    ASSERT_EQ(res, pl_CrsDatabaseResult_Ok);
    std::string crs_string = crs_chars.data();
    ASSERT_EQ(string_length, 80);
    ASSERT_EQ(
        crs_string,
        "+proj=utm +zone=57 +south +ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs ");
}
