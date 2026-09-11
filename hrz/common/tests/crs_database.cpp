// SPDX-FileCopyrightText: Copyright 2021 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/common/crs_database.h"

#include <gtest/gtest.h>
#include <proj_lite.h>

#include <cstring>

#define EPSG_4326_STR "+proj=longlat +datum=WGS84 +no_defs"
#define EPSG_3857_STR                                                                          \
    "+proj=merc +a=6378137 +b=6378137 +lat_ts=0.0 +lon_0=0.0 +x_0=0.0 +y_0=0 +k=1.0 +units=m " \
    "+nadgrids=@null +wktext +no_defs"

inline bool crs_equal(const pl_Crs* a, const pl_Crs* b)
{
    return std::memcmp(a, b, sizeof(pl_Crs)) == 0;
}

TEST(CrsDatabase, simple_srid_string_4326)
{
    pl_Crs crs;
    bool convert_success =
        hrz::convert_crs("EPSG:4326", hrz_proto::SrsDescriptorType::SRID_DESCRIPTOR, &crs);
    EXPECT_EQ(true, convert_success);

    pl_Crs ref_crs;
    pl_Result res = pl_crs_from_proj_zstr(EPSG_4326_STR, &ref_crs);
    EXPECT_EQ(pl_Result_Ok, res);

    EXPECT_EQ(true, crs_equal(&crs, &ref_crs));
}

TEST(CrsDatabase, simple_srid_string_3857)
{
    pl_Crs crs;
    bool convert_success =
        hrz::convert_crs("EPSG:3857", hrz_proto::SrsDescriptorType::SRID_DESCRIPTOR, &crs);
    EXPECT_EQ(true, convert_success);

    pl_Crs ref_crs;
    pl_Result res = pl_crs_from_proj_zstr(EPSG_3857_STR, &ref_crs);
    EXPECT_EQ(pl_Result_Ok, res);

    EXPECT_EQ(true, crs_equal(&crs, &ref_crs));
}

TEST(CrsDatabase, osgeo_srid_41001)
{
    pl_Crs crs;
    bool convert_success =
        hrz::convert_crs("OSGEO:41001", hrz_proto::SrsDescriptorType::SRID_DESCRIPTOR, &crs);
    EXPECT_EQ(true, convert_success);

    pl_Crs ref_crs;
    pl_Result res = pl_crs_from_proj_zstr(EPSG_3857_STR, &ref_crs);
    EXPECT_EQ(pl_Result_Ok, res);

    EXPECT_EQ(true, crs_equal(&crs, &ref_crs));
}

TEST(CrsDatabase, ogc_srid_string_full)
{
    pl_Crs crs;
    bool convert_success = hrz::convert_crs(
        "urn:ogc:def:crs:EPSG:6.18.3:3857", hrz_proto::SrsDescriptorType::SRID_DESCRIPTOR, &crs);
    EXPECT_EQ(true, convert_success);

    pl_Crs ref_crs;
    pl_Result res = pl_crs_from_proj_zstr(EPSG_3857_STR, &ref_crs);
    EXPECT_EQ(pl_Result_Ok, res);

    EXPECT_EQ(true, crs_equal(&crs, &ref_crs));
}

TEST(CrsDatabase, ogc_srid_string_empty_version)
{
    pl_Crs crs;
    bool convert_success = hrz::convert_crs(
        "urn:ogc:def:crs:EPSG::3857", hrz_proto::SrsDescriptorType::SRID_DESCRIPTOR, &crs);
    EXPECT_EQ(true, convert_success);

    pl_Crs ref_crs;
    pl_Result res = pl_crs_from_proj_zstr(EPSG_3857_STR, &ref_crs);
    EXPECT_EQ(pl_Result_Ok, res);

    EXPECT_EQ(true, crs_equal(&crs, &ref_crs));
}

TEST(CrsDatabase, ogc_srid_string_crs84_1_3_full)
{
    pl_Crs crs;
    bool convert_success = hrz::convert_crs(
        "urn:ogc:def:crs:OGC:1.3:CRS84", hrz_proto::SrsDescriptorType::SRID_DESCRIPTOR, &crs);
    EXPECT_EQ(true, convert_success);

    pl_Crs ref_crs;
    pl_Result res = pl_crs_from_proj_zstr(EPSG_4326_STR, &ref_crs);
    EXPECT_EQ(pl_Result_Ok, res);

    EXPECT_EQ(true, crs_equal(&crs, &ref_crs));
}

TEST(CrsDatabase, ogc_srid_string_crs84_2_full)
{
    pl_Crs crs;
    bool convert_success = hrz::convert_crs(
        "urn:ogc:def:crs:OGC:2:84", hrz_proto::SrsDescriptorType::SRID_DESCRIPTOR, &crs);
    EXPECT_EQ(true, convert_success);

    pl_Crs ref_crs;
    pl_Result res = pl_crs_from_proj_zstr(EPSG_4326_STR, &ref_crs);
    EXPECT_EQ(pl_Result_Ok, res);

    EXPECT_EQ(true, crs_equal(&crs, &ref_crs));
}

TEST(CrsDatabase, ogc_srid_string_invalid_version)
{
    pl_Crs crs;
    bool convert_success = hrz::convert_crs(
        "urn:ogc:def:crs:EPSG:not_a_version:3857", hrz_proto::SrsDescriptorType::SRID_DESCRIPTOR,
        &crs);
    EXPECT_EQ(false, convert_success);
}

TEST(CrsDatabase, unknown_projection_authority)
{
    pl_Crs crs;
    bool convert_success =
        hrz::convert_crs("SIRADEL:1234", hrz_proto::SrsDescriptorType::SRID_DESCRIPTOR, &crs);
    EXPECT_EQ(false, convert_success);
}
