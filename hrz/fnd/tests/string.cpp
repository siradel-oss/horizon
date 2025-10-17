#include <hrz_fnd_mem.h>
#include <hrz_fnd_string_utils.h>

#include <gtest/gtest.h>

using namespace hrz;

TEST(String, rtrim)
{
    EXPECT_EQ("aabbaa", str::rtrim("aabbaabb", 'b'));
    EXPECT_EQ("aabbaabb", str::rtrim("aabbaabb", 'a'));
    EXPECT_EQ("", str::rtrim("aaa", 'a'));
    EXPECT_EQ("", str::rtrim("a", 'a'));
    EXPECT_EQ("", str::rtrim("", 'a'));
}

TEST(String, rtrim_s)
{
    EXPECT_EQ("aabbaa", str::rtrim_s("aabbaabb", 'b'));
    EXPECT_EQ("aabbaabb", str::rtrim_s("aabbaabb", 'a'));
    EXPECT_EQ("", str::rtrim_s("aaa", 'a'));
    EXPECT_EQ("", str::rtrim_s("a", 'a'));
    EXPECT_EQ("", str::rtrim_s("", 'a'));
}

TEST(String, ltrim)
{
    EXPECT_EQ("aabbaabb", str::ltrim("aabbaabb", 'b'));
    EXPECT_EQ("bbaabb", str::ltrim("aabbaabb", 'a'));
    EXPECT_EQ("", str::ltrim("aaa", 'a'));
    EXPECT_EQ("", str::ltrim("a", 'a'));
    EXPECT_EQ("", str::ltrim("", 'a'));
}

TEST(String, ltrim_s)
{
    EXPECT_EQ("aabbaabb", str::ltrim_s("aabbaabb", 'b'));
    EXPECT_EQ("bbaabb", str::ltrim_s("aabbaabb", 'a'));
    EXPECT_EQ("", str::ltrim_s("aaa", 'a'));
    EXPECT_EQ("", str::ltrim_s("a", 'a'));
    EXPECT_EQ("", str::ltrim_s("", 'a'));
}

TEST(String, rfind)
{
    EXPECT_EQ(0, str::rfind("abcd", 'a'));
    EXPECT_EQ(2, str::rfind("abad", 'a'));
    EXPECT_EQ(1, str::rfind("abcd", 'b'));
    EXPECT_EQ(3, str::rfind("abcd", 'd'));
    EXPECT_EQ(-1, str::rfind("abcd", 'z'));
    EXPECT_EQ(-1, str::rfind("", 'a'));
}

TEST(String, find)
{
    EXPECT_EQ(0, str::find("abcd", 'a'));
    EXPECT_EQ(0, str::find("abad", 'a'));
    EXPECT_EQ(1, str::find("abcd", 'b'));
    EXPECT_EQ(3, str::find("abcd", 'd'));
    EXPECT_EQ(-1, str::find("abcd", 'z'));
    EXPECT_EQ(-1, str::find("", 'a'));
}

TEST(String, starts_with)
{
    EXPECT_TRUE(str::starts_with("hello world", "hell"));
    EXPECT_FALSE(str::starts_with("hello world", "help"));
    EXPECT_FALSE(str::starts_with("hello world", "hello world!"));
    EXPECT_TRUE(str::starts_with("hello world", "hello world"));
    EXPECT_TRUE(str::starts_with("hello world", "h"));
    EXPECT_TRUE(str::starts_with("hello world", ""));
    EXPECT_TRUE(str::starts_with("", ""));
    EXPECT_FALSE(str::starts_with("", "hi"));
}

TEST(String, istarts_with)
{
    EXPECT_TRUE(str::istarts_with("heLLo world", "hell"));
    EXPECT_FALSE(str::istarts_with("hEllo world", "hElP"));
    EXPECT_FALSE(str::istarts_with("hello world", "HELLO WORLD!"));
    EXPECT_TRUE(str::istarts_with("hELLO WORLD", "hello world"));
    EXPECT_TRUE(str::istarts_with("hello world", "h"));
    EXPECT_TRUE(str::istarts_with("hello world", ""));
    EXPECT_TRUE(str::istarts_with("", ""));
    EXPECT_FALSE(str::istarts_with("", "hi"));
}

