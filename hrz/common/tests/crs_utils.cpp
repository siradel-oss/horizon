#include "hrz_common_crs_utils.h"

#include <gtest/gtest.h>

TEST(CrsUtils, parse_srid)
{
    {
        auto res = hrz::crs::parse_srid("EPSG:4326");
        EXPECT_TRUE(res.has_value());
        EXPECT_EQ("EPSG", res->authority);
        EXPECT_EQ(4326, res->code);
    }
    {
        auto res = hrz::crs::parse_srid("EPSG:32630");
        EXPECT_TRUE(res.has_value());
        EXPECT_EQ("EPSG", res->authority);
        EXPECT_EQ(32630, res->code);
    }
    {
        auto res = hrz::crs::parse_srid("EPSG::3857");
        EXPECT_TRUE(res.has_value());
        EXPECT_EQ("EPSG", res->authority);
        EXPECT_EQ(3857, res->code);
    }
    {
        auto res = hrz::crs::parse_srid("EPSG:6.18.3:3857");
        EXPECT_TRUE(res.has_value());
        EXPECT_EQ("EPSG", res->authority);
        EXPECT_EQ(3857, res->code);
    }
    {
        auto res = hrz::crs::parse_srid("urn:ogc:def:crs:EPSG:6.18.3:3857");
        EXPECT_TRUE(res.has_value());
        EXPECT_EQ("EPSG", res->authority);
        EXPECT_EQ(3857, res->code);
    }
    {
        auto res = hrz::crs::parse_srid("urn:ogc:def:crs:OGC:1.3:CRS84");
        EXPECT_TRUE(res.has_value());
        EXPECT_EQ("OGC", res->authority);
        EXPECT_EQ(84, res->code);
    }
    {
        auto res = hrz::crs::parse_srid("urn:ogc:def:crs:OGC:2:84");
        EXPECT_TRUE(res.has_value());
        EXPECT_EQ("OGC", res->authority);
        EXPECT_EQ(84, res->code);
    }
    {
        auto res = hrz::crs::parse_srid("urn:ogc:def:crs:CRS::84");
        EXPECT_TRUE(res.has_value());
        EXPECT_EQ("CRS", res->authority);
        EXPECT_EQ(84, res->code);
    }

    {
        auto res = hrz::crs::parse_srid("EPSG:4326a");
        EXPECT_FALSE(res.has_value());
    }
    {
        auto res = hrz::crs::parse_srid("EPSG:6.18:3:3857");
        EXPECT_FALSE(res.has_value());
    }
    {
        auto res = hrz::crs::parse_srid("EPSG:not_a_version:3857");
        EXPECT_FALSE(res.has_value());
    }
    {
        auto res = hrz::crs::parse_srid("web Mercator");
        EXPECT_FALSE(res.has_value());
    }
    {
        // WMTS spec typo
        auto res = hrz::crs::parse_srid("urn:ogc:def:crs:EPSG:6.18:3:3857");
        EXPECT_FALSE(res.has_value());
    }
}
