#include <hrz_fnd_url_utils.h>

#include <gtest/gtest.h>

using namespace hrz;

TEST(Url, is_absolute_http)
{
    EXPECT_EQ(true, url::is_absolute("http://www.example.com"));
    EXPECT_EQ(true, url::is_absolute("http://www.example.com/"));
    EXPECT_EQ(true, url::is_absolute("https://www.example.com"));
    EXPECT_EQ(true, url::is_absolute("https://www.example.com/"));
    EXPECT_EQ(true, url::is_absolute("//www.example.com"));
    EXPECT_EQ(true, url::is_absolute("file:index.html"));
    EXPECT_EQ(true, url::is_absolute("file:/index.html"));
    EXPECT_EQ(true, url::is_absolute("file:directory/index.html"));
    EXPECT_EQ(true, url::is_absolute("file:/directory/index.html"));

    EXPECT_EQ(false, url::is_absolute("index.html"));
    EXPECT_EQ(false, url::is_absolute("/index.html"));
    EXPECT_EQ(false, url::is_absolute("directory/index.html"));
}

TEST(Url, is_relative)
{
    EXPECT_EQ(true, url::is_relative("index.html"));
    EXPECT_EQ(true, url::is_relative("/index.html"));
    EXPECT_EQ(true, url::is_relative(""));
    EXPECT_EQ(true, url::is_relative("/"));
    EXPECT_EQ(false, url::is_relative("file:index.html"));
    EXPECT_EQ(false, url::is_relative("file:/index.html"));

    EXPECT_EQ(false, url::is_relative("http://www.example.com"));
}

TEST(Url, is_protocol_relative)
{
    EXPECT_EQ(true, url::is_protocol_relative("//www.example.com"));
    EXPECT_EQ(false, url::is_protocol_relative("http://www.example.com"));
    EXPECT_EQ(false, url::is_protocol_relative("file:index.html"));

    EXPECT_EQ(false, url::is_protocol_relative("index.html"));
    EXPECT_EQ(false, url::is_protocol_relative("/index.html"));
}

TEST(Url, protocol)
{
    EXPECT_EQ("http", url::protocol_s("http://www.example.com"));
    EXPECT_EQ("http", url::protocol_s("http://www.example.com/"));
    EXPECT_EQ("http", url::protocol_s("http://www.example.com/index.html"));
    EXPECT_EQ("https", url::protocol_s("https://www.example.com/index.html"));
    EXPECT_EQ("file", url::protocol_s("file:index.html"));
    EXPECT_EQ("file", url::protocol_s("file:/index.html"));
    EXPECT_EQ("file", url::protocol_s("file:///index.html"));
    EXPECT_EQ("", url::protocol_s("//www.example.com"));
    EXPECT_EQ("", url::protocol_s("index.html"));
    EXPECT_EQ("", url::protocol_s("/index.html"));
}

TEST(Url, domain)
{
    EXPECT_EQ("www.example.com", url::domain_s("http://www.example.com"));
    EXPECT_EQ("www.example.com", url::domain_s("http://www.example.com/"));
    EXPECT_EQ("www.example.com", url::domain_s("http://www.example.com/index.html"));
    EXPECT_EQ("www.example.com", url::domain_s("http://www.example.com/directory/index.html"));
    EXPECT_EQ("example.com", url::domain_s("http://example.com"));
    EXPECT_EQ("localhost", url::domain_s("http://localhost"));
    EXPECT_EQ("localhost", url::domain_s("http://localhost/"));
    EXPECT_EQ("localhost", url::domain_s("http://localhost/index.html"));
    EXPECT_EQ("localhost", url::domain_s("http://localhost/directory/index.html"));
    EXPECT_EQ("localhost", url::domain_s("http://localhost:8080/directory/index.html"));
    EXPECT_EQ("example.com", url::domain_s("//example.com"));
    EXPECT_EQ("example.com", url::domain_s("//example.com/"));
    EXPECT_EQ("", url::domain_s("index.html"));
    EXPECT_EQ("", url::domain_s("/index.html"));
    EXPECT_EQ("", url::domain_s("/directory/index.html"));
    EXPECT_EQ("", url::domain_s("/directory/"));
    EXPECT_EQ("", url::domain_s(""));
    EXPECT_EQ("", url::domain_s("/"));
    EXPECT_EQ("", url::domain_s("file:"));
    EXPECT_EQ("", url::domain_s("file:index.html"));
    EXPECT_EQ("", url::domain_s("file:/index.html"));
    EXPECT_EQ("", url::domain_s("file:directory/index.html"));
    EXPECT_EQ("", url::domain_s("file:/directory/index.html"));
}