TEST(String, iequals)
{
    EXPECT_TRUE(str::iequals("", ""));
    EXPECT_TRUE(str::iequals("abcd", "abcd"));
    EXPECT_TRUE(str::iequals("ABCD", "ABCD"));
    EXPECT_TRUE(str::iequals("aBcD", "AbCd"));
    EXPECT_TRUE(str::iequals("ABcd", "aBCd"));
    EXPECT_FALSE(str::iequals("abcd", "abcd "));
    EXPECT_FALSE(str::iequals("ijkl", "ijkI"));
}

TEST(String, decode_base64_size_hint)
{
    EXPECT_EQ(20, str::decode_base64_size_hint("YW55IGNhcm5hbCBwbGVhc3VyZS4="));
    EXPECT_EQ(19, str::decode_base64_size_hint("YW55IGNhcm5hbCBwbGVhc3VyZQ=="));
    EXPECT_EQ(18, str::decode_base64_size_hint("YW55IGNhcm5hbCBwbGVhc3Vy"));
    EXPECT_EQ(17, str::decode_base64_size_hint("YW55IGNhcm5hbCBwbGVhc3U="));
    EXPECT_EQ(16, str::decode_base64_size_hint("YW55IGNhcm5hbCBwbGVhcw=="));

    EXPECT_EQ(0, str::decode_base64_size_hint("YW55IGNhcm5hbCBwbGVhcw="));
    EXPECT_EQ(0, str::decode_base64_size_hint("c=="));
    EXPECT_EQ(0, str::decode_base64_size_hint(""));
}

TEST(String, decode_base64)
{
    std::vector<std::byte> v;

    v.clear();
    EXPECT_EQ(20, str::decode_base64("YW55IGNhcm5hbCBwbGVhc3VyZS4=", &v));
    v.push_back(std::byte{0});
    v.pop_back();
    EXPECT_STREQ("any carnal pleasure.", (const char*)v.data());

    v.clear();
    EXPECT_EQ(19, str::decode_base64("YW55IGNhcm5hbCBwbGVhc3VyZQ==", &v));
    v.push_back(std::byte{0});
    v.pop_back();
    EXPECT_STREQ("any carnal pleasure", (const char*)v.data());

    v.clear();
    EXPECT_EQ(18, str::decode_base64("YW55IGNhcm5hbCBwbGVhc3Vy", &v));
    v.push_back(std::byte{0});
    v.pop_back();
    EXPECT_STREQ("any carnal pleasur", (const char*)v.data());

    v.clear();
    EXPECT_EQ(17, str::decode_base64("YW55IGNhcm5hbCBwbGVhc3U=", &v));
    v.push_back(std::byte{0});
    v.pop_back();
    EXPECT_STREQ("any carnal pleasu", (const char*)v.data());

    v.clear();
    EXPECT_EQ(16, str::decode_base64("YW55IGNhcm5hbCBwbGVhcw==", &v));
    v.push_back(std::byte{0});
    v.pop_back();
    EXPECT_STREQ("any carnal pleas", (const char*)v.data());

    EXPECT_EQ(0, str::decode_base64("YW55IGNhcm5hbCBwbGVhcw=", &v));
    EXPECT_EQ(0, str::decode_base64("c==", &v));
    EXPECT_EQ(0, str::decode_base64("", &v));
    EXPECT_NE(16, str::decode_base64("YW55IGNhc)5hbCBwbGVhcw==", &v));
}

