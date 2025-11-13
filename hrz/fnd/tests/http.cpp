#include "hrz/fnd/http.h"

#include <gtest/gtest.h>

using namespace hrz;

TEST(HttpHeaders, GetSetHeaders)
{
    HttpHeaders headers;
    EXPECT_EQ(headers.get_header("Hello"), "");

    headers.set_header("Hello", "World");
    EXPECT_EQ(headers.get_header("Hello"), "World");

    // Test case insensitiveness
    headers.set_header("HELLO", "World2");
    EXPECT_EQ(headers.get_header("heLlO"), "World2");
}

TEST(HttpHeaders, SetAgainLarger)
{
    HttpHeaders headers;
    headers.set_header("Hello", "World");
    headers.set_header("Hello", "Friends!!!");
    EXPECT_EQ(headers.get_header("Hello"), "Friends!!!");
}

TEST(HttpHeaders, SetAgainSmaller)
{
    HttpHeaders headers;
    headers.set_header("Hello", "World");
    auto old = headers.get_header("Hello");

    headers.set_header("Hello", "wrld");
    EXPECT_EQ(headers.get_header("Hello"), "wrld");
    // Test that we recycled the memory
    EXPECT_EQ(headers.get_header("Hello").data(), old.data());

    headers.set_header("Hello", "aaaa");
    EXPECT_EQ(headers.get_header("Hello"), "aaaa");
    // Test that we recycled the memory
    EXPECT_EQ(headers.get_header("Hello").data(), old.data());
}

TEST(HttpHeaders, Move)
{
    HttpHeaders headers;
    headers.set_header("Hello", "World");
    headers.set_header("Hello2", "World2");

    HttpHeaders headers2 = std::move(headers);
    EXPECT_EQ(headers2.get_header("HELLO"), "World");
    EXPECT_EQ(headers2.get_header("HELLO2"), "World2");
}

TEST(HttpHeaders, Copy)
{
    HttpHeaders headers;
    headers.set_header("Hello", "World");
    headers.set_header("Hello2", "World2");

    HttpHeaders headers2 = headers;
    EXPECT_EQ(headers2.get_header("HELLO"), "World");
    EXPECT_EQ(headers2.get_header("HELLO2"), "World2");
}

TEST(HttpHeaders, Remove)
{
    HttpHeaders headers;
    headers.set_header("Hello", "World");
    headers.set_header("Hello2", "World2");

    headers.remove_header("Hello");

    EXPECT_EQ(headers.get_header("HELLO"), "");
    EXPECT_EQ(headers.get_header("HELLO2"), "World2");
}

TEST(HttpHeaders, Hash)
{
    HttpHeaders headers1;
    headers1.set_header("Hello", "World");
    headers1.set_header("Hello2", "World2");

    HttpHeaders headers2;
    headers2.set_header("Hello2", "World2");
    headers2.set_header("HELLO", "World");

    HttpHeaders headers3;
    headers3.set_header("Hello2", "World3");
    headers3.set_header("HELLO", "World");

    HttpHeaders headers4;

    EXPECT_EQ(headers1.hash_full(), headers2.hash_full());
    EXPECT_NE(headers1.hash_full(), headers3.hash_full());
    EXPECT_NE(headers1.hash_full(), headers4.hash_full());
    EXPECT_NE(headers2.hash_full(), headers3.hash_full());
    EXPECT_NE(headers2.hash_full(), headers4.hash_full());
    EXPECT_NE(headers3.hash_full(), headers4.hash_full());

    HttpHeaders headers5 = headers1;
    HttpHeaders headers6 = headers3;

    EXPECT_NE(headers5.hash_full(), headers6.hash_full());

    headers5.remove_header("hello2");
    headers6.remove_header("hello2");
    EXPECT_EQ(headers5.hash_full(), headers6.hash_full());
}

