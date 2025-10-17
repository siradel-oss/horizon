#include <hrz_common_color.h>
#include <hrz_fnd_string_utils.h>
#include <hrz_mapbox.h>
#include <hrz_mapbox_common.h>
#include <hrz_mapbox_tests_resources.h>
#include <hrz_protocol_all.h>

#include <gtest/gtest.h>
#include <lin_maths.h>

#include <string_view>

#define EXPECT_COLORS_EQ(a, b)          \
    do                                  \
    {                                   \
        EXPECT_DOUBLE_EQ(a.r(), b.r()); \
        EXPECT_DOUBLE_EQ(a.g(), b.g()); \
        EXPECT_DOUBLE_EQ(a.b(), b.b()); \
        EXPECT_DOUBLE_EQ(a.a(), b.a()); \
    } while (0)

#define HRZ_LAYERS                             \
    HRZ_LAYER_WRAPPER(single_model)            \
    HRZ_LAYER_WRAPPER(dtm_raster)              \
    HRZ_LAYER_WRAPPER(imagery_raster)          \
    HRZ_LAYER_WRAPPER(vector_data)             \
    HRZ_LAYER_WRAPPER(vector_tiles)            \
    HRZ_LAYER_WRAPPER(three_d_tiles)           \
    HRZ_LAYER_WRAPPER(clipping_plane)          \
    HRZ_LAYER_WRAPPER(in_memory_vector_source) \
    HRZ_LAYER_WRAPPER(gizmo)                   \
    HRZ_LAYER_WRAPPER(editable_shape)

struct SceneLayerStats
{
#define HRZ_LAYER_WRAPPER(TYPE) int TYPE##_count = 0;
    HRZ_LAYERS
#undef HRZ_LAYER_WRAPPER
};

bool try_convert_mapbox_resource(
    mapbox::Resources res,
    hrz_proto::SceneDump* scene_dump,
    std::optional<mapbox::Resources> sprite_index_res = std::nullopt)
{
    std::span<const std::byte> raw_json = mapbox::get_data(res);
    auto json = std::string_view((char*)raw_json.data(), raw_json.size());

    std::string_view sprite_index_json{};
    if (sprite_index_res.has_value())
    {
        std::span<const std::byte> raw_sprite_index = mapbox::get_data(sprite_index_res.value());
        sprite_index_json =
            std::string_view((char*)raw_sprite_index.data(), raw_sprite_index.size());
    }

    hrz_mapbox::TranslationSettings settings;
    settings.camera_fovy = 70.0;
    settings.viewport_size = {1280.0, 720.0};
    settings.raster_group = hrz_proto::RasterGroup::MIDDLE_RASTER_GROUP;
    settings.first_raster_slot = 1;
    settings.first_vector_data_layer_id = 1;
    settings.first_flat_overlay_z_index = 1;
    settings.first_symbol_z_index = 1;

    return hrz_mapbox::translate_scene(json, sprite_index_json, settings, scene_dump).success;
}