TEST(String, encode_base64)
{
    std::string out;

    std::string in = "any carnal pleasure.";
    out.clear();
    EXPECT_EQ(str::encode_base64({(std::byte*)in.data(), in.size()}, &out), 28);
    EXPECT_STREQ("YW55IGNhcm5hbCBwbGVhc3VyZS4=", out.c_str());

    in = "any carnal pleasure";
    out.clear();
    EXPECT_EQ(str::encode_base64({(std::byte*)in.data(), in.size()}, &out), 28);
    EXPECT_STREQ("YW55IGNhcm5hbCBwbGVhc3VyZQ==", out.c_str());

    in = "any carnal pleasur";
    out.clear();
    EXPECT_EQ(str::encode_base64({(std::byte*)in.data(), in.size()}, &out), 24);
    EXPECT_STREQ("YW55IGNhcm5hbCBwbGVhc3Vy", out.c_str());

    in = "any carnal pleasu";
    out.clear();
    EXPECT_EQ(str::encode_base64({(std::byte*)in.data(), in.size()}, &out), 24);
    EXPECT_STREQ("YW55IGNhcm5hbCBwbGVhc3U=", out.c_str());

    in = "any carnal pleas";
    out.clear();
    EXPECT_EQ(str::encode_base64({(std::byte*)in.data(), in.size()}, &out), 24);
    EXPECT_STREQ("YW55IGNhcm5hbCBwbGVhcw==", out.c_str());

    in = "Ma";
    out.clear();
    EXPECT_EQ(str::encode_base64({(std::byte*)in.data(), in.size()}, &out), 4);
    EXPECT_STREQ("TWE=", out.c_str());

    in = "M";
    out.clear();
    EXPECT_EQ(str::encode_base64({(std::byte*)in.data(), in.size()}, &out), 4);
    EXPECT_STREQ("TQ==", out.c_str());

    in = "";
    out.clear();
    EXPECT_EQ(str::encode_base64({(std::byte*)in.data(), in.size()}, &out), 0);
    EXPECT_STREQ("", out.c_str());
}