TEST(HttpHeaders, HashContent)
{
    HttpHeaders headers1;
    headers1.set_header("Hello", "World");

    HttpHeaders headers2;
    headers2.set_header("Hello", "World");
    headers2.set_header("Accept", "yes");

    HttpHeaders headers3;
    headers3.set_header("Hello", "World");
    headers3.set_header("Accept", "no");

    HttpHeaders headers4;
    headers4.set_header("Hello", "World4");
    headers4.set_header("Accept", "yes");

    HttpHeaders headers5;
    headers1.set_header("Hello", "Worldpopopo");

    EXPECT_NE(headers1.hash_content(), headers2.hash_content());
    EXPECT_NE(headers1.hash_content(), headers3.hash_content());
    EXPECT_NE(headers1.hash_content(), headers4.hash_content());
    EXPECT_NE(headers2.hash_content(), headers3.hash_content());
    EXPECT_EQ(headers2.hash_content(), headers4.hash_content());
    EXPECT_NE(headers3.hash_content(), headers4.hash_content());
    EXPECT_EQ(headers1.hash_content(), headers5.hash_content());
}

struct DateExample
{
    const char* imf_fixdate;
    int64_t timestamp;
};

static const DateExample date_examples[] = {
    {"Sun, 14 Jan 2029 12:57:35 GMT", 1863089855}, {"Mon, 15 Jan 2029 03:54:44 GMT", 1863143684},
    {"Tue, 16 Jan 2029 14:17:41 GMT", 1863267461}, {"Wed, 17 Jan 2029 19:08:41 GMT", 1863371321},
    {"Thu, 18 Jan 2029 21:53:33 GMT", 1863467613}, {"Fri, 19 Jan 2029 08:59:33 GMT", 1863507573},
    {"Sat, 20 Jan 2029 00:41:23 GMT", 1863564083}, {"Sat, 12 Feb 2011 16:27:56 GMT", 1297528076},
    {"Sun, 13 Feb 2011 17:26:41 GMT", 1297618001}, {"Mon, 14 Feb 2011 08:48:52 GMT", 1297673332},
    {"Tue, 15 Feb 2011 23:52:13 GMT", 1297813933}, {"Wed, 16 Feb 2011 05:24:42 GMT", 1297833882},
    {"Thu, 17 Feb 2011 16:14:56 GMT", 1297959296}, {"Fri, 18 Feb 2011 17:11:07 GMT", 1298049067},
    {"Mon, 03 Mar 2003 21:20:04 GMT", 1046726404}, {"Tue, 04 Mar 2003 12:02:17 GMT", 1046779337},
    {"Wed, 05 Mar 2003 00:38:05 GMT", 1046824685}, {"Thu, 06 Mar 2003 14:53:10 GMT", 1046962390},
    {"Fri, 07 Mar 2003 08:45:08 GMT", 1047026708}, {"Sat, 08 Mar 2003 01:01:17 GMT", 1047085277},
    {"Sun, 09 Mar 2003 15:36:32 GMT", 1047224192}, {"Fri, 19 Apr 1991 11:40:50 GMT", 672061250},
    {"Sat, 20 Apr 1991 04:57:51 GMT", 672123471},  {"Sun, 21 Apr 1991 22:25:38 GMT", 672272738},
    {"Mon, 22 Apr 1991 19:30:16 GMT", 672348616},  {"Tue, 23 Apr 1991 21:42:17 GMT", 672442937},
    {"Wed, 24 Apr 1991 07:02:34 GMT", 672476554},  {"Thu, 25 Apr 1991 03:44:20 GMT", 672551060},
    {"Thu, 03 May 2001 14:51:27 GMT", 988901487},  {"Fri, 04 May 2001 04:48:30 GMT", 988951710},
    {"Sat, 05 May 2001 00:07:29 GMT", 989021249},  {"Sun, 06 May 2001 05:29:25 GMT", 989126965},
    {"Mon, 07 May 2001 22:16:01 GMT", 989273761},  {"Tue, 08 May 2001 18:52:25 GMT", 989347945},
    {"Wed, 09 May 2001 21:23:14 GMT", 989443394},  {"Sun, 11 Jun 2000 17:17:11 GMT", 960743831},
    {"Mon, 12 Jun 2000 03:12:01 GMT", 960779521},  {"Tue, 13 Jun 2000 10:37:53 GMT", 960892673},
    {"Wed, 14 Jun 2000 12:41:36 GMT", 960986496},  {"Thu, 15 Jun 2000 18:18:04 GMT", 961093084},
    {"Fri, 16 Jun 2000 19:34:55 GMT", 961184095},  {"Sat, 17 Jun 2000 11:56:35 GMT", 961242995},
    {"Tue, 08 Jul 1980 11:05:30 GMT", 331902330},  {"Wed, 09 Jul 1980 01:54:07 GMT", 331955647},
    {"Thu, 10 Jul 1980 16:18:47 GMT", 332093927},  {"Fri, 11 Jul 1980 03:07:33 GMT", 332132853},
    {"Sat, 12 Jul 1980 20:37:28 GMT", 332282248},  {"Sun, 13 Jul 1980 00:43:08 GMT", 332296988},
    {"Mon, 14 Jul 1980 03:12:02 GMT", 332392322},  {"Sun, 08 Aug 1982 17:12:39 GMT", 397674759},
    {"Mon, 09 Aug 1982 05:21:50 GMT", 397718510},  {"Tue, 10 Aug 1982 04:12:53 GMT", 397800773},
    {"Wed, 11 Aug 1982 18:39:20 GMT", 397939160},  {"Thu, 12 Aug 1982 04:45:47 GMT", 397975547},
    {"Fri, 13 Aug 1982 17:47:54 GMT", 398108874},  {"Sat, 14 Aug 1982 10:41:39 GMT", 398169699},
    {"Fri, 10 Sep 2027 14:37:32 GMT", 1820587052}, {"Sat, 11 Sep 2027 11:15:38 GMT", 1820661338},
    {"Sun, 12 Sep 2027 02:18:55 GMT", 1820715535}, {"Mon, 13 Sep 2027 12:40:17 GMT", 1820839217},
    {"Tue, 14 Sep 2027 14:17:22 GMT", 1820931442}, {"Wed, 15 Sep 2027 22:51:44 GMT", 1821048704},
    {"Thu, 16 Sep 2027 07:19:18 GMT", 1821079158}, {"Sat, 06 Oct 2007 22:18:09 GMT", 1191709089},
    {"Sun, 07 Oct 2007 21:19:06 GMT", 1191791946}, {"Mon, 08 Oct 2007 17:01:42 GMT", 1191862902},
    {"Tue, 09 Oct 2007 23:08:19 GMT", 1191971299}, {"Wed, 10 Oct 2007 19:25:06 GMT", 1192044306},
    {"Thu, 11 Oct 2007 23:14:14 GMT", 1192144454}, {"Fri, 12 Oct 2007 18:09:58 GMT", 1192212598},
    {"Sun, 10 Nov 2024 09:19:51 GMT", 1731230391}, {"Mon, 11 Nov 2024 20:09:32 GMT", 1731355772},
    {"Tue, 12 Nov 2024 18:09:27 GMT", 1731434967}, {"Wed, 13 Nov 2024 15:40:33 GMT", 1731512433},
    {"Thu, 14 Nov 2024 14:15:42 GMT", 1731593742}, {"Fri, 15 Nov 2024 09:57:59 GMT", 1731664679},
    {"Sat, 16 Nov 2024 03:00:41 GMT", 1731726041}, {"Sun, 05 Dec 1982 13:36:52 GMT", 407943412},
    {"Mon, 06 Dec 1982 13:50:30 GMT", 408030630},  {"Tue, 07 Dec 1982 07:41:10 GMT", 408094870},
    {"Wed, 08 Dec 1982 23:01:10 GMT", 408236470},  {"Thu, 09 Dec 1982 10:35:13 GMT", 408278113},
    {"Fri, 10 Dec 1982 22:03:11 GMT", 408405791},  {"Sat, 11 Dec 1982 16:48:40 GMT", 408473320},
    {"Ven, 10 Dec 1982 22:03:11 GMT", -1},         {"Sat, 11 Aou 1982 16:48:40 GMT", -1},
    {"Fri, te Dec 1982 22:03:11 GMT", -1},         {"Sat, 11 Dec 1982 16:48:40ABCD", -1},
    {"Fri, 10 Dec 1982 22:03:11 GMTzefzef", -1},   {"", -1},
};