TEST(Url, authority)
{
    EXPECT_EQ("www.example.com", url::authority_s("http://www.example.com"));
    EXPECT_EQ("www.example.com", url::authority_s("http://www.example.com/"));
    EXPECT_EQ("www.example.com", url::authority_s("http://www.example.com/index.html"));
    EXPECT_EQ("www.example.com", url::authority_s("http://www.example.com/directory/index.html"));
    EXPECT_EQ("example.com", url::authority_s("http://example.com"));
    EXPECT_EQ("localhost", url::authority_s("http://localhost"));
    EXPECT_EQ("localhost", url::authority_s("http://localhost/"));
    EXPECT_EQ("localhost", url::authority_s("http://localhost/index.html"));
    EXPECT_EQ("localhost", url::authority_s("http://localhost/directory/index.html"));
    EXPECT_EQ("localhost:8080", url::authority_s("http://localhost:8080/directory/index.html"));
    EXPECT_EQ("example.com", url::authority_s("//example.com"));
    EXPECT_EQ("example.com", url::authority_s("//example.com/"));
    EXPECT_EQ("", url::authority_s("index.html"));
    EXPECT_EQ("", url::authority_s("/index.html"));
    EXPECT_EQ("", url::authority_s("/directory/index.html"));
    EXPECT_EQ("", url::authority_s("/directory/"));
    EXPECT_EQ("", url::authority_s(""));
    EXPECT_EQ("", url::authority_s("/"));
    EXPECT_EQ("", url::authority_s("file:index.html"));
    EXPECT_EQ("", url::authority_s("file:/index.html"));
    EXPECT_EQ("", url::authority_s("file:///index.html"));
    EXPECT_EQ("", url::authority_s("file:directory/index.html"));
    EXPECT_EQ("", url::authority_s("file:/directory/index.html"));
}

TEST(Url, path)
{
    EXPECT_EQ("", url::path_s("http://www.example.com"));
    EXPECT_EQ("/", url::path_s("http://www.example.com/"));
    EXPECT_EQ("/index.html", url::path_s("http://www.example.com/index.html"));
    EXPECT_EQ("/directory/index.html", url::path_s("http://www.example.com/directory/index.html"));
    EXPECT_EQ("index.html", url::path_s("index.html"));
    EXPECT_EQ("/index.html", url::path_s("/index.html"));
    EXPECT_EQ("/directory/index.html", url::path_s("/directory/index.html"));
    EXPECT_EQ("/directory/", url::path_s("/directory/"));
    EXPECT_EQ("", url::path_s(""));
    EXPECT_EQ("/", url::path_s("/"));
    EXPECT_EQ("/index.html", url::path_s("http://www.example.com/index.html?key=value"));
    EXPECT_EQ("/index.html", url::path_s("http://www.example.com/index.html#anchor"));
    EXPECT_EQ("index.html", url::path_s("index.html?key=value"));
    EXPECT_EQ("/index.html", url::path_s("/index.html#anchor"));
    EXPECT_EQ("index.html", url::path_s("file:index.html"));
    EXPECT_EQ("/index.html", url::path_s("file:/index.html"));
    EXPECT_EQ("/index.html", url::path_s("file:///index.html"));
    EXPECT_EQ("directory/index.html", url::path_s("file:directory/index.html"));
    EXPECT_EQ("/directory/index.html", url::path_s("file:/directory/index.html"));
}

