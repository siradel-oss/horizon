// SPDX-FileCopyrightText: Copyright 2023 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/fnd/lru.h"

#include <gtest/gtest.h>

using namespace hrz;

namespace
{

struct NonTrivialDestructor
{
    ~NonTrivialDestructor() {}

    NonTrivialDestructor* prev{};
    NonTrivialDestructor* next{};
};

struct DestructorWatcher
{
    DestructorWatcher* prev{};
    DestructorWatcher* next{};

    bool* destroyed;

    DestructorWatcher(bool* b) : destroyed(b) {}

    ~DestructorWatcher() { *destroyed = true; }
};

struct IntNode
{
    IntNode* prev{};
    IntNode* next{};

    int n;

    IntNode(int n_ = 0) : n(n_) {}

    bool operator ==(int o) const { return n == o; }
};

} // namespace

TEST(LruList, DestroyAllAtDestruction)
{
    LruList<NonTrivialDestructor> list;
    list.emplace_front();
    list.emplace_front();
}

TEST(LruList, CallDestructorsAtDestruction)
{
    bool b1 = false;
    bool b2 = false;

    {
        LruList<DestructorWatcher> list;
        list.emplace_front(&b1);
        list.emplace_front(&b2);

        EXPECT_FALSE(b1);
        EXPECT_FALSE(b2);
    }

    EXPECT_TRUE(b1);
    EXPECT_TRUE(b2);
}

TEST(LruList, Emplace)
{
    LruList<IntNode> list;

    EXPECT_EQ(list.size(), 0);
    EXPECT_EQ(list.front(), nullptr);
    EXPECT_EQ(list.back(), nullptr);

    IntNode* v1 = list.emplace_front(1);
    EXPECT_EQ(list.size(), 1);
    EXPECT_EQ(list.front(), v1);
    EXPECT_EQ(list.back(), v1);

    IntNode* v2 = list.emplace_front(2);
    EXPECT_EQ(list.size(), 2);
    EXPECT_EQ(list.front(), v2);
    EXPECT_EQ(list.back(), v1);

    IntNode* v3 = list.emplace_front(3);
    EXPECT_EQ(list.size(), 3);
    EXPECT_EQ(list.front(), v3);
    EXPECT_EQ(list.back(), v1);

    IntNode* v4 = list.emplace_front(4);
    EXPECT_EQ(list.size(), 4);
    EXPECT_EQ(list.front(), v4);
    EXPECT_EQ(list.back(), v1);

    EXPECT_EQ(*v1, 1);
    EXPECT_EQ(*v2, 2);
    EXPECT_EQ(*v3, 3);
    EXPECT_EQ(*v4, 4);
}

TEST(LruList, PopBack)
{
    LruList<IntNode> list;

    EXPECT_FALSE(list.pop_back());
    EXPECT_EQ(list.size(), 0);
    EXPECT_EQ(list.front(), nullptr);
    EXPECT_EQ(list.back(), nullptr);

    IntNode* v1 = list.emplace_front(1);
    IntNode* v2 = list.emplace_front(2);
    IntNode* v3 = list.emplace_front(3);
    IntNode* v4 = list.emplace_front(4);

    EXPECT_EQ(list.size(), 4);
    EXPECT_EQ(list.front(), v4);
    EXPECT_EQ(list.back(), v1);

    EXPECT_TRUE(list.pop_back());
    EXPECT_EQ(list.size(), 3);
    EXPECT_EQ(list.front(), v4);
    EXPECT_EQ(list.back(), v2);

    EXPECT_TRUE(list.pop_back());
    EXPECT_EQ(list.size(), 2);
    EXPECT_EQ(list.front(), v4);
    EXPECT_EQ(list.back(), v3);

    EXPECT_TRUE(list.pop_back());
    EXPECT_EQ(list.size(), 1);
    EXPECT_EQ(list.front(), v4);
    EXPECT_EQ(list.back(), v4);

    EXPECT_TRUE(list.pop_back());
    EXPECT_EQ(list.size(), 0);
    EXPECT_EQ(list.front(), nullptr);
    EXPECT_EQ(list.back(), nullptr);

    EXPECT_FALSE(list.pop_back());
    EXPECT_EQ(list.size(), 0);
    EXPECT_EQ(list.front(), nullptr);
    EXPECT_EQ(list.back(), nullptr);

    EXPECT_FALSE(list.pop_back());
    EXPECT_EQ(list.size(), 0);
    EXPECT_EQ(list.front(), nullptr);
    EXPECT_EQ(list.back(), nullptr);
}