TEST(Http, ParseImfFixdate)
{
    for (const auto& example : date_examples)
    {
        SCOPED_TRACE(example.imf_fixdate);
        EXPECT_EQ(
            HttpTime::from_imf_fixdate(example.imf_fixdate).unix_timestamp(), example.timestamp);
    }

    EXPECT_EQ(
        HttpTime::from_imf_fixdate("Fri, 10 Dec 1982 22:03:11 GMT").unix_timestamp(), 408405791);
    EXPECT_EQ(
        HttpTime::from_imf_fixdate("FRI, 10 DEC 1982 22:03:11 GMT").unix_timestamp(), 408405791);
    EXPECT_EQ(
        HttpTime::from_imf_fixdate("fri, 10 dec 1982 22:03:11 gmt").unix_timestamp(), 408405791);
    EXPECT_EQ(
        HttpTime::from_imf_fixdate("\"Fri, 10 Dec 1982 22:03:11 GMT\"").unix_timestamp(),
        408405791);
    EXPECT_EQ(
        HttpTime::from_imf_fixdate("    \"Fri, 10 Dec 1982 22:03:11 GMT\"   ").unix_timestamp(),
        408405791);
    EXPECT_FALSE(HttpTime::from_imf_fixdate("\"Fri, 10 Dec 1982 22:03:11 GMT").is_valid());
    EXPECT_FALSE(HttpTime::from_imf_fixdate("").is_valid());
    EXPECT_FALSE(HttpTime::from_imf_fixdate("    ").is_valid());
    EXPECT_FALSE(HttpTime::from_imf_fixdate("\"").is_valid());
    EXPECT_FALSE(HttpTime::from_imf_fixdate("  \"  ").is_valid());
}

