#include "hrz/common/reprojection.h"

#include "hrz/common/crs_database.h"
#include "hrz/common/planet/tiled_raster_geometry.h"

#include <gtest/gtest.h>

using namespace hrz;

class ReprojectionTest : public ::testing::Test
{
protected:
    hrz_proto::RasterGeometry local_geometry;
    hrz_proto::TilingSchemeParams local_top_clipped_ts;
    hrz_proto::TilingSchemeParams local_top_full_sized_ts;
    hrz_proto::TilingSchemeParams local_bottom_clipped_ts;
    hrz_proto::TilingSchemeParams local_bottom_full_sized_ts;

    hrz_proto::RasterGeometry global_geometry;
    hrz_proto::TilingSchemeParams global_clipped_ts;
    hrz_proto::TilingSchemeParams global_full_sized_ts;

    static constexpr float eps = 0.01;

    void SetUp() override
    {
        local_geometry.mutable_projection()->set_descriptor_type(
            hrz_proto::SrsDescriptorType::SRID_DESCRIPTOR);
        local_geometry.mutable_projection()->set_descriptor_("EPSG:3948");
        local_geometry.mutable_projection_bounds()->set_x_min(1100000);
        local_geometry.mutable_projection_bounds()->set_x_max(1600000);
        local_geometry.mutable_projection_bounds()->set_y_min(7000000);
        local_geometry.mutable_projection_bounds()->set_y_max(7333205);
        local_geometry.mutable_bounds()->set_x_min(1100000);
        local_geometry.mutable_bounds()->set_x_max(1600000);
        local_geometry.mutable_bounds()->set_y_min(7000000);
        local_geometry.mutable_bounds()->set_y_max(7333205);

        local_top_clipped_ts.set_type(hrz_proto::TilingSchemeType::LOCAL);
        local_top_clipped_ts.mutable_local_tiling()->set_full_image_width(1301);
        local_top_clipped_ts.mutable_local_tiling()->set_full_image_height(867);
        local_top_clipped_ts.mutable_local_tiling()->set_tile_size(256);
        local_top_clipped_ts.mutable_local_tiling()->set_has_max_level(true);
        local_top_clipped_ts.mutable_local_tiling()->set_max_level(3);
        local_top_clipped_ts.mutable_local_tiling()->set_tiling_origin(
            hrz_proto::TilingOrigin::TOP_ORIGIN);
        local_top_clipped_ts.mutable_local_tiling()->set_border_tile_aspect(
            hrz_proto::BorderTileAspect::CLIPPED);

        local_top_full_sized_ts.set_type(hrz_proto::TilingSchemeType::LOCAL);
        local_top_full_sized_ts.mutable_local_tiling()->set_full_image_width(1301);
        local_top_full_sized_ts.mutable_local_tiling()->set_full_image_height(867);
        local_top_full_sized_ts.mutable_local_tiling()->set_tile_size(256);
        local_top_full_sized_ts.mutable_local_tiling()->set_has_max_level(true);
        local_top_full_sized_ts.mutable_local_tiling()->set_max_level(3);
        local_top_full_sized_ts.mutable_local_tiling()->set_tiling_origin(
            hrz_proto::TilingOrigin::TOP_ORIGIN);
        local_top_full_sized_ts.mutable_local_tiling()->set_border_tile_aspect(
            hrz_proto::BorderTileAspect::FULL_SIZED);

        local_bottom_clipped_ts.set_type(hrz_proto::TilingSchemeType::LOCAL);
        local_bottom_clipped_ts.mutable_local_tiling()->set_full_image_width(1301);
        local_bottom_clipped_ts.mutable_local_tiling()->set_full_image_height(867);
        local_bottom_clipped_ts.mutable_local_tiling()->set_tile_size(256);
        local_bottom_clipped_ts.mutable_local_tiling()->set_has_max_level(true);
        local_bottom_clipped_ts.mutable_local_tiling()->set_max_level(3);
        local_bottom_clipped_ts.mutable_local_tiling()->set_tiling_origin(
            hrz_proto::TilingOrigin::BOTTOM_ORIGIN);
        local_bottom_clipped_ts.mutable_local_tiling()->set_border_tile_aspect(
            hrz_proto::BorderTileAspect::CLIPPED);

        local_bottom_full_sized_ts.set_type(hrz_proto::TilingSchemeType::LOCAL);
        local_bottom_full_sized_ts.mutable_local_tiling()->set_full_image_width(1301);
        local_bottom_full_sized_ts.mutable_local_tiling()->set_full_image_height(867);
        local_bottom_full_sized_ts.mutable_local_tiling()->set_tile_size(256);
        local_bottom_full_sized_ts.mutable_local_tiling()->set_has_max_level(true);
        local_bottom_full_sized_ts.mutable_local_tiling()->set_max_level(3);
        local_bottom_full_sized_ts.mutable_local_tiling()->set_tiling_origin(
            hrz_proto::TilingOrigin::BOTTOM_ORIGIN);
        local_bottom_full_sized_ts.mutable_local_tiling()->set_border_tile_aspect(
            hrz_proto::BorderTileAspect::FULL_SIZED);

        global_geometry.mutable_projection()->set_descriptor_type(
            hrz_proto::SrsDescriptorType::SRID_DESCRIPTOR);
        global_geometry.mutable_projection()->set_descriptor_("EPSG:3948");
        global_geometry.mutable_projection_bounds()->set_x_min(864796);
        global_geometry.mutable_projection_bounds()->set_x_max(1651883);
        global_geometry.mutable_projection_bounds()->set_y_min(6870100);
        global_geometry.mutable_projection_bounds()->set_y_max(7657186);
        global_geometry.mutable_bounds()->set_x_min(1100000);
        global_geometry.mutable_bounds()->set_x_max(1600000);
        global_geometry.mutable_bounds()->set_y_min(7000000);
        global_geometry.mutable_bounds()->set_y_max(7333205);

        global_clipped_ts.set_type(hrz_proto::TilingSchemeType::GLOBAL);
        global_clipped_ts.mutable_global_tiling()->set_tile_size(256);
        global_clipped_ts.mutable_global_tiling()->set_level_zero_tile_count_x(1);
        global_clipped_ts.mutable_global_tiling()->set_level_zero_tile_count_y(1);
        global_clipped_ts.mutable_global_tiling()->set_min_level(0);
        global_clipped_ts.mutable_global_tiling()->set_max_level(3);
        global_clipped_ts.mutable_global_tiling()->set_border_tile_aspect(
            hrz_proto::BorderTileAspect::CLIPPED);

        global_full_sized_ts.set_type(hrz_proto::TilingSchemeType::GLOBAL);
        global_full_sized_ts.mutable_global_tiling()->set_tile_size(256);
        global_full_sized_ts.mutable_global_tiling()->set_level_zero_tile_count_x(1);
        global_full_sized_ts.mutable_global_tiling()->set_level_zero_tile_count_y(1);
        global_full_sized_ts.mutable_global_tiling()->set_min_level(0);
        global_full_sized_ts.mutable_global_tiling()->set_max_level(3);
        global_full_sized_ts.mutable_global_tiling()->set_border_tile_aspect(
            hrz_proto::BorderTileAspect::FULL_SIZED);
    }
};

