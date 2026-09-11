// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/core/tile_url_generator.h"

#include "hrz/core/base_url.h"
#include "hrz/fnd/defines.h"

#include <gtest/gtest.h>

namespace
{

TEST(Url, parse_tile_url_all)
{
    hrz::PatternTileUrlGenerator url("{l-p}.{x}.{y}.{z}.{-y}.{quadkey}", 1);
    EXPECT_EQ("{sub}.{x}.{y}.{z}.{ry}.{quadkey}", url.base);
    EXPECT_EQ('l', url.subdomain_first);
    EXPECT_EQ('p', url.subdomain_last);
    EXPECT_EQ(true, url.has_quadkey);
}

TEST(Url, parse_tile_url_no_quadkey)
{
    hrz::PatternTileUrlGenerator url("{l-p}.{x}.{y}.{z}.{-y}", 1);
    EXPECT_EQ("{sub}.{x}.{y}.{z}.{ry}", url.base);
    EXPECT_EQ('l', url.subdomain_first);
    EXPECT_EQ('p', url.subdomain_last);
    EXPECT_EQ(false, url.has_quadkey);
}

TEST(Url, parse_tile_url_reverse_subdomain_range)
{
    hrz::PatternTileUrlGenerator url("hello://{x}.{y}.{p-l}.{z}.{-y}", 1);
    EXPECT_EQ("hello://{x}.{y}.{sub}.{z}.{ry}", url.base);
    EXPECT_EQ('l', url.subdomain_first);
    EXPECT_EQ('p', url.subdomain_last);
    EXPECT_EQ(false, url.has_quadkey);
}

TEST(Url, make_tile_url)
{
    hrz::PatternTileUrlGenerator url;
    url.base = "{sub}.{x}.{y}.{z}.{ry}.{quadkey}";
    url.subdomain_first = 'a';
    url.subdomain_last = 'z';
    url.has_quadkey = true;
    url.level_zero_tile_count_y = 1;

    // Abseil's hash function is somehow seeded per run.
    // So we can't statically test these.

    auto a = url.make_url(1, 2, 2);
    auto b = url.make_url(1, 2, 2);
    auto c = url.make_url(1, 2, 2);
    auto d = url.make_url(5, 9, 5);
    auto e = url.make_url(3, 5, 3);

    EXPECT_EQ(a, b);
    EXPECT_EQ(a, c);

    EXPECT_TRUE(a[0] >= 'a' && a[0] <= 'z');
    EXPECT_TRUE(d[0] >= 'a' && d[0] <= 'z');
    EXPECT_TRUE(e[0] >= 'a' && e[0] <= 'z');

    EXPECT_EQ(a.substr(2), "1.2.2.1.21");
    EXPECT_EQ(d.substr(2), "5.9.5.22.02103");
    EXPECT_EQ(e.substr(2), "3.5.3.2.213");
}

TEST(Url, level_zero_tile_count_y)
{
    hrz::PatternTileUrlGenerator url1("{z}.{x}.{-y}", 1);

    EXPECT_EQ("0.0.0", url1.make_url(0, 0, 0));
    EXPECT_EQ("1.0.1", url1.make_url(0, 0, 1));
    EXPECT_EQ("1.0.0", url1.make_url(0, 1, 1));
    EXPECT_EQ("4.2.12", url1.make_url(2, 3, 4));
    EXPECT_EQ("4.2.2", url1.make_url(2, 13, 4));

    hrz::PatternTileUrlGenerator url2("{z}.{x}.{-y}", 2);

    EXPECT_EQ("0.0.1", url2.make_url(0, 0, 0));
    EXPECT_EQ("1.0.3", url2.make_url(0, 0, 1));
    EXPECT_EQ("1.0.2", url2.make_url(0, 1, 1));
    EXPECT_EQ("4.2.28", url2.make_url(2, 3, 4));
    EXPECT_EQ("4.2.18", url2.make_url(2, 13, 4));
}

} // namespace