TEST(String, decode_base64_variants)
{
    const std::byte decoded[] = {
        0x47_b, 0x49_b, 0x46_b, 0x38_b, 0x37_b, 0x61_b, 0x30_b, 0x00_b, 0x30_b, 0x00_b, 0xf0_b,
        0x00_b, 0x00_b, 0x00_b, 0x00_b, 0x00_b, 0xff_b, 0xff_b, 0xff_b, 0x2c_b, 0x00_b, 0x00_b,
        0x00_b, 0x00_b, 0x30_b, 0x00_b, 0x30_b, 0x00_b, 0x00_b, 0x02_b, 0xf0_b, 0x8c_b, 0x8f_b,
        0xa9_b, 0xcb_b, 0xed_b, 0xdf_b, 0x00_b, 0x9c_b, 0x0e_b, 0x48_b, 0x8b_b, 0x73_b, 0xb0_b,
        0xb4_b, 0xab_b, 0x0c_b, 0x86_b, 0x1e_b, 0x14_b, 0x96_b, 0xa6_b, 0x34_b, 0x2e_b, 0xe7_b,
        0x2a_b, 0xa6_b, 0x09_b, 0x8b_b, 0x1a_b, 0xa7_b, 0x2b_b, 0xaf_b, 0x51_b, 0x49_b, 0x6f_b,
        0x38_b, 0xd9_b, 0x8e_b, 0x66_b, 0xd7_b, 0xf3_b, 0x80_b, 0x5c_b, 0xc1_b, 0xc9_b, 0x30_b,
        0x75_b, 0xd1_b, 0x08_b, 0x31_b, 0x39_b, 0x1d_b, 0x13_b, 0xa8_b, 0x14_b, 0x1e_b, 0x8e_b,
        0x14_b, 0x4d_b, 0xcc_b, 0xe7_b, 0xe4_b, 0x54_b, 0x9f_b, 0xb9_b, 0x62_b, 0x25_b, 0x2a_b,
        0xad_b, 0x71_b, 0x79_b, 0x63_b, 0x99_b, 0x4a_b, 0x87_b, 0xf0_b, 0xde_b, 0xb8_b, 0xd7_b,
        0x0f_b, 0xe7_b, 0x22_b, 0xd6_b, 0x1a_b, 0xc1_b, 0x1b_b, 0xb4_b, 0xb8_b, 0x8e_b, 0x4a_b,
        0x96_b, 0xbf_b, 0x4c_b, 0xf8_b, 0x3b_b, 0x26_b, 0x92_b, 0x47_b, 0xc7_b, 0x27_b, 0xa7_b,
        0x77_b, 0x35_b, 0x93_b, 0x05_b, 0xf5_b, 0xf4_b, 0x73_b, 0x13_b, 0xa7_b, 0x28_b, 0xf8_b,
        0xe0_b, 0x07_b, 0x38_b, 0xb8_b, 0x76_b, 0x28_b, 0xa7_b, 0x58_b, 0x67_b, 0x64_b, 0x17_b,
        0xc9_b, 0x23_b, 0x75_b, 0xf9_b, 0xf2_b, 0x71_b, 0x06_b, 0x57_b, 0x65_b, 0xe6_b, 0x06_b,
        0x7a_b, 0x39_b, 0x89_b, 0x95_b, 0x97_b, 0x86_b, 0x97_b, 0xd8_b, 0xb6_b, 0x89_b, 0xc5_b,
        0x6a_b, 0x68_b, 0xd5_b, 0x5a_b, 0x8a_b, 0x54_b, 0xfa_b, 0x17_b, 0x98_b, 0x89_b, 0xf7_b,
        0x49_b, 0xba_b, 0x9a_b, 0xfb_b, 0x93_b, 0x4b_b, 0x83_b, 0x8a_b, 0xd3_b, 0x93_b, 0x34_b,
        0xbc_b, 0x39_b, 0x34_b, 0x43_b, 0x85_b, 0xd4_b, 0xb7_b, 0x0b_b, 0xca_b, 0x3b_b, 0x3a_b,
        0xdc_b, 0x77_b, 0x78_b, 0xaa_b, 0xf6_b, 0xe8_b, 0xac_b, 0x17_b, 0xcd_b, 0x4c_b, 0xd4_b,
        0xe2_b, 0x1c_b, 0x47_b, 0xb9_b, 0x44_b, 0x7b_b, 0x0c_b, 0x1c_b, 0x2e_b, 0x9e_b, 0xed_b,
        0xb8_b, 0xd3_b, 0x64_b, 0x33_b, 0x7b_b, 0x8d_b, 0x9d_b, 0xad_b, 0xc5_b, 0xd8_b, 0x94_b,
        0x05_b, 0xa3_b, 0xde_b, 0xd5_b, 0xae_b, 0x2c_b, 0xbf_b, 0x5e_b, 0x5e_b, 0x3f_b, 0x7f_b,
        0x57_b, 0x1f_b, 0xe5_b, 0x55_b, 0xb3_b, 0x5f_b, 0x2c_b, 0xa6_b, 0xec_b, 0xf1_b, 0xe1_b,
        0x87_b, 0x1e_b, 0x1d_b, 0x6e_b, 0x06_b, 0x0a_b, 0x00_b, 0x00_b, 0x3b_b};

    const size_t decoded_len = 273;

    const char* normal =
        "R0lGODdhMAAwAPAAAAAAAP///"
        "ywAAAAAMAAwAAAC8IyPqcvt3wCcDkiLc7C0qwyGHhSWpjQu5yqmCYsapyuvUUlvONmOZtfzgFzByTB10QgxOR0TqBQ"
        "ejhRNzOfkVJ+5YiUqrXF5Y5lKh/DeuNcP5yLWGsEbtLiOSpa/"
        "TPg7JpJHxyendzWTBfX0cxOnKPjgBzi4diinWGdkF8kjdfnycQZXZeYGejmJlZeGl9i2icVqaNVailT6F5iJ90m6mv"
        "uTS4OK05M0vDk0Q4XUtwvKOzrcd3iq9uisF81M1OIcR7lEewwcLp7tuNNkM3uNna3F2JQFo97Vriy/Xl4/"
        "f1cf5VWzXyym7PHhhx4dbgYKAAA7";
    const char* url =
        "R0lGODdhMAAwAPAAAAAAAP___"
        "ywAAAAAMAAwAAAC8IyPqcvt3wCcDkiLc7C0qwyGHhSWpjQu5yqmCYsapyuvUUlvONmOZtfzgFzByTB10QgxOR0TqBQ"
        "ejhRNzOfkVJ-5YiUqrXF5Y5lKh_DeuNcP5yLWGsEbtLiOSpa_"
        "TPg7JpJHxyendzWTBfX0cxOnKPjgBzi4diinWGdkF8kjdfnycQZXZeYGejmJlZeGl9i2icVqaNVailT6F5iJ90m6mv"
        "uTS4OK05M0vDk0Q4XUtwvKOzrcd3iq9uisF81M1OIcR7lEewwcLp7tuNNkM3uNna3F2JQFo97Vriy_Xl4_"
        "f1cf5VWzXyym7PHhhx4dbgYKAAA7";
    const char* both =
        "R0lGODdhMAAwAPAAAAAAAP_/"
        "_ywAAAAAMAAwAAAC8IyPqcvt3wCcDkiLc7C0qwyGHhSWpjQu5yqmCYsapyuvUUlvONmOZtfzgFzByTB10QgxOR0TqB"
        "QejhRNzOfkVJ-5YiUqrXF5Y5lKh/DeuNcP5yLWGsEbtLiOSpa/"
        "TPg7JpJHxyendzWTBfX0cxOnKPjgBzi4diinWGdkF8kjdfnycQZXZeYGejmJlZeGl9i2icVqaNVailT6F5iJ90m6mv"
        "uTS4OK05M0vDk0Q4XUtwvKOzrcd3iq9uisF81M1OIcR7lEewwcLp7tuNNkM3uNna3F2JQFo97Vriy/Xl4/"
        "f1cf5VWzXyym7PHhhx4dbgYKAAA7";

    ASSERT_EQ(
        decoded_len, str::decode_base64_size_hint(normal, str::Base64DecodingVariant::Normal));
    ASSERT_EQ(
        decoded_len, str::decode_base64_size_hint(url, str::Base64DecodingVariant::UrlFilename));
    ASSERT_EQ(decoded_len, str::decode_base64_size_hint(both, str::Base64DecodingVariant::Both));

    std::vector<std::byte> data;
    data.resize(decoded_len);

    ASSERT_EQ(decoded_len, str::decode_base64_s(normal, data, str::Base64DecodingVariant::Normal));
    EXPECT_TRUE(memcmp(decoded, data.data(), decoded_len) == 0);

    ASSERT_EQ(
        decoded_len, str::decode_base64_s(url, data, str::Base64DecodingVariant::UrlFilename));
    EXPECT_TRUE(memcmp(decoded, data.data(), decoded_len) == 0);

    ASSERT_EQ(decoded_len, str::decode_base64_s(both, data, str::Base64DecodingVariant::Both));
    EXPECT_TRUE(memcmp(decoded, data.data(), decoded_len) == 0);
}

