// SPDX-FileCopyrightText: Copyright 2023 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/fnd/object_pool.h"

#include <gtest/gtest.h>

using namespace hrz;

namespace
{

struct DestructorWatcher
{
    bool* destroyed;

    DestructorWatcher(bool* b) : destroyed(b) {}

    ~DestructorWatcher() { *destroyed = true; }
};

} // namespace

TEST(ObjectPool, DestructorCalled)
{
    ObjectPool<DestructorWatcher> pool;

    bool d1 = false;
    bool d2 = false;

    DestructorWatcher* w1 = pool.acquire(&d1);
    DestructorWatcher* w2 = pool.acquire(&d2);

    EXPECT_FALSE(d1);
    EXPECT_FALSE(d2);

    pool.release(w1);
    EXPECT_TRUE(d1);
    EXPECT_FALSE(d2);

    pool.release(w2);
    EXPECT_TRUE(d1);
    EXPECT_TRUE(d2);
}

TEST(ObjectPool, Reuse)
{
    ObjectPool<int> pool;

    int* a = pool.acquire(1);
    int* b = pool.acquire(2);
    int* c = pool.acquire(3);

    pool.release(b);

    int* d = pool.acquire(4);

    EXPECT_EQ(d, b);
    EXPECT_EQ(*a, 1);
    EXPECT_EQ(*d, 4);
    EXPECT_EQ(*c, 3);
}
