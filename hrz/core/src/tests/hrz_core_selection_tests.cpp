#include "hrz_core.h"
#include "hrz_core_selection.h"

#include <gtest/gtest.h>

using namespace hrz;
using namespace selection;

struct Selection : public ::testing::Test
{
    SelectionSystem* sys;

    void SetUp() override { sys = create(); }

    void TearDown() override { destroy(sys); }
};

TEST_F(Selection, no_selection)
{
    EXPECT_FALSE(is_selected(sys, 2, 3));
    EXPECT_FALSE(is_selected(sys, 4, 8));
    EXPECT_FALSE(is_selected(sys, 6, 10));
}

TEST_F(Selection, select_some)
{
    EXPECT_FALSE(is_selected(sys, 2, 3));
    EXPECT_FALSE(is_selected(sys, 4, 8));
    EXPECT_FALSE(is_selected(sys, 6, 10));

    select(sys, 4, 8);
    EXPECT_FALSE(is_selected(sys, 2, 3));
    EXPECT_TRUE(is_selected(sys, 4, 8));
    EXPECT_FALSE(is_selected(sys, 6, 10));

    select(sys, 6, 10);
    EXPECT_FALSE(is_selected(sys, 2, 3));
    EXPECT_TRUE(is_selected(sys, 4, 8));
    EXPECT_TRUE(is_selected(sys, 6, 10));

    select(sys, 15, 32);
    EXPECT_FALSE(is_selected(sys, 2, 3));
    EXPECT_TRUE(is_selected(sys, 4, 8));
    EXPECT_TRUE(is_selected(sys, 6, 10));
}

TEST_F(Selection, deselect_some)
{
    select(sys, 4, 8);
    select(sys, 6, 10);
    select(sys, 15, 32);

    EXPECT_FALSE(is_selected(sys, 2, 3));
    EXPECT_TRUE(is_selected(sys, 4, 8));
    EXPECT_TRUE(is_selected(sys, 6, 10));

    deselect(sys, 78, 78);
    EXPECT_FALSE(is_selected(sys, 2, 3));
    EXPECT_TRUE(is_selected(sys, 4, 8));
    EXPECT_TRUE(is_selected(sys, 6, 10));

    deselect(sys, 6, 10);
    EXPECT_FALSE(is_selected(sys, 2, 3));
    EXPECT_TRUE(is_selected(sys, 4, 8));
    EXPECT_FALSE(is_selected(sys, 6, 10));

    deselect(sys, 4, 8);
    EXPECT_FALSE(is_selected(sys, 2, 3));
    EXPECT_FALSE(is_selected(sys, 4, 8));
    EXPECT_FALSE(is_selected(sys, 6, 10));
}

TEST_F(Selection, deselect_all)
{
    select(sys, 4, 8);
    select(sys, 6, 10);
    select(sys, 15, 32);

    deselect_all(sys);
    EXPECT_FALSE(is_selected(sys, 2, 3));
    EXPECT_FALSE(is_selected(sys, 4, 8));
    EXPECT_FALSE(is_selected(sys, 6, 10));
}

TEST_F(Selection, has_changed_since_last_frame)
{
    EXPECT_FALSE(has_changed_since_last_frame(sys));

    select(sys, 4, 8);
    select(sys, 6, 10);
    select(sys, 15, 32);
    EXPECT_TRUE(has_changed_since_last_frame(sys));
    finish_frame(sys);
    EXPECT_FALSE(has_changed_since_last_frame(sys));

    select(sys, 15, 32);
    EXPECT_FALSE(has_changed_since_last_frame(sys));

    deselect(sys, 4, 8);
    EXPECT_TRUE(has_changed_since_last_frame(sys));
    finish_frame(sys);
    EXPECT_FALSE(has_changed_since_last_frame(sys));

    deselect(sys, 34, 8);
    EXPECT_FALSE(has_changed_since_last_frame(sys));

    deselect_all(sys);
    EXPECT_TRUE(has_changed_since_last_frame(sys));
    finish_frame(sys);
    EXPECT_FALSE(has_changed_since_last_frame(sys));

    deselect_all(sys);
    EXPECT_FALSE(has_changed_since_last_frame(sys));
}

TEST_F(Selection, global_selected_objects_count)
{
    EXPECT_EQ(0, selected_objects_count(sys));

    select(sys, 4, 8);
    select(sys, 6, 10);
    select(sys, 15, 32);
    EXPECT_EQ(3, selected_objects_count(sys));

    select(sys, 15, 32);
    EXPECT_EQ(3, selected_objects_count(sys));

    deselect(sys, 4, 8);
    EXPECT_EQ(2, selected_objects_count(sys));

    deselect(sys, 34, 8);
    EXPECT_EQ(2, selected_objects_count(sys));

    deselect_all(sys);
    EXPECT_EQ(0, selected_objects_count(sys));
}