TEST(Url, query)
{
    EXPECT_EQ("", url::query_s("http://www.example.com"));
    EXPECT_EQ("", url::query_s("http://www.example.com/"));
    EXPECT_EQ("", url::query_s("http://www.example.com/index.html"));
    EXPECT_EQ("", url::query_s("http://www.example.com/directory/index.html"));
    EXPECT_EQ("", url::query_s("index.html"));
    EXPECT_EQ("", url::query_s(""));
    EXPECT_EQ("", url::query_s("/"));
    EXPECT_EQ("", url::query_s("file:index.html"));
    EXPECT_EQ("", url::query_s("file:/index.html"));
    EXPECT_EQ("", url::query_s("file:directory/index.html"));
    EXPECT_EQ("", url::query_s("file:/directory/index.html"));
    EXPECT_EQ("", url::query_s("http://www.example.com/index.html#anchor"));
    EXPECT_EQ("", url::query_s("/index.html#anchor"));
    EXPECT_EQ("key=value", url::query_s("http://www.example.com/index.html?key=value"));
    EXPECT_EQ(
        "key1=value1&key2=value2",
        url::query_s("http://www.example.com/index.html?key1=value1&key2=value2"));
    EXPECT_EQ("key=value", url::query_s("http://www.example.com/index.html?key=value#anchor"));
    EXPECT_EQ("key=value", url::query_s("index.html?key=value"));
    EXPECT_EQ("key=value", url::query_s("file:index.html?key=value"));
    EXPECT_EQ("key=value", url::query_s("file:/index.html?key=value"));
    EXPECT_EQ("key=value", url::query_s("?key=value"));
    EXPECT_EQ("", url::query_s("index.html?#anchor"));
}

TEST(Url, anchor)
{
    EXPECT_EQ("", url::anchor_s("http://www.example.com"));
    EXPECT_EQ("", url::anchor_s("http://www.example.com/"));
    EXPECT_EQ("", url::anchor_s("http://www.example.com/index.html"));
    EXPECT_EQ("", url::anchor_s("http://www.example.com/directory/index.html"));
    EXPECT_EQ("", url::anchor_s("index.html"));
    EXPECT_EQ("", url::anchor_s(""));
    EXPECT_EQ("", url::anchor_s("/"));
    EXPECT_EQ("", url::anchor_s("file:index.html"));
    EXPECT_EQ("", url::anchor_s("file:/index.html"));
    EXPECT_EQ("", url::anchor_s("file:directory/index.html"));
    EXPECT_EQ("", url::anchor_s("file:/directory/index.html"));
    EXPECT_EQ("", url::anchor_s("http://www.example.com/index.html?key=value"));
    EXPECT_EQ("", url::anchor_s("#"));
    EXPECT_EQ("anchor", url::anchor_s("http://www.example.com/index.html#anchor"));
    EXPECT_EQ("anchor", url::anchor_s("/index.html#anchor"));
    EXPECT_EQ("anchor", url::anchor_s("file:/index.html#anchor"));
    EXPECT_EQ("anchor", url::anchor_s("file:index.html#anchor"));
    EXPECT_EQ("anchor", url::anchor_s("http://www.example.com/index.html?key=value#anchor"));
    EXPECT_EQ(
        "a-longer-anchor", url::anchor_s("http://www.example.com/index.html#a-longer-anchor"));
}