TEST(String, encode_base64_variants)
{
    const std::byte decoded[] = {
        0x47_b, 0x49_b, 0x46_b, 0x38_b, 0x37_b, 0x61_b, 0x30_b, 0x00_b, 0x30_b, 0x00_b, 0xf0_b,
        0x00_b, 0x00_b, 0x00_b, 0x00_b, 0x00_b, 0xff_b, 0xff_b, 0xff_b, 0x2c_b, 0x00_b, 0x00_b,
        0x00_b, 0x00_b, 0x30_b, 0x00_b, 0x30_b, 0x00_b, 0x00_b, 0x02_b, 0xf0_b, 0x8c_b, 0x8f_b,
        0xa9_b, 0xcb_b, 0xed_b, 0xdf_b, 0x00_b, 0x9c_b, 0x0e_b, 0x48_b, 0x8b_b, 0x73_b, 0xb0_b,
        0xb4_b, 0xab_b, 0x0c_b, 0x86_b, 0x1e_b, 0x14_b, 0x96_b, 0xa6_b, 0x34_b, 0x2e_b, 0xe7_b,
        0x2a_b, 0xa6_b, 0x09_b, 0x8b_b, 0x1a_b, 0xa7_b, 0x2b_b, 0xaf_b, 0x51_b, 0x49_b, 0x6f_b,
        0x38_b, 0xd9_b, 0x8e_b, 0x66_b, 0xd7_b, 0xf3_b, 0x80_b, 0x5c_b, 0xc1_b, 0xc9_b, 0x30_b,
        0x75_b, 0xd1_b, 0x08_b, 0x31_b, 0x39_b, 0x1d_b, 0x13_b, 0xa8_b, 0x14_b, 0x1e_b, 0x8e_b,
        0x14_b, 0x4d_b, 0xcc_b, 0xe7_b, 0xe4_b, 0x54_b, 0x9f_b, 0xb9_b, 0x62_b, 0x25_b, 0x2a_b,
        0xad_b, 0x71_b, 0x79_b, 0x63_b, 0x99_b, 0x4a_b, 0x87_b, 0xf0_b, 0xde_b, 0xb8_b, 0xd7_b,
        0x0f_b, 0xe7_b, 0x22_b, 0xd6_b, 0x1a_b, 0xc1_b, 0x1b_b, 0xb4_b, 0xb8_b, 0x8e_b, 0x4a_b,
        0x96_b, 0xbf_b, 0x4c_b, 0xf8_b, 0x3b_b, 0x26_b, 0x92_b, 0x47_b, 0xc7_b, 0x27_b, 0xa7_b,
        0x77_b, 0x35_b, 0x93_b, 0x05_b, 0xf5_b, 0xf4_b, 0x73_b, 0x13_b, 0xa7_b, 0x28_b, 0xf8_b,
        0xe0_b, 0x07_b, 0x38_b, 0xb8_b, 0x76_b, 0x28_b, 0xa7_b, 0x58_b, 0x67_b, 0x64_b, 0x17_b,
        0xc9_b, 0x23_b, 0x75_b, 0xf9_b, 0xf2_b, 0x71_b, 0x06_b, 0x57_b, 0x65_b, 0xe6_b, 0x06_b,
        0x7a_b, 0x39_b, 0x89_b, 0x95_b, 0x97_b, 0x86_b, 0x97_b, 0xd8_b, 0xb6_b, 0x89_b, 0xc5_b,
        0x6a_b, 0x68_b, 0xd5_b, 0x5a_b, 0x8a_b, 0x54_b, 0xfa_b, 0x17_b, 0x98_b, 0x89_b, 0xf7_b,
        0x49_b, 0xba_b, 0x9a_b, 0xfb_b, 0x93_b, 0x4b_b, 0x83_b, 0x8a_b, 0xd3_b, 0x93_b, 0x34_b,
        0xbc_b, 0x39_b, 0x34_b, 0x43_b, 0x85_b, 0xd4_b, 0xb7_b, 0x0b_b, 0xca_b, 0x3b_b, 0x3a_b,
        0xdc_b, 0x77_b, 0x78_b, 0xaa_b, 0xf6_b, 0xe8_b, 0xac_b, 0x17_b, 0xcd_b, 0x4c_b, 0xd4_b,
        0xe2_b, 0x1c_b, 0x47_b, 0xb9_b, 0x44_b, 0x7b_b, 0x0c_b, 0x1c_b, 0x2e_b, 0x9e_b, 0xed_b,
        0xb8_b, 0xd3_b, 0x64_b, 0x33_b, 0x7b_b, 0x8d_b, 0x9d_b, 0xad_b, 0xc5_b, 0xd8_b, 0x94_b,
        0x05_b, 0xa3_b, 0xde_b, 0xd5_b, 0xae_b, 0x2c_b, 0xbf_b, 0x5e_b, 0x5e_b, 0x3f_b, 0x7f_b,
        0x57_b, 0x1f_b, 0xe5_b, 0x55_b, 0xb3_b, 0x5f_b, 0x2c_b, 0xa6_b, 0xec_b, 0xf1_b, 0xe1_b,
        0x87_b, 0x1e_b, 0x1d_b, 0x6e_b, 0x06_b, 0x0a_b, 0x00_b, 0x00_b, 0x3b_b};

    const char* normal =
        "R0lGODdhMAAwAPAAAAAAAP///"
        "ywAAAAAMAAwAAAC8IyPqcvt3wCcDkiLc7C0qwyGHhSWpjQu5yqmCYsapyuvUUlvONmOZtfzgFzByTB10QgxOR0TqBQ"
        "ejhRNzOfkVJ+5YiUqrXF5Y5lKh/DeuNcP5yLWGsEbtLiOSpa/"
        "TPg7JpJHxyendzWTBfX0cxOnKPjgBzi4diinWGdkF8kjdfnycQZXZeYGejmJlZeGl9i2icVqaNVailT6F5iJ90m6mv"
        "uTS4OK05M0vDk0Q4XUtwvKOzrcd3iq9uisF81M1OIcR7lEewwcLp7tuNNkM3uNna3F2JQFo97Vriy/Xl4/"
        "f1cf5VWzXyym7PHhhx4dbgYKAAA7";
    const char* url =
        "R0lGODdhMAAwAPAAAAAAAP___"
        "ywAAAAAMAAwAAAC8IyPqcvt3wCcDkiLc7C0qwyGHhSWpjQu5yqmCYsapyuvUUlvONmOZtfzgFzByTB10QgxOR0TqBQ"
        "ejhRNzOfkVJ-5YiUqrXF5Y5lKh_DeuNcP5yLWGsEbtLiOSpa_"
        "TPg7JpJHxyendzWTBfX0cxOnKPjgBzi4diinWGdkF8kjdfnycQZXZeYGejmJlZeGl9i2icVqaNVailT6F5iJ90m6mv"
        "uTS4OK05M0vDk0Q4XUtwvKOzrcd3iq9uisF81M1OIcR7lEewwcLp7tuNNkM3uNna3F2JQFo97Vriy_Xl4_"
        "f1cf5VWzXyym7PHhhx4dbgYKAAA7";

    std::string out;
    EXPECT_EQ(str::encode_base64(decoded, &out, str::Base64EncodingVariant::Normal), 364);
    EXPECT_STREQ(out.c_str(), normal);

    out.clear();
    EXPECT_EQ(str::encode_base64(decoded, &out, str::Base64EncodingVariant::UrlFilename), 364);
    EXPECT_STREQ(out.c_str(), url);
}

