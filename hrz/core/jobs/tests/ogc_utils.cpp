// SPDX-FileCopyrightText: Copyright 2021 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/core/jobs/ogc_utils.h"

#include <gtest/gtest.h>
#include <pugixml.hpp>

TEST(OgcUtils, find_image_format_1)
{
    // Real XML extract from a GeoServer WMS endpoint.
    static const char* GET_MAP =
        "<GetMap>"
        "    <Format>image/png</Format>"
        "    <Format>application/atom+xml</Format>"
        "    <Format>application/json;type=utfgrid</Format>"
        "    <Format>application/pdf</Format>"
        "    <Format>application/rss+xml</Format>"
        "    <Format>application/vnd.google-earth.kml+xml</Format>"
        "    <Format>application/vnd.google-earth.kml+xml;mode=networklink</Format>"
        "    <Format>application/vnd.google-earth.kmz</Format>"
        "    <Format>application/x-sqlite3</Format>"
        "    <Format>image/bil</Format>"
        "    <Format>image/dds</Format>"
        "    <Format>image/geotiff</Format>"
        "    <Format>image/geotiff8</Format>"
        "    <Format>image/gif</Format>"
        "    <Format>image/jpeg</Format>"
        "    <Format>image/png; mode=8bit</Format>"
        "    <Format>image/svg+xml</Format>"
        "    <Format>image/tiff</Format>"
        "    <Format>image/tiff8</Format>"
        "    <Format>image/vnd.jpeg-png</Format>"
        "    <Format>image/vnd.jpeg-png8</Format>"
        "    <Format>text/html; subtype=openlayers</Format>"
        "    <Format>text/html; subtype=openlayers2</Format>"
        "    <Format>text/html; subtype=openlayers3</Format>"
        "</GetMap>";

    pugi::xml_document doc;
    pugi::xml_parse_result parse_result = doc.load_string(GET_MAP);
    ASSERT_TRUE(parse_result.status == pugi::status_ok);
    pugi::xml_node get_map_node = doc.child("GetMap");

    {
        auto res = hrz::ogc::find_image_format(get_map_node, "", false);
        EXPECT_TRUE(res.has_value());
        EXPECT_EQ("image/jpeg", res.value());
    }
    {
        auto res = hrz::ogc::find_image_format(get_map_node, "", true);
        EXPECT_TRUE(res.has_value());
        EXPECT_EQ("image/vnd.jpeg-png", res.value());
    }
    {
        auto res = hrz::ogc::find_image_format(get_map_node, "image/png", false);
        EXPECT_TRUE(res.has_value());
        EXPECT_EQ("image/png", res.value());
    }
    {
        auto res = hrz::ogc::find_image_format(get_map_node, "image/png", true);
        EXPECT_TRUE(res.has_value());
        EXPECT_EQ("image/png", res.value());
    }
    {
        auto res = hrz::ogc::find_image_format(get_map_node, "invalid-MIME-type", true);
        EXPECT_FALSE(res.has_value());
    }
}

TEST(OgcUtils, find_image_format_2)
{
    static const char* GET_MAP =
        "<GetMap>"
        "    <Format>image/webp</Format>"
        "    <Format>image/png</Format>"
        "    <Format>image/gif</Format>"
        "    <Format>image/jpeg</Format>"
        "</GetMap>";

    pugi::xml_document doc;
    pugi::xml_parse_result parse_result = doc.load_string(GET_MAP);
    ASSERT_TRUE(parse_result.status == pugi::status_ok);
    pugi::xml_node get_map_node = doc.child("GetMap");

    {
        auto res = hrz::ogc::find_image_format(get_map_node, "", false);
        EXPECT_TRUE(res.has_value());
        EXPECT_EQ("image/webp", res.value());
    }
    {
        auto res = hrz::ogc::find_image_format(get_map_node, "", true);
        EXPECT_TRUE(res.has_value());
        EXPECT_EQ("image/webp", res.value());
    }
    {
        auto res = hrz::ogc::find_image_format(get_map_node, "image/png", false);
        EXPECT_TRUE(res.has_value());
        EXPECT_EQ("image/png", res.value());
    }
    {
        auto res = hrz::ogc::find_image_format(get_map_node, "image/png", true);
        EXPECT_TRUE(res.has_value());
        EXPECT_EQ("image/png", res.value());
    }
    {
        auto res = hrz::ogc::find_image_format(get_map_node, "image/vnd.jpeg-png", true);
        EXPECT_FALSE(res.has_value());
    }
}

TEST(OgcUtils, find_image_format_3)
{
    static const char* GET_MAP =
        "<GetMap>"
        "    <Format>image/jpeg</Format>"
        "    <Format>image/png</Format>"
        "    <Format>image/gif</Format>"
        "    <Format>image/webp</Format>"
        "</GetMap>";

    pugi::xml_document doc;
    pugi::xml_parse_result parse_result = doc.load_string(GET_MAP);
    ASSERT_TRUE(parse_result.status == pugi::status_ok);
    pugi::xml_node get_map_node = doc.child("GetMap");

    {
        auto res = hrz::ogc::find_image_format(get_map_node, "", false);
        EXPECT_TRUE(res.has_value());
        EXPECT_EQ("image/webp", res.value());
    }
    {
        auto res = hrz::ogc::find_image_format(get_map_node, "", true);
        EXPECT_TRUE(res.has_value());
        EXPECT_EQ("image/webp", res.value());
    }
    {
        auto res = hrz::ogc::find_image_format(get_map_node, "image/png", false);
        EXPECT_TRUE(res.has_value());
        EXPECT_EQ("image/png", res.value());
    }
    {
        auto res = hrz::ogc::find_image_format(get_map_node, "image/png", true);
        EXPECT_TRUE(res.has_value());
        EXPECT_EQ("image/png", res.value());
    }
    {
        auto res = hrz::ogc::find_image_format(get_map_node, "image/vnd.jpeg-png", true);
        EXPECT_FALSE(res.has_value());
    }
}

