// SPDX-FileCopyrightText: Copyright 2025 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/fnd/mime.h"

#include <gtest/gtest.h>

TEST(Mime, ParseMimeSimple)
{
    auto parsed = hrz::parse_mime("text/x.raw");

    EXPECT_EQ(parsed.type, "text");
    EXPECT_EQ(parsed.subtype, "x.raw");
    EXPECT_EQ(parsed.suffixes.size(), 0);
    EXPECT_EQ(parsed.parameters.size(), 0);
}

TEST(Mime, ParseMimeSuffix)
{
    auto parsed = hrz::parse_mime("text/x.raw+json");

    EXPECT_EQ(parsed.type, "text");
    EXPECT_EQ(parsed.subtype, "x.raw");
    ASSERT_EQ(parsed.suffixes.size(), 1);
    EXPECT_EQ(parsed.suffixes[0], "json");
    EXPECT_EQ(parsed.parameters.size(), 0);
}

TEST(Mime, ParseMimeMultipleSuffixes)
{
    auto parsed = hrz::parse_mime("text/x.raw+json+gzip");

    EXPECT_EQ(parsed.type, "text");
    EXPECT_EQ(parsed.subtype, "x.raw");
    ASSERT_EQ(parsed.suffixes.size(), 2);
    EXPECT_EQ(parsed.suffixes[0], "json");
    EXPECT_EQ(parsed.suffixes[1], "gzip");
    EXPECT_EQ(parsed.parameters.size(), 0);
}

TEST(Mime, ParseMimeParam)
{
    auto parsed = hrz::parse_mime("text/x.raw; a=b");

    EXPECT_EQ(parsed.type, "text");
    EXPECT_EQ(parsed.subtype, "x.raw");
    EXPECT_EQ(parsed.suffixes.size(), 0);
    ASSERT_EQ(parsed.parameters.size(), 1);
    EXPECT_EQ(parsed.parameters[0].first, "a");
    EXPECT_EQ(parsed.parameters[0].second, "b");
}

TEST(Mime, ParseMimeMultipleParams)
{
    auto parsed = hrz::parse_mime("text/x.raw; a=b;c = dddd ; eee=f");

    EXPECT_EQ(parsed.type, "text");
    EXPECT_EQ(parsed.subtype, "x.raw");
    EXPECT_EQ(parsed.suffixes.size(), 0);
    ASSERT_EQ(parsed.parameters.size(), 3);
    EXPECT_EQ(parsed.parameters[0].first, "a");
    EXPECT_EQ(parsed.parameters[0].second, "b");
    EXPECT_EQ(parsed.parameters[1].first, "c");
    EXPECT_EQ(parsed.parameters[1].second, " dddd ");
    EXPECT_EQ(parsed.parameters[2].first, "eee");
    EXPECT_EQ(parsed.parameters[2].second, "f");
}

TEST(Mime, ParseMimeSuffixParam)
{
    auto parsed = hrz::parse_mime("text/x.raw+json; a=b");

    EXPECT_EQ(parsed.type, "text");
    EXPECT_EQ(parsed.subtype, "x.raw");
    ASSERT_EQ(parsed.suffixes.size(), 1);
    EXPECT_EQ(parsed.suffixes[0], "json");
    EXPECT_EQ(parsed.parameters.size(), 1);
    EXPECT_EQ(parsed.parameters[0].first, "a");
    EXPECT_EQ(parsed.parameters[0].second, "b");
}

TEST(Mime, ParseMimeMultipleSuffixesAndParams)
{
    auto parsed = hrz::parse_mime("text/x.raw+json+gzip; a=b;c = dddd ");

    EXPECT_EQ(parsed.type, "text");
    EXPECT_EQ(parsed.subtype, "x.raw");
    ASSERT_EQ(parsed.suffixes.size(), 2);
    EXPECT_EQ(parsed.suffixes[0], "json");
    EXPECT_EQ(parsed.suffixes[1], "gzip");
    ASSERT_EQ(parsed.parameters.size(), 2);
    EXPECT_EQ(parsed.parameters[0].first, "a");
    EXPECT_EQ(parsed.parameters[0].second, "b");
    EXPECT_EQ(parsed.parameters[1].first, "c");
    EXPECT_EQ(parsed.parameters[1].second, " dddd ");
}

TEST(Mime, GetParameter)
{
    auto parsed = hrz::parse_mime("text/x.raw+json+gzip; a=b;base64;c = dddd ");

    EXPECT_EQ(parsed.get_parameter("a"), "b");
    EXPECT_EQ(parsed.get_parameter("c"), " dddd ");
    EXPECT_EQ(parsed.get_parameter("C"), " dddd ");
    EXPECT_EQ(parsed.get_parameter("d"), std::nullopt);
    EXPECT_TRUE(parsed.get_parameter("base64").has_value());
}

TEST(Mime, HasParameter)
{
    auto parsed = hrz::parse_mime("text/x.raw+json+gzip; a=b;base64;c = dddd ");

    EXPECT_TRUE(parsed.get_parameter("a"));
    EXPECT_TRUE(parsed.get_parameter("c"));
    EXPECT_TRUE(parsed.get_parameter("C"));
    EXPECT_FALSE(parsed.get_parameter("d"));
    EXPECT_TRUE(parsed.has_parameter("base64"));
}

TEST(Mime, ToString)
{
    EXPECT_EQ(
        "text/x.raw+json+gzip;a=b;base64;c= dddd ",
        hrz::parse_mime("text/x.raw+json+gzip; a=b;base64;c = dddd ").to_string());
    EXPECT_EQ("text/x.raw+json+gzip", hrz::parse_mime("text/x.raw+json+gzip").to_string());
    EXPECT_EQ("text/x.raw", hrz::parse_mime("text/x.raw").to_string());
    EXPECT_EQ("text/x.raw;a=b", hrz::parse_mime("text/x.raw; a=b").to_string());
}