TEST_F(ReprojectionTest, proj_pos_to_tile_uv_local_top_clipped)
{
    auto tiled_raster_geometry = planet::TiledRasterGeometry::from_geometry_and_tiling_scheme(
        local_geometry, local_top_clipped_ts);

    pl_Crs crs;
    EXPECT_TRUE(hrz::convert_crs(tiled_raster_geometry.projection, &crs));

    auto tiling_info = compute_image_tiling_info(tiled_raster_geometry, &crs);

    {
        auto tile_uv = proj_pos_to_tile_uv({1000000, 7329128}, 3, tiling_info);
        EXPECT_FALSE(tile_uv.has_value());
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1107857.3756304502, 7329128.878252053}, 3, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        EXPECT_EQ(tile_uv->tile_coords.lod, 3);
        EXPECT_EQ(tile_uv->tile_coords.x, 0);
        EXPECT_EQ(tile_uv->tile_coords.y, 0);
        EXPECT_EQ(tile_uv->tile_size.x, 256);
        EXPECT_EQ(tile_uv->tile_size.y, 256);
        EXPECT_EQ((uint32_t)(tile_uv->uv.x * tile_uv->tile_size.x), 20);
        EXPECT_EQ((uint32_t)(tile_uv->uv.y * tile_uv->tile_size.y), 10);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7004010.094375875}, 3, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        EXPECT_EQ(tile_uv->tile_coords.lod, 3);
        EXPECT_EQ(tile_uv->tile_coords.x, 5);
        EXPECT_EQ(tile_uv->tile_coords.y, 3);
        EXPECT_EQ(tile_uv->tile_size.x, 21);
        EXPECT_EQ(tile_uv->tile_size.y, 99);
        EXPECT_EQ((uint32_t)(tile_uv->uv.x * tile_uv->tile_size.x), 8);
        EXPECT_EQ((uint32_t)(tile_uv->uv.y * tile_uv->tile_size.y), 88);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7004010.094375875}, 2, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        EXPECT_EQ(tile_uv->tile_coords.lod, 2);
        EXPECT_EQ(tile_uv->tile_coords.x, 2);
        EXPECT_EQ(tile_uv->tile_coords.y, 1);
        EXPECT_EQ(tile_uv->tile_size.x, 138);
        EXPECT_EQ(tile_uv->tile_size.y, 177);
        EXPECT_EQ((uint32_t)(tile_uv->uv.x * tile_uv->tile_size.x), 131);
        EXPECT_EQ((uint32_t)(tile_uv->uv.y * tile_uv->tile_size.y), 171);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7004010.094375875}, 1, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        EXPECT_EQ(tile_uv->tile_coords.lod, 1);
        EXPECT_EQ(tile_uv->tile_coords.x, 1);
        EXPECT_EQ(tile_uv->tile_coords.y, 0);
        EXPECT_EQ(tile_uv->tile_size.x, 69);
        EXPECT_EQ(tile_uv->tile_size.y, 216);
        EXPECT_EQ((uint32_t)(tile_uv->uv.x * tile_uv->tile_size.x), 65);
        EXPECT_EQ((uint32_t)(tile_uv->uv.y * tile_uv->tile_size.y), 213);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7004010.094375875}, 0, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        EXPECT_EQ(tile_uv->tile_coords.lod, 0);
        EXPECT_EQ(tile_uv->tile_coords.x, 0);
        EXPECT_EQ(tile_uv->tile_coords.y, 0);
        EXPECT_EQ(tile_uv->tile_size.x, 162);
        EXPECT_EQ(tile_uv->tile_size.y, 108);
        EXPECT_EQ((uint32_t)(tile_uv->uv.x * tile_uv->tile_size.x), 160);
        EXPECT_EQ((uint32_t)(tile_uv->uv.y * tile_uv->tile_size.y), 106);
    }
}