TEST(OgcUtils, find_image_format_4)
{
    static const char* GET_MAP =
        "<GetMap>"
        "    <Format>image/jpeg</Format>"
        "    <Format>image/webp</Format>"
        "</GetMap>";

    pugi::xml_document doc;
    pugi::xml_parse_result parse_result = doc.load_string(GET_MAP);
    ASSERT_TRUE(parse_result.status == pugi::status_ok);
    pugi::xml_node get_map_node = doc.child("GetMap");

    {
        auto res = hrz::ogc::find_image_format(get_map_node, "", false);
        EXPECT_TRUE(res.has_value());
        EXPECT_EQ("image/webp", res.value());
    }
    {
        auto res = hrz::ogc::find_image_format(get_map_node, "", true);
        EXPECT_TRUE(res.has_value());
        EXPECT_EQ("image/webp", res.value());
    }
    {
        auto res = hrz::ogc::find_image_format(get_map_node, "image/webp", false);
        EXPECT_TRUE(res.has_value());
        EXPECT_EQ("image/webp", res.value());
    }
    {
        auto res = hrz::ogc::find_image_format(get_map_node, "image/webp", true);
        EXPECT_TRUE(res.has_value());
        EXPECT_EQ("image/webp", res.value());
    }
    {
        auto res = hrz::ogc::find_image_format(get_map_node, "image/jpeg", false);
        EXPECT_TRUE(res.has_value());
        EXPECT_EQ("image/jpeg", res.value());
    }
    {
        auto res = hrz::ogc::find_image_format(get_map_node, "image/jpeg", true);
        EXPECT_TRUE(res.has_value());
        EXPECT_EQ("image/jpeg", res.value());
    }
    {
        auto res = hrz::ogc::find_image_format(get_map_node, "image/png", true);
        EXPECT_FALSE(res.has_value());
    }
}

TEST(OgcUtils, find_image_format_5)
{
    static const char* GET_MAP =
        "<GetMap>"
        "    <Format>image/jpeg</Format>"
        "</GetMap>";

    pugi::xml_document doc;
    pugi::xml_parse_result parse_result = doc.load_string(GET_MAP);
    ASSERT_TRUE(parse_result.status == pugi::status_ok);
    pugi::xml_node get_map_node = doc.child("GetMap");

    {
        auto res = hrz::ogc::find_image_format(get_map_node, "", false);
        EXPECT_TRUE(res.has_value());
        EXPECT_EQ("image/jpeg", res.value());
    }
    {
        auto res = hrz::ogc::find_image_format(get_map_node, "", true);
        EXPECT_TRUE(res.has_value());
        EXPECT_EQ("image/jpeg", res.value());
    }
    {
        auto res = hrz::ogc::find_image_format(get_map_node, "image/png", true);
        EXPECT_FALSE(res.has_value());
    }
}

TEST(OgcUtils, find_image_format_6)
{
    static const char* GET_MAP =
        "<GetMap>"
        "    <Format>image/png24</Format>"
        "    <Format>image/vnd.jpeg-png</Format>"
        "    <Format>image/png32</Format>"
        "    <Format>image/png8</Format>"
        "</GetMap>";

    pugi::xml_document doc;
    pugi::xml_parse_result parse_result = doc.load_string(GET_MAP);
    ASSERT_TRUE(parse_result.status == pugi::status_ok);
    pugi::xml_node get_map_node = doc.child("GetMap");

    {
        auto res = hrz::ogc::find_image_format(get_map_node, "", false);
        EXPECT_TRUE(res.has_value());
        EXPECT_EQ("image/vnd.jpeg-png", res.value());
    }
    {
        auto res = hrz::ogc::find_image_format(get_map_node, "", true);
        EXPECT_TRUE(res.has_value());
        EXPECT_EQ("image/vnd.jpeg-png", res.value());
    }
    {
        auto res = hrz::ogc::find_image_format(get_map_node, "image/png24", false);
        EXPECT_TRUE(res.has_value());
        EXPECT_EQ("image/png24", res.value());
    }
    {
        auto res = hrz::ogc::find_image_format(get_map_node, "image/png32", true);
        EXPECT_TRUE(res.has_value());
        EXPECT_EQ("image/png32", res.value());
    }
    {
        auto res = hrz::ogc::find_image_format(get_map_node, "image/png", true);
        EXPECT_FALSE(res.has_value());
    }
    {
        auto res = hrz::ogc::find_image_format(get_map_node, "image/jpeg", true);
        EXPECT_FALSE(res.has_value());
    }
}

TEST(OgcUtils, crs_has_flipped_axes)
{
    EXPECT_TRUE(hrz::ogc::crs_has_flipped_axes("EPSG", 2036));
    EXPECT_TRUE(hrz::ogc::crs_has_flipped_axes("EPSG", 4326));
    EXPECT_TRUE(hrz::ogc::crs_has_flipped_axes("EPSG", 32761));

    EXPECT_FALSE(hrz::ogc::crs_has_flipped_axes("EPSG", 2035));
    EXPECT_FALSE(hrz::ogc::crs_has_flipped_axes("EPSG", 3857));
    EXPECT_FALSE(hrz::ogc::crs_has_flipped_axes("EPSG", 32762));

    EXPECT_FALSE(hrz::ogc::crs_has_flipped_axes("CRS", 84));

    EXPECT_FALSE(hrz::ogc::crs_has_flipped_axes("NotEPSG", 4326));
}