TEST(Http, WriteImfFixdate)
{
    for (const auto& example : date_examples)
    {
        SCOPED_TRACE(example.imf_fixdate);
        auto time = HttpTime::from_imf_fixdate(example.imf_fixdate);
        if (time.is_valid())
        {
            char buffer[30];
            time.write_imf_fixdate(buffer);
            ASSERT_EQ(buffer[29], '\0');
            EXPECT_STREQ(buffer, example.imf_fixdate);
        }
    }
}

TEST(Http, ParseHeaderValueEmpty)
{
    int visited = 0;

    parse_http_header_value(
        "", ',',
        [&](std::string_view field, std::string_view value)
        {
            visited++;
            return true;
        });
    EXPECT_EQ(visited, 0);

    visited = 0;
    parse_http_header_value(
        "    ", ',',
        [&](std::string_view field, std::string_view value)
        {
            visited++;
            return true;
        });
    EXPECT_EQ(visited, 0);

    visited = 0;
    parse_http_header_value(
        " , ", ',',
        [&](std::string_view field, std::string_view value)
        {
            visited++;
            EXPECT_EQ(field, "");
            EXPECT_EQ(value, "");
            return true;
        });
    EXPECT_EQ(visited, 1);

    visited = 0;
    parse_http_header_value(
        ",", ',',
        [&](std::string_view field, std::string_view value)
        {
            visited++;
            EXPECT_EQ(field, "");
            EXPECT_EQ(value, "");
            return true;
        });
    EXPECT_EQ(visited, 1);

    visited = 0;
    parse_http_header_value(
        " ,  , ", ',',
        [&](std::string_view field, std::string_view value)
        {
            visited++;
            EXPECT_EQ(field, "");
            EXPECT_EQ(value, "");
            return true;
        });
    EXPECT_EQ(visited, 2);

    visited = 0;
    parse_http_header_value(
        ",,", ',',
        [&](std::string_view field, std::string_view value)
        {
            visited++;
            EXPECT_EQ(field, "");
            EXPECT_EQ(value, "");
            return true;
        });
    EXPECT_EQ(visited, 2);

    visited = 0;
    parse_http_header_value(
        " ,, ", ',',
        [&](std::string_view field, std::string_view value)
        {
            visited++;
            EXPECT_EQ(field, "");
            EXPECT_EQ(value, "");
            return true;
        });
    EXPECT_EQ(visited, 2);
}