TEST_F(Selection, has_layer_changed_since_last_frame)
{
    EXPECT_FALSE(has_changed_since_last_frame(sys, 4));
    EXPECT_FALSE(has_changed_since_last_frame(sys, 6));
    EXPECT_FALSE(has_changed_since_last_frame(sys, 15));
    EXPECT_FALSE(has_changed_since_last_frame(sys, 34));

    select(sys, 4, 8);
    select(sys, 6, 10);
    select(sys, 15, 32);
    EXPECT_TRUE(has_changed_since_last_frame(sys, 4));
    EXPECT_TRUE(has_changed_since_last_frame(sys, 6));
    EXPECT_TRUE(has_changed_since_last_frame(sys, 15));
    EXPECT_FALSE(has_changed_since_last_frame(sys, 34));
    finish_frame(sys);
    EXPECT_FALSE(has_changed_since_last_frame(sys, 4));
    EXPECT_FALSE(has_changed_since_last_frame(sys, 6));
    EXPECT_FALSE(has_changed_since_last_frame(sys, 15));
    EXPECT_FALSE(has_changed_since_last_frame(sys, 34));

    select(sys, 15, 32);
    EXPECT_FALSE(has_changed_since_last_frame(sys, 4));
    EXPECT_FALSE(has_changed_since_last_frame(sys, 6));
    EXPECT_FALSE(has_changed_since_last_frame(sys, 15));
    EXPECT_FALSE(has_changed_since_last_frame(sys, 34));

    deselect(sys, 4, 8);
    EXPECT_TRUE(has_changed_since_last_frame(sys, 4));
    EXPECT_FALSE(has_changed_since_last_frame(sys, 6));
    EXPECT_FALSE(has_changed_since_last_frame(sys, 15));
    EXPECT_FALSE(has_changed_since_last_frame(sys, 34));
    finish_frame(sys);
    EXPECT_FALSE(has_changed_since_last_frame(sys, 4));
    EXPECT_FALSE(has_changed_since_last_frame(sys, 6));
    EXPECT_FALSE(has_changed_since_last_frame(sys, 15));
    EXPECT_FALSE(has_changed_since_last_frame(sys, 34));

    deselect(sys, 34, 8);
    EXPECT_FALSE(has_changed_since_last_frame(sys, 4));
    EXPECT_FALSE(has_changed_since_last_frame(sys, 6));
    EXPECT_FALSE(has_changed_since_last_frame(sys, 15));
    EXPECT_FALSE(has_changed_since_last_frame(sys, 34));

    deselect_all(sys);
    EXPECT_FALSE(has_changed_since_last_frame(sys, 4));
    EXPECT_TRUE(has_changed_since_last_frame(sys, 6));
    EXPECT_TRUE(has_changed_since_last_frame(sys, 15));
    EXPECT_FALSE(has_changed_since_last_frame(sys, 34));
    finish_frame(sys);
    EXPECT_FALSE(has_changed_since_last_frame(sys, 4));
    EXPECT_FALSE(has_changed_since_last_frame(sys, 6));
    EXPECT_FALSE(has_changed_since_last_frame(sys, 15));
    EXPECT_FALSE(has_changed_since_last_frame(sys, 34));

    deselect_all(sys);
    EXPECT_FALSE(has_changed_since_last_frame(sys, 4));
    EXPECT_FALSE(has_changed_since_last_frame(sys, 6));
    EXPECT_FALSE(has_changed_since_last_frame(sys, 15));
    EXPECT_FALSE(has_changed_since_last_frame(sys, 34));
}