TEST(String, encode_code_point_to_utf8)
{
    char utf8_bytes[5];

    // [U+0000, U+007F]
    {
        const char expected[] = {'\x7e', '\x0'};
        int n = str::encode_code_point_to_utf8(0x007e, utf8_bytes);
        ASSERT_EQ(n, 1);
        utf8_bytes[n] = 0;
        EXPECT_STREQ(std::span<const char>(utf8_bytes, n).data(), expected);
    }

    // [U+0080, U+07FF]
    {
        const char expected[] = {'\xc3', '\xa9', '\x0'};
        int n = str::encode_code_point_to_utf8(0x00e9, utf8_bytes);
        ASSERT_EQ(n, 2);
        utf8_bytes[n] = 0;
        EXPECT_STREQ(std::span<const char>(utf8_bytes, n).data(), expected);
    }

    // [U+00800, U+FFFF]
    {
        const char expected[] = {'\xe0', '\xa4', '\xa0', '\x0'};
        int n = str::encode_code_point_to_utf8(0x0920, utf8_bytes);
        ASSERT_EQ(n, 3);
        utf8_bytes[n] = 0;
        EXPECT_STREQ(std::span<const char>(utf8_bytes, n).data(), expected);
    }

    // [U+10000, U+10FFFF]
    {
        const char expected[] = {'\xf0', '\x9d', '\x84', '\x9e', '\x0'};
        int n = str::encode_code_point_to_utf8(0x1d11e, utf8_bytes);
        ASSERT_EQ(n, 4);
        utf8_bytes[n] = 0;
        EXPECT_STREQ(std::span<const char>(utf8_bytes, n).data(), expected);
    }
}