TEST(Http, ParseHeaderValueSimple)
{
    int visited = 0;

    parse_http_header_value(
        "Hello world", ',',
        [&](std::string_view field, std::string_view value)
        {
            visited++;
            EXPECT_EQ(field, "Hello world");
            EXPECT_EQ(value, "");
            return true;
        });
    EXPECT_EQ(visited, 1);

    visited = 0;
    parse_http_header_value(
        "   Hello world   ", ',',
        [&](std::string_view field, std::string_view value)
        {
            visited++;
            EXPECT_EQ(field, "Hello world");
            EXPECT_EQ(value, "");
            return true;
        });
    EXPECT_EQ(visited, 1);
}

TEST(Http, ParseHeaderValueQuoted)
{
    int visited = 0;

    parse_http_header_value(
        "\"Hello \\\" my \\\\ name is Jeff   \"", ',',
        [&](std::string_view field, std::string_view value)
        {
            visited++;
            EXPECT_EQ(field, "\"Hello \\\" my \\\\ name is Jeff   \"");
            EXPECT_EQ(value, "");
            return true;
        });
    EXPECT_EQ(visited, 1);

    visited = 0;
    parse_http_header_value(
        "   \"Hello \\\" my \\\\ name is Jeff   \"   ", ',',
        [&](std::string_view field, std::string_view value)
        {
            visited++;
            EXPECT_EQ(field, "\"Hello \\\" my \\\\ name is Jeff   \"");
            EXPECT_EQ(value, "");
            return true;
        });
    EXPECT_EQ(visited, 1);
}

TEST(Http, ParseHeaderValueQuotedError)
{
    int visited = 0;

    parse_http_header_value(
        "   \"Hello \\\" my \\\\ name is Jeff    ", ',',
        [&](std::string_view field, std::string_view value)
        {
            visited++;
            return true;
        });
    EXPECT_EQ(visited, 0);
}

TEST(Http, ParseHeaderValueCommaSeparated)
{
    int visited = 0;

    const char* expected[5] = {"a", "b", "cd", "ed", "hello\"world\""};

    parse_http_header_value(
        "a,b,cd,ed,hello\"world\"", ',',
        [&](std::string_view field, std::string_view value)
        {
            EXPECT_TRUE(visited < 5);
            assert(visited < 5);
            EXPECT_EQ(field, expected[visited]);
            EXPECT_EQ(value, "");
            visited++;
            return true;
        });
    EXPECT_EQ(visited, 5);

    visited = 0;
    parse_http_header_value(
        "a,b  ,    cd,  ed   , hello\"world\"  ", ',',
        [&](std::string_view field, std::string_view value)
        {
            EXPECT_TRUE(visited < 5);
            assert(visited < 5);
            EXPECT_EQ(field, expected[visited]);
            EXPECT_EQ(value, "");
            visited++;
            return true;
        });
    EXPECT_EQ(visited, 5);

    visited = 0;
    parse_http_header_value(
        "a,b  ,    cd,  ed   , hello\"world\"  ,", ',',
        [&](std::string_view field, std::string_view value)
        {
            EXPECT_TRUE(visited < 5);
            assert(visited < 5);
            EXPECT_EQ(field, expected[visited]);
            EXPECT_EQ(value, "");
            visited++;
            return true;
        });
    EXPECT_EQ(visited, 5);

    const char* expected2[7] = {"a", "b", "", "cd", "", "ed", "hello\"world\""};

    visited = 0;
    parse_http_header_value(
        "  a,b  ,  ,  cd,,  ed   , hello\"world\"  , ", ',',
        [&](std::string_view field, std::string_view value)
        {
            EXPECT_TRUE(visited < 7);
            assert(visited < 7);
            EXPECT_EQ(field, expected2[visited]);
            EXPECT_EQ(value, "");
            visited++;
            return true;
        });
    EXPECT_EQ(visited, 7);
}

