// SPDX-FileCopyrightText: Copyright 2019 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/core/scene_model.h"

#include "hrz/core/scene_model_accessor.h"
#include "hrz/protocol/color/color.pb.h"
#include "hrz/protocol/path_builder/layer/imagery_raster_layer.h"
#include "hrz/protocol/path_builder/layer/single_model_layer.h"
#include "hrz/protocol/raster/nodata.pb.h"
#include "hrz/protocol/raster/provider.pb.h"
#include "hrz/protocol/visibility_constraints.pb.h"

#include <gtest/gtest.h>

namespace
{

using namespace hrz;

void build_test_proto(hrz_proto::SingleModelLayer& layer)
{
    layer.set_url("http://google.com");
    layer.mutable_transform()->mutable_offset()->set_x(5);

    hrz_proto::Color red;
    red.set_r(1);
    red.set_g(0);
    red.set_b(0);

    hrz_proto::Color green;
    green.set_r(0);
    green.set_g(1);
    green.set_b(0);

    auto mat1 = layer.add_materials();
    mat1->set_name("Mat 1");

    auto mat2 = layer.add_materials();
    mat2->set_name("Mat 2");
}

TEST(SceneModelAccessor, get)
{
    hrz_proto::SingleModelLayer layer;
    build_test_proto(layer);

    uint32_t data_source_path[] = {1};

    hrz_proto::StringValue url;
    ASSERT_TRUE(scene_model::get_message_part(layer, data_source_path, url));
    EXPECT_EQ(url.value(), "http://google.com");

    hrz_proto::NumericPalette palette;
    ASSERT_FALSE(scene_model::get_message_part(layer, data_source_path, palette));

    uint32_t transform_pos_path[] = {2, 1};
    hrz_proto::Vec3f pos;
    ASSERT_TRUE(scene_model::get_message_part(layer, transform_pos_path, pos));
    EXPECT_EQ(pos.x(), 5);

    uint32_t mat_name_path[] = {14, 0, 4};
    hrz_proto::StringValue mat_name;
    ASSERT_TRUE(scene_model::get_message_part(layer, mat_name_path, mat_name));
    EXPECT_EQ(mat_name.value(), "Mat 1");

    hrz_proto::SingleModelLayer layer2;
    ASSERT_TRUE(scene_model::get_message_part(layer, std::span<const uint32_t>{}, layer2));
}

TEST(SceneModelAccessor, set)
{
    hrz_proto::SingleModelLayer layer;

    layer.add_materials()->set_name("Mat 1");
    layer.add_materials()->set_name("Mat 2");

    uint32_t data_source_path[] = {1};

    hrz_proto::StringValue url;
    url.set_value("http://google.com");
    scene_model::set_message_part(layer, data_source_path, url);

    uint32_t transform_pos_path[] = {2, 1};
    hrz_proto::Vec3f pos;
    pos.set_x(5);
    scene_model::set_message_part(layer, transform_pos_path, pos);

    uint32_t mat_name_path[] = {14, 1, 4};
    hrz_proto::StringValue new_name;
    new_name.set_value("New mat");
    scene_model::set_message_part(layer, mat_name_path, new_name);

    EXPECT_EQ(layer.url(), "http://google.com");
    EXPECT_EQ(layer.transform().offset().x(), 5);
    EXPECT_EQ(layer.materials(1).name(), "New mat");
}

TEST(SceneModelAccessor, count)
{
    hrz_proto::SingleModelLayer layer;
    build_test_proto(layer);

    uint32_t data_source_path[] = {1};

    EXPECT_EQ(0, scene_model::count_message_part(layer, data_source_path));

    uint32_t transform_pos_path[] = {2, 1};
    EXPECT_EQ(0, scene_model::count_message_part(layer, transform_pos_path));

    uint32_t mat1_name_path[] = {14, 0, 4};
    uint32_t mats_path[] = {14};
    EXPECT_EQ(0, scene_model::count_message_part(layer, mat1_name_path));
    EXPECT_EQ(2, scene_model::count_message_part(layer, mats_path));
}

TEST(SceneModelAccessor, add)
{
    hrz_proto::SingleModelLayer layer;

    uint32_t mats_path[] = {14};

    hrz_proto::Material mat1;
    mat1.set_name("Mat 1");

    hrz_proto::Material mat2;
    mat2.set_name("Mat 2");

    ASSERT_EQ(1, scene_model::add_message_part(layer, mats_path, mat1));
    ASSERT_EQ(2, scene_model::add_message_part(layer, mats_path, mat2));
    EXPECT_EQ(layer.materials(0).name(), "Mat 1");
    EXPECT_EQ(layer.materials(1).name(), "Mat 2");
}

TEST(SceneModelAccessor, remove)
{
    hrz_proto::SingleModelLayer layer;
    build_test_proto(layer);

    ASSERT_EQ(layer.materials_size(), 2);

    uint32_t mat1_path[] = {14, 0};
    ASSERT_EQ(1, scene_model::remove_message_part(layer, mat1_path));
    ASSERT_EQ(1, layer.materials_size());
    ASSERT_EQ("Mat 2", layer.materials(0).name());

    ASSERT_EQ(0, scene_model::remove_message_part(layer, mat1_path));
    ASSERT_EQ(0, layer.materials_size());

    ASSERT_EQ(0, scene_model::remove_message_part(layer, mat1_path));
    ASSERT_EQ(0, layer.materials_size());
}

hrz_proto::LayerHandle make_layer_handle(uint64_t id)
{
    hrz_proto::LayerHandle handle;
    handle.set_opaque(id);
    return handle;
}

TEST(SceneModel, register_get_set)
{
    SceneModel* model = scene_model::create();
    SceneModelAccessor accessor(model);

    hrz_proto::SingleModelLayerPathBuilder<SceneModelAccessor> builder0(
        accessor, make_layer_handle(0));

    builder0.clone().url().set("hello");
    ASSERT_EQ("", builder0.clone().url().get());

    hrz_proto::PathRoot root;
    *root.mutable_single_model_layer() = make_layer_handle(0);
    scene_model::register_element(model, root);

    builder0.clone().url().set("hello");
    ASSERT_EQ("hello", builder0.clone().url().get());

    scene_model::unregister_element(model, root);
    ASSERT_EQ("", builder0.clone().url().get());

    scene_model::destroy(model);
}

TEST(SceneModel, count_add_remove)
{
    SceneModel* model = scene_model::create();
    SceneModelAccessor accessor(model);

    hrz_proto::SingleModelLayerPathBuilder<SceneModelAccessor> builder0(
        accessor, make_layer_handle(0));

    hrz_proto::PathRoot root;
    *root.mutable_single_model_layer() = make_layer_handle(0);
    scene_model::register_element(model, root);

    hrz_proto::Material mat1;
    mat1.set_name("Mat 1");

    hrz_proto::Material mat2;
    mat2.set_name("Mat 2");

    ASSERT_EQ(0, builder0.clone().materials_count());
    ASSERT_EQ(1, builder0.clone().add_materials(mat1));
    ASSERT_EQ(2, builder0.clone().add_materials(mat2));
    ASSERT_EQ(1, builder0.clone().remove_materials(1));
    EXPECT_EQ(builder0.clone().materials(0).name().get(), "Mat 1");
    ASSERT_EQ(0, builder0.clone().remove_materials(0));

    scene_model::destroy(model);
}

TEST(SceneModel, oneof)
{
    SceneModel* model = scene_model::create();
    SceneModelAccessor accessor(model);

    hrz_proto::ImageryRasterLayerPathBuilder<SceneModelAccessor> builder(
        accessor, make_layer_handle(0));

    hrz_proto::PathRoot root;
    *root.mutable_imagery_raster_layer() = make_layer_handle(0);
    scene_model::register_element(model, root);

    EXPECT_EQ(0, builder.clone().raster().provider().get_provider_type_case());

    {
        hrz_proto::RasterProvider provider;
        provider.mutable_tiled();
        builder.clone().raster().provider().set(provider);
        EXPECT_EQ(
            hrz_proto::RasterProvider::kTiled,
            builder.clone().raster().provider().get_provider_type_case());

        provider.mutable_bing();
        builder.clone().raster().provider().set(provider);
        EXPECT_EQ(
            hrz_proto::RasterProvider::kBing,
            builder.clone().raster().provider().get_provider_type_case());
    }

    {
        const hrz_proto::UntiledRasterProviderParams params;
        builder.clone().raster().provider().untiled().set(params);
        EXPECT_EQ(
            hrz_proto::RasterProvider::kUntiled,
            builder.clone().raster().provider().get_provider_type_case());
    }

    {
        hrz_proto::RasterProvider provider;
        provider.mutable_palettized()->mutable_provider()->mutable_bing();
        builder.clone().raster().provider().set(provider);
        EXPECT_EQ(
            hrz_proto::RasterProvider::kPalettized,
            builder.clone().raster().provider().get_provider_type_case());
        EXPECT_EQ(
            hrz_proto::RasterProvider::kBing,
            builder.clone().raster().provider().palettized().provider().get_provider_type_case());
    }

    // Reading a field in an inactive oneof should return the default value
    // and not change the oneof case.
    {
        builder.clone().raster().provider().tiled().get();
        EXPECT_EQ(
            hrz_proto::RasterProvider::kPalettized,
            builder.clone().raster().provider().get_provider_type_case());

        builder.clone().raster().provider().tiled().geometry().get();
        EXPECT_EQ(
            hrz_proto::RasterProvider::kPalettized,
            builder.clone().raster().provider().get_provider_type_case());

        EXPECT_EQ("", builder.clone().raster().provider().tiled().url_pattern().get());
        EXPECT_EQ(
            hrz_proto::RasterProvider::kPalettized,
            builder.clone().raster().provider().get_provider_type_case());

        EXPECT_EQ(0, builder.clone().raster().provider().tiled().http_headers().headers_count());
        EXPECT_EQ(
            hrz_proto::RasterProvider::kPalettized,
            builder.clone().raster().provider().get_provider_type_case());
    }

    // Writing a field in an inactive oneof should activate the oneof and set the field.
    {
        builder.clone().raster().provider().tiled().url_pattern().set("hello");
        EXPECT_EQ(
            hrz_proto::RasterProvider::kTiled,
            builder.clone().raster().provider().get_provider_type_case());

        EXPECT_EQ(
            1,
            builder.clone()
                .raster()
                .provider()
                .palettized()
                .provider()
                .arcgis()
                .http_headers()
                .add_headers({}));
        EXPECT_EQ(
            hrz_proto::RasterProvider::kPalettized,
            builder.clone().raster().provider().get_provider_type_case());
        EXPECT_EQ(
            hrz_proto::RasterProvider::kArcgis,
            builder.clone().raster().provider().palettized().provider().get_provider_type_case());
    }

    scene_model::destroy(model);
}

TEST(SceneModel, oneof_primitive)
{
    SceneModel* model = scene_model::create();
    SceneModelAccessor accessor(model);

    hrz_proto::ImageryRasterLayerPathBuilder<SceneModelAccessor> builder(
        accessor, make_layer_handle(0));

    hrz_proto::PathRoot root;
    *root.mutable_imagery_raster_layer() = make_layer_handle(0);
    scene_model::register_element(model, root);

    hrz_proto::TiledRasterProviderParams params;
    params.mutable_nodata()->mutable_value()->set_int_value(45);
    builder.clone().raster().provider().tiled().set(params);

    EXPECT_EQ(45, builder.clone().raster().provider().tiled().nodata().value().int_value().get());
    EXPECT_EQ(
        hrz_proto::NodataValue::kIntValue,
        builder.clone().raster().provider().tiled().nodata().value().get_value_type_case());

    hrz_proto::Color color;
    builder.clone().raster().provider().tiled().nodata().value().color().set(color);

    EXPECT_EQ(
        hrz_proto::NodataValue::kColor,
        builder.clone().raster().provider().tiled().nodata().value().get_value_type_case());

    hrz_proto::Void void_value;
    builder.clone().raster().provider().tiled().nodata().value().float_nan().set(void_value);

    EXPECT_EQ(
        hrz_proto::NodataValue::kFloatNan,
        builder.clone().raster().provider().tiled().nodata().value().get_value_type_case());

    builder.clone().raster().provider().tiled().nodata().value().float_value().set(2.5F);
    EXPECT_EQ(
        hrz_proto::NodataValue::kFloatValue,
        builder.clone().raster().provider().tiled().nodata().value().get_value_type_case());
    EXPECT_FLOAT_EQ(
        2.5F, builder.clone().raster().provider().tiled().nodata().value().float_value().get());
}

// LayerVisibilityConstraint is also a leaf type. So this tests that as well.
TEST(SceneModel, oneof_behind_repeated)
{
    SceneModel* model = scene_model::create();
    SceneModelAccessor accessor(model);

    hrz_proto::SingleModelLayerPathBuilder<SceneModelAccessor> builder(
        accessor, make_layer_handle(0));

    hrz_proto::PathRoot root;
    *root.mutable_single_model_layer() = make_layer_handle(0);
    scene_model::register_element(model, root);

    hrz_proto::LayerVisibilityConstraintList constraints;
    constraints.add_constraints()->mutable_altitude();
    constraints.add_constraints()->mutable_bounds();
    builder.clone().visibility_constraints().set(constraints);

    EXPECT_EQ(2, builder.clone().visibility_constraints().constraints_count());
    EXPECT_EQ(
        hrz_proto::LayerVisibilityConstraint::kAltitude,
        builder.clone().visibility_constraints().constraints(0).get_constraint_type_case());
    EXPECT_EQ(
        hrz_proto::LayerVisibilityConstraint::kBounds,
        builder.clone().visibility_constraints().constraints(1).get_constraint_type_case());

    hrz_proto::LayerVisibilityConstraint new_constraint;
    new_constraint.mutable_altitude();
    builder.clone().visibility_constraints().add_constraints(new_constraint);
    EXPECT_EQ(3, builder.clone().visibility_constraints().constraints_count());
    EXPECT_EQ(
        hrz_proto::LayerVisibilityConstraint::kAltitude,
        builder.clone().visibility_constraints().constraints(0).get_constraint_type_case());
    EXPECT_EQ(
        hrz_proto::LayerVisibilityConstraint::kBounds,
        builder.clone().visibility_constraints().constraints(1).get_constraint_type_case());
    EXPECT_EQ(
        hrz_proto::LayerVisibilityConstraint::kAltitude,
        builder.clone().visibility_constraints().constraints(2).get_constraint_type_case());

    builder.clone().visibility_constraints().constraints(1).set(new_constraint);
    EXPECT_EQ(3, builder.clone().visibility_constraints().constraints_count());
    EXPECT_EQ(
        hrz_proto::LayerVisibilityConstraint::kAltitude,
        builder.clone().visibility_constraints().constraints(0).get_constraint_type_case());
    EXPECT_EQ(
        hrz_proto::LayerVisibilityConstraint::kAltitude,
        builder.clone().visibility_constraints().constraints(1).get_constraint_type_case());
    EXPECT_EQ(
        hrz_proto::LayerVisibilityConstraint::kAltitude,
        builder.clone().visibility_constraints().constraints(2).get_constraint_type_case());

    new_constraint.mutable_bounds();
    builder.clone().visibility_constraints().constraints(2).set(new_constraint);
    builder.clone().visibility_constraints().remove_constraints(1);
    EXPECT_EQ(2, builder.clone().visibility_constraints().constraints_count());
    EXPECT_EQ(
        hrz_proto::LayerVisibilityConstraint::kAltitude,
        builder.clone().visibility_constraints().constraints(0).get_constraint_type_case());
    EXPECT_EQ(
        hrz_proto::LayerVisibilityConstraint::kBounds,
        builder.clone().visibility_constraints().constraints(1).get_constraint_type_case());
}

// @Todo Not tested for now because those cases don't appear in the scene model:
//   - oneof with enum type

} // namespace