TEST_F(ReprojectionTest, proj_pos_to_tile_uv_local_top_full_sized)
{
    auto tiled_raster_geometry = planet::TiledRasterGeometry::from_geometry_and_tiling_scheme(
        local_geometry, local_top_full_sized_ts);

    pl_Crs crs;
    EXPECT_TRUE(hrz::convert_crs(tiled_raster_geometry.projection, &crs));

    auto tiling_info = compute_image_tiling_info(tiled_raster_geometry, &crs);

    {
        auto tile_uv = proj_pos_to_tile_uv({1000000, 7329128}, 3, tiling_info);
        EXPECT_FALSE(tile_uv.has_value());
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1107857.3756304502, 7329128.878252053}, 3, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        EXPECT_EQ(tile_uv->tile_coords.lod, 3);
        EXPECT_EQ(tile_uv->tile_coords.x, 0);
        EXPECT_EQ(tile_uv->tile_coords.y, 0);
        EXPECT_EQ(tile_uv->tile_size.x, 256);
        EXPECT_EQ(tile_uv->tile_size.y, 256);
        EXPECT_EQ((uint32_t)(tile_uv->uv.x * tile_uv->tile_size.x), 20);
        EXPECT_EQ((uint32_t)(tile_uv->uv.y * tile_uv->tile_size.y), 10);
    }

    // Pixel coordinates below this points are off by one from the previous tests.
    // This is because the tiles on the last column and row have odd dimensions.
    // When tiles are clipped, images sizes are rounded down when generating lower
    // LODs.
    // When tiles are full sized, in order to support the case where they are tiles
    // that are technically part of a geographically larger tileset, there is no
    // such rounding down of the area with pixels that are considered part of the
    // dataset.

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7004010.094375875}, 3, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        EXPECT_EQ(tile_uv->tile_coords.lod, 3);
        EXPECT_EQ(tile_uv->tile_coords.x, 5);
        EXPECT_EQ(tile_uv->tile_coords.y, 3);
        EXPECT_EQ(tile_uv->tile_size.x, 256);
        EXPECT_EQ(tile_uv->tile_size.y, 256);
        EXPECT_EQ((uint32_t)(tile_uv->uv.x * tile_uv->tile_size.x), 8);
        EXPECT_EQ((uint32_t)(tile_uv->uv.y * tile_uv->tile_size.y), 88);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7004010.094375875}, 2, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        EXPECT_EQ(tile_uv->tile_coords.lod, 2);
        EXPECT_EQ(tile_uv->tile_coords.x, 2);
        EXPECT_EQ(tile_uv->tile_coords.y, 1);
        EXPECT_EQ(tile_uv->tile_size.x, 256);
        EXPECT_EQ(tile_uv->tile_size.y, 256);
        EXPECT_EQ((uint32_t)(tile_uv->uv.x * tile_uv->tile_size.x), 132);
        EXPECT_EQ((uint32_t)(tile_uv->uv.y * tile_uv->tile_size.y), 172);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7004010.094375875}, 1, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        EXPECT_EQ(tile_uv->tile_coords.lod, 1);
        EXPECT_EQ(tile_uv->tile_coords.x, 1);
        EXPECT_EQ(tile_uv->tile_coords.y, 0);
        EXPECT_EQ(tile_uv->tile_size.x, 256);
        EXPECT_EQ(tile_uv->tile_size.y, 256);
        EXPECT_EQ((uint32_t)(tile_uv->uv.x * tile_uv->tile_size.x), 66);
        EXPECT_EQ((uint32_t)(tile_uv->uv.y * tile_uv->tile_size.y), 214);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7004010.094375875}, 0, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        EXPECT_EQ(tile_uv->tile_coords.lod, 0);
        EXPECT_EQ(tile_uv->tile_coords.x, 0);
        EXPECT_EQ(tile_uv->tile_coords.y, 0);
        EXPECT_EQ(tile_uv->tile_size.x, 256);
        EXPECT_EQ(tile_uv->tile_size.y, 256);
        EXPECT_EQ((uint32_t)(tile_uv->uv.x * tile_uv->tile_size.x), 161);
        EXPECT_EQ((uint32_t)(tile_uv->uv.y * tile_uv->tile_size.y), 107);
    }
}

TEST_F(ReprojectionTest, proj_pos_to_tile_uv_local_bottom_clipped)
{
    auto tiled_raster_geometry = planet::TiledRasterGeometry::from_geometry_and_tiling_scheme(
        local_geometry, local_bottom_clipped_ts);

    pl_Crs crs;
    EXPECT_TRUE(hrz::convert_crs(tiled_raster_geometry.projection, &crs));

    auto tiling_info = compute_image_tiling_info(tiled_raster_geometry, &crs);

    {
        auto tile_uv = proj_pos_to_tile_uv({1000000, 7329128}, 3, tiling_info);
        EXPECT_FALSE(tile_uv.has_value());
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1107857.3756304502, 7329128.878252053}, 3, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        EXPECT_EQ(tile_uv->tile_coords.lod, 3);
        EXPECT_EQ(tile_uv->tile_coords.x, 0);
        EXPECT_EQ(tile_uv->tile_coords.y, 0);
        EXPECT_EQ(tile_uv->tile_size.x, 256);
        EXPECT_EQ(tile_uv->tile_size.y, 99);
        EXPECT_EQ((uint32_t)(tile_uv->uv.x * tile_uv->tile_size.x), 20);
        EXPECT_EQ((uint32_t)(tile_uv->uv.y * tile_uv->tile_size.y), 10);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7004010.094375875}, 2, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        EXPECT_EQ(tile_uv->tile_coords.lod, 2);
        EXPECT_EQ(tile_uv->tile_coords.x, 2);
        EXPECT_EQ(tile_uv->tile_coords.y, 1);
        EXPECT_EQ(tile_uv->tile_size.x, 138);
        EXPECT_EQ(tile_uv->tile_size.y, 256);
        EXPECT_EQ((uint32_t)(tile_uv->uv.x * tile_uv->tile_size.x), 131);
        EXPECT_EQ((uint32_t)(tile_uv->uv.y * tile_uv->tile_size.y), 250);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7004010.094375875}, 1, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        EXPECT_EQ(tile_uv->tile_coords.lod, 1);
        EXPECT_EQ(tile_uv->tile_coords.x, 1);
        EXPECT_EQ(tile_uv->tile_coords.y, 0);
        EXPECT_EQ(tile_uv->tile_size.x, 69);
        EXPECT_EQ(tile_uv->tile_size.y, 216);
        EXPECT_EQ((uint32_t)(tile_uv->uv.x * tile_uv->tile_size.x), 65);
        EXPECT_EQ((uint32_t)(tile_uv->uv.y * tile_uv->tile_size.y), 213);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7004010.094375875}, 0, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        EXPECT_EQ(tile_uv->tile_coords.lod, 0);
        EXPECT_EQ(tile_uv->tile_coords.x, 0);
        EXPECT_EQ(tile_uv->tile_coords.y, 0);
        EXPECT_EQ(tile_uv->tile_size.x, 162);
        EXPECT_EQ(tile_uv->tile_size.y, 108);
        EXPECT_EQ((uint32_t)(tile_uv->uv.x * tile_uv->tile_size.x), 160);
        EXPECT_EQ((uint32_t)(tile_uv->uv.y * tile_uv->tile_size.y), 106);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7329128.878252053}, 3, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        EXPECT_EQ(tile_uv->tile_coords.lod, 3);
        EXPECT_EQ(tile_uv->tile_coords.x, 5);
        EXPECT_EQ(tile_uv->tile_coords.y, 0);
        EXPECT_EQ(tile_uv->tile_size.x, 21);
        EXPECT_EQ(tile_uv->tile_size.y, 99);
        EXPECT_EQ((uint32_t)(tile_uv->uv.x * tile_uv->tile_size.x), 8);
        EXPECT_EQ((uint32_t)(tile_uv->uv.y * tile_uv->tile_size.y), 10);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7329128.878252053}, 2, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        EXPECT_EQ(tile_uv->tile_coords.lod, 2);
        EXPECT_EQ(tile_uv->tile_coords.x, 2);
        EXPECT_EQ(tile_uv->tile_coords.y, 0);
        EXPECT_EQ(tile_uv->tile_size.x, 138);
        EXPECT_EQ(tile_uv->tile_size.y, 177);
        EXPECT_EQ((uint32_t)(tile_uv->uv.x * tile_uv->tile_size.x), 131);
        EXPECT_EQ((uint32_t)(tile_uv->uv.y * tile_uv->tile_size.y), 5);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7329128.878252053}, 1, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        EXPECT_EQ(tile_uv->tile_coords.lod, 1);
        EXPECT_EQ(tile_uv->tile_coords.x, 1);
        EXPECT_EQ(tile_uv->tile_coords.y, 0);
        EXPECT_EQ(tile_uv->tile_size.x, 69);
        EXPECT_EQ(tile_uv->tile_size.y, 216);
        EXPECT_EQ((uint32_t)(tile_uv->uv.x * tile_uv->tile_size.x), 65);
        EXPECT_EQ((uint32_t)(tile_uv->uv.y * tile_uv->tile_size.y), 2);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7329128.878252053}, 0, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        EXPECT_EQ(tile_uv->tile_coords.lod, 0);
        EXPECT_EQ(tile_uv->tile_coords.x, 0);
        EXPECT_EQ(tile_uv->tile_coords.y, 0);
        EXPECT_EQ(tile_uv->tile_size.x, 162);
        EXPECT_EQ(tile_uv->tile_size.y, 108);
        EXPECT_EQ((uint32_t)(tile_uv->uv.x * tile_uv->tile_size.x), 160);
        EXPECT_EQ((uint32_t)(tile_uv->uv.y * tile_uv->tile_size.y), 1);
    }
}

