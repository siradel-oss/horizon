#include "hrz/core/base_url.h"

#include "hrz/fnd/defines.h"

#include <gtest/gtest.h>

namespace
{
using namespace hrz;

TEST(BaseUrl, empty)
{
    {
        auto base = BaseUrl();
        EXPECT_EQ("", base.base());
    }
    {
        auto base = BaseUrl();
        EXPECT_EQ("", base.derive(""));
        EXPECT_EQ("index.html", base.derive("index.html"));
        EXPECT_EQ("index.html?key=value", base.derive("index.html?key=value"));
    }
    {
        auto base1 = BaseUrl();
        auto base2 = base1.derive_base("directory/?key1=value1");
        EXPECT_EQ("directory/index.html", base2.derive("index.html"));
        EXPECT_EQ("directory/index.html?key2=value2", base2.derive("index.html?key2=value2"));
        EXPECT_EQ(
            "directory/index.html?key1=value1&key2=value2",
            base2.derive("index.html?key2=value2", true));
    }
}

TEST(BaseUrl, base)
{
    {
        auto base = BaseUrl("http://www.example.com", false);
        EXPECT_EQ("http://www.example.com", base.base());
    }
    {
        auto base = BaseUrl("http://www.example.com/index.html", false);
        EXPECT_EQ("http://www.example.com/index.html", base.base());
    }
    {
        auto base = BaseUrl("http://www.example.com/index.html?key=value", false);
        EXPECT_EQ("http://www.example.com/index.html?key=value", base.base());
    }
    {
        auto base = BaseUrl("http://www.example.com/index.html?key=value", true);
        EXPECT_EQ("http://www.example.com/index.html?key=value", base.base());
    }
}

TEST(BaseUrl, derive)
{
    {
        auto base = BaseUrl("http://www.example.com", false);
        EXPECT_EQ("http://www.example.com/", base.derive(""));
        EXPECT_EQ("http://www.example.com/index.html", base.derive("index.html"));
        EXPECT_EQ(
            "http://www.example.com/index.html?key=value", base.derive("index.html?key=value"));
    }
    {
        auto base = BaseUrl("http://www.example.com/directory/", false);
        EXPECT_EQ("http://www.example.com/directory/", base.derive(""));
        EXPECT_EQ("http://www.example.com/directory/index.html", base.derive("index.html"));
        EXPECT_EQ(
            "http://www.example.com/directory/index.html?key=value",
            base.derive("index.html?key=value"));
    }
    {
        auto base = BaseUrl("http://www.example.com?key1=value1", false);
        EXPECT_EQ("http://www.example.com/", base.derive(""));
        EXPECT_EQ("http://www.example.com/", base.derive("", false));
        EXPECT_EQ("http://www.example.com/?key1=value1", base.derive("", true));
        EXPECT_EQ("http://www.example.com/index.html", base.derive("index.html"));
        EXPECT_EQ("http://www.example.com/index.html", base.derive("index.html", false));
        EXPECT_EQ("http://www.example.com/index.html?key1=value1", base.derive("index.html", true));
        EXPECT_EQ(
            "http://www.example.com/index.html?key2=value2", base.derive("index.html?key2=value2"));
        EXPECT_EQ(
            "http://www.example.com/index.html?key2=value2",
            base.derive("index.html?key2=value2", false));
        EXPECT_EQ(
            "http://www.example.com/index.html?key1=value1&key2=value2",
            base.derive("index.html?key2=value2", true));
    }
    {
        auto base = BaseUrl("http://www.example.com?key1=value1", true);
        EXPECT_EQ("http://www.example.com/?key1=value1", base.derive(""));
        EXPECT_EQ("http://www.example.com/", base.derive("", false));
        EXPECT_EQ("http://www.example.com/?key1=value1", base.derive("", true));
        EXPECT_EQ("http://www.example.com/index.html?key1=value1", base.derive("index.html"));
        EXPECT_EQ("http://www.example.com/index.html", base.derive("index.html", false));
        EXPECT_EQ("http://www.example.com/index.html?key1=value1", base.derive("index.html", true));
        EXPECT_EQ(
            "http://www.example.com/index.html?key1=value1&key2=value2",
            base.derive("index.html?key2=value2"));
        EXPECT_EQ(
            "http://www.example.com/index.html?key2=value2",
            base.derive("index.html?key2=value2", false));
        EXPECT_EQ(
            "http://www.example.com/index.html?key1=value1&key2=value2",
            base.derive("index.html?key2=value2", true));
    }
}

TEST(BaseUrl, add_slash)
{
    {
        auto base = BaseUrl("http://www.example.com/page", false);
        EXPECT_EQ("http://www.example.com/", base.derive(""));
        EXPECT_EQ("http://www.example.com/index.html", base.derive("index.html"));
        EXPECT_EQ(
            "http://www.example.com/index.html?key=value", base.derive("index.html?key=value"));
    }
    {
        auto base = BaseUrl("http://www.example.com/pagebutdir", false);
        base.add_slash();
        EXPECT_EQ("http://www.example.com/pagebutdir/", base.derive(""));
        EXPECT_EQ("http://www.example.com/pagebutdir/index.html", base.derive("index.html"));
        EXPECT_EQ(
            "http://www.example.com/pagebutdir/index.html?key=value",
            base.derive("index.html?key=value"));
    }
    {
        auto base = BaseUrl("http://www.example.com/directory/", false);
        base.add_slash();
        EXPECT_EQ("http://www.example.com/directory/", base.derive(""));
        EXPECT_EQ("http://www.example.com/directory/index.html", base.derive("index.html"));
        EXPECT_EQ(
            "http://www.example.com/directory/index.html?key=value",
            base.derive("index.html?key=value"));
    }
}

TEST(BaseUrl, derive_base)
{
    {
        auto base1 = BaseUrl("http://www.example.com?key1=value1", false);
        auto base2 = base1.derive_base("directory/?key2=value2");
        EXPECT_EQ("http://www.example.com/directory/", base2.derive(""));
        EXPECT_EQ("http://www.example.com/directory/", base2.derive("", false));
        EXPECT_EQ(
            "http://www.example.com/directory/?key1=value1&key2=value2", base2.derive("", true));
        EXPECT_EQ("http://www.example.com/directory/index.html", base2.derive("index.html"));
        EXPECT_EQ("http://www.example.com/directory/index.html", base2.derive("index.html", false));
        EXPECT_EQ(
            "http://www.example.com/directory/index.html?key1=value1&key2=value2",
            base2.derive("index.html", true));
        EXPECT_EQ(
            "http://www.example.com/directory/index.html?key3=value3",
            base2.derive("index.html?key3=value3"));
        EXPECT_EQ(
            "http://www.example.com/directory/index.html?key3=value3",
            base2.derive("index.html?key3=value3", false));
        EXPECT_EQ(
            "http://www.example.com/directory/index.html?key1=value1&key2=value2&key3=value3",
            base2.derive("index.html?key3=value3", true));
    }
    {
        auto base1 = BaseUrl("http://www.example.com?key1=value1", true);
        auto base2 = base1.derive_base("directory/?key2=value2");
        EXPECT_EQ("http://www.example.com/directory/?key1=value1&key2=value2", base2.derive(""));
        EXPECT_EQ("http://www.example.com/directory/", base2.derive("", false));
        EXPECT_EQ(
            "http://www.example.com/directory/?key1=value1&key2=value2", base2.derive("", true));
        EXPECT_EQ(
            "http://www.example.com/directory/index.html?key1=value1&key2=value2",
            base2.derive("index.html"));
        EXPECT_EQ("http://www.example.com/directory/index.html", base2.derive("index.html", false));
        EXPECT_EQ(
            "http://www.example.com/directory/index.html?key1=value1&key2=value2",
            base2.derive("index.html", true));
        EXPECT_EQ(
            "http://www.example.com/directory/index.html?key1=value1&key2=value2&key3=value3",
            base2.derive("index.html?key3=value3"));
        EXPECT_EQ(
            "http://www.example.com/directory/index.html?key3=value3",
            base2.derive("index.html?key3=value3", false));
        EXPECT_EQ(
            "http://www.example.com/directory/index.html?key1=value1&key2=value2&key3=value3",
            base2.derive("index.html?key3=value3", true));
    }
}
} // namespace