TEST(Url, parent_directory)
{
    EXPECT_EQ("http://www.example.com", url::parent_directory("http://www.example.com"));
    EXPECT_EQ("http://www.example.com/", url::parent_directory("http://www.example.com/"));
    EXPECT_EQ(
        "http://www.example.com/", url::parent_directory("http://www.example.com/index.html"));
    EXPECT_EQ(
        "http://www.example.com/directory/",
        url::parent_directory("http://www.example.com/directory/"));
    EXPECT_EQ(
        "http://www.example.com/directory/",
        url::parent_directory("http://www.example.com/directory/index"));
    EXPECT_EQ(
        "http://www.example.com/directory/",
        url::parent_directory("http://www.example.com/directory/index.html"));
    EXPECT_EQ(
        "http://www.example.com/directory/",
        url::parent_directory("http://www.example.com/directory/?query"));
    EXPECT_EQ(
        "http://www.example.com/directory/",
        url::parent_directory("http://www.example.com/directory/index.html?query"));
    EXPECT_EQ(
        "http://www.example.com/directory/",
        url::parent_directory("http://www.example.com/directory/index.html#anchor"));
    EXPECT_EQ(
        "http://www.example.com/directory/",
        url::parent_directory("http://www.example.com/directory/index.html?query#anchor"));
    EXPECT_EQ("//www.example.com", url::parent_directory("//www.example.com"));
    EXPECT_EQ("", url::parent_directory("index.html"));
    EXPECT_EQ("directory/", url::parent_directory("directory/index.html"));
    EXPECT_EQ("directory1/directory2/", url::parent_directory("directory1/directory2/index.html"));
    EXPECT_EQ("file:", url::parent_directory("file:index.html"));
    EXPECT_EQ("file:directory/", url::parent_directory("file:directory/"));
    EXPECT_EQ("file:directory/", url::parent_directory("file:directory/index.html"));
    EXPECT_EQ("file:/", url::parent_directory("file:/index.html"));
    EXPECT_EQ("file:///", url::parent_directory("file:///index.html"));
    EXPECT_EQ("file:/directory/", url::parent_directory("file:/directory/"));
    EXPECT_EQ("file:/directory/", url::parent_directory("file:/directory/index.html"));
}

TEST(Url, without_query)
{
    ASSERT_EQ("http://www.example.com", url::without_query("http://www.example.com"));
    ASSERT_EQ("http://www.example.com", url::without_query("http://www.example.com?key=value"));
    ASSERT_EQ(
        "http://www.example.com", url::without_query("http://www.example.com?key1=&key2=value2"));
    ASSERT_EQ("http://www.example.com", url::without_query("http://www.example.com#anchor"));
    ASSERT_EQ(
        "http://www.example.com", url::without_query("http://www.example.com?key=value#anchor"));
    ASSERT_EQ("http://www.example.com/", url::without_query("http://www.example.com/"));
    ASSERT_EQ("http://www.example.com/", url::without_query("http://www.example.com/?key=value"));
    ASSERT_EQ(
        "http://www.example.com/index.html",
        url::without_query("http://www.example.com/index.html"));
    ASSERT_EQ(
        "http://www.example.com/index.html",
        url::without_query("http://www.example.com/index.html?key=value"));
    ASSERT_EQ("index.html", url::without_query("index.html"));
    ASSERT_EQ("index.html", url::without_query("index.html?key=value"));
    ASSERT_EQ("directory/", url::without_query("directory/"));
    ASSERT_EQ("directory/", url::without_query("directory/?key=value"));
    ASSERT_EQ("file:index.html", url::without_query("file:index.html"));
    ASSERT_EQ("file:index.html", url::without_query("file:index.html?key=value"));
    ASSERT_EQ("file:directory/", url::without_query("file:directory/"));
    ASSERT_EQ("file:directory/", url::without_query("file:directory/?key=value"));
}

TEST(Url, query_parameter_value)
{
    ASSERT_EQ("value", url::query_parameter_value("http://www.example.com?key=value", "key"));
    ASSERT_EQ(
        "value1",
        url::query_parameter_value("http://www.example.com?key1=value1&key2=value2", "key1"));
    ASSERT_EQ(
        "value1",
        url::query_parameter_value("http://www.example.com?key1=value1;key2=value2", "key1"));
    ASSERT_EQ(
        "value2",
        url::query_parameter_value("http://www.example.com?key1=value1&key2=value2", "key2"));
    ASSERT_EQ(
        "value2",
        url::query_parameter_value(
            "http://www.example.com?key1=value1&&key2=value2&&key3=value3", "key2"));
    ASSERT_EQ(
        "value2", url::query_parameter_value("http://www.example.com?key1=&key2=value2", "key2"));
    ASSERT_EQ("", url::query_parameter_value("http://www.example.com?key1=value1&key2=", "key2"));
    ASSERT_EQ("", url::query_parameter_value("http://www.example.com", "key"));
    ASSERT_EQ("", url::query_parameter_value("http://www.example.com?key=value", ""));
}