SceneLayerStats get_scene_dump_layer_stats(const hrz_proto::SceneDump* scene_dump)
{
    SceneLayerStats stats;

    for (const auto& layer : scene_dump->layers())
    {
        // Are we having fun or what??
#define HRZ_LAYER_WRAPPER(TYPE) \
    if (layer.has_##TYPE())     \
    {                           \
        stats.TYPE##_count++;   \
        continue;               \
    };

        HRZ_LAYERS
#undef HRZ_LAYER_WRAPPER
    }

    return stats;
}

void check_expected_layer_stats(const SceneLayerStats& input, const SceneLayerStats& expected)
{
#define HRZ_LAYER_WRAPPER(TYPE) EXPECT_EQ(input.TYPE##_count, expected.TYPE##_count);
    HRZ_LAYERS
#undef HRZ_LAYER_WRAPPER
}

void convert_mapbox_resource(
    mapbox::Resources res,
    hrz_proto::SceneDump* scene_dump,
    const SceneLayerStats& expected,
    std::optional<mapbox::Resources> sprite_index_res = std::nullopt)
{
    bool success = try_convert_mapbox_resource(res, scene_dump, sprite_index_res);
    EXPECT_EQ(success, true);

    if (success)
    {
        SceneLayerStats stats = get_scene_dump_layer_stats(scene_dump);
        check_expected_layer_stats(stats, expected);
    }
}

TEST(MapboxTranslation, parse_minimal)
{
    {
        const char* json = "{}";

        hrz_proto::SceneDump scene_dump;
        bool success = hrz_mapbox::translate_scene(json, {}, {}, &scene_dump).success;

        EXPECT_EQ(success, false);
    }

    {
        const char* json = "{\"version\": 8}";

        hrz_proto::SceneDump scene_dump;
        bool success = hrz_mapbox::translate_scene(json, {}, {}, &scene_dump).success;

        EXPECT_EQ(success, true);
    }
}

TEST(MapboxTranslation, parse_name)
{
    const char* json = "{\"version\": 8, \"name\": \"test\"}";

    hrz_proto::SceneDump scene_dump;
    bool success = hrz_mapbox::translate_scene(json, {}, {}, &scene_dump).success;

    ASSERT_EQ(success, true);
    EXPECT_STREQ(scene_dump.name().c_str(), "test");
}

TEST(MapboxTranslation, parse_camera_viewpoint)
{
    hrz_proto::SceneDump scene_dump;
    bool success =
        try_convert_mapbox_resource(mapbox::Resources::Camera_40_30_20_10_3, &scene_dump);

    ASSERT_EQ(success, true);

    EXPECT_GE(scene_dump.cameras_size(), 1);

    EXPECT_DOUBLE_EQ(scene_dump.cameras(0).viewpoint().target().longitude(), 40.0);
    EXPECT_DOUBLE_EQ(scene_dump.cameras(0).viewpoint().target().latitude(), 30.0);
    EXPECT_DOUBLE_EQ(scene_dump.cameras(0).viewpoint().target().altitude(), 0.0);

    EXPECT_FLOAT_EQ(scene_dump.cameras(0).viewpoint().tilt(), lm::radians(20.0));
    EXPECT_FLOAT_EQ(scene_dump.cameras(0).viewpoint().bearing(), lm::radians(10.0));

    // Note: the distance of the viewpoint is untested because it doesn"t exactly match Mapbox's
    // behaviour.
}

struct RasterSourceConfig
{
    // https://docs.mapbox.com/mapbox-gl-js/style-spec/sources/#raster-bounds
    hrz_proto::GeographicBounds bounds;
    // https://docs.mapbox.com/mapbox-gl-js/style-spec/sources/#raster-maxzoom
    int maxzoom = 22;
    // https://docs.mapbox.com/mapbox-gl-js/style-spec/sources/#raster-minzoom
    int minzoom = 0;
    // https://docs.mapbox.com/mapbox-gl-js/style-spec/sources/#raster-tiles
    // Note: we only support for one tile URL pattern per layer.
    const char* tiles = "";
    // https://docs.mapbox.com/mapbox-gl-js/style-spec/sources/#raster-tileSize
    int tile_size = 512;

    RasterSourceConfig()
    {
        bounds.set_west(-180.0);
        bounds.set_south(-85.051129);
        bounds.set_east(180.0);
        bounds.set_north(85.051129);
    }

    void test_output(const hrz_proto::LayerDump& layer)
    {
        EXPECT_FLOAT_EQ(bounds.west(), layer.imagery_raster().raster().display_bounds().west());
        EXPECT_FLOAT_EQ(bounds.south(), layer.imagery_raster().raster().display_bounds().south());
        EXPECT_FLOAT_EQ(bounds.east(), layer.imagery_raster().raster().display_bounds().east());
        EXPECT_FLOAT_EQ(bounds.north(), layer.imagery_raster().raster().display_bounds().north());

        EXPECT_EQ(
            hrz_proto::TILED_RASTER_PROVIDER, layer.imagery_raster().raster().provider().type());
        EXPECT_STREQ(
            tiles, layer.imagery_raster().raster().provider().tiled().url_pattern().c_str());

        // There is no way of enforcing a local or global tiling scheme in Mapbox (?)
        EXPECT_EQ(
            hrz_proto::GLOBAL,
            layer.imagery_raster().raster().provider().tiled().tiling_scheme().type());

        EXPECT_EQ(
            maxzoom,
            layer.imagery_raster()
                .raster()
                .provider()
                .tiled()
                .tiling_scheme()
                .global_tiling()
                .max_level());
        EXPECT_EQ(
            minzoom,
            layer.imagery_raster()
                .raster()
                .provider()
                .tiled()
                .tiling_scheme()
                .global_tiling()
                .min_level());
        EXPECT_EQ(
            tile_size,
            layer.imagery_raster()
                .raster()
                .provider()
                .tiled()
                .tiling_scheme()
                .global_tiling()
                .tile_size());
    }

    // -- Unsupported properties:
    // https://docs.mapbox.com/mapbox-gl-js/style-spec/sources/#raster-attribution
    // https://docs.mapbox.com/mapbox-gl-js/style-spec/sources/#raster-scheme
    // https://docs.mapbox.com/mapbox-gl-js/style-spec/sources/#raster-url
    // https://docs.mapbox.com/mapbox-gl-js/style-spec/sources/#raster-volatile
};

struct RasterLayerConfig
{
    // https://docs.mapbox.com/mapbox-gl-js/style-spec/layers/#id
    const char* id = "";
    // https://docs.mapbox.com/mapbox-gl-js/style-spec/layers/#maxzoom
    double maxzoom = 24;
    // https://docs.mapbox.com/mapbox-gl-js/style-spec/layers/#minzoom
    double minzoom = 0;
    // https://docs.mapbox.com/mapbox-gl-js/style-spec/layers/#paint-raster-raster-opacity
    double paint_raster_opacity = 1.0;
    // https://docs.mapbox.com/mapbox-gl-js/style-spec/layers/#paint-raster-raster-resampling
    hrz_proto::TextureFiltering paint_raster_resampling = hrz_proto::BILINEAR;
    // https://docs.mapbox.com/mapbox-gl-js/style-spec/layers/#layout-raster-visibility
    bool layout_visibility = true;

    void test_output(const hrz_proto::LayerDump& layer)
    {
        EXPECT_STREQ(id, layer.name().c_str());
        EXPECT_DOUBLE_EQ(
            paint_raster_opacity, layer.imagery_raster().raster().blending().opacity());
        EXPECT_EQ(paint_raster_resampling, layer.imagery_raster().raster().sampling().filtering());

        EXPECT_EQ(layout_visibility, layer.imagery_raster().visible());
        EXPECT_EQ(layout_visibility, (bool)(layer.imagery_raster().scene_views().bits() & 1));
        EXPECT_EQ(layout_visibility, (bool)(layer.imagery_raster().scene_views().bits() & 2));

        int constraint_count = 0;
        if (minzoom > 0.0) constraint_count++;
        if (maxzoom < 24.0) constraint_count++;

        EXPECT_EQ(
            constraint_count, layer.imagery_raster().visibility_constraints().constraints_size());
        for (const auto& constraint : layer.imagery_raster().visibility_constraints().constraints())
        {
            EXPECT_EQ(hrz_proto::ALTITUDE, constraint.type());
            if (!constraint.has_altitude()) continue;

            if (constraint.altitude().relative_position() == hrz_proto::BELOW)
            {
                EXPECT_GT(minzoom, 0.0);
            }
            else if (constraint.altitude().relative_position() == hrz_proto::ABOVE)
            {
                EXPECT_LT(maxzoom, 24.0);
            }
        }
    }

    // -- Unsupported properties:
    // https://docs.mapbox.com/mapbox-gl-js/style-spec/layers/#paint-raster-raster-saturation
    // https://docs.mapbox.com/mapbox-gl-js/style-spec/layers/#paint-raster-raster-brightness-max
    // https://docs.mapbox.com/mapbox-gl-js/style-spec/layers/#paint-raster-raster-brightness-min
    // https://docs.mapbox.com/mapbox-gl-js/style-spec/layers/#paint-raster-raster-contrast
    // https://docs.mapbox.com/mapbox-gl-js/style-spec/layers/#paint-raster-raster-fade-duration
    // https://docs.mapbox.com/mapbox-gl-js/style-spec/layers/#paint-raster-raster-hue-rotate
};

TEST(MapboxTranslation, parse_raster_minimal)
{
    SceneLayerStats expected_layers;
    expected_layers.imagery_raster_count = 1;

    hrz_proto::SceneDump scene_dump;
    convert_mapbox_resource(mapbox::Resources::RasterMinimal, &scene_dump, expected_layers);

    const auto& layer = scene_dump.layers(0);

    RasterSourceConfig raster_source_minimal_config;
    raster_source_minimal_config.tiles = "https://redacted.localhost/osm/{z}/{x}/{y}.png";
    raster_source_minimal_config.test_output(layer);

    RasterLayerConfig raster_layer_minimal_config;
    raster_layer_minimal_config.id = "OSM";
    raster_layer_minimal_config.test_output(layer);
}

TEST(MapboxTranslation, parse_raster)
{
    SceneLayerStats expected_layers;
    expected_layers.imagery_raster_count = 1;

    hrz_proto::SceneDump scene_dump;
    convert_mapbox_resource(mapbox::Resources::Raster, &scene_dump, expected_layers);

    const auto& layer = scene_dump.layers(0);

    RasterSourceConfig raster_source;
    raster_source.bounds.set_west(-100);
    raster_source.bounds.set_south(0);
    raster_source.bounds.set_east(150);
    raster_source.bounds.set_north(62.5);
    raster_source.maxzoom = 7;
    raster_source.minzoom = 7;
    raster_source.tiles = "https://redacted.localhost/osm/{z}/{x}/{y}.png";
    raster_source.tile_size = 256;
    raster_source.test_output(layer);

    RasterLayerConfig raster_layer;
    raster_layer.id = "OSM";
    raster_layer.maxzoom = 14;
    raster_layer.minzoom = 4;
    raster_layer.paint_raster_opacity = 0.75;
    raster_layer.paint_raster_resampling = hrz_proto::NEAREST;
    raster_layer.layout_visibility = false;
    raster_layer.test_output(layer);
}

TEST(MapboxTranslation, parse_raster_shared_source)
{
    SceneLayerStats expected_layers;
    expected_layers.imagery_raster_count = 2;

    hrz_proto::SceneDump scene_dump;
    convert_mapbox_resource(mapbox::Resources::RasterSharedSource, &scene_dump, expected_layers);

    for (int i = 0; i < scene_dump.layers_size(); i++)
    {
        const auto& layer = scene_dump.layers(i);

        RasterSourceConfig raster_source_config;
        raster_source_config.tiles = "https://redacted.localhost/osm/{z}/{x}/{y}.png";
        raster_source_config.test_output(layer);

        RasterLayerConfig raster_layer_config;
        raster_layer_config.id = (i == 0) ? "layer1" : "layer2";
        raster_layer_config.test_output(layer);
    }
}

TEST(MapboxTranslation, parse_raster_unused_source)
{
    SceneLayerStats expected_layers;
    expected_layers.imagery_raster_count = 1;

    hrz_proto::SceneDump scene_dump;
    convert_mapbox_resource(mapbox::Resources::RasterUnusedSource, &scene_dump, expected_layers);

    const auto& layer = scene_dump.layers(0);

    RasterSourceConfig raster_source_config;
    raster_source_config.tiles = "https://redacted.localhost/osm/{z}/{x}/{y}.png";
    raster_source_config.test_output(layer);

    RasterLayerConfig raster_layer_config;
    raster_layer_config.id = "OSM";
    raster_layer_config.test_output(layer);
}

TEST(MapboxTranslation, parse_raster_multiple_tile_urls)
{
    SceneLayerStats expected_layers;
    expected_layers.imagery_raster_count = 1;

    hrz_proto::SceneDump scene_dump;
    convert_mapbox_resource(
        mapbox::Resources::RasterMultipleTileUrls, &scene_dump, expected_layers);

    const auto& layer = scene_dump.layers(0);

    RasterSourceConfig raster_source_config;
    raster_source_config.tiles = "https://redacted.localhost/osm/{z}/{x}/{y}.png";
    raster_source_config.test_output(layer);

    RasterLayerConfig raster_layer_config;
    raster_layer_config.id = "OSM";
    raster_layer_config.test_output(layer);
}

TEST(MapboxTranslation, parse_raster_no_tile_url)
{
    SceneLayerStats expected_layers;
    expected_layers.imagery_raster_count = 0;

    hrz_proto::SceneDump scene_dump;
    convert_mapbox_resource(mapbox::Resources::RasterNoTileUrl, &scene_dump, expected_layers);
}

TEST(MapboxTranslation, parse_background)
{
    SceneLayerStats expected_layers;

    hrz_proto::SceneDump scene_dump;
    convert_mapbox_resource(mapbox::Resources::Background, &scene_dump, expected_layers);

    for (const auto& settings : scene_dump.scene_view_settings())
    {
        EXPECT_FLOAT_EQ(settings.settings().terrain().terrain_color().r(), 1.0);
        EXPECT_FLOAT_EQ(settings.settings().terrain().terrain_color().g(), 1.0);
        EXPECT_FLOAT_EQ(settings.settings().terrain().terrain_color().b(), 1.0);
        EXPECT_FLOAT_EQ(settings.settings().terrain().terrain_color().a(), 1.0);
    }
}

TEST(MapboxTranslation, parse_geojson)
{
    SceneLayerStats expected_stats;
    expected_stats.vector_data_count = 1;
    expected_stats.vector_tiles_count = 1;

    hrz_proto::SceneDump scene_dump;
    convert_mapbox_resource(mapbox::Resources::Geojson, &scene_dump, expected_stats);

    for (const auto& layer : scene_dump.layers())
    {
        if (layer.has_vector_data())
        {
            const auto& vdl = layer.vector_data();
            EXPECT_EQ(vdl.sources_size(), 1);
            EXPECT_EQ(vdl.id(), 1);

            const auto& source = vdl.sources(0);
            EXPECT_EQ(
                source.provider_type(),
                hrz_proto::VectorDataProviderType::UNTILED_VECTOR_DATA_PROVIDER);
            EXPECT_EQ(source.has_geometry(), true);
            EXPECT_EQ(source.attributes_size(), 0);

            const auto& provider = source.untiled_data_provider();
            EXPECT_STREQ(
                provider.url().c_str(),
                "https://redacted.localhost/assets/geojson/france_regions.geojson");
            EXPECT_EQ(provider.format(), hrz_proto::VectorDataFormat::GEOJSON_VECTOR_DATA);
            EXPECT_EQ(provider.min_level(), 2);
            EXPECT_EQ(provider.max_level(), 10);
            EXPECT_EQ(provider.bounds().west(), -180.0);
            EXPECT_EQ(provider.bounds().south(), -85.051129);
            EXPECT_EQ(provider.bounds().east(), 180.0);
            EXPECT_EQ(provider.bounds().north(), 85.051129);
            EXPECT_EQ(provider.clip_margin(), true);
        }
    }
}

TEST(MapboxTranslation, parse_geojson_no_maxzoom)
{
    SceneLayerStats expected_stats;
    expected_stats.vector_data_count = 1;
    expected_stats.vector_tiles_count = 1;

    hrz_proto::SceneDump scene_dump;
    convert_mapbox_resource(mapbox::Resources::GeojsonNoMaxzoom, &scene_dump, expected_stats);

    for (const auto& layer : scene_dump.layers())
    {
        if (layer.has_vector_data())
        {
            const auto& vdl = layer.vector_data();
            EXPECT_EQ(vdl.sources_size(), 1);
            EXPECT_EQ(vdl.id(), 1);

            const auto& source = vdl.sources(0);
            EXPECT_EQ(
                source.provider_type(),
                hrz_proto::VectorDataProviderType::UNTILED_VECTOR_DATA_PROVIDER);
            EXPECT_EQ(source.has_geometry(), true);
            EXPECT_EQ(source.attributes_size(), 0);

            const auto& provider = source.untiled_data_provider();
            EXPECT_STREQ(
                provider.url().c_str(),
                "https://redacted.localhost/assets/geojson/france_regions.geojson");
            EXPECT_EQ(provider.format(), hrz_proto::VectorDataFormat::GEOJSON_VECTOR_DATA);
            EXPECT_EQ(provider.min_level(), 0);
            EXPECT_EQ(provider.max_level(), 22);
            EXPECT_EQ(provider.bounds().west(), -180.0);
            EXPECT_EQ(provider.bounds().south(), -85.051129);
            EXPECT_EQ(provider.bounds().east(), 180.0);
            EXPECT_EQ(provider.bounds().north(), 85.051129);
            EXPECT_EQ(provider.clip_margin(), true);
        }
    }
}

TEST(MapboxTranslation, parse_geojson_inline)
{
    SceneLayerStats expected_stats;
    expected_stats.vector_data_count = 1;
    expected_stats.vector_tiles_count = 1;

    hrz_proto::SceneDump scene_dump;
    convert_mapbox_resource(mapbox::Resources::GeojsonInline, &scene_dump, expected_stats);

    for (const auto& layer : scene_dump.layers())
    {
        if (layer.has_vector_data())
        {
            const auto& vdl = layer.vector_data();
            EXPECT_EQ(vdl.sources_size(), 1);
            EXPECT_EQ(vdl.id(), 1);

            const auto& source = vdl.sources(0);
            EXPECT_EQ(
                source.provider_type(),
                hrz_proto::VectorDataProviderType::UNTILED_VECTOR_DATA_PROVIDER);
            EXPECT_EQ(source.has_geometry(), true);
            EXPECT_EQ(source.attributes_size(), 0);

            const auto& provider = source.untiled_data_provider();

            EXPECT_STREQ(
                provider.url().c_str(),
                "data:;base64,"
                "eyJ0eXBlIjoiRmVhdHVyZUNvbGxlY3Rpb24iLCJuYW1lIjoiZ3JpZF8xMHgxMCIsImNycyI6eyJ0eXBlIj"
                "oibmFtZSIsInByb3BlcnRpZXMiOnsibmFtZSI6InVybjpvZ2M6ZGVmOmNyczpPR0M6MS4zOkNSUzg0In19"
                "LCJmZWF0dXJlcyI6W3sidHlwZSI6IkZlYXR1cmUiLCJwcm9wZXJ0aWVzIjp7fSwiZ2VvbWV0cnkiOnsidH"
                "lwZSI6IlBvaW50IiwiY29vcmRpbmF0ZXMiOlstMS42Nzk4MzIyNTk5ODgxOTcsNDguMTExNzIxNjUzNzcy"
                "ODZdfX0seyJ0eXBlIjoiRmVhdHVyZSIsInByb3BlcnRpZXMiOnt9LCJnZW9tZXRyeSI6eyJ0eXBlIjoiUG"
                "9pbnQiLCJjb29yZGluYXRlcyI6Wy0xLjY3OTc0MjQyODQ1OTc4NSw0OC4xMTE3MjE2NTM3NzI4Nl19fSx7"
                "InR5cGUiOiJGZWF0dXJlIiwicHJvcGVydGllcyI6e30sImdlb21ldHJ5Ijp7InR5cGUiOiJQb2ludCIsIm"
                "Nvb3JkaW5hdGVzIjpbLTEuNjc5NjUyNTk2OTMxMzczLDQ4LjExMTcyMTY1Mzc3Mjg2XX19LHsidHlwZSI6"
                "IkZlYXR1cmUiLCJwcm9wZXJ0aWVzIjp7fSwiZ2VvbWV0cnkiOnsidHlwZSI6IlBvaW50IiwiY29vcmRpbm"
                "F0ZXMiOlstMS42Nzk4MzIyNTk5ODgxOTcsNDguMTExNjYxNjc0OTk4Nzg2XX19LHsidHlwZSI6IkZlYXR1"
                "cmUiLCJwcm9wZXJ0aWVzIjp7fSwiZ2VvbWV0cnkiOnsidHlwZSI6IlBvaW50IiwiY29vcmRpbmF0ZXMiOl"
                "stMS42Nzk3NDI0Mjg0NTk3ODUsNDguMTExNjYxNjc0OTk4Nzg2XX19LHsidHlwZSI6IkZlYXR1cmUiLCJw"
                "cm9wZXJ0aWVzIjp7fSwiZ2VvbWV0cnkiOnsidHlwZSI6IlBvaW50IiwiY29vcmRpbmF0ZXMiOlstMS42Nz"
                "k2NTI1OTY5MzEzNzMsNDguMTExNjYxNjc0OTk4Nzg2XX19LHsidHlwZSI6IkZlYXR1cmUiLCJwcm9wZXJ0"
                "aWVzIjp7fSwiZ2VvbWV0cnkiOnsidHlwZSI6IlBvaW50IiwiY29vcmRpbmF0ZXMiOlstMS42Nzk4MzIyNT"
                "k5ODgxOTcsNDguMTExNjAxNjk2MTU0NzFdfX0seyJ0eXBlIjoiRmVhdHVyZSIsInByb3BlcnRpZXMiOnt9"
                "LCJnZW9tZXRyeSI6eyJ0eXBlIjoiUG9pbnQiLCJjb29yZGluYXRlcyI6Wy0xLjY3OTc0MjQyODQ1OTc4NS"
                "w0OC4xMTE2MDE2OTYxNTQ3MV19fSx7InR5cGUiOiJGZWF0dXJlIiwicHJvcGVydGllcyI6e30sImdlb21l"
                "dHJ5Ijp7InR5cGUiOiJQb2ludCIsImNvb3JkaW5hdGVzIjpbLTEuNjc5NjUyNTk2OTMxMzczLDQ4LjExMT"
                "YwMTY5NjE1NDcxXX19XX0=");

            EXPECT_EQ(provider.format(), hrz_proto::VectorDataFormat::GEOJSON_VECTOR_DATA);
            EXPECT_EQ(provider.max_level(), 22);
            EXPECT_EQ(provider.clip_margin(), true);
        }
    }
}

TEST(MapboxTranslation, parse_vector_source)
{
    SceneLayerStats expected_stats;
    expected_stats.vector_data_count = 1;
    expected_stats.vector_tiles_count = 1;

    hrz_proto::SceneDump scene_dump;
    convert_mapbox_resource(mapbox::Resources::Vector, &scene_dump, expected_stats);

    for (const auto& layer : scene_dump.layers())
    {
        if (layer.has_vector_data())
        {
            const auto& vdl = layer.vector_data();
            EXPECT_EQ(vdl.id(), 1);

            const auto& src = vdl.sources(0);
            EXPECT_EQ(
                src.provider_type(), hrz_proto::VectorDataProviderType::TILED_VECTOR_DATA_PROVIDER);
            EXPECT_STREQ(
                src.tiled_data_provider().url_pattern().c_str(),
                "https://redacted.localhost/assets/mvt/HK/{z}/{x}/{-y}.mvt");
            EXPECT_STREQ(src.tiled_data_provider().layer_name().c_str(), "HK_SAMPLE_3857");
            EXPECT_EQ(
                src.tiled_data_provider().format(), hrz_proto::VectorDataFormat::MVT_VECTOR_DATA);
            EXPECT_EQ(src.tiled_data_provider().min_level(), 0);
            EXPECT_EQ(src.tiled_data_provider().max_level(), 14);
            EXPECT_NEAR(src.tiled_data_provider().bounds().west(), -180.0, 0.001);
            EXPECT_NEAR(src.tiled_data_provider().bounds().south(), -85.051129, 0.001);
            EXPECT_NEAR(src.tiled_data_provider().bounds().east(), 180.0, 0.001);
            EXPECT_NEAR(src.tiled_data_provider().bounds().north(), 85.051129, 0.001);
        }
        else if (layer.has_vector_tiles())
        {
            const auto& vtl = layer.vector_tiles();
            EXPECT_EQ(vtl.clamping().method(), hrz_proto::VectorClampMode::PER_VERTEX);
            EXPECT_EQ(vtl.resolution().max_screen_space_error(), 4);
            EXPECT_EQ(vtl.clip_id(), -1);
            EXPECT_EQ(vtl.visible(), true);

            const auto& src = vtl.source();
            EXPECT_EQ(src.vector_data_layer_id(), 1);

            EXPECT_EQ(vtl.visibility_constraints().constraints_size(), 2);
            const auto& c1 = vtl.visibility_constraints().constraints(0);
            EXPECT_EQ(c1.type(), hrz_proto::ALTITUDE);
            EXPECT_EQ(
                c1.altitude().relative_position(), hrz_proto::RelativePositionQualifier::ABOVE);
            const auto& c2 = vtl.visibility_constraints().constraints(1);
            EXPECT_EQ(c2.type(), hrz_proto::ALTITUDE);
            EXPECT_EQ(
                c2.altitude().relative_position(), hrz_proto::RelativePositionQualifier::BELOW);
        }
        else
        {
            FAIL() << "Unexpected layer";
        }
    }
}

TEST(MapboxTranslation, parse_vector_bounds)
{
    SceneLayerStats expected_stats;
    expected_stats.vector_data_count = 1;
    expected_stats.vector_tiles_count = 1;

    hrz_proto::SceneDump scene_dump;
    convert_mapbox_resource(mapbox::Resources::VectorBounds, &scene_dump, expected_stats);

    for (const auto& layer : scene_dump.layers())
    {
        if (layer.has_vector_data())
        {
            const auto& vdl = layer.vector_data();
            EXPECT_EQ(vdl.id(), 1);

            const auto& src = vdl.sources(0);
            EXPECT_EQ(
                src.provider_type(), hrz_proto::VectorDataProviderType::TILED_VECTOR_DATA_PROVIDER);
            EXPECT_STREQ(
                src.tiled_data_provider().url_pattern().c_str(),
                "https://redacted.localhost/assets/mvt/HK/{z}/{x}/{-y}.mvt");
            EXPECT_STREQ(src.tiled_data_provider().layer_name().c_str(), "HK_SAMPLE_3857");
            EXPECT_EQ(
                src.tiled_data_provider().format(), hrz_proto::VectorDataFormat::MVT_VECTOR_DATA);
            EXPECT_EQ(src.tiled_data_provider().min_level(), 0);
            EXPECT_EQ(src.tiled_data_provider().max_level(), 14);
            EXPECT_NEAR(src.tiled_data_provider().bounds().west(), 114.1, 0.001);
            EXPECT_NEAR(src.tiled_data_provider().bounds().south(), 22.25, 0.001);
            EXPECT_NEAR(src.tiled_data_provider().bounds().east(), 114.25, 0.001);
            EXPECT_NEAR(src.tiled_data_provider().bounds().north(), 22.35, 0.001);
        }
        else if (layer.has_vector_tiles())
        {
            const auto& vtl = layer.vector_tiles();
            const auto& src = vtl.source();
            EXPECT_EQ(src.vector_data_layer_id(), 1);
        }
        else
        {
            FAIL() << "Unexpected layer";
        }
    }
}

TEST(MapboxTranslation, parse_vector_shared_source)
{
    SceneLayerStats expected_stats;
    expected_stats.vector_data_count = 2;
    expected_stats.vector_tiles_count = 3;

    hrz_proto::SceneDump scene_dump;
    convert_mapbox_resource(mapbox::Resources::VectorSharedSource, &scene_dump, expected_stats);

    for (const auto& layer : scene_dump.layers())
    {
        if (layer.has_vector_data())
        {
            const auto& vdl = layer.vector_data();
            if (vdl.id() == 1)
            {
                EXPECT_STREQ(
                    vdl.sources(0).tiled_data_provider().layer_name().c_str(), "HK_SAMPLE_3857");
            }
            else if (vdl.id() == 2)
            {
                EXPECT_STREQ(
                    vdl.sources(0).tiled_data_provider().layer_name().c_str(), "HK_SAMPLE_4326");
            }
            else
            {
                FAIL() << "Unexpected vector data layer";
            }
        }
        else if (layer.has_vector_tiles())
        {
            const auto& vtl = layer.vector_tiles();
            if (hrz::str::starts_with(layer.name(), "hk_mvt - HK_SAMPLE_3857"))
            {
                EXPECT_EQ(vtl.source().vector_data_layer_id(), 1);
            }
            else if (hrz::str::starts_with(layer.name(), "hk_mvt - HK_SAMPLE_4326"))
            {
                EXPECT_EQ(vtl.source().vector_data_layer_id(), 2);
            }
            else
            {
                FAIL() << "Unexpected vector tiles layer";
            }
        }
        else
        {
            FAIL() << "What is this layer?";
        }
    }
}

TEST(MapboxTranslation, parse_vector_source_attributes)
{
    SceneLayerStats expected_stats;
    expected_stats.vector_data_count = 1;
    expected_stats.vector_tiles_count = 1;

    hrz_proto::SceneDump scene_dump;
    convert_mapbox_resource(
        mapbox::Resources::VectorCircleStrokeWidth, &scene_dump, expected_stats);

    const hrz_proto::VectorAttribute* x_attr = nullptr;
    const hrz_proto::VectorAttribute* y_attr = nullptr;
    const hrz_proto::StylingAttributeRef* x_attr_ref = nullptr;
    const hrz_proto::StylingAttributeRef* y_attr_ref = nullptr;

    for (int i = 0; i < scene_dump.layers_size(); i++)
    {
        const auto& layer = scene_dump.layers(i);
        if (layer.has_vector_tiles())
        {
            const auto& vtl = layer.vector_tiles();

            EXPECT_EQ(vtl.source().vector_data_layer_id(), 1);
            EXPECT_EQ(vtl.source().override_bounds(), false);
            EXPECT_EQ(vtl.source().override_levels(), false);

            EXPECT_EQ(vtl.style().attributes_size(), 2);
            for (const auto& attribute : vtl.style().attributes())
            {
                if (attribute.styling_name() == "x")
                {
                    x_attr_ref = &attribute;
                }
                else if (attribute.styling_name() == "y")
                {
                    y_attr_ref = &attribute;
                }
                else
                {
                    FAIL() << "Unexpected attribute name";
                }
            }
        }
        else if (layer.has_vector_data())
        {
            const auto& vdl = layer.vector_data();
            EXPECT_EQ(vdl.id(), 1);

            EXPECT_EQ(vdl.sources_size(), 1);
            EXPECT_EQ(vdl.sources(0).has_untiled_data_provider(), true);

            EXPECT_EQ(vdl.sources(0).attributes_size(), 2);
            for (const auto& attribute : vdl.sources(0).attributes())
            {
                if (attribute.source_name() == "x")
                {
                    x_attr = &attribute;
                    EXPECT_EQ(attribute.is_feature_id(), false);
                    EXPECT_EQ(attribute.is_source_feature_ids(), false);
                }
                else if (attribute.source_name() == "y")
                {
                    y_attr = &attribute;
                    EXPECT_EQ(attribute.is_feature_id(), false);
                    EXPECT_EQ(attribute.is_source_feature_ids(), false);
                }
                else
                {
                    FAIL() << "Unexpected attribute name";
                }
            }
        }
    }

    ASSERT_TRUE(x_attr);
    ASSERT_TRUE(y_attr);
    ASSERT_TRUE(x_attr_ref);
    ASSERT_TRUE(y_attr_ref);

    EXPECT_EQ(x_attr_ref->vector_data_attr_id(), x_attr->id());
    EXPECT_EQ(y_attr_ref->vector_data_attr_id(), y_attr->id());
}

TEST(MapboxTranslation, parse_vector_attribute_type_metadata)
{
    SceneLayerStats expected_stats;
    expected_stats.vector_data_count = 2;
    expected_stats.vector_tiles_count = 2;

    hrz_proto::SceneDump scene_dump;
    convert_mapbox_resource(
        mapbox::Resources::VectorAttributeTypeMetadata, &scene_dump, expected_stats);

    for (int i = 0; i < scene_dump.layers_size(); i++)
    {
        const auto& layer = scene_dump.layers(i);
        if (layer.has_vector_data())
        {
            const auto& vdl = layer.vector_data();
            ASSERT_EQ(vdl.sources_size(), 1);

            const auto& source = vdl.sources(0);
            if (source.has_tilejson_data_provider())
            {
                ASSERT_EQ(source.tilejson_data_provider().layer_name(), "HK_SAMPLE_3857");
                ASSERT_EQ(source.attributes_size(), 1);
                EXPECT_EQ(source.attributes(0).source_name(), "ELEVATION");
            }
            else if (source.has_untiled_data_provider())
            {
                ASSERT_EQ(source.attributes_size(), 2);
                for (const auto& attribute : source.attributes())
                {
                    if (attribute.source_name() == "id")
                    {
                    }
                    else if (attribute.source_name() == "id-string")
                    {
                    }
                    else
                    {
                        FAIL() << "Unexpected attribute";
                    }
                }
            }
            else
            {
                FAIL() << "Unexpected source provider";
            }
        }
    }
}

TEST(MapboxTranslation, parse_vector_fill_extrusion_layer)
{
    SceneLayerStats expected_stats;
    expected_stats.vector_data_count = 1;
    expected_stats.vector_tiles_count = 1;
    expected_stats.imagery_raster_count = 1;

    hrz_proto::SceneDump scene_dump;
    convert_mapbox_resource(mapbox::Resources::VectorFillExtrusion, &scene_dump, expected_stats);

    for (int i = 0; i < scene_dump.layers_size(); i++)
    {
        const auto& layer = scene_dump.layers(i);
        if (layer.has_vector_tiles())
        {
            const auto& vtl = layer.vector_tiles();

            EXPECT_EQ(vtl.source().vector_data_layer_id(), 1);
            EXPECT_EQ(vtl.source().override_bounds(), false);
            EXPECT_EQ(vtl.source().override_levels(), false);

            EXPECT_EQ(vtl.style().attributes_size(), 0);

            EXPECT_EQ(vtl.style().representations_size(), 1);
            EXPECT_EQ(
                vtl.style().representations(0).type(),
                hrz_proto::VectorReprType::EXTRUDED_GEOMETRY_VECTOR_REPR);

            const auto& style = vtl.style().representations(0).extruded_geometry();

            EXPECT_FALSE(style.roof_color().name().empty());
            EXPECT_FALSE(style.upper_color().name().empty());
            EXPECT_FALSE(style.lower_color().name().empty());

            EXPECT_NE(style.roof_color().name(), style.lower_color().name());
            EXPECT_NE(style.lower_color().name(), style.upper_color().name());
            EXPECT_NE(style.upper_color().name(), style.roof_color().name());

            auto color = hrz::parse_color_string("#efefef").value();
            EXPECT_EQ(hrz::convert_proto_color_to_uint(style.roof_color().default_value()), color);
            EXPECT_EQ(hrz::convert_proto_color_to_uint(style.lower_color().default_value()), color);
            EXPECT_EQ(hrz::convert_proto_color_to_uint(style.upper_color().default_value()), color);

            EXPECT_EQ(style.extrusion().default_value(), 2.0f);
        }
        else if (layer.has_vector_data())
        {
            const auto& vdl = layer.vector_data();
            EXPECT_EQ(vdl.id(), 1);

            EXPECT_EQ(vdl.sources_size(), 1);
            EXPECT_EQ(vdl.sources(0).has_tilejson_data_provider(), true);
            EXPECT_STREQ(
                vdl.sources(0).tilejson_data_provider().url().c_str(),
                "https://redacted.localhost/assets/mvt/HK/tiles.json");
            EXPECT_STREQ(
                vdl.sources(0).tilejson_data_provider().layer_name().c_str(), "HK_SAMPLE_3857");

            EXPECT_EQ(vdl.sources(0).attributes_size(), 0);
        }
    }
}

TEST(MapboxTranslation, parse_vector_fill_extrusion_layer_no_gradient)
{
    SceneLayerStats expected_stats;
    expected_stats.vector_data_count = 1;
    expected_stats.vector_tiles_count = 1;

    hrz_proto::SceneDump scene_dump;
    convert_mapbox_resource(
        mapbox::Resources::VectorFillExtrusionNoGradient, &scene_dump, expected_stats);

    for (int i = 0; i < scene_dump.layers_size(); i++)
    {
        const auto& layer = scene_dump.layers(i);
        if (layer.has_vector_tiles())
        {
            const auto& style = layer.vector_tiles().style().representations(0).extruded_geometry();

            EXPECT_EQ(style.roof_color().name(), style.lower_color().name());
            EXPECT_EQ(style.lower_color().name(), style.upper_color().name());

            auto color = hrz::parse_color_string("#efefef").value();
            EXPECT_EQ(hrz::convert_proto_color_to_uint(style.roof_color().default_value()), color);
            EXPECT_EQ(hrz::convert_proto_color_to_uint(style.lower_color().default_value()), color);
            EXPECT_EQ(hrz::convert_proto_color_to_uint(style.upper_color().default_value()), color);
        }
    }
}

// @TODO Styling script generation remains untested...

TEST(MapboxTranslation, parse_vector_fill_layer)
{
    SceneLayerStats expected_stats;
    expected_stats.vector_data_count = 1;
    expected_stats.vector_tiles_count = 1;
    expected_stats.imagery_raster_count = 1;

    hrz_proto::SceneDump scene_dump;
    convert_mapbox_resource(mapbox::Resources::VectorFill, &scene_dump, expected_stats);

    for (int i = 0; i < scene_dump.layers_size(); i++)
    {
        const auto& layer = scene_dump.layers(i);
        if (layer.has_vector_tiles())
        {
            const auto& vtl = layer.vector_tiles();

            EXPECT_EQ(vtl.source().vector_data_layer_id(), 1);
            EXPECT_EQ(vtl.source().override_bounds(), false);
            EXPECT_EQ(vtl.source().override_levels(), false);

            EXPECT_EQ(vtl.style().attributes_size(), 0);

            EXPECT_EQ(vtl.style().representations_size(), 1);
            EXPECT_EQ(
                vtl.style().representations(0).type(),
                hrz_proto::VectorReprType::FLAT_OVERLAY_VECTOR_REPR);

            const auto& flat_overlay = vtl.style().representations(0).flat_overlay_geometry();

            auto color_rgba =
                hrz::convert_uint_color_to_rgba(hrz::parse_color_string("#7faf75").value());
            color_rgba.a = 0.8;
            EXPECT_EQ(
                hrz::convert_proto_color_to_uint(flat_overlay.color().default_value()),
                hrz::convert_rgba_color_to_uint(color_rgba));
        }
        else if (layer.has_vector_data())
        {
            const auto& vdl = layer.vector_data();
            EXPECT_EQ(vdl.id(), 1);

            EXPECT_EQ(vdl.sources_size(), 1);
            EXPECT_EQ(vdl.sources(0).has_tilejson_data_provider(), true);
            EXPECT_STREQ(
                vdl.sources(0).tilejson_data_provider().url().c_str(),
                "https://redacted.localhost/assets/mvt/HK/tiles.json");
            EXPECT_STREQ(
                vdl.sources(0).tilejson_data_provider().layer_name().c_str(), "HK_SAMPLE_3857");
        }
    };
}

TEST(MapboxTranslation, parse_vector_fill_layer_outline)
{
    SceneLayerStats expected_stats;
    expected_stats.vector_data_count = 1;
    expected_stats.vector_tiles_count = 1;

    hrz_proto::SceneDump scene_dump;
    convert_mapbox_resource(mapbox::Resources::VectorFillOutline, &scene_dump, expected_stats);

    for (int i = 0; i < scene_dump.layers_size(); i++)
    {
        const auto& layer = scene_dump.layers(i);
        if (layer.has_vector_tiles())
        {
            const auto& vtl = layer.vector_tiles();
            EXPECT_EQ(vtl.style().representations_size(), 4);

            uint32_t outline_z_index = 0;
            uint32_t fill_z_index = 0;

            for (const auto& repr : vtl.style().representations())
            {
                EXPECT_EQ(repr.type(), hrz_proto::VectorReprType::FLAT_OVERLAY_VECTOR_REPR);
                if (repr.name() == "no_outline_antialias" || repr.name() == "no_outline_pattern")
                {
                    EXPECT_EQ(repr.flat_overlay_geometry().polygons_outline(), false);
                }
                else if (repr.name() == "outline")
                {
                    EXPECT_EQ(repr.flat_overlay_geometry().polygons_outline(), false);
                    fill_z_index = repr.flat_overlay_geometry().z_index();
                }
                else if (repr.name() == "outline_hrz_outline")
                {
                    const auto& flat_overlay = repr.flat_overlay_geometry();

                    auto color = hrz::parse_color_string("#000000").value();
                    EXPECT_EQ(
                        hrz::convert_proto_color_to_uint(flat_overlay.color().default_value()),
                        color);

                    EXPECT_EQ(flat_overlay.polygons_outline(), true);
                    EXPECT_EQ(
                        flat_overlay.line_width_unit(),
                        hrz_proto::InWorldSizeUnit::IN_WORLD_SIZE_IN_PIXELS);
                    EXPECT_EQ(flat_overlay.line_width().default_value(), 1.0);
                    EXPECT_EQ(flat_overlay.side(), hrz_proto::PolylineSide::SIDE_INSIDE);
                    EXPECT_EQ(flat_overlay.clip_to_tile(), true);

                    outline_z_index = flat_overlay.z_index();
                }
                else
                {
                    FAIL() << "Unexpected representation name";
                }
            }

            EXPECT_EQ(outline_z_index, fill_z_index + 1);
        }
    };
}

TEST(MapboxTranslation, parse_vector_fill_pattern)
{
    SceneLayerStats expected_stats;
    expected_stats.vector_data_count = 1;
    expected_stats.vector_tiles_count = 1;

    hrz_proto::SceneDump scene_dump;
    convert_mapbox_resource(mapbox::Resources::VectorFillPattern, &scene_dump, expected_stats);

    for (int i = 0; i < scene_dump.layers_size(); i++)
    {
        const auto& layer = scene_dump.layers(i);
        if (layer.has_vector_tiles())
        {
            const auto& vtl = layer.vector_tiles();
            EXPECT_EQ(vtl.style().representations_size(), 2);

            uint32_t outline_z_index = 0;
            uint32_t fill_z_index = 0;

            for (const auto& repr : vtl.style().representations())
            {
                EXPECT_EQ(repr.type(), hrz_proto::VectorReprType::FLAT_OVERLAY_VECTOR_REPR);
                if (repr.name() == "pattern")
                {
                    const auto& flat_overlay = repr.flat_overlay_geometry();

                    EXPECT_EQ(flat_overlay.polygons_outline(), false);
                    fill_z_index = flat_overlay.z_index();

                    auto background_color = hrz::parse_color_string("#ffffff00").value();
                    EXPECT_EQ(
                        hrz::convert_proto_color_to_uint(flat_overlay.color().default_value()),
                        background_color);

                    EXPECT_STREQ(
                        flat_overlay.polygon_pattern_sprite_name().default_value().c_str(),
                        "SableHumide");

                    const auto& polygon_pattern_color =
                        flat_overlay.polygon_pattern_color().default_value();
                    EXPECT_EQ(polygon_pattern_color.r(), 1.0f);
                    EXPECT_EQ(polygon_pattern_color.g(), 1.0f);
                    EXPECT_EQ(polygon_pattern_color.b(), 1.0f);
                    EXPECT_EQ(polygon_pattern_color.a(), 0.5f);

                    EXPECT_EQ(flat_overlay.polygon_pattern_size().default_value().x(), 1.0f);
                    EXPECT_EQ(flat_overlay.polygon_pattern_size().default_value().y(), 1.0f);
                    EXPECT_EQ(
                        flat_overlay.polygon_pattern_size_unit(),
                        hrz_proto::PolygonPatternSizeUnit::
                            POLYGON_PATTERN_SIZE_RELATIVE_TO_SPRITE_IN_PIXELS);
                    EXPECT_EQ(flat_overlay.polygon_pattern_rotation().default_value(), 0.0f);
                }
                else if (repr.name() == "pattern_hrz_outline")
                {
                    const auto& flat_overlay = repr.flat_overlay_geometry();

                    const auto& line_color = flat_overlay.color().default_value();
                    EXPECT_EQ(line_color.r(), 0.0f);
                    EXPECT_EQ(line_color.g(), 1.0f);
                    EXPECT_EQ(line_color.b(), 0.0f);
                    EXPECT_EQ(line_color.a(), 0.5f);

                    EXPECT_EQ(flat_overlay.polygons_outline(), true);
                    EXPECT_EQ(
                        flat_overlay.line_width_unit(),
                        hrz_proto::InWorldSizeUnit::IN_WORLD_SIZE_IN_PIXELS);
                    EXPECT_EQ(flat_overlay.line_width().default_value(), 1.0);
                    EXPECT_EQ(flat_overlay.side(), hrz_proto::PolylineSide::SIDE_INSIDE);
                    EXPECT_EQ(flat_overlay.clip_to_tile(), true);

                    outline_z_index = flat_overlay.z_index();
                }
                else
                {
                    FAIL() << "Unexpected representation name";
                }
            }

            EXPECT_EQ(outline_z_index, fill_z_index + 1);
        }
    };
}

TEST(MapboxTranslation, parse_vector_line_layer)
{
    SceneLayerStats expected_stats;
    expected_stats.vector_data_count = 1;
    expected_stats.vector_tiles_count = 1;
    expected_stats.imagery_raster_count = 1;

    hrz_proto::SceneDump scene_dump;
    convert_mapbox_resource(mapbox::Resources::VectorLine, &scene_dump, expected_stats);

    for (int i = 0; i < scene_dump.layers_size(); i++)
    {
        const auto& layer = scene_dump.layers(i);
        if (layer.has_vector_tiles())
        {
            const auto& vtl = layer.vector_tiles();

            EXPECT_EQ(vtl.source().vector_data_layer_id(), 1);
            EXPECT_EQ(vtl.source().override_bounds(), false);
            EXPECT_EQ(vtl.source().override_levels(), false);

            EXPECT_EQ(vtl.style().attributes_size(), 0);

            EXPECT_EQ(vtl.style().representations_size(), 1);
            EXPECT_EQ(
                vtl.style().representations(0).type(),
                hrz_proto::VectorReprType::FLAT_OVERLAY_VECTOR_REPR);

            const auto& style = vtl.style().representations(0).flat_overlay_geometry();

            auto color_rgba =
                hrz::convert_uint_color_to_rgba(hrz::parse_color_string("#ff0000").value());
            color_rgba.a = 0.8;
            EXPECT_EQ(
                hrz::convert_proto_color_to_uint(style.color().default_value()),
                hrz::convert_rgba_color_to_uint(color_rgba));

            EXPECT_TRUE(style.round_tips());

            EXPECT_EQ(style.line_width().default_value(), 4);
            EXPECT_EQ(style.line_width_unit(), hrz_proto::InWorldSizeUnit::IN_WORLD_SIZE_IN_PIXELS);

            EXPECT_EQ(style.dash_mode(), hrz_proto::DashMode::DASH_DISABLED);
        }
        else if (layer.has_vector_data())
        {
            const auto& vdl = layer.vector_data();
            EXPECT_EQ(vdl.id(), 1);

            EXPECT_EQ(vdl.sources_size(), 1);
            EXPECT_EQ(vdl.sources(0).has_tilejson_data_provider(), true);
        }
    };
}

TEST(MapboxTranslation, parse_vector_line_layer_dashes)
{
    SceneLayerStats expected_stats;
    expected_stats.vector_data_count = 1;
    expected_stats.vector_tiles_count = 1;

    hrz_proto::SceneDump scene_dump;
    convert_mapbox_resource(mapbox::Resources::VectorLineDashes, &scene_dump, expected_stats);

    for (int i = 0; i < scene_dump.layers_size(); i++)
    {
        const auto& layer = scene_dump.layers(i);
        if (layer.has_vector_tiles())
        {
            const auto& vtl = layer.vector_tiles();

            EXPECT_EQ(vtl.source().vector_data_layer_id(), 1);
            EXPECT_EQ(vtl.source().override_bounds(), false);
            EXPECT_EQ(vtl.source().override_levels(), false);

            EXPECT_EQ(vtl.style().attributes_size(), 0);

            EXPECT_EQ(vtl.style().representations_size(), 1);
            EXPECT_EQ(
                vtl.style().representations(0).type(),
                hrz_proto::VectorReprType::FLAT_OVERLAY_VECTOR_REPR);

            const auto& style = vtl.style().representations(0).flat_overlay_geometry();

            auto main_color = hrz::parse_color_string("#ff0000").value();
            EXPECT_EQ(hrz::convert_proto_color_to_uint(style.color().default_value()), main_color);

            auto empty_color = hrz::convert_rgba_color_to_uint({});
            EXPECT_EQ(
                hrz::convert_proto_color_to_uint(style.line_empty_color().default_value()),
                empty_color);

            EXPECT_FALSE(style.round_tips());

            EXPECT_EQ(style.line_width().default_value(), 4);
            EXPECT_EQ(style.line_width_unit(), hrz_proto::InWorldSizeUnit::IN_WORLD_SIZE_IN_PIXELS);

            EXPECT_EQ(style.dash_mode(), hrz_proto::DashMode::DASH_ENABLED_FILLED);
            EXPECT_EQ(style.dash_length().default_value(), 4 * 4 * 2);
            EXPECT_EQ(style.dash_period().default_value(), (4 + 2) * 4 * 2);
            EXPECT_EQ(style.dash_length_unit(), hrz_proto::DashSizeUnit::DASH_SIZE_IN_PIXELS);
            EXPECT_EQ(style.dash_period_unit(), hrz_proto::DashSizeUnit::DASH_SIZE_IN_PIXELS);
        }
    };
}

TEST(MapboxTranslation, parse_vector_circle)
{
    SceneLayerStats expected_stats;
    expected_stats.vector_data_count = 1;
    expected_stats.vector_tiles_count = 1;

    hrz_proto::SceneDump scene_dump;
    convert_mapbox_resource(mapbox::Resources::VectorCircle, &scene_dump, expected_stats);

    for (int i = 0; i < scene_dump.layers_size(); i++)
    {
        const auto& layer = scene_dump.layers(i);
        if (layer.has_vector_tiles())
        {
            const auto& vtl = layer.vector_tiles();

            EXPECT_EQ(vtl.style().representations_size(), 1);
            EXPECT_EQ(
                vtl.style().representations(0).type(),
                hrz_proto::VectorReprType::SYMBOL_VECTOR_REPR);

            const auto& symbol = vtl.style().representations(0).symbol();

            EXPECT_EQ(
                symbol.root_element().type(), hrz_proto::SymbolElementType::ANCHOR_SYMBOL_ELEMENT);
            const auto& anchor = symbol.root_element().anchor();

            EXPECT_EQ(anchor.position_offset().default_value().x(), 0);
            EXPECT_EQ(anchor.position_offset().default_value().y(), 0);
            EXPECT_EQ(anchor.can_overlap_other_symbols(), true);
            EXPECT_EQ(anchor.hides_other_symbols(), false);

            EXPECT_EQ(
                anchor.x_axis_alignment(), hrz_proto::SymbolAxisAlignment::AXIS_ALIGNMENT_SCREEN);
            EXPECT_EQ(
                anchor.y_axis_alignment(), hrz_proto::SymbolAxisAlignment::AXIS_ALIGNMENT_SCREEN);

            EXPECT_EQ(
                anchor.child().type(), hrz_proto::SymbolElementType::SIZED_BOX_SYMBOL_ELEMENT);
            const auto& sized_box = anchor.child().sized_box();

            // In Mapbox, circle stroke width is added to the total radius of the circle, unlike
            // Horizon where stroke width is taken from the radius.
            EXPECT_EQ(sized_box.size().default_value().x(), (10.0 + 5.0) * 2.0);
            EXPECT_EQ(sized_box.size().default_value().x(), (10.0 + 5.0) * 2.0);

            EXPECT_EQ(
                sized_box.child().type(),
                hrz_proto::SymbolElementType::DECORATED_SHAPE_SYMBOL_ELEMENT);
            const auto& shape = sized_box.child().decorated_shape();

            EXPECT_EQ(shape.shape_type(), hrz_proto::DecoratedShapeType::DECORATED_SHAPE_CIRCLE);
            EXPECT_EQ(shape.border_size().default_value(), 5);

            auto color = hrz::parse_color_string("#ff0000").value();
            EXPECT_EQ(hrz::convert_proto_color_to_uint(shape.color().default_value()), color);

            auto border_color = hrz::parse_color_string("#ffffff").value();
            EXPECT_EQ(
                hrz::convert_proto_color_to_uint(shape.border_color().default_value()),
                border_color);
        }
    };
}

TEST(MapboxTranslation, parse_vector_circle_translate_anchor)
{
    SceneLayerStats expected_stats;
    expected_stats.vector_data_count = 1;
    expected_stats.vector_tiles_count = 1;

    hrz_proto::SceneDump scene_dump;
    convert_mapbox_resource(
        mapbox::Resources::VectorCircleTranslateAnchor, &scene_dump, expected_stats);

    for (int i = 0; i < scene_dump.layers_size(); i++)
    {
        const auto& layer = scene_dump.layers(i);
        if (layer.has_vector_tiles())
        {
            const auto& vtl = layer.vector_tiles();
            EXPECT_EQ(vtl.style().representations_size(), 3);

            for (const auto& repr : vtl.style().representations())
            {
                EXPECT_EQ(repr.type(), hrz_proto::VectorReprType::SYMBOL_VECTOR_REPR);

                const auto& symbol = repr.symbol();

                EXPECT_EQ(
                    symbol.root_element().type(),
                    hrz_proto::SymbolElementType::ANCHOR_SYMBOL_ELEMENT);
                const auto& anchor = symbol.root_element().anchor();

                EXPECT_EQ(
                    anchor.position_offset_size_unit(),
                    hrz_proto::SymbolSizeUnit::SYMBOL_SIZE_IN_PIXELS);

                if (repr.name() == "map_anchor")
                {
                    EXPECT_EQ(anchor.position_offset().default_value().x(), 0);
                    EXPECT_EQ(anchor.position_offset().default_value().y(), 10);

                    EXPECT_NE(
                        anchor.child().type(),
                        hrz_proto::SymbolElementType::TRANSFORM_SYMBOL_ELEMENT);
                }
                else if (repr.name() == "viewport_anchor")
                {
                    EXPECT_EQ(anchor.position_offset().default_value().x(), 0);
                    EXPECT_EQ(anchor.position_offset().default_value().y(), 0);

                    EXPECT_EQ(
                        anchor.child().type(),
                        hrz_proto::SymbolElementType::TRANSFORM_SYMBOL_ELEMENT);

                    const auto& transform = anchor.child().transform();
                    EXPECT_EQ(transform.components_size(), 1);

                    const auto& component = transform.components(0);
                    EXPECT_EQ(
                        component.type(),
                        hrz_proto::TransformSymbolComponentType::TRANSFORM_SYMBOL_TRANSLATION);

                    const auto& translation = component.translation();
                    EXPECT_EQ(translation.default_value().x(), 0);
                    EXPECT_EQ(translation.default_value().y(), -10);
                }
                else if (repr.name() == "default_anchor")
                {
                    EXPECT_EQ(anchor.position_offset().default_value().x(), 0);
                    EXPECT_EQ(anchor.position_offset().default_value().y(), 1);

                    EXPECT_NE(
                        anchor.child().type(),
                        hrz_proto::SymbolElementType::TRANSFORM_SYMBOL_ELEMENT);
                }
                else
                {
                    FAIL() << "Unexpected representation name";
                }
            }
        }
    };
}

TEST(MapboxTranslation, parse_vector_circle_pitch_alignment)
{
    SceneLayerStats expected_stats;
    expected_stats.vector_data_count = 1;
    expected_stats.vector_tiles_count = 1;

    hrz_proto::SceneDump scene_dump;
    convert_mapbox_resource(
        mapbox::Resources::VectorCirclePitchAlignment, &scene_dump, expected_stats);

    for (int i = 0; i < scene_dump.layers_size(); i++)
    {
        const auto& layer = scene_dump.layers(i);
        if (layer.has_vector_tiles())
        {
            const auto& vtl = layer.vector_tiles();
            EXPECT_EQ(vtl.style().representations_size(), 3);

            for (const auto& repr : vtl.style().representations())
            {
                EXPECT_EQ(repr.type(), hrz_proto::VectorReprType::SYMBOL_VECTOR_REPR);

                const auto& symbol = repr.symbol();

                EXPECT_EQ(
                    symbol.root_element().type(),
                    hrz_proto::SymbolElementType::ANCHOR_SYMBOL_ELEMENT);
                const auto& anchor = symbol.root_element().anchor();

                if (repr.name() == "map_alignment")
                {
                    EXPECT_EQ(
                        anchor.x_axis_alignment(),
                        hrz_proto::SymbolAxisAlignment::AXIS_ALIGNMENT_SCREEN);
                    EXPECT_EQ(
                        anchor.y_axis_alignment(),
                        hrz_proto::SymbolAxisAlignment::AXIS_ALIGNMENT_WORLD);
                }
                else if (repr.name() == "viewport_alignment" || repr.name() == "default_alignment")
                {
                    EXPECT_EQ(
                        anchor.x_axis_alignment(),
                        hrz_proto::SymbolAxisAlignment::AXIS_ALIGNMENT_SCREEN);
                    EXPECT_EQ(
                        anchor.y_axis_alignment(),
                        hrz_proto::SymbolAxisAlignment::AXIS_ALIGNMENT_SCREEN);
                }
                else
                {
                    FAIL() << "Unexpected representation name";
                }
            }
        }
    };
}

TEST(MapboxTranslation, parse_vector_symbol_sprite)
{
    SceneLayerStats expected_stats;
    expected_stats.vector_data_count = 1;
    expected_stats.vector_tiles_count = 1;

    hrz_proto::SceneDump scene_dump;
    convert_mapbox_resource(
        mapbox::Resources::VectorSymbolSprite, &scene_dump, expected_stats,
        mapbox::Resources::Sprite);

    for (int i = 0; i < scene_dump.layers_size(); i++)
    {
        const auto& layer = scene_dump.layers(i);
        if (layer.has_vector_tiles())
        {
            const auto& vtl = layer.vector_tiles();

            EXPECT_EQ(vtl.style().representations_size(), 1);
            EXPECT_EQ(
                vtl.style().representations(0).type(),
                hrz_proto::VectorReprType::SYMBOL_VECTOR_REPR);

            const auto& symbol = vtl.style().representations(0).symbol();

            EXPECT_EQ(
                symbol.root_element().type(), hrz_proto::SymbolElementType::ANCHOR_SYMBOL_ELEMENT);
            const auto& anchor = symbol.root_element().anchor();

            EXPECT_EQ(anchor.child().type(), hrz_proto::SymbolElementType::PADDING_SYMBOL_ELEMENT);
            const auto& padding = anchor.child().padding();

            EXPECT_EQ(padding.child().type(), hrz_proto::SymbolElementType::IMAGE_SYMBOL_ELEMENT);
            const auto& image = padding.child().image();

            for (const auto& sprite : image.sprites())
            {
                if (sprite.name() == "minimal")
                {
                    EXPECT_EQ(sprite.offset().x(), 0);
                    EXPECT_EQ(sprite.offset().y(), 16);

                    EXPECT_EQ(sprite.size().x(), 16);
                    EXPECT_EQ(sprite.size().y(), 32);

                    EXPECT_EQ(sprite.content().x_min(), 0);
                    EXPECT_EQ(sprite.content().y_min(), 0);
                    EXPECT_EQ(sprite.content().x_max(), 0);
                    EXPECT_EQ(sprite.content().y_max(), 0);

                    EXPECT_EQ(sprite.stretch_x_size(), 0);
                    EXPECT_EQ(sprite.stretch_y_size(), 0);
                }
                else if (sprite.name() == "content")
                {
                    EXPECT_EQ(sprite.content().x_min(), 8);
                    EXPECT_EQ(sprite.content().y_min(), 9);
                    EXPECT_EQ(sprite.content().x_max(), 10);
                    EXPECT_EQ(sprite.content().y_max(), 11);
                }
                else if (sprite.name() == "stretch")
                {
                    EXPECT_EQ(sprite.stretch_x_size(), 1);
                    EXPECT_EQ(sprite.stretch_x(0).offset(), 10);
                    EXPECT_EQ(sprite.stretch_x(0).size(), 2);

                    EXPECT_EQ(sprite.stretch_y_size(), 2);
                    EXPECT_EQ(sprite.stretch_y(0).offset(), 10);
                    EXPECT_EQ(sprite.stretch_y(0).size(), 2);
                    EXPECT_EQ(sprite.stretch_y(1).offset(), 14);
                    EXPECT_EQ(sprite.stretch_y(1).size(), 2);
                }
            }
        }
    };
}

TEST(MapboxTranslation, parse_vector_symbol_icon)
{
    SceneLayerStats expected_stats;
    expected_stats.vector_data_count = 1;
    expected_stats.vector_tiles_count = 1;

    hrz_proto::SceneDump scene_dump;
    convert_mapbox_resource(
        mapbox::Resources::VectorSymbolIcon, &scene_dump, expected_stats,
        mapbox::Resources::Sprite);

    for (int i = 0; i < scene_dump.layers_size(); i++)
    {
        const auto& layer = scene_dump.layers(i);
        if (layer.has_vector_tiles())
        {
            const auto& vtl = layer.vector_tiles();

            EXPECT_EQ(vtl.style().representations_size(), 1);
            EXPECT_EQ(
                vtl.style().representations(0).type(),
                hrz_proto::VectorReprType::SYMBOL_VECTOR_REPR);

            const auto& symbol = vtl.style().representations(0).symbol();

            EXPECT_EQ(
                symbol.root_element().type(), hrz_proto::SymbolElementType::ANCHOR_SYMBOL_ELEMENT);
            const auto& anchor = symbol.root_element().anchor();

            EXPECT_EQ(anchor.element_alignment().default_value().x(), 0);
            EXPECT_EQ(anchor.element_alignment().default_value().y(), 1);

            EXPECT_EQ(anchor.can_overlap_other_symbols(), true);
            EXPECT_EQ(anchor.hides_other_symbols(), true);
            EXPECT_EQ(anchor.keep_upright(), false);

            EXPECT_EQ(anchor.rotation_order(), hrz_proto::EulerRotationOrder::EULER_XYZ);
            EXPECT_FLOAT_EQ(anchor.rotation().default_value().x(), lm::PI / 2.0);
            EXPECT_FLOAT_EQ(anchor.rotation().default_value().y(), lm::radians(30.0));
            EXPECT_FLOAT_EQ(anchor.rotation().default_value().z(), 0.0);

            EXPECT_EQ(
                anchor.x_axis_alignment(), hrz_proto::SymbolAxisAlignment::AXIS_ALIGNMENT_WORLD);
            EXPECT_EQ(
                anchor.y_axis_alignment(), hrz_proto::SymbolAxisAlignment::AXIS_ALIGNMENT_WORLD);

            EXPECT_EQ(anchor.child().type(), hrz_proto::SymbolElementType::PADDING_SYMBOL_ELEMENT);
            const auto& padding = anchor.child().padding();

            EXPECT_EQ(padding.left_padding().default_value(), 8 + 4);
            EXPECT_EQ(padding.top_padding().default_value(), 8 + 4);
            EXPECT_EQ(padding.right_padding().default_value(), 4);
            EXPECT_EQ(padding.bottom_padding().default_value(), 4);

            EXPECT_EQ(
                padding.child().type(), hrz_proto::SymbolElementType::TRANSFORM_SYMBOL_ELEMENT);
            const auto& transform = padding.child().transform();

            EXPECT_EQ(transform.components_size(), 1);
            const auto& transform_component = transform.components(0);

            EXPECT_EQ(
                transform_component.type(),
                hrz_proto::TransformSymbolComponentType::TRANSFORM_SYMBOL_TRANSLATION);
            EXPECT_EQ(transform_component.translation().default_value().x(), 2);
            EXPECT_EQ(transform_component.translation().default_value().y(), 2);
            EXPECT_EQ(transform_component.translation().default_value().z(), 0);

            EXPECT_EQ(transform.child().type(), hrz_proto::SymbolElementType::IMAGE_SYMBOL_ELEMENT);
            const auto& image = transform.child().image();

            auto color_rgba =
                hrz::convert_uint_color_to_rgba(hrz::parse_color_string("#ffffff").value());
            color_rgba.a = 0.9;
            EXPECT_EQ(
                hrz::convert_proto_color_to_uint(image.color().default_value()),
                hrz::convert_rgba_color_to_uint(color_rgba));

            EXPECT_STREQ(
                image.url().c_str(),
                "https://redacted.localhost/assets/sprites/sprites_mapbox/tree.png");
            EXPECT_FLOAT_EQ(image.scale().default_value(), 0.1);
        }
    };
}

TEST(MapboxTranslation, parse_vector_symbol_text)
{
    SceneLayerStats expected_stats;
    expected_stats.vector_data_count = 1;
    expected_stats.vector_tiles_count = 1;

    hrz_proto::SceneDump scene_dump;
    convert_mapbox_resource(
        mapbox::Resources::VectorSymbolText, &scene_dump, expected_stats,
        mapbox::Resources::Sprite);

    for (int i = 0; i < scene_dump.layers_size(); i++)
    {
        const auto& layer = scene_dump.layers(i);
        if (layer.has_vector_tiles())
        {
            const auto& vtl = layer.vector_tiles();

            EXPECT_EQ(vtl.style().representations_size(), 1);
            EXPECT_EQ(
                vtl.style().representations(0).type(),
                hrz_proto::VectorReprType::SYMBOL_VECTOR_REPR);

            const auto& symbol = vtl.style().representations(0).symbol();

            EXPECT_EQ(
                symbol.root_element().type(), hrz_proto::SymbolElementType::ANCHOR_SYMBOL_ELEMENT);
            const auto& anchor = symbol.root_element().anchor();

            EXPECT_EQ(anchor.element_alignment().default_value().x(), 0);
            EXPECT_EQ(anchor.element_alignment().default_value().y(), 1);

            EXPECT_EQ(anchor.can_overlap_other_symbols(), true);
            EXPECT_EQ(anchor.hides_other_symbols(), true);
            EXPECT_EQ(anchor.keep_upright(), false);

            EXPECT_EQ(anchor.rotation_order(), hrz_proto::EulerRotationOrder::EULER_XYZ);
            EXPECT_FLOAT_EQ(anchor.rotation().default_value().x(), lm::PI / 2.0);
            EXPECT_FLOAT_EQ(anchor.rotation().default_value().y(), lm::radians(30.0));
            EXPECT_FLOAT_EQ(anchor.rotation().default_value().z(), 0.0);

            EXPECT_EQ(
                anchor.x_axis_alignment(), hrz_proto::SymbolAxisAlignment::AXIS_ALIGNMENT_WORLD);
            EXPECT_EQ(
                anchor.y_axis_alignment(), hrz_proto::SymbolAxisAlignment::AXIS_ALIGNMENT_WORLD);

            EXPECT_EQ(anchor.child().type(), hrz_proto::SymbolElementType::PADDING_SYMBOL_ELEMENT);
            const auto& padding = anchor.child().padding();

            EXPECT_EQ(padding.left_padding().default_value(), 8 + 4);
            EXPECT_EQ(padding.top_padding().default_value(), 8 + 4);
            EXPECT_EQ(padding.right_padding().default_value(), 4);
            EXPECT_EQ(padding.bottom_padding().default_value(), 4);

            EXPECT_EQ(
                padding.child().type(), hrz_proto::SymbolElementType::TRANSFORM_SYMBOL_ELEMENT);
            const auto& transform = padding.child().transform();

            EXPECT_EQ(transform.components_size(), 1);
            const auto& transform_component = transform.components(0);

            EXPECT_EQ(
                transform_component.type(),
                hrz_proto::TransformSymbolComponentType::TRANSFORM_SYMBOL_TRANSLATION);
            EXPECT_EQ(transform_component.translation().default_value().x(), 2);
            EXPECT_EQ(transform_component.translation().default_value().y(), 2);
            EXPECT_EQ(transform_component.translation().default_value().z(), 0);

            EXPECT_EQ(transform.child().type(), hrz_proto::SymbolElementType::TEXT_SYMBOL_ELEMENT);
            const auto& text = transform.child().text();

            auto color_rgba =
                hrz::convert_uint_color_to_rgba(hrz::parse_color_string("#ff0000").value());
            color_rgba.a = 0.9;
            EXPECT_EQ(
                hrz::convert_proto_color_to_uint(text.text_color().default_value()),
                hrz::convert_rgba_color_to_uint(color_rgba));

            uint32_t outline_color = 0;
            ASSERT_TRUE(
                hrz_mapbox::parse_mapbox_color_string("rgba(0, 0, 0, 0.9)", &outline_color));

            auto outline_color_rgba = hrz::convert_uint_color_to_rgba(outline_color);
            EXPECT_EQ(
                hrz::convert_proto_color_to_uint(text.outline_color().default_value()),
                hrz::convert_rgba_color_to_uint(outline_color_rgba));

            EXPECT_STREQ(text.text().default_value().c_str(), "sample\ntext");
            EXPECT_STREQ(
                text.font_url().c_str(),
                "https://redacted.localhost/assets/fonts/Roboto/Roboto-Regular.ttf");
            EXPECT_FLOAT_EQ(text.font_size().default_value(), 16.0);
            EXPECT_FLOAT_EQ(text.outline_width().default_value(), 1.0);
            EXPECT_EQ(
                text.outline_width_unit(),
                hrz_proto::TextOutlineWidthUnit::OUTLINE_WIDTH_IN_FONT_UNIT);
            EXPECT_FLOAT_EQ(text.line_spacing().default_value(), 0.9);
            EXPECT_EQ(text.alignment().default_value(), hrz_proto::TextAlignment::LEFT_ALIGNED);
        }
    };
}

TEST(MapboxTranslation, parse_vector_symbol_text_justify_auto)
{
    SceneLayerStats expected_stats;
    expected_stats.vector_data_count = 1;
    expected_stats.vector_tiles_count = 1;

    hrz_proto::SceneDump scene_dump;
    convert_mapbox_resource(
        mapbox::Resources::VectorSymbolTextJustifyAuto, &scene_dump, expected_stats,
        mapbox::Resources::Sprite);

    for (int i = 0; i < scene_dump.layers_size(); i++)
    {
        const auto& layer = scene_dump.layers(i);
        if (layer.has_vector_tiles())
        {
            const auto& vtl = layer.vector_tiles();

            EXPECT_EQ(vtl.style().representations_size(), 1);
            EXPECT_EQ(
                vtl.style().representations(0).type(),
                hrz_proto::VectorReprType::SYMBOL_VECTOR_REPR);

            const auto& symbol = vtl.style().representations(0).symbol();

            EXPECT_EQ(
                symbol.root_element().type(), hrz_proto::SymbolElementType::ANCHOR_SYMBOL_ELEMENT);
            const auto& anchor = symbol.root_element().anchor();

            EXPECT_EQ(anchor.element_alignment().default_value().x(), 1);
            EXPECT_EQ(anchor.element_alignment().default_value().y(), 0);

            EXPECT_EQ(anchor.child().type(), hrz_proto::SymbolElementType::PADDING_SYMBOL_ELEMENT);
            const auto& padding = anchor.child().padding();

            EXPECT_EQ(padding.child().type(), hrz_proto::SymbolElementType::TEXT_SYMBOL_ELEMENT);
            const auto& text = padding.child().text();

            EXPECT_EQ(text.alignment().default_value(), hrz_proto::TextAlignment::RIGHT_ALIGNED);
        }
    };
}

TEST(MapboxTranslation, parse_vector_symbol_icon_text)
{
    SceneLayerStats expected_stats;
    expected_stats.vector_data_count = 1;
    expected_stats.vector_tiles_count = 1;

    hrz_proto::SceneDump scene_dump;
    convert_mapbox_resource(
        mapbox::Resources::VectorSymbolIconText, &scene_dump, expected_stats,
        mapbox::Resources::Sprite);

    for (int i = 0; i < scene_dump.layers_size(); i++)
    {
        const auto& layer = scene_dump.layers(i);
        if (layer.has_vector_tiles())
        {
            const auto& vtl = layer.vector_tiles();

            EXPECT_EQ(vtl.style().representations_size(), 1);
            EXPECT_EQ(
                vtl.style().representations(0).type(),
                hrz_proto::VectorReprType::SYMBOL_VECTOR_REPR);

            const auto& symbol = vtl.style().representations(0).symbol();

            EXPECT_EQ(
                symbol.root_element().type(), hrz_proto::SymbolElementType::STACK_SYMBOL_ELEMENT);
            const auto& stack = symbol.root_element().stack();

            EXPECT_EQ(stack.children_size(), 2);
            for (const auto& stack_child : stack.children())
            {
                EXPECT_EQ(stack_child.type(), hrz_proto::SymbolElementType::ANCHOR_SYMBOL_ELEMENT);
                const auto& anchor = stack_child.anchor();

                EXPECT_EQ(anchor.is_optional(), true);

                EXPECT_EQ(
                    anchor.child().type(), hrz_proto::SymbolElementType::PADDING_SYMBOL_ELEMENT);
                const auto& padding = anchor.child().padding();

                EXPECT_EQ(
                    padding.child().type(), hrz_proto::SymbolElementType::TRANSFORM_SYMBOL_ELEMENT);
                const auto& transform = padding.child().transform();

                EXPECT_TRUE(
                    transform.child().type() == hrz_proto::SymbolElementType::IMAGE_SYMBOL_ELEMENT
                    || transform.child().type()
                        == hrz_proto::SymbolElementType::TEXT_SYMBOL_ELEMENT);
            }
        }
    };
}

TEST(MapboxTranslation, parse_vector_symbol_icon_text_fit)
{
    SceneLayerStats expected_stats;
    expected_stats.vector_data_count = 1;
    expected_stats.vector_tiles_count = 1;

    hrz_proto::SceneDump scene_dump;
    convert_mapbox_resource(
        mapbox::Resources::VectorSymbolIconTextFit, &scene_dump, expected_stats,
        mapbox::Resources::Sprite);

    for (int i = 0; i < scene_dump.layers_size(); i++)
    {
        const auto& layer = scene_dump.layers(i);
        if (layer.has_vector_tiles())
        {
            const auto& vtl = layer.vector_tiles();

            EXPECT_EQ(vtl.style().representations_size(), 1);
            EXPECT_EQ(
                vtl.style().representations(0).type(),
                hrz_proto::VectorReprType::SYMBOL_VECTOR_REPR);

            const auto& symbol = vtl.style().representations(0).symbol();

            EXPECT_EQ(
                symbol.root_element().type(), hrz_proto::SymbolElementType::STACK_SYMBOL_ELEMENT);
            const auto& stack = symbol.root_element().stack();

            EXPECT_EQ(stack.children_size(), 2);
            for (const auto& stack_child : stack.children())
            {
                if (stack_child.type() == hrz_proto::SymbolElementType::STACK_EXPAND_SYMBOL_ELEMENT)
                {
                    const auto& stack_expand = stack_child.stack_expand();

                    EXPECT_EQ(
                        stack_expand.child().type(),
                        hrz_proto::SymbolElementType::ANCHOR_SYMBOL_ELEMENT);
                    const auto& anchor = stack_expand.child().anchor();

                    EXPECT_EQ(
                        anchor.child().type(),
                        hrz_proto::SymbolElementType::PADDING_SYMBOL_ELEMENT);
                    const auto& padding = anchor.child().padding();

                    EXPECT_EQ(
                        padding.child().type(),
                        hrz_proto::SymbolElementType::TRANSFORM_SYMBOL_ELEMENT);
                    const auto& transform = padding.child().transform();

                    EXPECT_EQ(
                        transform.child().type(),
                        hrz_proto::SymbolElementType::IMAGE_SYMBOL_ELEMENT);
                    const auto& image = transform.child().image();

                    EXPECT_EQ(image.fit_mode(), hrz_proto::BoxFit::BOX_FIT_FILL);
                    EXPECT_EQ(image.fit_axes(), hrz_proto::BoxFitAxes::BOX_FIT_AXES_BOTH);
                }
                else if (stack_child.type() == hrz_proto::SymbolElementType::PADDING_SYMBOL_ELEMENT)
                {
                    const auto& fit_padding = stack_child.padding();
                    EXPECT_FLOAT_EQ(fit_padding.top_padding().default_value(), 10.0f);
                    EXPECT_FLOAT_EQ(fit_padding.right_padding().default_value(), 20.0f);
                    EXPECT_FLOAT_EQ(fit_padding.bottom_padding().default_value(), 11.0f);
                    EXPECT_FLOAT_EQ(fit_padding.left_padding().default_value(), 21.0f);

                    EXPECT_EQ(
                        fit_padding.child().type(),
                        hrz_proto::SymbolElementType::ANCHOR_SYMBOL_ELEMENT);
                    const auto& anchor = fit_padding.child().anchor();

                    EXPECT_EQ(
                        anchor.child().type(),
                        hrz_proto::SymbolElementType::PADDING_SYMBOL_ELEMENT);
                    const auto& padding = anchor.child().padding();

                    EXPECT_EQ(
                        padding.child().type(),
                        hrz_proto::SymbolElementType::TRANSFORM_SYMBOL_ELEMENT);
                    const auto& transform = padding.child().transform();

                    EXPECT_EQ(
                        transform.child().type(),
                        hrz_proto::SymbolElementType::TEXT_SYMBOL_ELEMENT);
                }
                else
                {
                    FAIL() << "Unexpected stack child type";
                }
            }
        }
    };
}

TEST(MapboxTranslation, parse_vector_symbol_rotation_alignment)
{
    SceneLayerStats expected_stats;
    expected_stats.vector_data_count = 1;
    expected_stats.vector_tiles_count = 1;

    hrz_proto::SceneDump scene_dump;
    convert_mapbox_resource(
        mapbox::Resources::VectorSymbolRotationAlignment, &scene_dump, expected_stats,
        mapbox::Resources::Sprite);

    for (int i = 0; i < scene_dump.layers_size(); i++)
    {
        const auto& layer = scene_dump.layers(i);
        if (layer.has_vector_tiles())
        {
            const auto& vtl = layer.vector_tiles();

            EXPECT_EQ(vtl.style().representations_size(), 8);
            for (const auto& repr : vtl.style().representations())
            {
                EXPECT_EQ(repr.type(), hrz_proto::VectorReprType::SYMBOL_VECTOR_REPR);

                const auto& symbol = repr.symbol();

                EXPECT_EQ(
                    symbol.root_element().type(),
                    hrz_proto::SymbolElementType::ANCHOR_SYMBOL_ELEMENT);
                const auto& anchor = symbol.root_element().anchor();

                if (hrz::str::starts_with(repr.name(), "auto_alignment_with_point_placement")
                    || hrz::str::starts_with(repr.name(), "viewport_alignment"))
                {
                    EXPECT_EQ(
                        anchor.x_axis_alignment(),
                        hrz_proto::SymbolAxisAlignment::AXIS_ALIGNMENT_SCREEN);
                }
                else if (
                    hrz::str::starts_with(repr.name(), "auto_alignment_with_line_placement")
                    || hrz::str::starts_with(repr.name(), "map_alignment"))
                {
                    EXPECT_EQ(
                        anchor.x_axis_alignment(),
                        hrz_proto::SymbolAxisAlignment::AXIS_ALIGNMENT_WORLD);
                }
                else
                {
                    FAIL() << "Unexpected representation name";
                }
            }
        }
    };
}

TEST(MapboxTranslation, parse_vector_symbol_pitch_alignment)
{
    SceneLayerStats expected_stats;
    expected_stats.vector_data_count = 1;
    expected_stats.vector_tiles_count = 1;

    hrz_proto::SceneDump scene_dump;
    convert_mapbox_resource(
        mapbox::Resources::VectorSymbolPitchAlignment, &scene_dump, expected_stats,
        mapbox::Resources::Sprite);

    for (int i = 0; i < scene_dump.layers_size(); i++)
    {
        const auto& layer = scene_dump.layers(i);
        if (layer.has_vector_tiles())
        {
            const auto& vtl = layer.vector_tiles();
            EXPECT_EQ(vtl.style().representations_size(), 8);

            for (const auto& repr : vtl.style().representations())
            {
                EXPECT_EQ(repr.type(), hrz_proto::VectorReprType::SYMBOL_VECTOR_REPR);

                const auto& symbol = repr.symbol();

                EXPECT_EQ(
                    symbol.root_element().type(),
                    hrz_proto::SymbolElementType::ANCHOR_SYMBOL_ELEMENT);
                const auto& anchor = symbol.root_element().anchor();

                if (hrz::str::starts_with(repr.name(), "auto_alignment_with_point_placement")
                    || hrz::str::starts_with(repr.name(), "auto_alignment_with_line_placement"))
                {
                    EXPECT_EQ(anchor.x_axis_alignment(), anchor.y_axis_alignment());
                }
                else if (hrz::str::starts_with(repr.name(), "map_alignment"))
                {
                    EXPECT_EQ(
                        anchor.y_axis_alignment(),
                        hrz_proto::SymbolAxisAlignment::AXIS_ALIGNMENT_WORLD);
                }
                else if (hrz::str::starts_with(repr.name(), "viewport_alignment"))
                {
                    EXPECT_EQ(
                        anchor.y_axis_alignment(),
                        hrz_proto::SymbolAxisAlignment::AXIS_ALIGNMENT_SCREEN);
                }
                else
                {
                    FAIL() << "Unexpected representation name";
                }
            }
        }
    };
}

TEST(MapboxTranslation, parse_vector_layer_z_order)
{
    SceneLayerStats expected_stats;
    expected_stats.vector_data_count = 3;
    expected_stats.vector_tiles_count = 3;

    hrz_proto::SceneDump scene_dump;
    convert_mapbox_resource(mapbox::Resources::VectorLayerZOrder, &scene_dump, expected_stats);

    uint32_t flat_overlay_z_indices[3];
    uint32_t symbol_z_indices[2];

    for (int i = 0; i < scene_dump.layers_size(); i++)
    {
        const auto& layer = scene_dump.layers(i);
        if (layer.has_vector_tiles())
        {
            const auto& vtl = layer.vector_tiles();

            if (layer.name() == "hk_tilejson - HK_SAMPLE_3857")
            {
                EXPECT_EQ(vtl.style().representations_size(), 2);
                for (const auto& repr : vtl.style().representations())
                {
                    EXPECT_EQ(repr.type(), hrz_proto::VectorReprType::FLAT_OVERLAY_VECTOR_REPR);
                    if (repr.name() == "fill-1")
                    {
                        flat_overlay_z_indices[0] = repr.flat_overlay_geometry().z_index();
                    }
                    else if (repr.name() == "fill-2")
                    {
                        flat_overlay_z_indices[2] = repr.flat_overlay_geometry().z_index();
                    }
                    else
                    {
                        FAIL() << "Unexpected representation_name";
                    }
                }
            }
            else if (layer.name() == "osm_roads - osm_france_roads")
            {
                EXPECT_EQ(vtl.style().representations_size(), 1);
                EXPECT_EQ(vtl.style().representations(0).name(), "line");
                EXPECT_EQ(
                    vtl.style().representations(0).type(),
                    hrz_proto::VectorReprType::FLAT_OVERLAY_VECTOR_REPR);
                flat_overlay_z_indices[1] =
                    vtl.style().representations(0).flat_overlay_geometry().z_index();
            }
            else if (layer.name() == "grid")
            {
                EXPECT_EQ(vtl.style().representations_size(), 2);
                for (const auto& repr : vtl.style().representations())
                {
                    EXPECT_EQ(repr.type(), hrz_proto::VectorReprType::SYMBOL_VECTOR_REPR);
                    if (repr.name() == "circle-1")
                    {
                        symbol_z_indices[0] = repr.symbol().z_index();
                    }
                    else if (repr.name() == "circle-2")
                    {
                        symbol_z_indices[1] = repr.symbol().z_index();
                    }
                    else
                    {
                        FAIL() << "Unexpected representation_name";
                    }
                }
            }
            else
            {
                FAIL() << "Unexpected layer name";
            }
        }
    };

    for (int i = 0; i < 2; i++)
    {
        EXPECT_LT(flat_overlay_z_indices[i], flat_overlay_z_indices[i + 1]);
    }

    for (int i = 0; i < 1; i++)
    {
        EXPECT_LT(symbol_z_indices[i], symbol_z_indices[i + 1]);
    }
}

TEST(MapboxTranslation, parse_vector_layer_clamping)
{
    SceneLayerStats expected_stats;
    expected_stats.vector_data_count = 6;
    expected_stats.vector_tiles_count = 6;

    hrz_proto::SceneDump scene_dump;
    convert_mapbox_resource(mapbox::Resources::VectorLayerClamping, &scene_dump, expected_stats);

    for (int i = 0; i < scene_dump.layers_size(); i++)
    {
        const auto& layer = scene_dump.layers(i);
        if (layer.has_vector_tiles())
        {
            const auto& vtl = layer.vector_tiles();

            if (layer.name() == "line" || layer.name() == "fill")
            {
                EXPECT_EQ(vtl.clamping().method(), hrz_proto::VectorClampMode::NO_CLAMPING);
            }
            else if (layer.name() == "circle" || layer.name() == "fill+circle")
            {
                EXPECT_EQ(vtl.clamping().method(), hrz_proto::VectorClampMode::ANCHOR);
            }
            else if (layer.name() == "fill-extrusion" || layer.name() == "fill-extrusion+circle")
            {
                EXPECT_EQ(vtl.clamping().method(), hrz_proto::VectorClampMode::PER_VERTEX);
            }
            else
            {
                FAIL() << "Unexpected layer name";
            }
        }
    };
}

TEST(MapboxTranslation, parse_vector_heatmap_interpolate)
{
    SceneLayerStats expected_stats;
    expected_stats.vector_data_count = 1;
    expected_stats.vector_tiles_count = 1;

    hrz_proto::SceneDump scene_dump;
    convert_mapbox_resource(
        mapbox::Resources::VectorHeatmapInterpolate, &scene_dump, expected_stats);

    for (int i = 0; i < scene_dump.layers_size(); i++)
    {
        const auto& layer = scene_dump.layers(i);
        if (layer.has_vector_tiles())
        {
            const auto& vtl = layer.vector_tiles();

            EXPECT_EQ(vtl.source().vector_data_layer_id(), 1);
            EXPECT_EQ(vtl.source().override_bounds(), false);
            EXPECT_EQ(vtl.source().override_levels(), false);

            EXPECT_EQ(vtl.style().attributes_size(), 1);
            EXPECT_EQ(vtl.style().attributes(0).styling_name(), "mag");

            EXPECT_EQ(vtl.style().representations_size(), 1);
            EXPECT_EQ(
                vtl.style().representations(0).type(),
                hrz_proto::VectorReprType::HEATMAP_VECTOR_REPR);

            const auto& heatmap = vtl.style().representations(0).heatmap();

            EXPECT_EQ(
                heatmap.disc_radius_size_unit(),
                hrz_proto::InWorldSizeUnit::IN_WORLD_SIZE_IN_PIXELS);
            EXPECT_TRUE(heatmap.disc_radius().name().empty());
            EXPECT_FLOAT_EQ(heatmap.disc_radius().default_value(), 4.4f);

            const auto& palette = heatmap.numeric_palette();

            EXPECT_EQ(palette.interpolation_mode(), hrz_proto::ColorInterpolationMode::SRGB);
            EXPECT_FALSE(palette.has_nan_color());
            EXPECT_EQ(palette.color_points_size(), 5);

            {
                EXPECT_FLOAT_EQ(palette.color_points(0).value(), 0.0f);
                const auto& first_color = palette.color_points(0).first_color();
                EXPECT_FLOAT_EQ(first_color.r(), 1.0f);
                EXPECT_FLOAT_EQ(first_color.g(), 0.0f);
                EXPECT_FLOAT_EQ(first_color.b(), 0.0f);
                EXPECT_FLOAT_EQ(first_color.a(), 0.0f);
                const auto& second_color = palette.color_points(0).second_color();
                EXPECT_FLOAT_EQ(second_color.r(), 1.0f);
                EXPECT_FLOAT_EQ(second_color.g(), 0.0f);
                EXPECT_FLOAT_EQ(second_color.b(), 0.0f);
                EXPECT_FLOAT_EQ(second_color.a(), 0.0f);
            }

            {
                EXPECT_FLOAT_EQ(palette.color_points(1).value(), 0.25f);
                const auto& first_color = palette.color_points(1).first_color();
                EXPECT_FLOAT_EQ(first_color.r(), 1.0f);
                EXPECT_FLOAT_EQ(first_color.g(), 1.0f);
                EXPECT_FLOAT_EQ(first_color.b(), 0.0f);
                EXPECT_FLOAT_EQ(first_color.a(), 0.5f);
                const auto& second_color = palette.color_points(1).second_color();
                EXPECT_FLOAT_EQ(second_color.r(), 1.0f);
                EXPECT_FLOAT_EQ(second_color.g(), 1.0f);
                EXPECT_FLOAT_EQ(second_color.b(), 0.0f);
                EXPECT_FLOAT_EQ(second_color.a(), 0.5f);
            }

            {
                EXPECT_FLOAT_EQ(palette.color_points(2).value(), 0.5f);
                const auto& first_color = palette.color_points(2).first_color();
                EXPECT_FLOAT_EQ(first_color.r(), 0.0f);
                EXPECT_FLOAT_EQ(first_color.g(), 1.0f);
                EXPECT_FLOAT_EQ(first_color.b(), 0.0f);
                EXPECT_FLOAT_EQ(first_color.a(), 0.5f);
                const auto& second_color = palette.color_points(2).second_color();
                EXPECT_FLOAT_EQ(second_color.r(), 0.0f);
                EXPECT_FLOAT_EQ(second_color.g(), 1.0f);
                EXPECT_FLOAT_EQ(second_color.b(), 0.0f);
                EXPECT_FLOAT_EQ(second_color.a(), 0.5f);
            }

            {
                EXPECT_FLOAT_EQ(palette.color_points(3).value(), 0.75f);
                const auto& first_color = palette.color_points(3).first_color();
                EXPECT_FLOAT_EQ(first_color.r(), 0.0f);
                EXPECT_FLOAT_EQ(first_color.g(), 1.0f);
                EXPECT_FLOAT_EQ(first_color.b(), 1.0f);
                EXPECT_FLOAT_EQ(first_color.a(), 0.5f);
                const auto& second_color = palette.color_points(3).second_color();
                EXPECT_FLOAT_EQ(second_color.r(), 0.0f);
                EXPECT_FLOAT_EQ(second_color.g(), 1.0f);
                EXPECT_FLOAT_EQ(second_color.b(), 1.0f);
                EXPECT_FLOAT_EQ(second_color.a(), 0.5f);
            }

            {
                EXPECT_FLOAT_EQ(palette.color_points(4).value(), 1.0f);
                const auto& first_color = palette.color_points(4).first_color();
                EXPECT_FLOAT_EQ(first_color.r(), 0.0f);
                EXPECT_FLOAT_EQ(first_color.g(), 0.0f);
                EXPECT_FLOAT_EQ(first_color.b(), 1.0f);
                EXPECT_FLOAT_EQ(first_color.a(), 0.5f);
                const auto& second_color = palette.color_points(4).second_color();
                EXPECT_FLOAT_EQ(second_color.r(), 0.0f);
                EXPECT_FLOAT_EQ(second_color.g(), 0.0f);
                EXPECT_FLOAT_EQ(second_color.b(), 1.0f);
                EXPECT_FLOAT_EQ(second_color.a(), 0.5f);
            }
        }
        else if (layer.has_vector_data())
        {
            const auto& vdl = layer.vector_data();
            EXPECT_EQ(vdl.id(), 1);

            EXPECT_EQ(vdl.sources_size(), 1);

            const auto& source = vdl.sources(0);
            EXPECT_EQ(
                source.provider_type(),
                hrz_proto::VectorDataProviderType::UNTILED_VECTOR_DATA_PROVIDER);
            EXPECT_EQ(source.has_geometry(), true);
            EXPECT_EQ(source.attributes_size(), 1);

            const auto& provider = source.untiled_data_provider();
            EXPECT_STREQ(
                provider.url().c_str(),
                "https://redacted.localhost/assets/geojson/earthquakes.geojson");
            EXPECT_EQ(provider.format(), hrz_proto::VectorDataFormat::GEOJSON_VECTOR_DATA);

            const auto& attribute = source.attributes(0);
            EXPECT_STREQ(attribute.source_name().c_str(), "mag");
        }
    };
}

TEST(MapboxTranslation, parse_vector_heatmap_step)
{
    SceneLayerStats expected_stats;
    expected_stats.vector_data_count = 1;
    expected_stats.vector_tiles_count = 1;

    hrz_proto::SceneDump scene_dump;
    convert_mapbox_resource(mapbox::Resources::VectorHeatmapStep, &scene_dump, expected_stats);

    for (int i = 0; i < scene_dump.layers_size(); i++)
    {
        const auto& layer = scene_dump.layers(i);
        if (layer.has_vector_tiles())
        {
            const auto& vtl = layer.vector_tiles();

            EXPECT_EQ(vtl.source().vector_data_layer_id(), 1);
            EXPECT_EQ(vtl.source().override_bounds(), false);
            EXPECT_EQ(vtl.source().override_levels(), false);

            EXPECT_EQ(vtl.style().attributes_size(), 1);
            EXPECT_EQ(vtl.style().attributes(0).styling_name(), "mag");

            EXPECT_EQ(vtl.style().representations_size(), 1);
            EXPECT_EQ(
                vtl.style().representations(0).type(),
                hrz_proto::VectorReprType::HEATMAP_VECTOR_REPR);

            const auto& heatmap = vtl.style().representations(0).heatmap();

            EXPECT_EQ(
                heatmap.disc_radius_size_unit(),
                hrz_proto::InWorldSizeUnit::IN_WORLD_SIZE_IN_PIXELS);
            EXPECT_TRUE(heatmap.disc_radius().name().empty());
            EXPECT_FLOAT_EQ(heatmap.disc_radius().default_value(), 4.4f);

            const auto& palette = heatmap.numeric_palette();

            EXPECT_EQ(palette.interpolation_mode(), hrz_proto::ColorInterpolationMode::THRESHOLD);
            EXPECT_FALSE(palette.has_nan_color());
            EXPECT_EQ(palette.color_points_size(), 4);

            {
                EXPECT_FLOAT_EQ(palette.color_points(0).value(), 0.25f);
                const auto& first_color = palette.color_points(0).first_color();
                EXPECT_FLOAT_EQ(first_color.r(), 1.0f);
                EXPECT_FLOAT_EQ(first_color.g(), 0.0f);
                EXPECT_FLOAT_EQ(first_color.b(), 0.0f);
                EXPECT_FLOAT_EQ(first_color.a(), 0.0f);
                const auto& second_color = palette.color_points(0).second_color();
                EXPECT_FLOAT_EQ(second_color.r(), 1.0f);
                EXPECT_FLOAT_EQ(second_color.g(), 1.0f);
                EXPECT_FLOAT_EQ(second_color.b(), 0.0f);
                EXPECT_FLOAT_EQ(second_color.a(), 0.5f);
            }

            {
                EXPECT_FLOAT_EQ(palette.color_points(1).value(), 0.5f);
                const auto& first_color = palette.color_points(1).first_color();
                EXPECT_FLOAT_EQ(first_color.r(), 1.0f);
                EXPECT_FLOAT_EQ(first_color.g(), 1.0f);
                EXPECT_FLOAT_EQ(first_color.b(), 0.0f);
                EXPECT_FLOAT_EQ(first_color.a(), 0.5f);
                const auto& second_color = palette.color_points(1).second_color();
                EXPECT_FLOAT_EQ(second_color.r(), 0.0f);
                EXPECT_FLOAT_EQ(second_color.g(), 1.0f);
                EXPECT_FLOAT_EQ(second_color.b(), 0.0f);
                EXPECT_FLOAT_EQ(second_color.a(), 0.5f);
            }

            {
                EXPECT_FLOAT_EQ(palette.color_points(2).value(), 0.75f);
                const auto& first_color = palette.color_points(2).first_color();
                EXPECT_FLOAT_EQ(first_color.r(), 0.0f);
                EXPECT_FLOAT_EQ(first_color.g(), 1.0f);
                EXPECT_FLOAT_EQ(first_color.b(), 0.0f);
                EXPECT_FLOAT_EQ(first_color.a(), 0.5f);
                const auto& second_color = palette.color_points(2).second_color();
                EXPECT_FLOAT_EQ(second_color.r(), 0.0f);
                EXPECT_FLOAT_EQ(second_color.g(), 1.0f);
                EXPECT_FLOAT_EQ(second_color.b(), 1.0f);
                EXPECT_FLOAT_EQ(second_color.a(), 0.5f);
            }

            {
                EXPECT_FLOAT_EQ(palette.color_points(3).value(), 1.0f);
                const auto& first_color = palette.color_points(3).first_color();
                EXPECT_FLOAT_EQ(first_color.r(), 0.0f);
                EXPECT_FLOAT_EQ(first_color.g(), 1.0f);
                EXPECT_FLOAT_EQ(first_color.b(), 1.0f);
                EXPECT_FLOAT_EQ(first_color.a(), 0.5f);
                const auto& second_color = palette.color_points(3).second_color();
                EXPECT_FLOAT_EQ(second_color.r(), 0.0f);
                EXPECT_FLOAT_EQ(second_color.g(), 0.0f);
                EXPECT_FLOAT_EQ(second_color.b(), 1.0f);
                EXPECT_FLOAT_EQ(second_color.a(), 0.5f);
            }
        }
        else if (layer.has_vector_data())
        {
            const auto& vdl = layer.vector_data();
            EXPECT_EQ(vdl.id(), 1);

            EXPECT_EQ(vdl.sources_size(), 1);

            const auto& source = vdl.sources(0);
            EXPECT_EQ(
                source.provider_type(),
                hrz_proto::VectorDataProviderType::UNTILED_VECTOR_DATA_PROVIDER);
            EXPECT_EQ(source.has_geometry(), true);
            EXPECT_EQ(source.attributes_size(), 1);

            const auto& provider = source.untiled_data_provider();
            EXPECT_STREQ(
                provider.url().c_str(),
                "https://redacted.localhost/assets/geojson/earthquakes.geojson");
            EXPECT_EQ(provider.format(), hrz_proto::VectorDataFormat::GEOJSON_VECTOR_DATA);

            const auto& attribute = source.attributes(0);
            EXPECT_STREQ(attribute.source_name().c_str(), "mag");
        }
    };
}
