#include <hrz_fnd_intern_string.h>

#include <gtest/gtest.h>

TEST(InternString, interning)
{
    hrz::InternString interner;

    const char* hello_0 = interner.intern("hello");
    const char* hello_1 = interner.intern("hello");
    const char* hello_world = interner.intern("hello world");
    const char* empty_0 = interner.intern("");
    const char* empty_1 = interner.intern("");

    EXPECT_EQ(hello_0, hello_1);
    EXPECT_EQ(empty_0, empty_1);
    EXPECT_NE(hello_0, hello_world);
    EXPECT_NE(hello_0, empty_0);
    EXPECT_NE(hello_world, empty_0);
    EXPECT_STREQ(hello_0, "hello");
    EXPECT_STREQ(hello_world, "hello world");
    EXPECT_STREQ(empty_0, "");
}