TEST_F(ReprojectionTest, proj_pos_to_tile_uv_local_bottom_full_sized)
{
    auto tiled_raster_geometry = planet::TiledRasterGeometry::from_geometry_and_tiling_scheme(
        local_geometry, local_bottom_full_sized_ts);

    pl_Crs crs;
    EXPECT_TRUE(hrz::convert_crs(tiled_raster_geometry.projection, &crs));

    auto tiling_info = compute_image_tiling_info(tiled_raster_geometry, &crs);

    {
        auto tile_uv = proj_pos_to_tile_uv({1000000, 7329128}, 3, tiling_info);
        EXPECT_FALSE(tile_uv.has_value());
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1107857.3756304502, 7329128.878252053}, 3, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        EXPECT_EQ(tile_uv->tile_coords.lod, 3);
        EXPECT_EQ(tile_uv->tile_coords.x, 0);
        EXPECT_EQ(tile_uv->tile_coords.y, 0);
        EXPECT_EQ(tile_uv->tile_size.x, 256);
        EXPECT_EQ(tile_uv->tile_size.y, 256);
        EXPECT_EQ((uint32_t)(tile_uv->uv.x * tile_uv->tile_size.x), 20);
        EXPECT_EQ((uint32_t)(tile_uv->uv.y * tile_uv->tile_size.y), 167);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7004010.094375875}, 2, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        EXPECT_EQ(tile_uv->tile_coords.lod, 2);
        EXPECT_EQ(tile_uv->tile_coords.x, 2);
        EXPECT_EQ(tile_uv->tile_coords.y, 1);
        EXPECT_EQ(tile_uv->tile_size.x, 256);
        EXPECT_EQ(tile_uv->tile_size.y, 256);
        EXPECT_EQ((uint32_t)(tile_uv->uv.x * tile_uv->tile_size.x), 132);
        EXPECT_EQ((uint32_t)(tile_uv->uv.y * tile_uv->tile_size.y), 250);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7004010.094375875}, 1, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        EXPECT_EQ(tile_uv->tile_coords.lod, 1);
        EXPECT_EQ(tile_uv->tile_coords.x, 1);
        EXPECT_EQ(tile_uv->tile_coords.y, 0);
        EXPECT_EQ(tile_uv->tile_size.x, 256);
        EXPECT_EQ(tile_uv->tile_size.y, 256);
        EXPECT_EQ((uint32_t)(tile_uv->uv.x * tile_uv->tile_size.x), 66);
        EXPECT_EQ((uint32_t)(tile_uv->uv.y * tile_uv->tile_size.y), 253);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7004010.094375875}, 0, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        EXPECT_EQ(tile_uv->tile_coords.lod, 0);
        EXPECT_EQ(tile_uv->tile_coords.x, 0);
        EXPECT_EQ(tile_uv->tile_coords.y, 0);
        EXPECT_EQ(tile_uv->tile_size.x, 256);
        EXPECT_EQ(tile_uv->tile_size.y, 256);
        EXPECT_EQ((uint32_t)(tile_uv->uv.x * tile_uv->tile_size.x), 161);
        EXPECT_EQ((uint32_t)(tile_uv->uv.y * tile_uv->tile_size.y), 254);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7329128.878252053}, 3, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        EXPECT_EQ(tile_uv->tile_coords.lod, 3);
        EXPECT_EQ(tile_uv->tile_coords.x, 5);
        EXPECT_EQ(tile_uv->tile_coords.y, 0);
        EXPECT_EQ(tile_uv->tile_size.x, 256);
        EXPECT_EQ(tile_uv->tile_size.y, 256);
        EXPECT_EQ((uint32_t)(tile_uv->uv.x * tile_uv->tile_size.x), 8);
        EXPECT_EQ((uint32_t)(tile_uv->uv.y * tile_uv->tile_size.y), 167);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7329128.878252053}, 2, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        EXPECT_EQ(tile_uv->tile_coords.lod, 2);
        EXPECT_EQ(tile_uv->tile_coords.x, 2);
        EXPECT_EQ(tile_uv->tile_coords.y, 0);
        EXPECT_EQ(tile_uv->tile_size.x, 256);
        EXPECT_EQ(tile_uv->tile_size.y, 256);
        EXPECT_EQ((uint32_t)(tile_uv->uv.x * tile_uv->tile_size.x), 132);
        EXPECT_EQ((uint32_t)(tile_uv->uv.y * tile_uv->tile_size.y), 83);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7329128.878252053}, 1, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        EXPECT_EQ(tile_uv->tile_coords.lod, 1);
        EXPECT_EQ(tile_uv->tile_coords.x, 1);
        EXPECT_EQ(tile_uv->tile_coords.y, 0);
        EXPECT_EQ(tile_uv->tile_size.x, 256);
        EXPECT_EQ(tile_uv->tile_size.y, 256);
        EXPECT_EQ((uint32_t)(tile_uv->uv.x * tile_uv->tile_size.x), 66);
        EXPECT_EQ((uint32_t)(tile_uv->uv.y * tile_uv->tile_size.y), 41);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7329128.878252053}, 0, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        EXPECT_EQ(tile_uv->tile_coords.lod, 0);
        EXPECT_EQ(tile_uv->tile_coords.x, 0);
        EXPECT_EQ(tile_uv->tile_coords.y, 0);
        EXPECT_EQ(tile_uv->tile_size.x, 256);
        EXPECT_EQ(tile_uv->tile_size.y, 256);
        EXPECT_EQ((uint32_t)(tile_uv->uv.x * tile_uv->tile_size.x), 161);
        EXPECT_EQ((uint32_t)(tile_uv->uv.y * tile_uv->tile_size.y), 148);
    }
}