TEST(Http, ParseHeaderValuePair)
{
    int visited;

    visited = 0;
    parse_http_header_value(
        "  a=27 ", ',',
        [&](std::string_view field, std::string_view value)
        {
            EXPECT_EQ(field, "a");
            EXPECT_EQ(value, "27");
            visited++;
            return true;
        });
    EXPECT_EQ(visited, 1);

    visited = 0;
    parse_http_header_value(
        "  hello=\"Bonjour\" ", ',',
        [&](std::string_view field, std::string_view value)
        {
            EXPECT_EQ(field, "hello");
            EXPECT_EQ(value, "\"Bonjour\"");
            visited++;
            return true;
        });
    EXPECT_EQ(visited, 1);

    visited = 0;
    parse_http_header_value(
        "max-age = 60 ", ',',
        [&](std::string_view field, std::string_view value)
        {
            EXPECT_EQ(field, "max-age");
            EXPECT_EQ(value, "60");
            visited++;
            return true;
        });
    EXPECT_EQ(visited, 1);

    visited = 0;
    parse_http_header_value(
        " max-age= 60 ", ',',
        [&](std::string_view field, std::string_view value)
        {
            EXPECT_EQ(field, "max-age");
            EXPECT_EQ(value, "60");
            visited++;
            return true;
        });
    EXPECT_EQ(visited, 1);

    visited = 0;
    parse_http_header_value(
        "max-age=60", ',',
        [&](std::string_view field, std::string_view value)
        {
            EXPECT_EQ(field, "max-age");
            EXPECT_EQ(value, "60");
            visited++;
            return true;
        });
    EXPECT_EQ(visited, 1);

    visited = 0;
    parse_http_header_value(
        " =hello", ',',
        [&](std::string_view field, std::string_view value)
        {
            EXPECT_EQ(field, "");
            EXPECT_EQ(value, "hello");
            visited++;
            return true;
        });
    EXPECT_EQ(visited, 1);

    visited = 0;
    parse_http_header_value(
        "hello=", ',',
        [&](std::string_view field, std::string_view value)
        {
            EXPECT_EQ(field, "hello");
            EXPECT_EQ(value, "");
            visited++;
            return true;
        });
    EXPECT_EQ(visited, 1);

    visited = 0;
    parse_http_header_value(
        "hello= ", ',',
        [&](std::string_view field, std::string_view value)
        {
            EXPECT_EQ(field, "hello");
            EXPECT_EQ(value, "");
            visited++;
            return true;
        });
    EXPECT_EQ(visited, 1);
}

TEST(Http, ParseHeaderValuePairCommaSeparated)
{
    int visited = 0;
    const char* test_str = "hello=world, hello = world  ,  hello=,=hello,, hello";
    const char* expected_keys[] = {"hello", "hello", "hello", "", "", "hello"};
    const char* expected_values[] = {"world", "world", "", "hello", "", ""};

    parse_http_header_value(
        test_str, ',',
        [&](std::string_view field, std::string_view value)
        {
            EXPECT_TRUE(visited < 6);
            assert(visited < 6);
            EXPECT_EQ(expected_keys[visited], field);
            EXPECT_EQ(expected_values[visited], value);
            visited += 1;
            return true;
        });
    EXPECT_EQ(visited, 6);
}