TEST(Url, parameter_value_from_query)
{
    ASSERT_EQ("value", url::parameter_value_from_query("key=value", "key"));
    ASSERT_EQ("value1", url::parameter_value_from_query("key1=value1&key2=value2", "key1"));
    ASSERT_EQ("value1", url::parameter_value_from_query("key1=value1;key2=value2", "key1"));
    ASSERT_EQ("value2", url::parameter_value_from_query("key1=value1&key2=value2", "key2"));
    ASSERT_EQ(
        "value2", url::parameter_value_from_query("key1=value1&&key2=value2&&key3=value3", "key2"));
    ASSERT_EQ("value2", url::parameter_value_from_query("key1=&key2=value2", "key2"));
    ASSERT_EQ("value1", url::parameter_value_from_query("&key1=value1&key2=", "key1"));
    ASSERT_EQ("", url::parameter_value_from_query("key1=value1&key2=", "key2"));
    ASSERT_EQ("", url::parameter_value_from_query("", "key"));
    ASSERT_EQ("", url::parameter_value_from_query("key=value", ""));
}

TEST(Url, join_simple_case)
{
    EXPECT_EQ("http://www.example.com/", url::join({"http://www.example.com"}));
    EXPECT_EQ("http://www.example.com/", url::join({"http://www.example.com", ""}));
    EXPECT_EQ("index.html", url::join({"index.html"}));
    EXPECT_EQ("file:index.html", url::join({"file:index.html"}));
    EXPECT_EQ(
        "http://www.example.com/index.html", url::join({"http://www.example.com/index.html"}));
    EXPECT_EQ(
        "http://www.example.com/index.html", url::join({"http://www.example.com", "index.html"}));
    EXPECT_EQ(
        "http://www.example.com/index.html", url::join({"http://www.example.com/", "index.html"}));
    EXPECT_EQ(
        "http://www.example.com/index.html", url::join({"http://www.example.com", "/index.html"}));
    EXPECT_EQ(
        "http://www.example.com/index.html", url::join({"http://www.example.com/", "/index.html"}));
    EXPECT_EQ("file:index.html", url::join({"file:", "index.html"}));
    EXPECT_EQ("file:/index.html", url::join({"file:", "/index.html"}));
    EXPECT_EQ("file:/index.html", url::join({"file:/", "index.html"}));
    EXPECT_EQ("file:/index.html", url::join({"file:/", "/index.html"}));
    EXPECT_EQ(
        "http://www.example.com/directory/", url::join({"http://www.example.com/", "directory/"}));
    EXPECT_EQ(
        "http://www.example.com/directory/index.html",
        url::join({"http://www.example.com/", "directory/", "index.html"}));
    EXPECT_EQ("file:directory/", url::join({"file:", "directory/"}));
    EXPECT_EQ("file:/directory/", url::join({"file:", "/directory/"}));
    EXPECT_EQ(
        "http://www.example.com/favicon.ico",
        url::join({"http://www.example.com/index.html", "/favicon.ico"}));
    EXPECT_EQ(
        "http://www.example.com/favicon.ico",
        url::join({"http://www.example.com/index.html", "favicon.ico"}));
    EXPECT_EQ(
        "http://www.example.com/index.html", url::join({"http://www.example.com/index.html", ""}));
    EXPECT_EQ("http://www.example.com/", url::join({"http://www.example.com/index.html", "/"}));
    EXPECT_EQ(
        "http://www.example.com/", url::join({"http://www.example.com/pages/index.html", "/"}));
    EXPECT_EQ("file:/", url::join({"file:/directory/index.html", "/"}));
    EXPECT_EQ("file:/", url::join({"file:directory/index.html", "/"}));
}

TEST(Url, join_multiple_absolute_parts)
{
    EXPECT_EQ(
        "http://localhost/home.php",
        url::join({"http://www.example.com", "index.html", "http://localhost/", "home.php"}));
}

TEST(Url, join_multiple_relative_parts)
{
    EXPECT_EQ("directory/document", url::join({"directory/", "document"}));
    EXPECT_EQ(
        "directory1/directory2/document", url::join({"directory1/", "directory2/", "document"}));
    EXPECT_EQ(
        "http://www.example.com/index.html",
        url::join({"http://www.example.com/", "directory", "index.html"}));
    EXPECT_EQ("file:directory/index.html", url::join({"file:", "directory/", "index.html"}));
    EXPECT_EQ("file:/directory/index.html", url::join({"file:/", "directory/", "index.html"}));
}