TEST_F(ReprojectionTest, tile_uv_to_proj_pos_local_top_clipped)
{
    auto tiled_raster_geometry = planet::TiledRasterGeometry::from_geometry_and_tiling_scheme(
        local_geometry, local_top_clipped_ts);

    pl_Crs crs;
    EXPECT_TRUE(hrz::convert_crs(tiled_raster_geometry.projection, &crs));

    auto tiling_info = compute_image_tiling_info(tiled_raster_geometry, &crs);

    {
        auto tile_uv = proj_pos_to_tile_uv({1107857.3756304502, 7329128.878252053}, 3, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        auto proj_pos = tile_uv_to_proj_pos(tile_uv->tile_coords, tile_uv->uv, tiling_info);
        EXPECT_NEAR(proj_pos.x, 1107857.3756304502, eps);
        EXPECT_NEAR(proj_pos.y, 7329128.878252053, eps);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7004010.094375875}, 3, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        auto proj_pos = tile_uv_to_proj_pos(tile_uv->tile_coords, tile_uv->uv, tiling_info);
        EXPECT_NEAR(proj_pos.x, 1595218.0470061149, eps);
        EXPECT_NEAR(proj_pos.y, 7004010.094375875, eps);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7004010.094375875}, 2, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        auto proj_pos = tile_uv_to_proj_pos(tile_uv->tile_coords, tile_uv->uv, tiling_info);
        EXPECT_NEAR(proj_pos.x, 1595218.0470061149, eps);
        EXPECT_NEAR(proj_pos.y, 7004010.094375875, eps);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7004010.094375875}, 1, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        auto proj_pos = tile_uv_to_proj_pos(tile_uv->tile_coords, tile_uv->uv, tiling_info);
        EXPECT_NEAR(proj_pos.x, 1595218.0470061149, eps);
        EXPECT_NEAR(proj_pos.y, 7004010.094375875, eps);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7004010.094375875}, 0, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        auto proj_pos = tile_uv_to_proj_pos(tile_uv->tile_coords, tile_uv->uv, tiling_info);
        EXPECT_NEAR(proj_pos.x, 1595218.0470061149, eps);
        EXPECT_NEAR(proj_pos.y, 7004010.094375875, eps);
    }
}

TEST_F(ReprojectionTest, tile_uv_to_proj_pos_local_top_full_sized)
{
    auto tiled_raster_geometry = planet::TiledRasterGeometry::from_geometry_and_tiling_scheme(
        local_geometry, local_top_full_sized_ts);

    pl_Crs crs;
    EXPECT_TRUE(hrz::convert_crs(tiled_raster_geometry.projection, &crs));

    auto tiling_info = compute_image_tiling_info(tiled_raster_geometry, &crs);

    {
        auto tile_uv = proj_pos_to_tile_uv({1107857.3756304502, 7329128.878252053}, 3, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        auto proj_pos = tile_uv_to_proj_pos(tile_uv->tile_coords, tile_uv->uv, tiling_info);
        EXPECT_NEAR(proj_pos.x, 1107857.3756304502, eps);
        EXPECT_NEAR(proj_pos.y, 7329128.878252053, eps);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7004010.094375875}, 3, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        auto proj_pos = tile_uv_to_proj_pos(tile_uv->tile_coords, tile_uv->uv, tiling_info);
        EXPECT_NEAR(proj_pos.x, 1595218.0470061149, eps);
        EXPECT_NEAR(proj_pos.y, 7004010.094375875, eps);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7004010.094375875}, 2, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        auto proj_pos = tile_uv_to_proj_pos(tile_uv->tile_coords, tile_uv->uv, tiling_info);
        EXPECT_NEAR(proj_pos.x, 1595218.0470061149, eps);
        EXPECT_NEAR(proj_pos.y, 7004010.094375875, eps);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7004010.094375875}, 1, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        auto proj_pos = tile_uv_to_proj_pos(tile_uv->tile_coords, tile_uv->uv, tiling_info);
        EXPECT_NEAR(proj_pos.x, 1595218.0470061149, eps);
        EXPECT_NEAR(proj_pos.y, 7004010.094375875, eps);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7004010.094375875}, 0, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        auto proj_pos = tile_uv_to_proj_pos(tile_uv->tile_coords, tile_uv->uv, tiling_info);
        EXPECT_NEAR(proj_pos.x, 1595218.0470061149, eps);
        EXPECT_NEAR(proj_pos.y, 7004010.094375875, eps);
    }
}