TEST(String, split)
{
    EXPECT_EQ(hrz::str::split("", ' '), std::make_pair(std::string_view(""), std::string_view("")));

    EXPECT_EQ(
        hrz::str::split("hello world", ';'),
        std::make_pair(std::string_view("hello world"), std::string_view("")));

    EXPECT_EQ(
        hrz::str::split("hello; world", ';'),
        std::make_pair(std::string_view("hello"), std::string_view(" world")));

    EXPECT_EQ(
        hrz::str::split("hello; world; byebye", ';'),
        std::make_pair(std::string_view("hello"), std::string_view(" world; byebye")));

    EXPECT_EQ(
        hrz::str::split("; hello", ';'),
        std::make_pair(std::string_view(""), std::string_view(" hello")));

    EXPECT_EQ(
        hrz::str::split("hello;", ';'),
        std::make_pair(std::string_view("hello"), std::string_view("")));
}

TEST(String, sanitize_named_fmt_arguments)
{
    std::string_view args[] = {"world", "cheese"};

    EXPECT_EQ("hello  world", hrz::str::sanitize_named_fmt_arguments("hello {cheese} world", {}));
    EXPECT_EQ(
        "hello {cheese} world",
        hrz::str::sanitize_named_fmt_arguments("hello {cheese} world", args));
    EXPECT_EQ(" {world} ", hrz::str::sanitize_named_fmt_arguments("{a} {world} {b}", args));
    EXPECT_EQ("", hrz::str::sanitize_named_fmt_arguments("", args));
    EXPECT_EQ("", hrz::str::sanitize_named_fmt_arguments("{no}", args));
    EXPECT_EQ("{world}", hrz::str::sanitize_named_fmt_arguments("{world}", args));
}