TEST(LruList, CallDestructorsAtPopBack)
{
    bool b1 = false;
    bool b2 = false;
    bool b3 = false;

    LruList<DestructorWatcher> list;
    list.emplace_front(&b1);
    list.emplace_front(&b2);
    list.emplace_front(&b3);

    EXPECT_FALSE(b1);
    EXPECT_FALSE(b2);
    EXPECT_FALSE(b3);

    list.pop_back();
    EXPECT_TRUE(b1);
    EXPECT_FALSE(b2);
    EXPECT_FALSE(b3);

    list.pop_back();
    EXPECT_TRUE(b1);
    EXPECT_TRUE(b2);
    EXPECT_FALSE(b3);

    list.pop_back();
    EXPECT_TRUE(b1);
    EXPECT_TRUE(b2);
    EXPECT_TRUE(b3);
}

TEST(LruList, TouchFront)
{
    LruList<IntNode> list;
    IntNode* v1 = list.emplace_front(1);
    IntNode* v2 = list.emplace_front(2);
    IntNode* v3 = list.emplace_front(3);

    list.touch(v3);
    EXPECT_EQ(list.front(), v3);
    EXPECT_EQ(list.back(), v1);

    list.pop_back();
    EXPECT_EQ(list.size(), 2);
    EXPECT_EQ(list.front(), v3);
    EXPECT_EQ(list.back(), v2);

    list.pop_back();
    EXPECT_EQ(list.size(), 1);
    EXPECT_EQ(list.front(), v3);
    EXPECT_EQ(list.back(), v3);

    list.pop_back();
    EXPECT_EQ(list.size(), 0);
    EXPECT_EQ(list.front(), nullptr);
    EXPECT_EQ(list.back(), nullptr);
}

TEST(LruList, TouchBack)
{
    LruList<IntNode> list;
    IntNode* v1 = list.emplace_front(1);
    IntNode* v2 = list.emplace_front(2);
    IntNode* v3 = list.emplace_front(3);

    list.touch(v1);
    EXPECT_EQ(list.front(), v1);
    EXPECT_EQ(list.back(), v2);

    list.pop_back();
    EXPECT_EQ(list.size(), 2);
    EXPECT_EQ(list.front(), v1);
    EXPECT_EQ(list.back(), v3);

    list.pop_back();
    EXPECT_EQ(list.size(), 1);
    EXPECT_EQ(list.front(), v1);
    EXPECT_EQ(list.back(), v1);

    list.pop_back();
    EXPECT_EQ(list.size(), 0);
    EXPECT_EQ(list.front(), nullptr);
    EXPECT_EQ(list.back(), nullptr);
}

TEST(LruList, TouchMiddle)
{
    LruList<IntNode> list;
    IntNode* v1 = list.emplace_front(1);
    IntNode* v2 = list.emplace_front(2);
    IntNode* v3 = list.emplace_front(3);

    list.touch(v2);
    EXPECT_EQ(list.front(), v2);
    EXPECT_EQ(list.back(), v1);

    list.pop_back();
    EXPECT_EQ(list.size(), 2);
    EXPECT_EQ(list.front(), v2);
    EXPECT_EQ(list.back(), v3);

    list.pop_back();
    EXPECT_EQ(list.size(), 1);
    EXPECT_EQ(list.front(), v2);
    EXPECT_EQ(list.back(), v2);

    list.pop_back();
    EXPECT_EQ(list.size(), 0);
    EXPECT_EQ(list.front(), nullptr);
    EXPECT_EQ(list.back(), nullptr);
}