TEST_F(ReprojectionTest, tile_uv_to_proj_pos_local_bottom_clipped)
{
    auto tiled_raster_geometry = planet::TiledRasterGeometry::from_geometry_and_tiling_scheme(
        local_geometry, local_bottom_clipped_ts);

    pl_Crs crs;
    EXPECT_TRUE(hrz::convert_crs(tiled_raster_geometry.projection, &crs));

    auto tiling_info = compute_image_tiling_info(tiled_raster_geometry, &crs);

    {
        auto tile_uv = proj_pos_to_tile_uv({1107857.3756304502, 7329128.878252053}, 3, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        auto proj_pos = tile_uv_to_proj_pos(tile_uv->tile_coords, tile_uv->uv, tiling_info);
        EXPECT_NEAR(proj_pos.x, 1107857.3756304502, eps);
        EXPECT_NEAR(proj_pos.y, 7329128.878252053, eps);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7004010.094375875}, 2, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        auto proj_pos = tile_uv_to_proj_pos(tile_uv->tile_coords, tile_uv->uv, tiling_info);
        EXPECT_NEAR(proj_pos.x, 1595218.0470061149, eps);
        EXPECT_NEAR(proj_pos.y, 7004010.094375875, eps);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7004010.094375875}, 1, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        auto proj_pos = tile_uv_to_proj_pos(tile_uv->tile_coords, tile_uv->uv, tiling_info);
        EXPECT_NEAR(proj_pos.x, 1595218.0470061149, eps);
        EXPECT_NEAR(proj_pos.y, 7004010.094375875, eps);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7004010.094375875}, 0, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        auto proj_pos = tile_uv_to_proj_pos(tile_uv->tile_coords, tile_uv->uv, tiling_info);
        EXPECT_NEAR(proj_pos.x, 1595218.0470061149, eps);
        EXPECT_NEAR(proj_pos.y, 7004010.094375875, eps);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7329128.878252053}, 3, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        auto proj_pos = tile_uv_to_proj_pos(tile_uv->tile_coords, tile_uv->uv, tiling_info);
        EXPECT_NEAR(proj_pos.x, 1595218.0470061149, eps);
        EXPECT_NEAR(proj_pos.y, 7329128.878252053, eps);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7329128.878252053}, 2, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        auto proj_pos = tile_uv_to_proj_pos(tile_uv->tile_coords, tile_uv->uv, tiling_info);
        EXPECT_NEAR(proj_pos.x, 1595218.0470061149, eps);
        EXPECT_NEAR(proj_pos.y, 7329128.878252053, eps);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7329128.878252053}, 1, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        auto proj_pos = tile_uv_to_proj_pos(tile_uv->tile_coords, tile_uv->uv, tiling_info);
        EXPECT_NEAR(proj_pos.x, 1595218.0470061149, eps);
        EXPECT_NEAR(proj_pos.y, 7329128.878252053, eps);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7329128.878252053}, 0, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        auto proj_pos = tile_uv_to_proj_pos(tile_uv->tile_coords, tile_uv->uv, tiling_info);
        EXPECT_NEAR(proj_pos.x, 1595218.0470061149, eps);
        EXPECT_NEAR(proj_pos.y, 7329128.878252053, eps);
    }
}

TEST_F(ReprojectionTest, tile_uv_to_proj_pos_local_bottom_full_sized)
{
    auto tiled_raster_geometry = planet::TiledRasterGeometry::from_geometry_and_tiling_scheme(
        local_geometry, local_bottom_full_sized_ts);

    pl_Crs crs;
    EXPECT_TRUE(hrz::convert_crs(tiled_raster_geometry.projection, &crs));

    auto tiling_info = compute_image_tiling_info(tiled_raster_geometry, &crs);

    {
        auto tile_uv = proj_pos_to_tile_uv({1107857.3756304502, 7329128.878252053}, 3, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        auto proj_pos = tile_uv_to_proj_pos(tile_uv->tile_coords, tile_uv->uv, tiling_info);
        EXPECT_NEAR(proj_pos.x, 1107857.3756304502, eps);
        EXPECT_NEAR(proj_pos.y, 7329128.878252053, eps);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7004010.094375875}, 2, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        auto proj_pos = tile_uv_to_proj_pos(tile_uv->tile_coords, tile_uv->uv, tiling_info);
        EXPECT_NEAR(proj_pos.x, 1595218.0470061149, eps);
        EXPECT_NEAR(proj_pos.y, 7004010.094375875, eps);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7004010.094375875}, 1, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        auto proj_pos = tile_uv_to_proj_pos(tile_uv->tile_coords, tile_uv->uv, tiling_info);
        EXPECT_NEAR(proj_pos.x, 1595218.0470061149, eps);
        EXPECT_NEAR(proj_pos.y, 7004010.094375875, eps);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7004010.094375875}, 0, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        auto proj_pos = tile_uv_to_proj_pos(tile_uv->tile_coords, tile_uv->uv, tiling_info);
        EXPECT_NEAR(proj_pos.x, 1595218.0470061149, eps);
        EXPECT_NEAR(proj_pos.y, 7004010.094375875, eps);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7329128.878252053}, 3, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        auto proj_pos = tile_uv_to_proj_pos(tile_uv->tile_coords, tile_uv->uv, tiling_info);
        EXPECT_NEAR(proj_pos.x, 1595218.0470061149, eps);
        EXPECT_NEAR(proj_pos.y, 7329128.878252053, eps);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7329128.878252053}, 2, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        auto proj_pos = tile_uv_to_proj_pos(tile_uv->tile_coords, tile_uv->uv, tiling_info);
        EXPECT_NEAR(proj_pos.x, 1595218.0470061149, eps);
        EXPECT_NEAR(proj_pos.y, 7329128.878252053, eps);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7329128.878252053}, 1, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        auto proj_pos = tile_uv_to_proj_pos(tile_uv->tile_coords, tile_uv->uv, tiling_info);
        EXPECT_NEAR(proj_pos.x, 1595218.0470061149, eps);
        EXPECT_NEAR(proj_pos.y, 7329128.878252053, eps);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7329128.878252053}, 0, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        auto proj_pos = tile_uv_to_proj_pos(tile_uv->tile_coords, tile_uv->uv, tiling_info);
        EXPECT_NEAR(proj_pos.x, 1595218.0470061149, eps);
        EXPECT_NEAR(proj_pos.y, 7329128.878252053, eps);
    }
}