TEST(String, parse_int)
{
    EXPECT_EQ(0, hrz::str::parse_int64("0"));
    EXPECT_EQ(-2, hrz::str::parse_int64("-2  "));
    EXPECT_EQ(-2, hrz::str::parse_int64("-2.0  "));
    EXPECT_EQ(78, hrz::str::parse_int64("   78.96 "));
    EXPECT_EQ(std::nullopt, hrz::str::parse_int64("75557863725914323419135"));
    EXPECT_EQ(std::nullopt, hrz::str::parse_int64("   "));
}

TEST(String, parse_uint)
{
    EXPECT_EQ(0, hrz::str::parse_uint64("0"));
    EXPECT_EQ(2, hrz::str::parse_uint64("2  "));
    EXPECT_EQ(2, hrz::str::parse_uint64("2.0  "));
    EXPECT_EQ(78, hrz::str::parse_uint64("   78.96 "));
    EXPECT_EQ(std::nullopt, hrz::str::parse_uint64("75557863725914323419135"));
    EXPECT_EQ(std::nullopt, hrz::str::parse_uint64("   "));
    EXPECT_EQ(std::nullopt, hrz::str::parse_uint64("-2  "));
}

TEST(String, parse_double)
{
    EXPECT_EQ(0.0, hrz::str::parse_double("0"));
    EXPECT_EQ(-2.0, hrz::str::parse_double("-2  "));
    EXPECT_EQ(-2.0, hrz::str::parse_double("-2.0  "));
    EXPECT_EQ(78.96, hrz::str::parse_double("   78.96 "));
    EXPECT_EQ(75557863725914323419135.0, hrz::str::parse_double("75557863725914323419135"));
    EXPECT_EQ(std::nullopt, hrz::str::parse_double("   "));
}
