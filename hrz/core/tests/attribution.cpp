#include "hrz/core/attribution.h"

#include "hrz/core/planet/tile_fetcher.h"
#include "hrz/fnd/defines.h"

#include <gtest/gtest.h>

namespace
{

struct Attribution : public ::testing::Test
{
    hrz::AttributionRegistry* reg;

    void SetUp() override { reg = hrz::attribution::create_registry(); }

    void TearDown() override { hrz::attribution::destroy(reg); }
};

TEST_F(Attribution, deduplication)
{
    auto null = hrz::attribution::register_attribution(reg, {});
    auto hello_nologo = hrz::attribution::register_attribution(reg, {"hello", ""});
    auto hello_logo = hrz::attribution::register_attribution(reg, {"hello", "logo.png"});
    auto world_nologo = hrz::attribution::register_attribution(reg, {"world", ""});
    auto null2 = hrz::attribution::register_attribution(reg, {});
    auto hello_nologo2 = hrz::attribution::register_attribution(reg, {"hello", ""});
    auto hello_logo2 = hrz::attribution::register_attribution(reg, {"hello", "logo.png"});
    auto world_nologo2 = hrz::attribution::register_attribution(reg, {"world", ""});

    EXPECT_EQ(null, null2);
    EXPECT_EQ(hello_nologo, hello_nologo2);
    EXPECT_EQ(hello_logo, hello_logo2);
    EXPECT_EQ(world_nologo, world_nologo2);

    EXPECT_NE(null, hello_nologo);
    EXPECT_NE(null, hello_logo);
    EXPECT_NE(null, world_nologo);
    EXPECT_NE(hello_nologo, hello_logo);
    EXPECT_NE(hello_nologo, world_nologo);
    EXPECT_NE(hello_logo, world_nologo);
}

TEST_F(Attribution, deduplication_group)
{
    auto null = hrz::attribution::register_attribution(reg, {});
    auto a = hrz::attribution::register_attribution(reg, {"a", ""});
    auto b = hrz::attribution::register_attribution(reg, {"b", ""});
    auto c = hrz::attribution::register_attribution(reg, {"c", ""});
    auto d = hrz::attribution::register_attribution(reg, {"d", ""});
    auto e = hrz::attribution::register_attribution(reg, {"e", ""});
    auto f = hrz::attribution::register_attribution(reg, {"f", ""});

    hrz::AttributionHandle null_handle[] = {null};
    hrz::AttributionHandle abc_handles[] = {a, b, c};
    hrz::AttributionHandle ab_handles[] = {a, b};
    hrz::AttributionHandle cba_handles[] = {c, b, a};
    hrz::AttributionHandle caab_handles[] = {c, a, a, b};
    hrz::AttributionHandle def_handles[] = {d, e, f};
    auto null_group = hrz::attribution::register_attribution_group(reg, {});
    auto null_group2 = hrz::attribution::register_attribution_group(reg, null_handle);
    auto abc = hrz::attribution::register_attribution_group(reg, abc_handles);
    /*auto ab =*/hrz::attribution::register_attribution_group(reg, ab_handles);
    auto cba = hrz::attribution::register_attribution_group(reg, cba_handles);
    auto caab = hrz::attribution::register_attribution_group(reg, caab_handles);
    auto def = hrz::attribution::register_attribution_group(reg, def_handles);

    EXPECT_EQ(null_group, null_group2);
    EXPECT_EQ(abc, cba);
    EXPECT_EQ(abc, caab);

    EXPECT_NE(null_group, abc);
    EXPECT_NE(null_group, def);
    EXPECT_NE(abc, def);
}

TEST_F(Attribution, use)
{
    auto a = hrz::attribution::register_attribution(reg, {"a", "la"});
    auto b = hrz::attribution::register_attribution(reg, {"b", "lb"});
    auto c = hrz::attribution::register_attribution(reg, {"c", "lc"});

    hrz::attribution::use_this_frame(reg, a);
    hrz::attribution::use_this_frame(reg, c);
    auto used = hrz::attribution::get_frame_attributions(reg);
    ASSERT_EQ(used.size(), 2);
    EXPECT_EQ(used[0].title, "a");
    EXPECT_EQ(used[0].logo, "la");
    EXPECT_EQ(used[1].title, "c");
    EXPECT_EQ(used[1].logo, "lc");

    hrz::attribution::reset_used_attributions(reg);
    hrz::attribution::use_this_frame(reg, c);
    hrz::attribution::use_this_frame(reg, b);
    used = hrz::attribution::get_frame_attributions(reg);
    ASSERT_EQ(used.size(), 2);
    EXPECT_EQ(used[0].title, "c");
    EXPECT_EQ(used[1].title, "b");

    hrz::attribution::reset_used_attributions(reg);
    used = hrz::attribution::get_frame_attributions(reg);
    ASSERT_EQ(used.size(), 0);

    hrz::attribution::reset_used_attributions(reg);
    hrz::attribution::use_this_frame(reg, a);
    hrz::attribution::use_this_frame(reg, a);
    hrz::attribution::use_this_frame(reg, a);
    hrz::attribution::use_this_frame(reg, b);
    hrz::attribution::use_this_frame(reg, b);
    hrz::attribution::use_this_frame(reg, a);
    hrz::attribution::use_this_frame(reg, a);
    hrz::attribution::use_this_frame(reg, b);
    hrz::attribution::use_this_frame(reg, b);
    hrz::attribution::use_this_frame(reg, b);
    used = hrz::attribution::get_frame_attributions(reg);
    ASSERT_EQ(used.size(), 2);
    EXPECT_EQ(used[0].title, "a");
    EXPECT_EQ(used[1].title, "b");

    hrz::attribution::reset_used_attributions(reg);
    hrz::AttributionHandle array[] = {a, b, {}, a, a};
    hrz::attribution::use_this_frame(reg, array);
    used = hrz::attribution::get_frame_attributions(reg);
    ASSERT_EQ(used.size(), 2);
    EXPECT_EQ(used[0].title, "a");
    EXPECT_EQ(used[1].title, "b");
}

TEST_F(Attribution, use_groups)
{
    auto null = hrz::attribution::register_attribution(reg, {});
    auto a = hrz::attribution::register_attribution(reg, {"a", ""});
    auto b = hrz::attribution::register_attribution(reg, {"b", ""});
    auto c = hrz::attribution::register_attribution(reg, {"c", ""});
    auto d = hrz::attribution::register_attribution(reg, {"d", ""});
    auto e = hrz::attribution::register_attribution(reg, {"e", ""});
    auto f = hrz::attribution::register_attribution(reg, {"f", ""});

    hrz::AttributionHandle null_handle[] = {null};
    hrz::AttributionHandle abc_handles[] = {a, b, c};
    hrz::AttributionHandle ab_handles[] = {a, b};
    hrz::AttributionHandle cba_handles[] = {c, b, a};
    hrz::AttributionHandle def_handles[] = {d, e, f};
    /*auto null_group =*/hrz::attribution::register_attribution_group(reg, {});
    /*auto null_group2 =*/hrz::attribution::register_attribution_group(reg, null_handle);
    auto abc = hrz::attribution::register_attribution_group(reg, abc_handles);
    auto ab = hrz::attribution::register_attribution_group(reg, ab_handles);
    /*auto cba =*/hrz::attribution::register_attribution_group(reg, cba_handles);
    auto def = hrz::attribution::register_attribution_group(reg, def_handles);

    hrz::attribution::reset_used_attributions(reg);
    hrz::attribution::use_this_frame(reg, abc);
    auto used = hrz::attribution::get_frame_attributions(reg);
    ASSERT_EQ(used.size(), 3);
    EXPECT_EQ(used[0].title, "a");
    EXPECT_EQ(used[1].title, "b");
    EXPECT_EQ(used[2].title, "c");

    hrz::attribution::reset_used_attributions(reg);
    hrz::attribution::use_this_frame(reg, def);
    used = hrz::attribution::get_frame_attributions(reg);
    ASSERT_EQ(used.size(), 3);
    EXPECT_EQ(used[0].title, "d");
    EXPECT_EQ(used[1].title, "e");
    EXPECT_EQ(used[2].title, "f");

    hrz::attribution::reset_used_attributions(reg);
    hrz::attribution::use_this_frame(reg, abc);
    hrz::attribution::use_this_frame(reg, ab);
    hrz::attribution::use_this_frame(reg, a);
    used = hrz::attribution::get_frame_attributions(reg);
    ASSERT_EQ(used.size(), 3);
    EXPECT_EQ(used[0].title, "a");
    EXPECT_EQ(used[1].title, "b");
    EXPECT_EQ(used[2].title, "c");

    hrz::attribution::reset_used_attributions(reg);
    hrz::attribution::use_this_frame(reg, def);
    hrz::attribution::use_this_frame(reg, ab);
    used = hrz::attribution::get_frame_attributions(reg);
    ASSERT_EQ(used.size(), 5);
    EXPECT_EQ(used[0].title, "d");
    EXPECT_EQ(used[1].title, "e");
    EXPECT_EQ(used[2].title, "f");
    EXPECT_EQ(used[3].title, "a");
    EXPECT_EQ(used[4].title, "b");

    hrz::attribution::reset_used_attributions(reg);
    hrz::attribution::use_this_frame(reg, b);
    hrz::attribution::use_this_frame(reg, abc);
    used = hrz::attribution::get_frame_attributions(reg);
    ASSERT_EQ(used.size(), 3);
    EXPECT_EQ(used[0].title, "b");
    EXPECT_EQ(used[1].title, "a");
    EXPECT_EQ(used[2].title, "c");
}

static constexpr hrz::GeoBounds kFullBounds = {-lm::PI, lm::PI, -lm::PI / 2, lm::PI / 2};

TEST(WebMercatorZoneAttribution, lods_min)
{
    hrz::planet::WebMercatorZonesTileAttributionPolicy attributions;
    attributions.add_zone(1, 20, kFullBounds, {1});
    attributions.add_zone(5, 20, kFullBounds, {2});
    attributions.add_zone(10, 20, kFullBounds, {3});

    auto handles = attributions.get_tile_attribution({0, 0, 2});
    ASSERT_EQ(handles.size(), 1);
    EXPECT_EQ(handles[0].o, 1);

    handles = attributions.get_tile_attribution({0, 0, 0});
    ASSERT_EQ(handles.size(), 0);

    handles = attributions.get_tile_attribution({0, 0, 5});
    ASSERT_EQ(handles.size(), 2);
    EXPECT_EQ(handles[0].o, 1);
    EXPECT_EQ(handles[1].o, 2);

    handles = attributions.get_tile_attribution({0, 0, 9});
    ASSERT_EQ(handles.size(), 2);
    EXPECT_EQ(handles[0].o, 1);
    EXPECT_EQ(handles[1].o, 2);
}

TEST(WebMercatorZoneAttribution, lods_max)
{
    hrz::planet::WebMercatorZonesTileAttributionPolicy attributions;
    attributions.add_zone(0, 5, kFullBounds, {1});
    attributions.add_zone(0, 10, kFullBounds, {2});
    attributions.add_zone(0, 20, kFullBounds, {3});

    auto handles = attributions.get_tile_attribution({0, 0, 5});
    ASSERT_EQ(handles.size(), 3);
    EXPECT_EQ(handles[0].o, 1);
    EXPECT_EQ(handles[1].o, 2);
    EXPECT_EQ(handles[2].o, 3);

    handles = attributions.get_tile_attribution({0, 0, 16});
    ASSERT_EQ(handles.size(), 1);
    EXPECT_EQ(handles[0].o, 3);

    handles = attributions.get_tile_attribution({0, 0, 22});
    ASSERT_EQ(handles.size(), 0);
}

TEST(WebMercatorZoneAttribution, lods)
{
    hrz::planet::WebMercatorZonesTileAttributionPolicy attributions;
    attributions.add_zone(0, 5, kFullBounds, {1});
    attributions.add_zone(10, 20, kFullBounds, {2});
    attributions.add_zone(14, 16, kFullBounds, {3});

    auto handles = attributions.get_tile_attribution({0, 0, 4});
    ASSERT_EQ(handles.size(), 1);
    EXPECT_EQ(handles[0].o, 1);

    handles = attributions.get_tile_attribution({0, 0, 7});
    ASSERT_EQ(handles.size(), 0);

    handles = attributions.get_tile_attribution({0, 0, 56});
    ASSERT_EQ(handles.size(), 0);

    handles = attributions.get_tile_attribution({0, 0, 11});
    ASSERT_EQ(handles.size(), 1);
    EXPECT_EQ(handles[0].o, 2);

    handles = attributions.get_tile_attribution({0, 0, 19});
    ASSERT_EQ(handles.size(), 1);
    EXPECT_EQ(handles[0].o, 2);

    handles = attributions.get_tile_attribution({0, 0, 15});
    ASSERT_EQ(handles.size(), 2);
    EXPECT_EQ(handles[0].o, 2);
    EXPECT_EQ(handles[1].o, 3);
}

TEST(WebMercatorZoneAttribution, full_bounds)
{
    hrz::planet::WebMercatorZonesTileAttributionPolicy attributions;
    attributions.add_zone(0, 50, kFullBounds, {1});

    auto handles = attributions.get_tile_attribution({0, 0, 0});
    ASSERT_EQ(handles.size(), 1);

    handles = attributions.get_tile_attribution({0, 0, 1});
    ASSERT_EQ(handles.size(), 1);

    handles = attributions.get_tile_attribution({13, 67, 8});
    ASSERT_EQ(handles.size(), 1);
}

TEST(WebMercatorZoneAttribution, partial_bounds)
{
    hrz::planet::WebMercatorZonesTileAttributionPolicy attributions;
    attributions.add_zone(0, 50, {-lm::PI, 0, -lm::PI / 2, 0}, {1});
    attributions.add_zone(0, 50, {0, lm::PI, -lm::PI / 2, 0}, {2});
    attributions.add_zone(0, 50, {-lm::PI, 0, 0, lm::PI / 2}, {3});
    attributions.add_zone(0, 50, {0, lm::PI, 0, lm::PI / 2}, {4});
    attributions.add_zone(0, 50, {-lm::PI, lm::PI, -lm::PI / 2, 0}, {5});

    auto handles = attributions.get_tile_attribution({0, 0, 0});
    ASSERT_EQ(handles.size(), 5);
    EXPECT_EQ(handles[0].o, 1);
    EXPECT_EQ(handles[1].o, 2);
    EXPECT_EQ(handles[2].o, 3);
    EXPECT_EQ(handles[3].o, 4);
    EXPECT_EQ(handles[4].o, 5);

    handles = attributions.get_tile_attribution({0, 3, 2});
    ASSERT_EQ(handles.size(), 2);
    EXPECT_EQ(handles[0].o, 1);
    EXPECT_EQ(handles[1].o, 5);

    handles = attributions.get_tile_attribution({3, 0, 2});
    ASSERT_EQ(handles.size(), 1);
    EXPECT_EQ(handles[0].o, 4);
}

} // namespace