TEST_F(ReprojectionTest, proj_pos_to_tile_uv_global_clipped)
{
    auto tiled_raster_geometry = planet::TiledRasterGeometry::from_geometry_and_tiling_scheme(
        global_geometry, global_clipped_ts);

    pl_Crs crs;
    EXPECT_TRUE(hrz::convert_crs(tiled_raster_geometry.projection, &crs));

    auto tiling_info = compute_image_tiling_info(tiled_raster_geometry, &crs);

    {
        auto tile_uv = proj_pos_to_tile_uv({1000000, 7329128}, 3, tiling_info);
        EXPECT_FALSE(tile_uv.has_value());
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1107857.3756304502, 7329128.878252053}, 3, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        EXPECT_EQ(tile_uv->tile_coords.lod, 3);
        EXPECT_EQ(tile_uv->tile_coords.x, 2);
        EXPECT_EQ(tile_uv->tile_coords.y, 3);
        EXPECT_EQ(tile_uv->tile_size.x, 156);
        EXPECT_EQ(tile_uv->tile_size.y, 181);
        EXPECT_EQ((uint32_t)(tile_uv->uv.x * tile_uv->tile_size.x), 20);
        EXPECT_EQ((uint32_t)(tile_uv->uv.y * tile_uv->tile_size.y), 10);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7004010.094375875}, 3, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        EXPECT_EQ(tile_uv->tile_coords.lod, 3);
        EXPECT_EQ(tile_uv->tile_coords.x, 7);
        EXPECT_EQ(tile_uv->tile_coords.y, 6);
        EXPECT_EQ(tile_uv->tile_size.x, 121);
        EXPECT_EQ(tile_uv->tile_size.y, 174);
        EXPECT_EQ((uint32_t)(tile_uv->uv.x * tile_uv->tile_size.x), 108);
        EXPECT_EQ((uint32_t)(tile_uv->uv.y * tile_uv->tile_size.y), 163);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7004010.094375875}, 2, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        EXPECT_EQ(tile_uv->tile_coords.lod, 2);
        EXPECT_EQ(tile_uv->tile_coords.x, 3);
        EXPECT_EQ(tile_uv->tile_coords.y, 3);
        EXPECT_EQ(tile_uv->tile_size.x, 188);
        EXPECT_EQ(tile_uv->tile_size.y, 87);
        EXPECT_EQ((uint32_t)(tile_uv->uv.x * tile_uv->tile_size.x), 181);
        EXPECT_EQ((uint32_t)(tile_uv->uv.y * tile_uv->tile_size.y), 81);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7004010.094375875}, 1, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        EXPECT_EQ(tile_uv->tile_coords.lod, 1);
        EXPECT_EQ(tile_uv->tile_coords.x, 1);
        EXPECT_EQ(tile_uv->tile_coords.y, 1);
        EXPECT_EQ(tile_uv->tile_size.x, 222);
        EXPECT_EQ(tile_uv->tile_size.y, 171);
        EXPECT_EQ((uint32_t)(tile_uv->uv.x * tile_uv->tile_size.x), 218);
        EXPECT_EQ((uint32_t)(tile_uv->uv.y * tile_uv->tile_size.y), 168);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7004010.094375875}, 0, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        EXPECT_EQ(tile_uv->tile_coords.lod, 0);
        EXPECT_EQ(tile_uv->tile_coords.x, 0);
        EXPECT_EQ(tile_uv->tile_coords.y, 0);
        EXPECT_EQ(tile_uv->tile_size.x, 162);
        EXPECT_EQ(tile_uv->tile_size.y, 108);
        EXPECT_EQ((uint32_t)(tile_uv->uv.x * tile_uv->tile_size.x), 160);
        EXPECT_EQ((uint32_t)(tile_uv->uv.y * tile_uv->tile_size.y), 106);
    }
}

TEST_F(ReprojectionTest, proj_pos_to_tile_uv_global_full_sized)
{
    auto tiled_raster_geometry = planet::TiledRasterGeometry::from_geometry_and_tiling_scheme(
        global_geometry, global_full_sized_ts);

    pl_Crs crs;
    EXPECT_TRUE(hrz::convert_crs(tiled_raster_geometry.projection, &crs));

    auto tiling_info = compute_image_tiling_info(tiled_raster_geometry, &crs);

    {
        auto tile_uv = proj_pos_to_tile_uv({1000000, 7329128}, 3, tiling_info);
        EXPECT_FALSE(tile_uv.has_value());
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1107857.3756304502, 7329128.878252053}, 3, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        EXPECT_EQ(tile_uv->tile_coords.lod, 3);
        EXPECT_EQ(tile_uv->tile_coords.x, 2);
        EXPECT_EQ(tile_uv->tile_coords.y, 3);
        EXPECT_EQ(tile_uv->tile_size.x, 256);
        EXPECT_EQ(tile_uv->tile_size.y, 256);
        EXPECT_EQ((uint32_t)(tile_uv->uv.x * tile_uv->tile_size.x), 120);
        EXPECT_EQ((uint32_t)(tile_uv->uv.y * tile_uv->tile_size.y), 85);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7004010.094375875}, 3, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        EXPECT_EQ(tile_uv->tile_coords.lod, 3);
        EXPECT_EQ(tile_uv->tile_coords.x, 7);
        EXPECT_EQ(tile_uv->tile_coords.y, 6);
        EXPECT_EQ(tile_uv->tile_size.x, 256);
        EXPECT_EQ(tile_uv->tile_size.y, 256);
        EXPECT_EQ((uint32_t)(tile_uv->uv.x * tile_uv->tile_size.x), 108);
        EXPECT_EQ((uint32_t)(tile_uv->uv.y * tile_uv->tile_size.y), 163);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7004010.094375875}, 2, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        EXPECT_EQ(tile_uv->tile_coords.lod, 2);
        EXPECT_EQ(tile_uv->tile_coords.x, 3);
        EXPECT_EQ(tile_uv->tile_coords.y, 3);
        EXPECT_EQ(tile_uv->tile_size.x, 256);
        EXPECT_EQ(tile_uv->tile_size.y, 256);
        EXPECT_EQ((uint32_t)(tile_uv->uv.x * tile_uv->tile_size.x), 182);
        EXPECT_EQ((uint32_t)(tile_uv->uv.y * tile_uv->tile_size.y), 81);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7004010.094375875}, 1, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        EXPECT_EQ(tile_uv->tile_coords.lod, 1);
        EXPECT_EQ(tile_uv->tile_coords.x, 1);
        EXPECT_EQ(tile_uv->tile_coords.y, 1);
        EXPECT_EQ(tile_uv->tile_size.x, 256);
        EXPECT_EQ(tile_uv->tile_size.y, 256);
        EXPECT_EQ((uint32_t)(tile_uv->uv.x * tile_uv->tile_size.x), 219);
        EXPECT_EQ((uint32_t)(tile_uv->uv.y * tile_uv->tile_size.y), 168);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7004010.094375875}, 0, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        EXPECT_EQ(tile_uv->tile_coords.lod, 0);
        EXPECT_EQ(tile_uv->tile_coords.x, 0);
        EXPECT_EQ(tile_uv->tile_coords.y, 0);
        EXPECT_EQ(tile_uv->tile_size.x, 256);
        EXPECT_EQ(tile_uv->tile_size.y, 256);
        EXPECT_EQ((uint32_t)(tile_uv->uv.x * tile_uv->tile_size.x), 237);
        EXPECT_EQ((uint32_t)(tile_uv->uv.y * tile_uv->tile_size.y), 212);
    }
}

