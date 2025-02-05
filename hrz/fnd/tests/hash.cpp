#include <hrz_fnd_hash.h>

#include <gtest/gtest.h>

using namespace hrz;

TEST(Hash, hash_kv)
{
    std::vector<std::pair<std::string_view, std::string_view>> config_a;
    config_a.emplace_back("hello", "world");
    config_a.emplace_back("foo", "bar");

    std::vector<std::pair<std::string_view, std::string_view>> config_b;
    config_b.emplace_back("hello", "world 2");
    config_b.emplace_back("foo 2", "bar");

    std::vector<std::pair<std::string_view, std::string_view>> config_c;

    uint64_t a = hash_kv(config_a);
    uint64_t b = hash_kv(config_b);
    uint64_t c = hash_kv(config_c);

    EXPECT_NE(a, b);
    EXPECT_NE(b, c);
    EXPECT_NE(a, c);
}

TEST(Hash, hash_kv_empty)
{
    uint64_t a = hash_kv({});
    uint64_t b = hash_kv({});
    ASSERT_EQ(a, b);
}

TEST(Hash, hash_kv_key_order)
{
    std::vector<std::pair<std::string_view, std::string_view>> config_a;
    config_a.emplace_back("hello", "world");
    config_a.emplace_back("foo", "bar");

    std::vector<std::pair<std::string_view, std::string_view>> config_b;
    config_b.emplace_back("foo", "bar");
    config_b.emplace_back("hello", "world");

    uint64_t a = hash_kv(config_a);
    uint64_t b = hash_kv(config_b);

    ASSERT_EQ(a, b);
}