TEST(Url, join_query_parameters)
{
    EXPECT_EQ(
        "http://www.example.com/index.html?key=value",
        url::join({"http://www.example.com/index.html", "?key=value"}));
    EXPECT_EQ(
        "http://www.example.com/index.html?key1=value1&key2=value2",
        url::join({"http://www.example.com/index.html", "?key1=value1&key2=value2"}));
    EXPECT_EQ(
        "http://www.example.com/?key=value", url::join({"http://www.example.com", "?key=value"}));
    EXPECT_EQ(
        "http://www.example.com/test?a=b",
        url::join({"http://www.example.com/index?c=d", "test?a=b"}));
    EXPECT_EQ(
        "http://www.example.com/a/test?a=b",
        url::join({"http://www.example.com/index?c=d", "a/?p=p", "test?a=b"}));
    EXPECT_EQ(
        "http://www.example.com/test?a=b",
        url::join({"http://www.example.com/index?c=d", "test?b=c", "?a=b"}));
    EXPECT_EQ(
        "http://www.example.com/test?c=d", url::join({"http://www.example.com/test?c=d", ""}));
}

TEST(Url, join_absolute_paths)
{
    EXPECT_EQ(
        "http://www.example.com/home.php",
        url::join({"http://www.example.com", "index.html", "/home.php"}));
    EXPECT_EQ(
        "http://www.example.com/home.php",
        url::join(
            {"http://www.example.com",
             "directory/"
             "index.html",
             "/home.php"}));
    EXPECT_EQ("file:/index.html", url::join({"file:", "directory/", "/index.html"}));
}

TEST(Url, join_root_paths)
{
    EXPECT_EQ("/a/b/c.txt", url::join({"/a/b/", "c.txt"}));
    EXPECT_EQ("/c.txt", url::join({"/a/b/", "/c.txt"}));
    EXPECT_EQ("/a/b/c.txt", url::join({"/a/b/", "/a/b/c.txt"}));
    EXPECT_EQ("/d/e/f.txt", url::join({"/a/b/c.txt", "/d/e/f.txt"}));
}

TEST(Url, join_protocol_relative)
{
    EXPECT_EQ(
        "http://localhost/home.php",
        url::join({"http://www.example.com", "index.html", "//localhost", "home.php"}));
}

TEST(Url, join_full_urls)
{
    EXPECT_EQ(
        "https://www.fromage.com/goat/cabecou.png",
        url::join(
            {"http://www.example.com/dir1/dir2", "https://www.fromage.com/goat/cabecou.png"}));

    EXPECT_EQ(
        "https://www.fromage.com/goat/crotin.jpg",
        url::join(
            {"http://www.example.com/dir1/dir2", "https://www.fromage.com/goat/cabecou.png",
             "crotin.jpg"}));
}

TEST(Url, percent_encode)
{
    ASSERT_EQ("alpha-num_chars_and_~M0RE~.", url::percent_encode("alpha-num_chars_and_~M0RE~."));
    ASSERT_EQ("%20a%20b%20c%20%20d%20", url::percent_encode(" a b c  d "));
    ASSERT_EQ(
        "http%3A%2F%2Fwww.example.com%2F%3Fquery%23anchor",
        url::percent_encode("http://www.example.com/?query#anchor"));
    ASSERT_EQ(
        "%E6%97%A5%E6%9C%AC%E8%AA%9E", url::percent_encode("\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e"));
    ASSERT_EQ("", url::percent_encode(""));
}

TEST(Url, data_url_all_empty)
{
    url::EncodedData url;
    ASSERT_TRUE(url::parse_data_url_s("data:,", &url));
    ASSERT_EQ(url.is_base64, false);
    ASSERT_EQ(url.mime_type, "");
    ASSERT_EQ(url.encoded_payload, "");
}

