// SPDX-FileCopyrightText: Copyright 2019 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/fnd/path_utils.h"

#include <gtest/gtest.h>

using namespace hrz;

TEST(Path, parent_s)
{
    EXPECT_EQ("", path::parent_s("hello"));
    EXPECT_EQ("he", path::parent_s("he//llo"));
    EXPECT_EQ("/he", path::parent_s("/he/llo/"));
    EXPECT_EQ("", path::parent_s("//hello"));
    EXPECT_EQ("", path::parent_s("/hello"));
    EXPECT_EQ("", path::parent_s("hello///"));
    EXPECT_EQ("", path::parent_s("/"));
    EXPECT_EQ("", path::parent_s("//"));
    EXPECT_EQ("", path::parent_s(""));
    EXPECT_EQ("/hel", path::parent_s("/hel/lo"));
}

TEST(Path, parent)
{
    EXPECT_EQ("", path::parent("hello"));
    EXPECT_EQ("he", path::parent("he//llo"));
    EXPECT_EQ("/he", path::parent("/he/llo/"));
    EXPECT_EQ("", path::parent("//hello"));
    EXPECT_EQ("", path::parent("/hello"));
    EXPECT_EQ("", path::parent("hello///"));
    EXPECT_EQ("", path::parent("/"));
    EXPECT_EQ("", path::parent("//"));
    EXPECT_EQ("", path::parent(""));
    EXPECT_EQ("/hel", path::parent("/hel/lo"));
}

TEST(Path, join)
{
    EXPECT_EQ("/hello/world", path::join({"/hello", "world"}));
    EXPECT_EQ("/hello/world", path::join({"/hello/", "world"}));
    EXPECT_EQ("/hello/world", path::join({"/hello", "//world"}));
    EXPECT_EQ("hello/world", path::join({"hello", "world/"}));
    EXPECT_EQ("hello/world", path::join({"hello", "world"}));
    EXPECT_EQ("/hello/world/bye", path::join({"/hello/", "/world", "bye/"}));
}

TEST(Path, basename_s)
{
    EXPECT_EQ("world", path::basename_s("/hello/world"));
    EXPECT_EQ("world", path::basename_s("/hello/world//"));
    EXPECT_EQ("world", path::basename_s("/hello///world"));
    EXPECT_EQ("hello", path::basename_s("hello"));
    EXPECT_EQ("hello", path::basename_s("/hello"));
    EXPECT_EQ("hello", path::basename_s("hello/"));
    EXPECT_EQ("hello", path::basename_s("/hello/"));
    EXPECT_EQ("", path::basename_s("/"));
    EXPECT_EQ("", path::basename_s(""));
    EXPECT_EQ("", path::basename_s("///"));
}

TEST(Path, basename)
{
    EXPECT_EQ("world", path::basename("/hello/world"));
    EXPECT_EQ("world", path::basename("/hello/world//"));
    EXPECT_EQ("world", path::basename("/hello///world"));
    EXPECT_EQ("hello", path::basename("hello"));
    EXPECT_EQ("hello", path::basename("/hello"));
    EXPECT_EQ("hello", path::basename("hello/"));
    EXPECT_EQ("hello", path::basename("/hello/"));
    EXPECT_EQ("", path::basename("/"));
    EXPECT_EQ("", path::basename(""));
    EXPECT_EQ("", path::basename("///"));
}