TEST_F(ReprojectionTest, tile_uv_to_proj_pos_global_clipped)
{
    auto tiled_raster_geometry = planet::TiledRasterGeometry::from_geometry_and_tiling_scheme(
        global_geometry, global_clipped_ts);

    pl_Crs crs;
    EXPECT_TRUE(hrz::convert_crs(tiled_raster_geometry.projection, &crs));

    auto tiling_info = compute_image_tiling_info(tiled_raster_geometry, &crs);

    {
        auto tile_uv = proj_pos_to_tile_uv({1107857.3756304502, 7329128.878252053}, 3, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        auto proj_pos = tile_uv_to_proj_pos(tile_uv->tile_coords, tile_uv->uv, tiling_info);
        EXPECT_NEAR(proj_pos.x, 1107857.3756304502, eps);
        EXPECT_NEAR(proj_pos.y, 7329128.878252053, eps);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7004010.094375875}, 3, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        auto proj_pos = tile_uv_to_proj_pos(tile_uv->tile_coords, tile_uv->uv, tiling_info);
        EXPECT_NEAR(proj_pos.x, 1595218.0470061149, eps);
        EXPECT_NEAR(proj_pos.y, 7004010.094375875, eps);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7004010.094375875}, 2, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        auto proj_pos = tile_uv_to_proj_pos(tile_uv->tile_coords, tile_uv->uv, tiling_info);
        EXPECT_NEAR(proj_pos.x, 1595218.0470061149, eps);
        EXPECT_NEAR(proj_pos.y, 7004010.094375875, eps);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7004010.094375875}, 1, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        auto proj_pos = tile_uv_to_proj_pos(tile_uv->tile_coords, tile_uv->uv, tiling_info);
        EXPECT_NEAR(proj_pos.x, 1595218.0470061149, eps);
        EXPECT_NEAR(proj_pos.y, 7004010.094375875, eps);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7004010.094375875}, 0, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        auto proj_pos = tile_uv_to_proj_pos(tile_uv->tile_coords, tile_uv->uv, tiling_info);
        EXPECT_NEAR(proj_pos.x, 1595218.0470061149, eps);
        EXPECT_NEAR(proj_pos.y, 7004010.094375875, eps);
    }
}

TEST_F(ReprojectionTest, tile_uv_to_proj_pos_global_full_sized)
{
    auto tiled_raster_geometry = planet::TiledRasterGeometry::from_geometry_and_tiling_scheme(
        global_geometry, global_full_sized_ts);

    pl_Crs crs;
    EXPECT_TRUE(hrz::convert_crs(tiled_raster_geometry.projection, &crs));

    auto tiling_info = compute_image_tiling_info(tiled_raster_geometry, &crs);

    {
        auto tile_uv = proj_pos_to_tile_uv({1107857.3756304502, 7329128.878252053}, 3, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        auto proj_pos = tile_uv_to_proj_pos(tile_uv->tile_coords, tile_uv->uv, tiling_info);
        EXPECT_NEAR(proj_pos.x, 1107857.3756304502, eps);
        EXPECT_NEAR(proj_pos.y, 7329128.878252053, eps);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7004010.094375875}, 3, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        auto proj_pos = tile_uv_to_proj_pos(tile_uv->tile_coords, tile_uv->uv, tiling_info);
        EXPECT_NEAR(proj_pos.x, 1595218.0470061149, eps);
        EXPECT_NEAR(proj_pos.y, 7004010.094375875, eps);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7004010.094375875}, 2, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        auto proj_pos = tile_uv_to_proj_pos(tile_uv->tile_coords, tile_uv->uv, tiling_info);
        EXPECT_NEAR(proj_pos.x, 1595218.0470061149, eps);
        EXPECT_NEAR(proj_pos.y, 7004010.094375875, eps);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7004010.094375875}, 1, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        auto proj_pos = tile_uv_to_proj_pos(tile_uv->tile_coords, tile_uv->uv, tiling_info);
        EXPECT_NEAR(proj_pos.x, 1595218.0470061149, eps);
        EXPECT_NEAR(proj_pos.y, 7004010.094375875, eps);
    }

    {
        auto tile_uv = proj_pos_to_tile_uv({1595218.0470061149, 7004010.094375875}, 0, tiling_info);
        EXPECT_TRUE(tile_uv.has_value());
        auto proj_pos = tile_uv_to_proj_pos(tile_uv->tile_coords, tile_uv->uv, tiling_info);
        EXPECT_NEAR(proj_pos.x, 1595218.0470061149, eps);
        EXPECT_NEAR(proj_pos.y, 7004010.094375875, eps);
    }
}