TEST(Url, data_url_empty_with_mime)
{
    url::EncodedData url;
    ASSERT_TRUE(url::parse_data_url_s("data:text/css,", &url));
    ASSERT_EQ(url.is_base64, false);
    ASSERT_EQ(url.mime_type, "text/css");
    ASSERT_EQ(url.encoded_payload, "");
}

TEST(Url, data_url_empty_with_mime_params)
{
    url::EncodedData url;
    ASSERT_TRUE(url::parse_data_url_s("data:text/javascript;hello=world,", &url));
    ASSERT_EQ(url.is_base64, false);
    ASSERT_EQ(url.mime_type, "text/javascript");
    ASSERT_EQ(url.encoded_payload, "");
}

TEST(Url, data_url_empty_base64)
{
    url::EncodedData url;
    ASSERT_TRUE(url::parse_data_url_s("data:;base64,", &url));
    ASSERT_EQ(url.is_base64, true);
    ASSERT_EQ(url.mime_type, "");
    ASSERT_EQ(url.encoded_payload, "");
}

TEST(Url, data_url_empty_with_mime_base64)
{
    url::EncodedData url;
    ASSERT_TRUE(url::parse_data_url_s("data:text/css;base64,", &url));
    ASSERT_EQ(url.is_base64, true);
    ASSERT_EQ(url.mime_type, "text/css");
    ASSERT_EQ(url.encoded_payload, "");
}

TEST(Url, data_url_empty_with_mime_params_base64)
{
    url::EncodedData url;
    ASSERT_TRUE(url::parse_data_url_s("data:text/css;hello=world;a=b;base64,", &url));
    ASSERT_EQ(url.is_base64, true);
    ASSERT_EQ(url.mime_type, "text/css");
    ASSERT_EQ(url.encoded_payload, "");
}

TEST(Url, data_url_ascii_payload)
{
    url::EncodedData url;
    ASSERT_TRUE(url::parse_data_url_s("data:text/plain,Hello%2C%20world%21", &url));
    ASSERT_EQ(url.is_base64, false);
    ASSERT_EQ(url.mime_type, "text/plain");
    ASSERT_EQ(url.encoded_payload, "Hello%2C%20world%21");
}

TEST(Url, data_url_base64_payload)
{
    url::EncodedData url;
    ASSERT_TRUE(url::parse_data_url_s("data:text/plain;base64,SGVsbG8sIHdvcmxkIQ==", &url));
    ASSERT_EQ(url.is_base64, true);
    ASSERT_EQ(url.mime_type, "text/plain");
    ASSERT_EQ(url.encoded_payload, "SGVsbG8sIHdvcmxkIQ==");
}

TEST(Url, append_query_parameters)
{
    EXPECT_EQ(
        url::append_query_parameters("https://example.com/index.html", {}),
        "https://example.com/index.html");
    EXPECT_EQ(
        url::append_query_parameters("https://example.com/index.html", {{"a", "b"}}),
        "https://example.com/index.html?a=b");
    EXPECT_EQ(
        url::append_query_parameters("https://example.com/index.html", {{"a", "b"}, {"c", "d"}}),
        "https://example.com/index.html?a=b&c=d");
    EXPECT_EQ(
        url::append_query_parameters("https://example.com/index.html?a=b", {{"c", "d"}}),
        "https://example.com/index.html?a=b&c=d");
    EXPECT_EQ(
        url::append_query_parameters("https://example.com/index.html?a=b&c=d", {}),
        "https://example.com/index.html?a=b&c=d");
    EXPECT_EQ(
        url::append_query_parameters("https://example.com/?a=b", {{"c", "d"}}),
        "https://example.com/?a=b&c=d");
    EXPECT_EQ(
        url::append_query_parameters("https://example.com/", {{"c", "d"}}),
        "https://example.com/?c=d");
    EXPECT_EQ(
        url::append_query_parameters("https://example.com", {{"c", "d"}}),
        "https://example.com/?c=d");
    EXPECT_EQ(
        url::append_query_parameters("https://example.com/path", {{"c", "d"}}),
        "https://example.com/path?c=d");
    EXPECT_EQ(
        url::append_query_parameters("https://example.com/path/", {{"c", "d"}}),
        "https://example.com/path/?c=d");
}