TEST(LruList, CallDestructorsAtRemove)
{
    bool b1 = false;
    bool b2 = false;
    bool b3 = false;

    LruList<DestructorWatcher> list;
    auto* v1 = list.emplace_front(&b1);
    auto* v2 = list.emplace_front(&b2);
    auto* v3 = list.emplace_front(&b3);

    EXPECT_FALSE(b1);
    EXPECT_FALSE(b2);
    EXPECT_FALSE(b3);

    list.remove(v2);
    EXPECT_FALSE(b1);
    EXPECT_TRUE(b2);
    EXPECT_FALSE(b3);

    list.remove(v1);
    EXPECT_TRUE(b1);
    EXPECT_TRUE(b2);
    EXPECT_FALSE(b3);

    list.remove(v3);
    EXPECT_TRUE(b1);
    EXPECT_TRUE(b2);
    EXPECT_TRUE(b3);
}

TEST(LruList, Remove)
{
    LruList<IntNode> list;
    IntNode* v1 = list.emplace_front(1);
    IntNode* v2 = list.emplace_front(2);
    IntNode* v3 = list.emplace_front(3);
    IntNode* v4 = list.emplace_front(4);

    list.remove(v4);
    EXPECT_EQ(list.size(), 3);
    EXPECT_EQ(list.front(), v3);
    EXPECT_EQ(list.back(), v1);

    list.remove(v2);
    EXPECT_EQ(list.size(), 2);
    EXPECT_EQ(list.front(), v3);
    EXPECT_EQ(list.back(), v1);

    list.remove(v1);
    EXPECT_EQ(list.size(), 1);
    EXPECT_EQ(list.front(), v3);
    EXPECT_EQ(list.back(), v3);

    list.remove(v3);
    EXPECT_EQ(list.size(), 0);
    EXPECT_EQ(list.front(), nullptr);
    EXPECT_EQ(list.back(), nullptr);
}

TEST(LruList, RemoveFront)
{
    LruList<IntNode> list;
    IntNode* v1 = list.emplace_front(1);
    IntNode* v2 = list.emplace_front(2);
    IntNode* v3 = list.emplace_front(3);

    list.remove(v3);
    EXPECT_EQ(list.size(), 2);
    EXPECT_EQ(list.front(), v2);
    EXPECT_EQ(list.back(), v1);

    list.pop_back();
    EXPECT_EQ(list.size(), 1);
    EXPECT_EQ(list.front(), v2);
    EXPECT_EQ(list.back(), v2);

    list.pop_back();
    EXPECT_EQ(list.size(), 0);
    EXPECT_EQ(list.front(), nullptr);
    EXPECT_EQ(list.back(), nullptr);
}

TEST(LruList, RemoveMiddle)
{
    LruList<IntNode> list;
    IntNode* v1 = list.emplace_front(1);
    IntNode* v2 = list.emplace_front(2);
    IntNode* v3 = list.emplace_front(3);

    list.remove(v2);
    EXPECT_EQ(list.size(), 2);
    EXPECT_EQ(list.front(), v3);
    EXPECT_EQ(list.back(), v1);

    list.pop_back();
    EXPECT_EQ(list.size(), 1);
    EXPECT_EQ(list.front(), v3);
    EXPECT_EQ(list.back(), v3);

    list.pop_back();
    EXPECT_EQ(list.size(), 0);
    EXPECT_EQ(list.front(), nullptr);
    EXPECT_EQ(list.back(), nullptr);
}

TEST(LruList, RemoveBack)
{
    LruList<IntNode> list;
    IntNode* v1 = list.emplace_front(1);
    IntNode* v2 = list.emplace_front(2);
    IntNode* v3 = list.emplace_front(3);

    list.remove(v1);
    EXPECT_EQ(list.size(), 2);
    EXPECT_EQ(list.front(), v3);
    EXPECT_EQ(list.back(), v2);

    list.pop_back();
    EXPECT_EQ(list.size(), 1);
    EXPECT_EQ(list.front(), v3);
    EXPECT_EQ(list.back(), v3);

    list.pop_back();
    EXPECT_EQ(list.size(), 0);
    EXPECT_EQ(list.front(), nullptr);
    EXPECT_EQ(list.back(), nullptr);
}