TEST_F(Selection, layer_selected_objects_count)
{
    EXPECT_EQ(0, selected_objects_count(sys, 0));
    EXPECT_EQ(0, selected_objects_count(sys, 1));
    EXPECT_EQ(0, selected_objects_count(sys, 2));
    EXPECT_EQ(0, selected_objects_count(sys, 3));

    select(sys, 0, 1);
    select(sys, 1, 1);
    select(sys, 1, 2);
    select(sys, 2, 1);
    EXPECT_EQ(1, selected_objects_count(sys, 0));
    EXPECT_EQ(2, selected_objects_count(sys, 1));
    EXPECT_EQ(1, selected_objects_count(sys, 2));
    EXPECT_EQ(0, selected_objects_count(sys, 3));

    deselect(sys, 0, 1);
    EXPECT_EQ(0, selected_objects_count(sys, 0));
    EXPECT_EQ(2, selected_objects_count(sys, 1));
    EXPECT_EQ(1, selected_objects_count(sys, 2));
    EXPECT_EQ(0, selected_objects_count(sys, 3));

    deselect(sys, 2, 2);
    EXPECT_EQ(0, selected_objects_count(sys, 0));
    EXPECT_EQ(2, selected_objects_count(sys, 1));
    EXPECT_EQ(1, selected_objects_count(sys, 2));
    EXPECT_EQ(0, selected_objects_count(sys, 3));

    deselect(sys, 3, 2);
    EXPECT_EQ(0, selected_objects_count(sys, 0));
    EXPECT_EQ(2, selected_objects_count(sys, 1));
    EXPECT_EQ(1, selected_objects_count(sys, 2));
    EXPECT_EQ(0, selected_objects_count(sys, 3));

    deselect(sys, 1, 1);
    EXPECT_EQ(0, selected_objects_count(sys, 0));
    EXPECT_EQ(1, selected_objects_count(sys, 1));
    EXPECT_EQ(1, selected_objects_count(sys, 2));
    EXPECT_EQ(0, selected_objects_count(sys, 3));

    deselect(sys, 1, 2);
    EXPECT_EQ(0, selected_objects_count(sys, 0));
    EXPECT_EQ(0, selected_objects_count(sys, 1));
    EXPECT_EQ(1, selected_objects_count(sys, 2));
    EXPECT_EQ(0, selected_objects_count(sys, 3));
}

TEST_F(Selection, layer_get_selected_objects)
{
    uint64_t selected_objects[4];

    auto contains = [&](uint64_t object_id, size_t max) -> bool
    {
        for (size_t i = 0; i < max; ++i)
        {
            if (selected_objects[i] == object_id)
            {
                return true;
            }
        }
        return false;
    };

    select(sys, 0, 1);
    select(sys, 1, 1);
    select(sys, 1, 2);
    select(sys, 2, 1);
    EXPECT_EQ(1, get_selected_objects(sys, 0, std::span<uint64_t>(selected_objects)));
    ASSERT_TRUE(contains(1, 1));
    EXPECT_EQ(2, get_selected_objects(sys, 1, std::span<uint64_t>(selected_objects)));
    ASSERT_TRUE(contains(1, 2));
    ASSERT_TRUE(contains(2, 2));
    EXPECT_EQ(1, get_selected_objects(sys, 2, std::span<uint64_t>(selected_objects)));
    ASSERT_TRUE(contains(1, 1));

    deselect(sys, 0, 1);
    EXPECT_EQ(2, get_selected_objects(sys, 1, std::span<uint64_t>(selected_objects)));
    ASSERT_TRUE(contains(1, 2));
    ASSERT_TRUE(contains(2, 2));
    EXPECT_EQ(1, get_selected_objects(sys, 2, std::span<uint64_t>(selected_objects)));
    ASSERT_TRUE(contains(1, 1));

    deselect(sys, 2, 2);
    EXPECT_EQ(2, get_selected_objects(sys, 1, std::span<uint64_t>(selected_objects)));
    ASSERT_TRUE(contains(1, 2));
    ASSERT_TRUE(contains(2, 2));
    EXPECT_EQ(1, get_selected_objects(sys, 2, std::span<uint64_t>(selected_objects)));
    ASSERT_TRUE(contains(1, 1));

    deselect(sys, 3, 2);
    EXPECT_EQ(2, get_selected_objects(sys, 1, std::span<uint64_t>(selected_objects)));
    ASSERT_TRUE(contains(1, 2));
    ASSERT_TRUE(contains(2, 2));
    EXPECT_EQ(1, get_selected_objects(sys, 2, std::span<uint64_t>(selected_objects)));
    ASSERT_TRUE(contains(1, 1));

    deselect(sys, 1, 1);
    EXPECT_EQ(1, get_selected_objects(sys, 1, std::span<uint64_t>(selected_objects)));
    ASSERT_TRUE(contains(2, 1));
    EXPECT_EQ(1, get_selected_objects(sys, 2, std::span<uint64_t>(selected_objects)));
    ASSERT_TRUE(contains(1, 1));

    deselect(sys, 1, 2);
    EXPECT_EQ(1, get_selected_objects(sys, 2, std::span<uint64_t>(selected_objects)));
    ASSERT_TRUE(contains(1, 1));
}
