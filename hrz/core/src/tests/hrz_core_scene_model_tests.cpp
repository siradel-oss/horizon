#include "hrz_core_scene_model.h"
#include "hrz_core_scene_model_accessor.h"
#include "hrz_core_scene_path.h"

#include <hrz_fnd_defines.h>
#include <hrz_protocol_path_builder.h>

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
    ASSERT_TRUE(
        scene_model::get_message_part(layer, gsl::span<const uint32_t>(nullptr, 0), layer2));
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

} // namespace
