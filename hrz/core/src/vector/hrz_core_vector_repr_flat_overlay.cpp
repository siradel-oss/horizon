#include "hrz_common_monitoring_defs.h"
#include "hrz_core_channel_group.h"
#include "hrz_core_selection_storage.h"
#include "hrz_core_shaders.h"
#include "vector/hrz_core_vector_flat_overlay.h"
#include "vector/hrz_core_vector_image_loader.h"
#include "vector/hrz_core_vector_repr.h"

#include <hrz_common_blob_array.h>
#include <hrz_common_fmt.h>
#include <hrz_common_monitoring_defs.h>
#include <hrz_common_proto_maths.h>
#include <hrz_common_style.h>
#include <hrz_common_vector_data.h>
#include <hrz_common_vector_tiles.h>
#include <hrz_fnd_gen_object_pool.h>
#include <hrz_fnd_hash.h>
#include <hrz_fnd_mem.h>
#include <hrz_fnd_meta.h>
#include <hrz_fnd_static_vector.h>
#include <hrz_fnd_thread.h>
#include <hrz_jobs_tickets.h>

#include <array>
#include <optional>

// Dashed lines -- how do they work?
//
// Each point has a "progress" which is how far along the polyline it's located.
// However it's not a number in meters, instead it's divided by the period of
// the dashing such that the space between two integers coordinates is the
// period of a dash and its corresponding gap.
//
// It's also possible to configure the dash ratio. For example if it's 0.3, the
// dash will be displayed between x.0 and x.3, and the gap will be between x.3
// and (x + 1).0, where x is an integer progress coordinate.
//
// At the beginning of a segment, the progress coordinate is normalized so that
// it's between 0 and 1, in order to avoid dealing with loss of precision for
// huge polylines.

namespace
{
enum
{
    UboTileParams = hrz::vector_flat_overlay::UboVectorOverlayPass + 1,

    InputStreamInTilePos = 0,
    InputStreamInMeshPos = 0,
    InputStreamColor = 1,

    InputStreamPolygonUv = 2,
    InputStreamPolygonInTileLat = 3,
    InputStreamPolygonPatternStyleIndex = 4,
    InputStreamPolygonFeatureIndex = 5,

    // This stores the width as x, the dash progress of first and second point
    // in y and z, and the dash length in w. See comments above for a description
    // of how dashed lines work.
    InputStreamLineWidth = 2,
    InputStreamLineGeometry = 3,
    InputStreamLinePos0 = 4,
    InputStreamLinePos1 = 5,
    InputStreamLineNormal0 = 6,
    InputStreamLineNormal1 = 7,
    InputStreamLineAnimationSpeed = 8,
    InputStreamLineEmptyColor = 9,
    InputStreamLineTotalLength = 10,
    InputStreamLineFeatureIndex = 11,

    InputStreamPointsPosition = 1,
    InputStreamPointsColor = 2,
    InputStreamPointsRadius = 3,
    InputStreamPointsFeatureIndex = 4,

    SamplerSelection = hrz::SamplerCustomStart,
    SamplerFeatureIds,
    SamplerPattern,
    SamplerPatternStyle,
};

struct TileUniformData
{
    lm::vec4 center_low;
    lm::vec4 center_high;
    lm::uvec3 feature_reference;
    hrz::bool32 has_feature_ids;
    lm::uvec2 object_reference;
    float disc_outline_width;
    uint32_t _padding2;
    lm::vec4 disc_outline_color;
    uint32_t polyline_sides;
    uint32_t line_width_unit;
    uint32_t dash_mode;
    uint32_t dash_period_unit;
    uint32_t dash_length_unit;
    uint32_t animation_speed_unit;
    lm::vec2 origin_uv_low;
    lm::vec2 origin_uv_high;
    float origin_lat;
    float lat_span;
    uint32_t polygon_pattern_size_unit;
    uint32_t polygon_pattern_tiling_type;
    uint32_t polygon_pattern_reference_latitude_type;
    float polygon_pattern_reference_lat_scale_factor_offset;
    uint32_t polygon_pattern_color_blend_mode;
    uint32_t disc_radius_unit;
    uint32_t _padding[2];
};

HRZ_CHECK_UBO_SIZE(TileUniformData);

struct DrawReport
{
    size_t features_drawn;
    size_t animated_features_drawn;

    void reset()
    {
        features_drawn = 0;
        animated_features_drawn = 0;
    }
};

struct RenderableFeatures : public my::Renderer::Renderable
{
    my::Renderer::BinMask bin_mask;
    my::Renderer::ViewMask main_views;
    lm::dvec3 clamped_center;
    double clamped_radius;
    lm::dvec3 sea_center;
    double sea_radius;
    uint32_t z_index;

    my::ResourceHandle polygons_vertex_buffer = my::ResourceHandle::null();
    my::ResourceHandle polygons_index_buffer = my::ResourceHandle::null();
    my::ResourceHandle polylines_instance_buffer = my::ResourceHandle::null();
    my::ResourceHandle points_instance_buffer = my::ResourceHandle::null();

    my::ResourceHandle feature_id_texture = my::ResourceHandle::null();
    my::ResourceHandle selection_texture = my::ResourceHandle::null();

    hrz::selection::SelectionStorageUint32TextureMultiIndex selection_storage;

    struct PolygonsRenderData
    {
        my::ResourceHandle vertex_input = my::ResourceHandle::null();
        my::ResourceHandle shader = my::ResourceHandle::null();
        my::ResourceHandle picking_shader = my::ResourceHandle::null();
        my::ResourceHandle selection_shader = my::ResourceHandle::null();
        my::ResourceHandle pattern_texture = my::ResourceHandle::null();
        my::ResourceHandle pattern_sampler = my::ResourceHandle::null();
        my::ResourceHandle pattern_style_texture = my::ResourceHandle::null();
        my::ResourceHandle feature_id_texture = my::ResourceHandle::null();
        my::ResourceHandle selection_texture = my::ResourceHandle::null();
        my::ResourceHandle metadata_sampler = my::ResourceHandle::null();
        uint32_t vertex_count = 0;
        bool has_selected_features = false;
    };

    struct PolylinesRenderData
    {
        my::ResourceHandle vertex_input = my::ResourceHandle::null();
        my::ResourceHandle shader = my::ResourceHandle::null();
        my::ResourceHandle picking_shader = my::ResourceHandle::null();
        my::ResourceHandle selection_shader = my::ResourceHandle::null();
        my::ResourceHandle feature_id_texture = my::ResourceHandle::null();
        my::ResourceHandle selection_texture = my::ResourceHandle::null();
        my::ResourceHandle metadata_sampler = my::ResourceHandle::null();
        uint32_t instance_count = 0;
        uint32_t vertex_count = 0;
        bool has_selected_features = false;
    };

    struct PointsRenderData
    {
        my::ResourceHandle vertex_input = my::ResourceHandle::null();
        my::ResourceHandle shader = my::ResourceHandle::null();
        my::ResourceHandle picking_shader = my::ResourceHandle::null();
        my::ResourceHandle selection_shader = my::ResourceHandle::null();
        my::ResourceHandle feature_id_texture = my::ResourceHandle::null();
        my::ResourceHandle selection_texture = my::ResourceHandle::null();
        my::ResourceHandle metadata_sampler = my::ResourceHandle::null();
        uint32_t instance_count = 0;
        uint32_t vertex_count = 0;
        bool has_selected_features = false;
    };

    struct FeatureRenderData
    {
        my::ResourceHandle ubo_buffer = my::ResourceHandle::null();

        PolygonsRenderData polygons_data;
        PolylinesRenderData polylines_data;
        PointsRenderData points_data;

        DrawReport* draw_report;
        bool is_animated = false;

        uint32_t scene_views = 0;
    };

    FeatureRenderData data;

    static void render_callback(
        uint32_t render_type,
        my::RenderContext* r,
        my::ResourceBinder* rb,
        const void* user_data_raw,
        const void* raw_data)
    {
        auto data = (const FeatureRenderData*)raw_data;
        const auto* user_data = (const hrz::SceneViewRenderGraphUserData*)user_data_raw;

        if (((1 << user_data->scene_view) & data->scene_views) == 0) return;

        bool draw_polygons = data->polygons_data.vertex_count > 0;
        bool draw_polylines = data->polylines_data.instance_count > 0;
        bool draw_points = data->points_data.instance_count > 0;

        my::ResourceHandle polygons_shader;
        my::ResourceHandle polylines_shader;
        my::ResourceHandle points_shader;
        switch (render_type)
        {
            case hrz::RenderVisual:
                polygons_shader = data->polygons_data.shader;
                polylines_shader = data->polylines_data.shader;
                points_shader = data->points_data.shader;
                break;
            case hrz::RenderPicking:
                polygons_shader = data->polygons_data.picking_shader;
                polylines_shader = data->polylines_data.picking_shader;
                points_shader = data->points_data.picking_shader;
                break;
            case hrz::RenderSelection:
                draw_polygons &= data->polygons_data.has_selected_features;
                draw_polylines &= data->polylines_data.has_selected_features;
                draw_points &= data->points_data.has_selected_features;
                polygons_shader = data->polygons_data.selection_shader;
                polylines_shader = data->polylines_data.selection_shader;
                points_shader = data->points_data.selection_shader;
                break;
            default: return;
        }

        if (!draw_polygons && !draw_polylines && !draw_points)
        {
            return;
        }

        auto polygons_batch =
            my::DrawBatchInfo(my::PrimitiveType::TriangleList, data->polygons_data.vertex_count);

        auto polylines_batch =
            my::DrawBatchInfo(my::PrimitiveType::TriangleList, data->polylines_data.vertex_count)
                .instanced(data->polylines_data.instance_count);

        auto points_batch =
            my::DrawBatchInfo(my::PrimitiveType::TriangleStrip, data->points_data.vertex_count)
                .instanced(data->points_data.instance_count);

        rb->push_state();

        if (draw_polygons)
        {
            my::UboBinding ubo_bindings[] = {
                {UboTileParams, data->ubo_buffer, 0, sizeof(TileUniformData)}};
            rb->bind(HRZ_ARRAY_COUNT(ubo_bindings), ubo_bindings);

            hrz::StaticVector<my::TextureBinding, 3> texture_bindings;

            if (!data->polygons_data.pattern_texture.is_null())
            {
                texture_bindings.push_back(
                    {SamplerPattern, data->polygons_data.pattern_texture,
                     data->polygons_data.pattern_sampler});
                texture_bindings.push_back(
                    {SamplerPatternStyle, data->polygons_data.pattern_style_texture,
                     data->polygons_data.metadata_sampler});
            }

            if (render_type == hrz::RenderVisual)
            {
                texture_bindings.push_back(
                    {SamplerFeatureIds, data->polygons_data.feature_id_texture,
                     data->polygons_data.metadata_sampler});
            }
            else if (render_type == hrz::RenderSelection)
            {
                texture_bindings.push_back(
                    {SamplerSelection, data->polygons_data.selection_texture,
                     data->polygons_data.metadata_sampler});
            }

            if (!texture_bindings.empty())
            {
                rb->bind(texture_bindings.size(), texture_bindings.data());
            }

            auto state = rb->get_current_state();
            r->draw(
                polygons_batch, polygons_shader, data->polygons_data.vertex_input, state.ubo_count,
                state.ubos, state.texture_count, state.textures);
        }

        if (draw_polylines)
        {
            my::UboBinding ubo_bindings[] = {
                {UboTileParams, data->ubo_buffer, 0, sizeof(TileUniformData)}};
            rb->bind(HRZ_ARRAY_COUNT(ubo_bindings), ubo_bindings);

            if (render_type == hrz::RenderVisual)
            {
                my::TextureBinding texture_bindings[] = {
                    {SamplerFeatureIds, data->polylines_data.feature_id_texture,
                     data->polylines_data.metadata_sampler}};
                rb->bind(HRZ_ARRAY_COUNT(texture_bindings), texture_bindings);
            }
            else if (render_type == hrz::RenderSelection)
            {
                my::TextureBinding texture_bindings[] = {
                    {SamplerSelection, data->polylines_data.selection_texture,
                     data->polylines_data.metadata_sampler}};
                rb->bind(HRZ_ARRAY_COUNT(texture_bindings), texture_bindings);
            }

            auto state = rb->get_current_state();
            r->draw(
                polylines_batch, polylines_shader, data->polylines_data.vertex_input,
                state.ubo_count, state.ubos, state.texture_count, state.textures);
        }

        if (draw_points)
        {
            my::UboBinding ubo_bindings[] = {
                {UboTileParams, data->ubo_buffer, 0, sizeof(TileUniformData)},
            };
            rb->bind(HRZ_ARRAY_COUNT(ubo_bindings), ubo_bindings);

            if (render_type == hrz::RenderVisual)
            {
                my::TextureBinding texture_bindings[] = {
                    {SamplerFeatureIds, data->points_data.feature_id_texture,
                     data->points_data.metadata_sampler}};
                rb->bind(HRZ_ARRAY_COUNT(texture_bindings), texture_bindings);
            }
            else if (render_type == hrz::RenderSelection)
            {
                my::TextureBinding texture_bindings[] = {
                    {SamplerSelection, data->points_data.selection_texture,
                     data->points_data.metadata_sampler}};
                rb->bind(HRZ_ARRAY_COUNT(texture_bindings), texture_bindings);
            }

            auto state = rb->get_current_state();
            r->draw(
                points_batch, points_shader, data->points_data.vertex_input, state.ubo_count,
                state.ubos, state.texture_count, state.textures);
        }

        data->draw_report->features_drawn += 1;
        data->draw_report->animated_features_drawn += data->is_animated ? 1 : 0;

        rb->pop_state();
    }

    void collect_render_info(my::Renderer::Queue& queue, const my::Renderer::Culler& culler)
        const override
    {
        if (culler.is_visible_in_some_views(clamped_center, clamped_radius, main_views)
            && culler.is_visible_in_any_view(sea_center, sea_radius, bin_mask))
        {
            queue.enqueue(bin_mask, render_callback, &data, sea_center, sea_radius, z_index);
        }
    }
};

struct Sprite
{
    std::string name;
    lm::uvec2 size;
    lm::uvec2 offset;
};

struct Config
{
    enum class Status
    {
        Loading,
        Ready,
        Error,
    };

    Status status;
    uint64_t layer_id;
    uint32_t repr_id;

    uint64_t line_width_prp = hrz::style::Parser::INVALID_PROPERTY;
    uint64_t color_prp = hrz::style::Parser::INVALID_PROPERTY;
    uint64_t disc_radius_prp = hrz::style::Parser::INVALID_PROPERTY;
    uint64_t dash_length_prp = hrz::style::Parser::INVALID_PROPERTY;
    uint64_t dash_period_prp = hrz::style::Parser::INVALID_PROPERTY;
    uint64_t animation_speed_prp = hrz::style::Parser::INVALID_PROPERTY;
    uint64_t empty_color_prp = hrz::style::Parser::INVALID_PROPERTY;

    lm::vec4 default_color = {0, 0, 0, 0};

    bool clip_to_tile = false;
    bool polygons_outline = false;

    double default_line_width = 0;
    bool round_tips = false;
    hrz_proto::DashMode dash_mode;
    hrz_proto::DashSizeUnit dash_period_unit;
    hrz_proto::DashSizeUnit dash_length_unit;
    hrz_proto::DashSizeUnit animation_speed_unit;
    float default_dash_period = 0.0f;
    float default_dash_length = 0.0f;
    float default_animation_speed = 0.0f;
    lm::vec4 default_empty_color = {0, 0, 0, 0};
    hrz_proto::PolylineSide polyline_side;
    hrz_proto::InWorldSizeUnit line_width_unit;

    float default_disc_radius = 0;
    hrz_proto::InWorldSizeUnit disc_radius_unit;
    float disc_outline_width = 0;
    lm::vec4 disc_outline_color = {0, 0, 0, 0};

    std::string pattern_image_url;
    hrz_proto::HttpHeaderList pattern_image_headers;
    std::vector<Sprite> pattern_sprites;
    hrz::vt::image_loader::ImageH pattern_image = 0;
    my::ResourceHandle pattern_texture = my::ResourceHandle::null();
    lm::uvec2 pattern_texture_size = {0, 0};

    uint32_t default_polygon_pattern_sprite_index = 0;
    uint64_t polygon_pattern_sprite_index_prp = hrz::style::Parser::INVALID_PROPERTY;
    std::string default_polygon_pattern_sprite_name;
    uint64_t polygon_pattern_sprite_name_prp = hrz::style::Parser::INVALID_PROPERTY;
    lm::vec2 default_polygon_pattern_size = {0, 0};
    lm::ulvec2 polygon_pattern_size_prp = {
        hrz::style::Parser::INVALID_PROPERTY, hrz::style::Parser::INVALID_PROPERTY};
    hrz_proto::PolygonPatternSizeUnit polygon_pattern_size_unit;
    float default_polygon_pattern_rotation = 0.0f;
    uint64_t polygon_pattern_rotation_prp = hrz::style::Parser::INVALID_PROPERTY;
    hrz_proto::PolygonPatternTilingType polygon_pattern_tiling_type;
    hrz_proto::PolygonPatternReferenceLatitudeType polygon_pattern_reference_latitude_type;
    float polygon_pattern_reference_latitude = 0.0f;
    lm::vec4 default_polygon_pattern_color = {0, 0, 0, 0};
    uint64_t polygon_pattern_color_prp = hrz::style::Parser::INVALID_PROPERTY;
    hrz_proto::BlendMode polygon_pattern_color_blend_mode;
    float default_polygon_pattern_color_blend_strength = 0.0f;
    uint64_t polygon_pattern_color_blend_strength_prp = hrz::style::Parser::INVALID_PROPERTY;

    uint32_t z_index = 0;

    uint32_t scene_views = 0;
};

struct TileGeometry
{
    lm::dvec3 sea_center;
    lm::dvec3 clamped_center;
    double sea_radius;
    double clamped_radius;

    // Positions are relative to the centre of the tile.
    std::variant<
        hrz::BlobArray<hrz::vt::FlatVectorGeometry::SolidColorPolygonVertex>,
        hrz::BlobArray<hrz::vt::FlatVectorGeometry::PatternPolygonVertex>>
        polygon_data;
    hrz::BlobArray<uint32_t> polygon_indices;
    hrz::BlobArray<hrz::vt::FlatVectorGeometry::PolylineInstance> polyline_data;
    hrz::BlobArray<hrz::vt::FlatVectorGeometry::PointInstance> point_data;
    hrz::BlobArray<hrz::vector_data::FeatureIdHash> feature_ids;

    uint32_t max_feature_index;
    bool is_animated;

    lm::dvec2 origin_uv;
    double origin_lat;
    double lat_span;

    hrz::BlobArray<hrz::vt::FlatVectorGeometry::PolygonPatternStyle> polygon_pattern_style_data;
};

struct TileId
{
    uint64_t channel_id;
    uint64_t tile_id;

    bool operator==(const TileId& other) const
    {
        return other.channel_id == channel_id && other.tile_id == tile_id;
    }

    template<typename H>
    friend H AbslHashValue(H h, const TileId& request)
    {
        return H::combine(std::move(h), request.channel_id, request.tile_id);
    }
};

struct Tile
{
    enum class Status
    {
        WaitingForConfig,
        ReadyToBake,
        Baking,
        FinishedBaking,
        Ready,
        Error,
    };

    std::optional<TileId> id;
    uint64_t layer_id;
    hrz::TileCoords coords;
    Status status;

    bool has_feature_ids;

    double min_elevation = 0.0;
    double max_elevation = 0.0;
    std::optional<lm::dbbox2> wmerc_bounds = std::nullopt;

    hrz_jobs::BakeFlatVectorGeometryTicket bake_ticket;
    std::optional<hrz::vt::FlatVectorData> bake_data;
    std::optional<TileGeometry> geometry;
    std::optional<RenderableFeatures> renderable;

    TileUniformData ubo;
    bool round_tips;
    my::ResourceHandle pattern_texture;
    bool has_polygon_pattern;
    uint32_t z_index;
    uint32_t scene_views;
};

class FlatOverlayReprSystem : public hrz::vt::ReprSystem
{
    using ConfigH = uint64_t;
    using TileH = uint64_t;

    using ConfigIndexPool = hrz::GenIndexPool<ConfigH, 32, 32>;
    using ConfigPool = hrz::GenObjectPool<Config, ConfigIndexPool, 64>;

    using TileIndexPool = hrz::GenIndexPool<TileH, 32, 32>;
    using TilePool = hrz::GenObjectPool<Tile, TileIndexPool, 64>;

    my::ResourceHandle _line_vertex_buffer;
    my::ResourceHandle _point_vertex_buffer;

    my::ResourceHandle _empty_feature_id_texture;
    hrz::selection::SelectionStorageUint32TextureMultiIndex _empty_selection_storage;

    my::ResourceHandle _polygons_solid_color_visual_shader;
    my::ResourceHandle _polygons_solid_color_picking_shader;
    my::ResourceHandle _polygons_solid_color_selection_shader;
    my::ResourceHandle _polygons_pattern_visual_shader;
    my::ResourceHandle _polygons_pattern_picking_shader;
    my::ResourceHandle _polygons_pattern_selection_shader;
    my::ResourceHandle _polylines_square_visual_shader;
    my::ResourceHandle _polylines_square_picking_shader;
    my::ResourceHandle _polylines_square_selection_shader;
    my::ResourceHandle _polylines_round_visual_shader;
    my::ResourceHandle _polylines_round_picking_shader;
    my::ResourceHandle _polylines_round_selection_shader;
    my::ResourceHandle _points_visual_shader;
    my::ResourceHandle _points_picking_shader;
    my::ResourceHandle _points_selection_shader;
    my::ResourceHandle _pattern_sampler;
    my::ResourceHandle _metadata_sampler;

    ConfigPool _configs;

    struct ConfigId
    {
        uint64_t channel_id;
        uint64_t config_id;

        bool operator==(const ConfigId& other) const
        {
            return other.channel_id == channel_id && other.config_id == config_id;
        }

        template<typename H>
        friend H AbslHashValue(H h, const ConfigId& request)
        {
            return H::combine(std::move(h), request.channel_id, request.config_id);
        }
    };

    hrz::flat_hash_map<ConfigId, ConfigH> _configs_by_id;

    TilePool _tiles;

    hrz::flat_hash_map<TileId, TileH> _tiles_by_id;

    hrz::flat_hash_set<ConfigH> _loading_configs;

    DrawReport _draw_report;
    size_t _frames_since_last_animation_draw = 0;

    hrz::flat_hash_set<std::pair<TileH, uint32_t>> _drawn_tiles;

    hrz::flat_hash_map<TileH, ConfigH> _waiting_for_config;
    hrz::flat_hash_set<TileH> _to_bake;
    hrz::flat_hash_set<TileH> _baking;
    hrz::flat_hash_set<TileH> _finished_baking;
    hrz::flat_hash_set<TileH> _removed;

    std::vector<hrz::vt::image_loader::ImageH> _images_to_free;
    std::vector<my::ResourceHandle> _resources_to_free;

    hrz::ChannelGroup<hrz::vt::FromReprMessage, hrz::vt::ToReprMessage> _channels;

public:
    ~FlatOverlayReprSystem() override = default;

    void deinit(WorkCtx& ctx, hrz::Render*) override { work_removed_tiles(ctx); }

    static void collect_shaders(hrz::GpuResourceContext* rc)
    {
        static const my::IndexName ubos[] = {
            {hrz::UboFrame, "Frame"},
            {hrz::vector_flat_overlay::UboVectorOverlayCameras, "OverlayCamerasUniform"},
            {hrz::vector_flat_overlay::UboVectorOverlayPass, "OverlayPasses"},
            {UboTileParams, "Tile"},
        };

        const char* color_outputs[] = {"o_color"};
        const char* picking_outputs[] = {"o_object_reference"};
        const char* selection_outputs[] = {"o_highlight"};

        {
            my::IndexName solid_color_attribs[] = {
                {InputStreamInTilePos, "i_in_tile_pos"},
                {InputStreamColor, "i_color"},
                {InputStreamPolygonFeatureIndex, "i_feature_index"}};

            my::IndexName pattern_attribs[] = {
                {InputStreamInTilePos, "i_in_tile_pos"},
                {InputStreamColor, "i_color"},
                {InputStreamPolygonUv, "i_uv"},
                {InputStreamPolygonInTileLat, "i_in_tile_lat"},
                {InputStreamPolygonPatternStyleIndex, "i_pattern_style_index"},
                {InputStreamPolygonFeatureIndex, "i_feature_index"}};

            my::IndexName solid_color_visual_samplers[] = {
                {hrz::SamplerCameraHeight, "u_camera_height"},
                {SamplerFeatureIds, "u_feature_ids"},
            };

            my::IndexName pattern_visual_samplers[] = {
                {hrz::SamplerCameraHeight, "u_camera_height"},
                {SamplerFeatureIds, "u_feature_ids"},
                {SamplerPattern, "u_pattern"},
                {SamplerPatternStyle, "u_pattern_styles"},
            };

            my::IndexName solid_color_picking_samplers[] = {
                {hrz::SamplerCameraHeight, "u_camera_height"},
            };

            my::IndexName pattern_picking_samplers[] = {
                {hrz::SamplerCameraHeight, "u_camera_height"},
                {SamplerPattern, "u_pattern"},
                {SamplerPatternStyle, "u_pattern_styles"},
            };

            my::IndexName solid_color_selection_samplers[] = {
                {hrz::SamplerCameraHeight, "u_camera_height"},
                {SamplerSelection, "u_selection"},
            };

            my::IndexName pattern_selection_samplers[] = {
                {hrz::SamplerCameraHeight, "u_camera_height"},
                {SamplerSelection, "u_selection"},
                {SamplerPattern, "u_pattern"},
                {SamplerPatternStyle, "u_pattern_styles"},
            };

            my::ShaderResource res{};
            res.name = hrz_shaders::FlatPolygonsSolidColor_name;
            res.vertex_source_len = hrz_shaders::FlatPolygonsSolidColor_vert_len;
            res.vertex_source = hrz_shaders::FlatPolygonsSolidColor_vert;
            res.fragment_source_len = hrz_shaders::FlatPolygonsSolidColor_frag_len;
            res.fragment_source = hrz_shaders::FlatPolygonsSolidColor_frag;
            res.attrib_count = HRZ_ARRAY_COUNT(solid_color_attribs);
            res.attribs = solid_color_attribs;
            res.uniform_block_count = HRZ_ARRAY_COUNT(ubos);
            res.uniform_blocks = ubos;
            res.sampler_count = HRZ_ARRAY_COUNT(solid_color_visual_samplers);
            res.samplers = solid_color_visual_samplers;
            res.output_count = HRZ_ARRAY_COUNT(color_outputs);
            res.outputs = color_outputs;

            res.initial_state.depth.test = true;

            res.initial_state.color_blend.enable = true;
            res.initial_state.color_blend.color.op = my::ColorBlendState::Add;
            res.initial_state.color_blend.color.src = my::ColorBlendState::One;
            res.initial_state.color_blend.color.dst = my::ColorBlendState::OneMinusSrcAlpha;
            res.initial_state.color_blend.alpha.op = my::ColorBlendState::Add;
            res.initial_state.color_blend.alpha.src = my::ColorBlendState::One;
            res.initial_state.color_blend.alpha.dst = my::ColorBlendState::OneMinusSrcAlpha;
            res.initial_state.rasterization.cull_mode = my::RasterizationState::CullMode::None;

            rc->alloc(&res, hrz::monitoring::systems::FlatOverlays);

            res.name = hrz_shaders::FlatPolygonsPattern_name;
            res.vertex_source_len = hrz_shaders::FlatPolygonsPattern_vert_len;
            res.vertex_source = hrz_shaders::FlatPolygonsPattern_vert;
            res.fragment_source_len = hrz_shaders::FlatPolygonsPattern_frag_len;
            res.fragment_source = hrz_shaders::FlatPolygonsPattern_frag;
            res.attrib_count = HRZ_ARRAY_COUNT(pattern_attribs);
            res.attribs = pattern_attribs;
            res.sampler_count = HRZ_ARRAY_COUNT(pattern_visual_samplers);
            res.samplers = pattern_visual_samplers;

            rc->alloc(&res, hrz::monitoring::systems::FlatOverlays);

            res.name = hrz_shaders::FlatPolygonsSolidColor_picking_name;
            res.vertex_source_len = hrz_shaders::FlatPolygonsSolidColor_picking_vert_len;
            res.vertex_source = hrz_shaders::FlatPolygonsSolidColor_picking_vert;
            res.fragment_source_len = hrz_shaders::FlatPolygonsSolidColor_picking_frag_len;
            res.fragment_source = hrz_shaders::FlatPolygonsSolidColor_picking_frag;
            res.attrib_count = HRZ_ARRAY_COUNT(solid_color_attribs);
            res.attribs = solid_color_attribs;
            res.sampler_count = HRZ_ARRAY_COUNT(solid_color_picking_samplers);
            res.samplers = solid_color_picking_samplers;
            res.output_count = HRZ_ARRAY_COUNT(picking_outputs);
            res.outputs = picking_outputs;
            res.initial_state.color_blend.enable = false;

            rc->alloc(&res, hrz::monitoring::systems::FlatOverlays);

            res.name = hrz_shaders::FlatPolygonsPattern_picking_name;
            res.vertex_source_len = hrz_shaders::FlatPolygonsPattern_picking_vert_len;
            res.vertex_source = hrz_shaders::FlatPolygonsPattern_picking_vert;
            res.fragment_source_len = hrz_shaders::FlatPolygonsPattern_picking_frag_len;
            res.fragment_source = hrz_shaders::FlatPolygonsPattern_picking_frag;
            res.attrib_count = HRZ_ARRAY_COUNT(pattern_attribs);
            res.attribs = pattern_attribs;
            res.sampler_count = HRZ_ARRAY_COUNT(pattern_picking_samplers);
            res.samplers = pattern_picking_samplers;

            rc->alloc(&res, hrz::monitoring::systems::FlatOverlays);

            res.name = hrz_shaders::FlatPolygonsSolidColor_selection_name;
            res.vertex_source_len = hrz_shaders::FlatPolygonsSolidColor_selection_vert_len;
            res.vertex_source = hrz_shaders::FlatPolygonsSolidColor_selection_vert;
            res.fragment_source_len = hrz_shaders::FlatPolygonsSolidColor_selection_frag_len;
            res.fragment_source = hrz_shaders::FlatPolygonsSolidColor_selection_frag;
            res.attrib_count = HRZ_ARRAY_COUNT(solid_color_attribs);
            res.attribs = solid_color_attribs;
            res.sampler_count = HRZ_ARRAY_COUNT(solid_color_selection_samplers);
            res.samplers = solid_color_selection_samplers;
            res.output_count = HRZ_ARRAY_COUNT(selection_outputs);
            res.outputs = selection_outputs;

            rc->alloc(&res, hrz::monitoring::systems::FlatOverlays);

            res.name = hrz_shaders::FlatPolygonsPattern_selection_name;
            res.vertex_source_len = hrz_shaders::FlatPolygonsPattern_selection_vert_len;
            res.vertex_source = hrz_shaders::FlatPolygonsPattern_selection_vert;
            res.fragment_source_len = hrz_shaders::FlatPolygonsPattern_selection_frag_len;
            res.fragment_source = hrz_shaders::FlatPolygonsPattern_selection_frag;
            res.attrib_count = HRZ_ARRAY_COUNT(pattern_attribs);
            res.attribs = pattern_attribs;
            res.sampler_count = HRZ_ARRAY_COUNT(pattern_selection_samplers);
            res.samplers = pattern_selection_samplers;

            rc->alloc(&res, hrz::monitoring::systems::FlatOverlays);
        }

        {
            my::IndexName attribs[] = {
                {InputStreamInMeshPos, "i_in_mesh_pos"},
                {InputStreamColor, "i_color"},
                {InputStreamLineWidth, "i_width"},
                {InputStreamLineGeometry, "i_geometry"},
                {InputStreamLinePos0, "i_pos0"},
                {InputStreamLinePos1, "i_pos1"},
                {InputStreamLineNormal0, "i_normal0"},
                {InputStreamLineNormal1, "i_normal1"},
                {InputStreamLineAnimationSpeed, "i_animation_speed"},
                {InputStreamLineEmptyColor, "i_empty_color"},
                {InputStreamLineTotalLength, "i_total_length"},
                {InputStreamLineFeatureIndex, "i_feature_index"},
            };

            my::IndexName visual_samplers[] = {
                {hrz::SamplerCameraHeight, "u_camera_height"},
                {SamplerFeatureIds, "u_feature_ids"},
            };

            my::IndexName picking_samplers[] = {
                {hrz::SamplerCameraHeight, "u_camera_height"},
            };

            my::IndexName selection_samplers[] = {
                {hrz::SamplerCameraHeight, "u_camera_height"},
                {SamplerSelection, "u_selection"},
            };

            my::ShaderResource res{};
            res.name = hrz_shaders::FlatPolylinesSquare_name;
            res.vertex_source_len = hrz_shaders::FlatPolylinesSquare_vert_len;
            res.vertex_source = hrz_shaders::FlatPolylinesSquare_vert;
            res.fragment_source_len = hrz_shaders::FlatPolylinesSquare_frag_len;
            res.fragment_source = hrz_shaders::FlatPolylinesSquare_frag;
            res.attrib_count = HRZ_ARRAY_COUNT(attribs);
            res.attribs = attribs;
            res.uniform_block_count = HRZ_ARRAY_COUNT(ubos);
            res.uniform_blocks = ubos;
            res.sampler_count = HRZ_ARRAY_COUNT(visual_samplers);
            res.samplers = visual_samplers;
            res.output_count = HRZ_ARRAY_COUNT(color_outputs);
            res.outputs = color_outputs;

            res.initial_state.depth.test = true;

            res.initial_state.color_blend.enable = true;
            res.initial_state.color_blend.color.op = my::ColorBlendState::Add;
            res.initial_state.color_blend.color.src = my::ColorBlendState::One;
            res.initial_state.color_blend.color.dst = my::ColorBlendState::OneMinusSrcAlpha;
            res.initial_state.color_blend.alpha.op = my::ColorBlendState::Add;
            res.initial_state.color_blend.alpha.src = my::ColorBlendState::One;
            res.initial_state.color_blend.alpha.dst = my::ColorBlendState::OneMinusSrcAlpha;
            res.initial_state.rasterization.cull_mode = my::RasterizationState::CullMode::None;

            rc->alloc(&res, hrz::monitoring::systems::FlatOverlays);

            res.name = hrz_shaders::FlatPolylinesRound_name;
            res.vertex_source_len = hrz_shaders::FlatPolylinesRound_vert_len;
            res.vertex_source = hrz_shaders::FlatPolylinesRound_vert;
            res.fragment_source_len = hrz_shaders::FlatPolylinesRound_frag_len;
            res.fragment_source = hrz_shaders::FlatPolylinesRound_frag;

            rc->alloc(&res, hrz::monitoring::systems::FlatOverlays);

            res.name = hrz_shaders::FlatPolylinesSquare_picking_name;
            res.vertex_source_len = hrz_shaders::FlatPolylinesSquare_picking_vert_len;
            res.vertex_source = hrz_shaders::FlatPolylinesSquare_picking_vert;
            res.fragment_source_len = hrz_shaders::FlatPolylinesSquare_picking_frag_len;
            res.fragment_source = hrz_shaders::FlatPolylinesSquare_picking_frag;
            res.sampler_count = HRZ_ARRAY_COUNT(picking_samplers);
            res.samplers = picking_samplers;
            res.output_count = HRZ_ARRAY_COUNT(picking_outputs);
            res.outputs = picking_outputs;
            res.initial_state.color_blend.enable = false;

            rc->alloc(&res, hrz::monitoring::systems::FlatOverlays);

            res.name = hrz_shaders::FlatPolylinesRound_picking_name;
            res.vertex_source_len = hrz_shaders::FlatPolylinesRound_picking_vert_len;
            res.vertex_source = hrz_shaders::FlatPolylinesRound_picking_vert;
            res.fragment_source_len = hrz_shaders::FlatPolylinesRound_picking_frag_len;
            res.fragment_source = hrz_shaders::FlatPolylinesRound_picking_frag;

            rc->alloc(&res, hrz::monitoring::systems::FlatOverlays);

            res.name = hrz_shaders::FlatPolylinesSquare_selection_name;
            res.vertex_source_len = hrz_shaders::FlatPolylinesSquare_selection_vert_len;
            res.vertex_source = hrz_shaders::FlatPolylinesSquare_selection_vert;
            res.fragment_source_len = hrz_shaders::FlatPolylinesSquare_selection_frag_len;
            res.fragment_source = hrz_shaders::FlatPolylinesSquare_selection_frag;
            res.sampler_count = HRZ_ARRAY_COUNT(selection_samplers);
            res.samplers = selection_samplers;
            res.output_count = HRZ_ARRAY_COUNT(selection_outputs);
            res.outputs = selection_outputs;

            rc->alloc(&res, hrz::monitoring::systems::FlatOverlays);

            res.name = hrz_shaders::FlatPolylinesRound_selection_name;
            res.vertex_source_len = hrz_shaders::FlatPolylinesRound_selection_vert_len;
            res.vertex_source = hrz_shaders::FlatPolylinesRound_selection_vert;
            res.fragment_source_len = hrz_shaders::FlatPolylinesRound_selection_frag_len;
            res.fragment_source = hrz_shaders::FlatPolylinesRound_selection_frag;

            rc->alloc(&res, hrz::monitoring::systems::FlatOverlays);
        }

        {
            my::IndexName attribs[] = {
                {InputStreamInMeshPos, "i_in_mesh_pos"},
                {InputStreamPointsPosition, "i_position"},
                {InputStreamPointsColor, "i_color"},
                {InputStreamPointsRadius, "i_radius"},
                {InputStreamPointsFeatureIndex, "i_instance_position"},
            };

            my::IndexName visual_samplers[] = {
                {hrz::SamplerCameraHeight, "u_camera_height"},
                {SamplerFeatureIds, "u_feature_ids"},
            };

            my::IndexName picking_samplers[] = {
                {hrz::SamplerCameraHeight, "u_camera_height"},
            };

            my::IndexName selection_samplers[] = {
                {hrz::SamplerCameraHeight, "u_camera_height"},
                {SamplerSelection, "u_selection"},
            };

            my::ShaderResource res{};
            res.name = hrz_shaders::FlatPoints_name;
            res.vertex_source_len = hrz_shaders::FlatPoints_vert_len;
            res.vertex_source = hrz_shaders::FlatPoints_vert;
            res.fragment_source_len = hrz_shaders::FlatPoints_frag_len;
            res.fragment_source = hrz_shaders::FlatPoints_frag;
            res.attrib_count = HRZ_ARRAY_COUNT(attribs);
            res.attribs = attribs;
            res.uniform_block_count = HRZ_ARRAY_COUNT(ubos);
            res.uniform_blocks = ubos;
            res.sampler_count = HRZ_ARRAY_COUNT(visual_samplers);
            res.samplers = visual_samplers;
            res.output_count = HRZ_ARRAY_COUNT(color_outputs);
            res.outputs = color_outputs;

            res.initial_state.depth.test = true;

            res.initial_state.color_blend.enable = true;
            res.initial_state.color_blend.color.op = my::ColorBlendState::Add;
            res.initial_state.color_blend.color.src = my::ColorBlendState::One;
            res.initial_state.color_blend.color.dst = my::ColorBlendState::OneMinusSrcAlpha;
            res.initial_state.color_blend.alpha.op = my::ColorBlendState::Add;
            res.initial_state.color_blend.alpha.src = my::ColorBlendState::One;
            res.initial_state.color_blend.alpha.dst = my::ColorBlendState::OneMinusSrcAlpha;
            res.initial_state.rasterization.cull_mode = my::RasterizationState::CullMode::None;

            rc->alloc(&res, hrz::monitoring::systems::FlatOverlays);

            res.name = hrz_shaders::FlatPoints_picking_name;
            res.vertex_source_len = hrz_shaders::FlatPoints_picking_vert_len;
            res.vertex_source = hrz_shaders::FlatPoints_picking_vert;
            res.fragment_source_len = hrz_shaders::FlatPoints_picking_frag_len;
            res.fragment_source = hrz_shaders::FlatPoints_picking_frag;
            res.sampler_count = HRZ_ARRAY_COUNT(picking_samplers);
            res.samplers = picking_samplers;
            res.output_count = HRZ_ARRAY_COUNT(picking_outputs);
            res.outputs = picking_outputs;
            res.initial_state.color_blend.enable = false;

            rc->alloc(&res, hrz::monitoring::systems::FlatOverlays);

            res.name = hrz_shaders::FlatPoints_selection_name;
            res.vertex_source_len = hrz_shaders::FlatPoints_selection_vert_len;
            res.vertex_source = hrz_shaders::FlatPoints_selection_vert;
            res.fragment_source_len = hrz_shaders::FlatPoints_selection_frag_len;
            res.fragment_source = hrz_shaders::FlatPoints_selection_frag;
            res.sampler_count = HRZ_ARRAY_COUNT(selection_samplers);
            res.samplers = selection_samplers;
            res.output_count = HRZ_ARRAY_COUNT(selection_outputs);
            res.outputs = selection_outputs;

            rc->alloc(&res, hrz::monitoring::systems::FlatOverlays);
        }
    }

    void init_render(hrz::Render* render) override
    {
        assert(render);

        _polygons_solid_color_visual_shader =
            render->rc->retrieve_shader(hrz_shaders::FlatPolygonsSolidColor_name);
        _polygons_solid_color_picking_shader =
            render->rc->retrieve_shader(hrz_shaders::FlatPolygonsSolidColor_picking_name);
        _polygons_solid_color_selection_shader =
            render->rc->retrieve_shader(hrz_shaders::FlatPolygonsSolidColor_selection_name);
        _polygons_pattern_visual_shader =
            render->rc->retrieve_shader(hrz_shaders::FlatPolygonsPattern_name);
        _polygons_pattern_picking_shader =
            render->rc->retrieve_shader(hrz_shaders::FlatPolygonsPattern_picking_name);
        _polygons_pattern_selection_shader =
            render->rc->retrieve_shader(hrz_shaders::FlatPolygonsPattern_selection_name);

        _polylines_square_visual_shader =
            render->rc->retrieve_shader(hrz_shaders::FlatPolylinesSquare_name);
        _polylines_square_picking_shader =
            render->rc->retrieve_shader(hrz_shaders::FlatPolylinesSquare_picking_name);
        _polylines_square_selection_shader =
            render->rc->retrieve_shader(hrz_shaders::FlatPolylinesSquare_selection_name);
        _polylines_round_visual_shader =
            render->rc->retrieve_shader(hrz_shaders::FlatPolylinesRound_name);
        _polylines_round_picking_shader =
            render->rc->retrieve_shader(hrz_shaders::FlatPolylinesRound_picking_name);
        _polylines_round_selection_shader =
            render->rc->retrieve_shader(hrz_shaders::FlatPolylinesRound_selection_name);

        _points_visual_shader = render->rc->retrieve_shader(hrz_shaders::FlatPoints_name);
        _points_picking_shader = render->rc->retrieve_shader(hrz_shaders::FlatPoints_picking_name);
        _points_selection_shader =
            render->rc->retrieve_shader(hrz_shaders::FlatPoints_selection_name);

        {
            // Generate a 2d rectangle extruded on z axis
            static const lm::vec3 positions[] = {{0, -0.5, 0}, {0, -0.5, 1}, {0, 0.5, 1},
                                                 {0, -0.5, 0}, {0, 0.5, 1},  {0, 0.5, 0}};

            my::BufferResource vertex_buffer(my::BufferResource::BufferType::Vertex);
            vertex_buffer.size = HRZ_ARRAY_COUNT(positions) * sizeof(lm::vec3);
            vertex_buffer.usage = my::UsageHint::Static;
            vertex_buffer.data = &positions[0].x;

            _line_vertex_buffer = render->rc->alloc(
                &vertex_buffer, hrz::monitoring::systems::FlatOverlays, hrz::monitoring::NoLayer,
                {{"contents"_ss, "polyline vertex positions"_ss}});
        }

        {
            const lm::vec2 positions[] = {{0.5, 0.5}, {0.5, -0.5}, {-0.5, 0.5}, {-0.5, -0.5}};

            my::BufferResource vertex_buffer(my::BufferResource::BufferType::Vertex);
            vertex_buffer.size = sizeof(positions);
            vertex_buffer.usage = my::UsageHint::Static;
            vertex_buffer.data = &positions[0].x;

            _point_vertex_buffer = render->rc->alloc(
                &vertex_buffer, hrz::monitoring::systems::FlatOverlays, hrz::monitoring::NoLayer,
                {{"contents"_ss, "point vertex positions"_ss}});
        }

        {
            uint32_t data[] = {0, 0};
            std::span<const std::byte> data_span = {(const std::byte*)&data, sizeof(data)};

            my::TextureResource texture;
            texture.layout.type = my::TextureLayout::Type2D;
            texture.layout.format = my::TextureFormat::RG32UI;
            texture.layout.width = 1;
            texture.layout.height = 1;
            texture.layout.depth = 1;
            texture.layout.levels = 1;
            texture.data = {&data_span, 1};
            texture.generate_mipmaps = false;

            _empty_feature_id_texture = render->rc->alloc(
                &texture, hrz::monitoring::systems::FlatOverlays, 0,
                {{"contents"_ss, "feature ID empty texture"_ss}});
        }

        _empty_selection_storage = hrz::selection::SelectionStorageUint32TextureMultiIndex(
            0, {hrz::monitoring::systems::FlatOverlays, 0}, {});

        {
            my::SamplerResource res;
            res.use_mipmaps = false;
            res.sampler.min_filter = my::SamplerParams::Filter::Linear;
            res.sampler.mag_filter = my::SamplerParams::Filter::Linear;
            res.sampler.mipmap_filter = my::SamplerParams::Filter::Linear;

            _pattern_sampler = render->rc->alloc(&res, hrz::monitoring::systems::FlatOverlays);
        }

        {
            my::SamplerResource res;
            res.use_mipmaps = false;
            res.sampler.min_filter = my::SamplerParams::Filter::Nearest;
            res.sampler.mag_filter = my::SamplerParams::Filter::Nearest;
            res.sampler.mipmap_filter = my::SamplerParams::Filter::Nearest;

            _metadata_sampler = render->rc->alloc(&res, hrz::monitoring::systems::FlatOverlays);
        }
    }

    void deinit_render(hrz::Render* render) override
    {
        assert(render);

        _empty_selection_storage.free_gpu_resources(_resources_to_free);
        for (my::ResourceHandle res : _resources_to_free)
        {
            render->rc->dealloc(res);
        }
        _resources_to_free.clear();

        render->rc->dealloc(_line_vertex_buffer);
        render->rc->dealloc(_point_vertex_buffer);
        render->rc->dealloc(_empty_feature_id_texture);
        render->rc->dealloc(_pattern_sampler);
        render->rc->dealloc(_metadata_sampler);
    }

    bool uses_z_coordinates() const override { return false; }

    std::optional<ConfigH> register_style(
        const hrz_proto::VectorRepr& repr,
        uint64_t layer_id,
        const std::function<
            uint64_t(std::string_view name, const hrz::vector_data::OwnedAttributeValue&)>&
            register_prp,
        ConfigId style_id)
    {
        if (repr.type() != hrz_proto::VectorReprType::FLAT_OVERLAY_VECTOR_REPR)
        {
            HRZ_LOG_ERROR("Unexpected vector representation type, expected flat overlay...");
            return std::nullopt;
        }

        if (_configs_by_id.contains(style_id))
        {
            HRZ_LOG_ERROR(
                "Cannot register style: ID {}-{} already in use", style_id.channel_id,
                style_id.config_id);
            return std::nullopt;
        }

        std::string_view line_width_prp_name = repr.flat_overlay_geometry().line_width().name();
        std::string_view color_prp_name = repr.flat_overlay_geometry().color().name();
        std::string_view disc_radius_prp_name = repr.flat_overlay_geometry().disc_radius().name();
        std::string_view dash_length_prp_name = repr.flat_overlay_geometry().dash_length().name();
        std::string_view dash_period_prp_name = repr.flat_overlay_geometry().dash_period().name();
        std::string_view animation_speed_prp_name =
            repr.flat_overlay_geometry().animation_speed().name();
        std::string_view empty_color_prp_name =
            repr.flat_overlay_geometry().line_empty_color().name();

        Config config;
        config.layer_id = layer_id;
        config.repr_id = repr.id();

        config.default_line_width = repr.flat_overlay_geometry().line_width().default_value();
        config.default_color = hrz::to_lm(repr.flat_overlay_geometry().color().default_value());
        config.default_dash_length = repr.flat_overlay_geometry().dash_length().default_value();
        config.default_dash_period = repr.flat_overlay_geometry().dash_period().default_value();
        config.default_animation_speed =
            repr.flat_overlay_geometry().animation_speed().default_value();
        config.default_empty_color =
            hrz::to_lm(repr.flat_overlay_geometry().line_empty_color().default_value());
        config.default_disc_radius = repr.flat_overlay_geometry().disc_radius().default_value();

        config.line_width_prp = register_prp(line_width_prp_name, config.default_line_width);
        config.color_prp = register_prp(
            color_prp_name,
            hrz::vector_data::attr_from_color<hrz::vector_data::OwnedAttributeValue>(
                config.default_color));
        config.disc_radius_prp = register_prp(disc_radius_prp_name, config.default_disc_radius);
        config.dash_length_prp = register_prp(dash_length_prp_name, config.default_dash_length);
        config.dash_period_prp = register_prp(dash_period_prp_name, config.default_dash_period);
        config.animation_speed_prp =
            register_prp(animation_speed_prp_name, config.default_animation_speed);
        config.empty_color_prp = register_prp(
            empty_color_prp_name,
            hrz::vector_data::attr_from_color<hrz::vector_data::OwnedAttributeValue>(
                config.default_empty_color));

        config.z_index = repr.flat_overlay_geometry().z_index();
        config.polygons_outline = repr.flat_overlay_geometry().polygons_outline();
        config.clip_to_tile = repr.flat_overlay_geometry().clip_to_tile();

        config.dash_mode = repr.flat_overlay_geometry().dash_mode();
        config.dash_period_unit = repr.flat_overlay_geometry().dash_period_unit();
        config.dash_length_unit = repr.flat_overlay_geometry().dash_length_unit();
        config.animation_speed_unit = repr.flat_overlay_geometry().animation_speed_unit();
        config.round_tips = repr.flat_overlay_geometry().round_tips();
        config.polyline_side = repr.flat_overlay_geometry().side();
        config.line_width_unit = repr.flat_overlay_geometry().line_width_unit();

        config.disc_outline_color = hrz::to_lm(repr.flat_overlay_geometry().disc_outline_color());
        config.disc_outline_width = repr.flat_overlay_geometry().disc_outline_width();
        config.disc_radius_unit = repr.flat_overlay_geometry().disc_radius_unit();

        config.pattern_image_url = repr.flat_overlay_geometry().pattern_image_url();
        config.pattern_image_headers = repr.flat_overlay_geometry().pattern_image_http_headers();
        config.default_polygon_pattern_sprite_index =
            repr.flat_overlay_geometry().polygon_pattern_sprite_index().default_value();
        config.polygon_pattern_sprite_index_prp = register_prp(
            repr.flat_overlay_geometry().polygon_pattern_sprite_index().name(),
            (int64_t)config.default_polygon_pattern_sprite_index);
        config.default_polygon_pattern_sprite_name =
            repr.flat_overlay_geometry().polygon_pattern_sprite_name().default_value();
        config.polygon_pattern_sprite_name_prp = register_prp(
            repr.flat_overlay_geometry().polygon_pattern_sprite_name().name(),
            config.default_polygon_pattern_sprite_name);
        for (const auto& proto_sprite : repr.flat_overlay_geometry().pattern_sprites())
        {
            config.pattern_sprites.push_back(
                {proto_sprite.name(), lm::uvec2(hrz::to_lm(proto_sprite.size())),
                 lm::uvec2(hrz::to_lm(proto_sprite.offset()))});
        }

        config.default_polygon_pattern_size =
            hrz::to_lm(repr.flat_overlay_geometry().polygon_pattern_size().default_value());
        {
            auto size_x_prp_name =
                fmt::format("{}_x", repr.flat_overlay_geometry().polygon_pattern_size().name());
            auto size_y_prp_name =
                fmt::format("{}_y", repr.flat_overlay_geometry().polygon_pattern_size().name());
            config.polygon_pattern_size_prp.x =
                register_prp(size_x_prp_name, config.default_polygon_pattern_size.x);
            config.polygon_pattern_size_prp.y =
                register_prp(size_y_prp_name, config.default_polygon_pattern_size.y);
        }
        config.polygon_pattern_size_unit = repr.flat_overlay_geometry().polygon_pattern_size_unit();
        config.default_polygon_pattern_rotation =
            repr.flat_overlay_geometry().polygon_pattern_rotation().default_value();
        config.polygon_pattern_rotation_prp = register_prp(
            repr.flat_overlay_geometry().polygon_pattern_rotation().name(),
            config.default_polygon_pattern_rotation);
        config.polygon_pattern_tiling_type =
            repr.flat_overlay_geometry().polygon_pattern_tiling_type();
        config.polygon_pattern_reference_latitude_type =
            repr.flat_overlay_geometry().polygon_pattern_reference_latitude_type();
        config.polygon_pattern_reference_latitude =
            lm::radians(repr.flat_overlay_geometry().polygon_pattern_reference_latitude());
        config.default_polygon_pattern_color =
            hrz::to_lm(repr.flat_overlay_geometry().polygon_pattern_color().default_value());
        config.polygon_pattern_color_prp = register_prp(
            repr.flat_overlay_geometry().polygon_pattern_color().name(),
            hrz::vector_data::attr_from_color<hrz::vector_data::OwnedAttributeValue>(
                config.default_polygon_pattern_color));
        config.polygon_pattern_color_blend_mode =
            repr.flat_overlay_geometry().polygon_pattern_color_blend_mode();
        config.default_polygon_pattern_color_blend_strength =
            repr.flat_overlay_geometry().polygon_pattern_color_blend_strength().default_value();
        config.polygon_pattern_color_blend_strength_prp = register_prp(
            repr.flat_overlay_geometry().polygon_pattern_color_blend_strength().name(),
            config.default_polygon_pattern_color_blend_strength);

        config.scene_views = repr.scene_views().bits();

        config.status = Config::Status::Loading;

        auto config_handle = _configs.alloc(std::move(config));

        _configs_by_id.insert({style_id, config_handle});

        _loading_configs.insert(config_handle);

        return config_handle;
    }

    void unregister_style(
        ConfigH config_handle,
        const std::function<void(uint64_t prp_id)>& unregister_property)
    {
        if (!_configs.is_valid(config_handle)) return;

        const Config* cfg = _configs.get_object(config_handle);

        unregister_property(cfg->line_width_prp);
        unregister_property(cfg->color_prp);
        unregister_property(cfg->disc_radius_prp);
        unregister_property(cfg->dash_length_prp);
        unregister_property(cfg->dash_period_prp);
        unregister_property(cfg->animation_speed_prp);
        unregister_property(cfg->empty_color_prp);
        unregister_property(cfg->polygon_pattern_sprite_index_prp);
        unregister_property(cfg->polygon_pattern_sprite_name_prp);
        unregister_property(cfg->polygon_pattern_size_prp.x);
        unregister_property(cfg->polygon_pattern_size_prp.y);
        unregister_property(cfg->polygon_pattern_rotation_prp);
        unregister_property(cfg->polygon_pattern_color_prp);
        unregister_property(cfg->polygon_pattern_color_blend_strength_prp);

        _images_to_free.push_back(cfg->pattern_image);

        _loading_configs.erase(config_handle);
        _configs.release(config_handle);
    }

    std::optional<TileH> add_tile(
        ConfigH config_handle,
        hrz::TileCoords coords,
        uint64_t layer_id,
        const hrz::picking::ObjectReference& object_ref,
        const hrz::picking::FeatureReference& feature_ref,
        const hrz::vector_data::FeatureIds& feature_ids,
        const hrz::vt::ReprGeometry& geometry,
        const hrz::style::StyledFeatures& style,
        double min_elevation,
        double max_elevation,
        TileId tile_id)
    {
        if (_tiles_by_id.contains(tile_id))
        {
            HRZ_LOG_ERROR(
                "Cannot add tile: ID {}-{} already in use", tile_id.channel_id, tile_id.tile_id);
            return std::nullopt;
        }

        Tile tile;
        tile.id = tile_id;
        tile.layer_id = layer_id;
        tile.coords = coords;
        tile.has_feature_ids = feature_ids.has_any_attribute();

        tile.min_elevation = min_elevation;
        tile.max_elevation = max_elevation;
        tile.wmerc_bounds = std::nullopt;

        Config cfg = {};
        Config* cfg_src = _configs.get_object(config_handle);
        if (cfg_src)
        {
            cfg = *cfg_src;
        }
        else
        {
            HRZ_LOG_WARNING("Unknown config. Using default values.");
        }

        hrz::vt::FlatVectorData bake_data;
        bake_data.coords = coords;

        bake_data.feature_ids = tile.has_feature_ids
            ? feature_ids.hashes()
            : hrz::BlobArray<hrz::vector_data::FeatureIdHash>();
        bake_data.geometry = geometry.geometry;

        auto& dst_style = bake_data.style;
        dst_style.prps = style.prps;
        dst_style.values = style.values;
        dst_style.out_of_line_data = style.out_of_line_data;
        dst_style.instances = style.instances;

        tile.bake_data = {std::move(bake_data)};

        tile.has_polygon_pattern = !cfg.pattern_image_url.empty();
        tile.scene_views = cfg.scene_views;
        tile.round_tips = cfg.round_tips;

        tile.ubo.object_reference = object_ref.to_uvec2();
        tile.ubo.feature_reference = feature_ref.to_uvec3();
        tile.ubo.has_feature_ids = tile.has_feature_ids;
        tile.ubo.disc_outline_width = cfg.disc_outline_width;
        tile.ubo.disc_outline_color = cfg.disc_outline_color;
        tile.ubo.disc_radius_unit = cfg.disc_radius_unit;
        tile.ubo.line_width_unit = cfg.line_width_unit;
        tile.ubo.dash_mode = cfg.dash_mode;
        tile.ubo.dash_period_unit = cfg.dash_period_unit;
        tile.ubo.dash_length_unit = cfg.dash_length_unit;
        tile.ubo.animation_speed_unit = cfg.animation_speed_unit;
        tile.ubo.polygon_pattern_size_unit = cfg.polygon_pattern_size_unit;
        tile.ubo.polygon_pattern_tiling_type = cfg.polygon_pattern_tiling_type;
        tile.ubo.polygon_pattern_reference_latitude_type =
            cfg.polygon_pattern_reference_latitude_type;
        tile.ubo.polygon_pattern_color_blend_mode = cfg.polygon_pattern_color_blend_mode;

        if (cfg.polygon_pattern_reference_latitude_type
            == hrz_proto::PolygonPatternReferenceLatitudeType::
                POLYGON_PATTERN_FIXED_REFERENCE_LATITUDE)
        {
            // An offset to the pattern scale factor is calculated:
            // * in order to avoid having a pattern scale break at the reference latitude,
            //   when the pattern size is favoured,
            // * in order to have the intended pattern size at the reference latitude, when
            //   the grid is favoured.
            // The pattern scale is computed in the shader, and changes with the latitude
            // and the camera height. To keep a consistent grid position, the scale is then
            // constrained to powers of two. (When favouring the grid, the pattern scale is
            // 1.)
            // The offset is added to the scale and is calculated so that at the reference
            // latitude the scale factor is the closest value from its natural value that is
            // a midpoint between two powers of two (where scale breaks happen).
            float reference_lat_size_factor =
                1.0 / std::abs(std::cos(cfg.polygon_pattern_reference_latitude));
            tile.ubo.polygon_pattern_reference_lat_scale_factor_offset =
                hrz::round_to_power_of_two(reference_lat_size_factor) - reference_lat_size_factor;
        }
        else
        {
            tile.ubo.polygon_pattern_reference_lat_scale_factor_offset = 0.0f;
        }

        switch (cfg.polyline_side)
        {
            case hrz_proto::PolylineSide::SIDE_BOTH:
                tile.ubo.polyline_sides = HRZ_S_POLYLINE_SIDE_IN | HRZ_S_POLYLINE_SIDE_OUT;
                break;
            case hrz_proto::PolylineSide::SIDE_INSIDE:
                tile.ubo.polyline_sides = HRZ_S_POLYLINE_SIDE_IN;
                break;
            case hrz_proto::PolylineSide::SIDE_OUTSIDE:
                tile.ubo.polyline_sides = HRZ_S_POLYLINE_SIDE_OUT;
                break;
            default: tile.ubo.polyline_sides = 0; break;
        }

        TileH handle = _tiles.alloc(std::move(tile));

        {
            auto tile = _tiles.get_object(handle);

            if (cfg_src)
            {
                if (cfg_src->status == Config::Status::Ready)
                {
                    prepare_tile_for_baking(tile, cfg_src);
                    _to_bake.insert(handle);
                }
                else if (cfg_src->status == Config::Status::Error)
                {
                    tile->status = Tile::Status::Error;

                    if (tile->id.has_value())
                    {
                        auto it = _channels.find(tile->id->channel_id);
                        if (it != _channels.end())
                        {
                            auto& channel = it->second;
                            channel.send(
                                hrz::vt::repr::messages::TileStatusUpdate{tile->id->tile_id, true});
                        }
                    }
                }
                else
                {
                    assert(cfg_src->status == Config::Status::Loading);

                    tile->status = Tile::Status::WaitingForConfig;
                    _waiting_for_config.insert({handle, config_handle});
                }
            }
            else
            {
                prepare_tile_for_baking(tile, &cfg);
                tile->status = Tile::Status::ReadyToBake;
                _to_bake.insert(handle);
            }
        }

        _tiles_by_id.insert({tile_id, handle});

        return handle;
    }

    void prepare_tile_for_baking(Tile* tile, Config* cfg)
    {
        assert(tile->bake_data.has_value());
        assert(cfg->status == Config::Status::Ready);

        tile->pattern_texture = cfg->pattern_texture;
        tile->has_polygon_pattern = !cfg->pattern_texture.is_null();

        auto& bake_data = tile->bake_data.value();

        bake_data.default_color = cfg->default_color;
        bake_data.default_line_width = cfg->default_line_width;
        bake_data.default_disc_radius = cfg->default_disc_radius;

        bake_data.repr_id = cfg->repr_id;
        bake_data.line_width_prp = cfg->line_width_prp;
        bake_data.disc_radius_prp = cfg->disc_radius_prp;
        bake_data.color_prp = cfg->color_prp;
        bake_data.clip_to_tile = cfg->clip_to_tile;
        bake_data.polygons_outline = cfg->polygons_outline;
        bake_data.default_dash_period = cfg->default_dash_period;
        bake_data.default_dash_length = cfg->default_dash_length;
        bake_data.default_animation_speed = cfg->default_animation_speed;
        bake_data.default_empty_color = cfg->default_empty_color;
        bake_data.dash_period_prp = cfg->dash_period_prp;
        bake_data.dash_length_prp = cfg->dash_length_prp;
        bake_data.animation_speed_prp = cfg->animation_speed_prp;
        bake_data.empty_color_prp = cfg->empty_color_prp;

        bake_data.pattern_texture_size = cfg->pattern_texture_size;
        for (const auto& sprite : cfg->pattern_sprites)
        {
            auto index = bake_data.pattern_sprites.size();
            bake_data.pattern_sprites.push_back({sprite.size, sprite.offset});
            bake_data.pattern_sprite_name_to_index.insert({sprite.name, index});
        }

        bake_data.has_polygon_pattern = tile->has_polygon_pattern;
        bake_data.polygon_pattern_sprite_index_prp = cfg->polygon_pattern_sprite_index_prp;
        bake_data.polygon_pattern_sprite_name_prp = cfg->polygon_pattern_sprite_name_prp;
        bake_data.polygon_pattern_size_prp = cfg->polygon_pattern_size_prp;
        bake_data.polygon_pattern_rotation_prp = cfg->polygon_pattern_rotation_prp;
        bake_data.polygon_pattern_color_prp = cfg->polygon_pattern_color_prp;
        bake_data.polygon_pattern_color_blend_strength_prp =
            cfg->polygon_pattern_color_blend_strength_prp;

        bake_data.default_polygon_pattern_sprite_index = cfg->default_polygon_pattern_sprite_index;
        bake_data.default_polygon_pattern_sprite_name = cfg->default_polygon_pattern_sprite_name;
        bake_data.default_polygon_pattern_size = cfg->default_polygon_pattern_size;
        bake_data.default_polygon_pattern_rotation = cfg->default_polygon_pattern_rotation;
        bake_data.default_polygon_pattern_color = cfg->default_polygon_pattern_color;
        bake_data.default_polygon_pattern_color_blend_strength =
            cfg->default_polygon_pattern_color_blend_strength;

        bake_data.polygon_pattern_size_unit = cfg->polygon_pattern_size_unit;

        tile->z_index = cfg->z_index;

        tile->status = Tile::Status::ReadyToBake;
    }

    void schedule_draw_now(uint64_t channel_id, uint64_t tile_id, uint32_t views_bitset) override
    {
        auto it = _tiles_by_id.find({channel_id, tile_id});
        if (it != _tiles_by_id.end())
        {
            _drawn_tiles.insert(std::make_pair(it->second, views_bitset));
        }
    }

    bool tile_is_ready(const Tile* tile) const
    {
        return tile->status == Tile::Status::Ready || tile->status == Tile::Status::Error;
    }

    void remove_tile(TileH handle) { _removed.insert(handle); }

    void remove_from_set(TileH handle, Tile::Status status)
    {
        switch (status)
        {
            case Tile::Status::WaitingForConfig: _waiting_for_config.erase(handle); break;
            case Tile::Status::ReadyToBake: _to_bake.erase(handle); break;
            case Tile::Status::Baking: _baking.erase(handle); break;
            case Tile::Status::FinishedBaking: _finished_baking.erase(handle); break;
            default: break;
        }
    }

    static void compute_tile_bsphere(Tile* tile)
    {
        if (!tile->wmerc_bounds.has_value())
        {
            return;
        }

        const auto& wmerc_bounds = tile->wmerc_bounds.value();

        std::array<lm::dvec3, 8> points;

        for (size_t i = 0; i < 4; ++i)
        {
            points[i * 2 + 0] =
                hrz::web_mercator_to_ecef(lm::corner(wmerc_bounds, i), tile->min_elevation);
            points[i * 2 + 1] =
                hrz::web_mercator_to_ecef(lm::corner(wmerc_bounds, i), tile->max_elevation);
        }

        auto bsphere = hrz::compute_bounding_sphere(std::span<const lm::dvec3>(points));

        if (tile->geometry.has_value())
        {
            tile->geometry->clamped_center = bsphere.center;
            tile->geometry->clamped_radius = bsphere.radius;
        }

        if (tile->renderable.has_value())
        {
            tile->renderable->clamped_center = bsphere.center;
            tile->renderable->clamped_radius = bsphere.radius;
        }
    }

    void set_tile_elevation(TileH handle, double min_elevation, double max_elevation)
    {
        auto tile = _tiles.get_object(handle);
        if (tile != nullptr)
        {
            tile->min_elevation = min_elevation;
            tile->max_elevation = max_elevation;

            compute_tile_bsphere(tile);
        }
    }

    // Returns whether this config should be erased from the _loading_configs set or not.
    bool work_load_config(WorkCtx& ctx, ConfigH handle, Config* config)
    {
        namespace image_loader = hrz::vt::image_loader;

        assert(config->status == Config::Status::Loading);

        if (!config->pattern_image_url.empty())
        {
            if (!image_loader::is_image_valid(ctx.il, config->pattern_image))
            {
                config->pattern_image = image_loader::load_image(
                    ctx.il, config->pattern_image_url, config->pattern_image_headers,
                    {hrz::monitoring::systems::FlatOverlays, config->layer_id});
            }
            else
            {
                auto image_status = image_loader::get_image_status(ctx.il, config->pattern_image);
                if (image_status == image_loader::ImageStatus::Loaded)
                {
                    auto image = image_loader::get_image_texture(ctx.il, config->pattern_image);
                    config->pattern_texture = image.texture;
                    config->pattern_texture_size = image.size;

                    if (config->pattern_sprites.empty())
                    {
                        config->pattern_sprites.push_back({"", image.size, {0, 0}});
                    }

                    if (config->default_polygon_pattern_sprite_index
                        >= config->pattern_sprites.size())
                    {
                        config->default_polygon_pattern_sprite_index =
                            config->pattern_sprites.size() - 1;
                    }

                    config->status = Config::Status::Ready;
                    return true;
                }
                else if (image_status == image_loader::ImageStatus::Error)
                {
                    HRZ_LOG_ERROR("Could not load pattern image");
                    image_loader::release_image(ctx.il, config->pattern_image);
                    config->status = Config::Status::Error;
                    return true;
                }
            }
        }
        else
        {
            config->status = Config::Status::Ready;
            return true;
        }

        return false;
    }

    void work_remove_tile(WorkCtx& ctx, TileH handle, Tile* tile)
    {
        if (tile->status == Tile::Status::Baking)
        {
            hrz_jobs::cancel_job(ctx.js, tile->bake_ticket);
        }

        if (tile->renderable.has_value())
        {
            auto& renderable = tile->renderable.value();
            _resources_to_free.push_back(renderable.polygons_vertex_buffer);
            _resources_to_free.push_back(renderable.polygons_index_buffer);
            _resources_to_free.push_back(renderable.data.polygons_data.vertex_input);
            _resources_to_free.push_back(renderable.data.polygons_data.pattern_style_texture);
            _resources_to_free.push_back(renderable.polylines_instance_buffer);
            _resources_to_free.push_back(renderable.data.polylines_data.vertex_input);
            _resources_to_free.push_back(renderable.points_instance_buffer);
            _resources_to_free.push_back(renderable.data.points_data.vertex_input);
            _resources_to_free.push_back(renderable.data.ubo_buffer);
            if (tile->has_feature_ids)
            {
                _resources_to_free.push_back(renderable.feature_id_texture);
                renderable.selection_storage.free_gpu_resources(_resources_to_free);
            }
        }

        remove_from_set(handle, tile->status);

        _tiles.release(handle);
    }

    // Returns whether this tile should be erased from the _waiting_for_config set or not.
    bool work_waiting_for_config(WorkCtx& ctx, TileH handle, Tile* tile, Config* config)
    {
        assert(tile->status == Tile::Status::WaitingForConfig);
        assert(tile->bake_data.has_value());

        auto send_status_update_message = [&]()
        {
            if (tile->id.has_value())
            {
                auto it = _channels.find(tile->id->channel_id);
                if (it != _channels.end())
                {
                    auto& channel = it->second;
                    channel.send(
                        hrz::vt::repr::messages::TileStatusUpdate{tile->id->tile_id, true});
                }
            }
        };

        if (config)
        {
            if (config->status == Config::Status::Ready)
            {
                prepare_tile_for_baking(tile, config);
                work_start_baking(ctx, handle, tile);
                return true;
            }
            else if (config->status == Config::Status::Error)
            {
                tile->status = Tile::Status::Error;
                send_status_update_message();
                return true;
            }
            else
            {
                assert(config->status == Config::Status::Loading);
                return false;
            }
        }
        else
        {
            tile->status = Tile::Status::Error;
            send_status_update_message();
            return true;
        }
    }

    void work_start_baking(WorkCtx& ctx, TileH handle, Tile* tile)
    {
        assert(tile->status == Tile::Status::ReadyToBake);
        assert(tile->bake_data.has_value());

        tile->bake_ticket = hrz_jobs::add_job_bake_flat_vector_geometry(
            ctx.js, tile->bake_data.value(),
            {hrz::monitoring::systems::FlatOverlays, tile->layer_id});
        tile->bake_data = std::nullopt;
        tile->status = Tile::Status::Baking;

        _baking.insert(handle);
    }

    // Returns whether this tile should be erased from the _baking set or not.
    bool work_continue_baking(WorkCtx& ctx, TileH handle, Tile* tile)
    {
        assert(tile->status == Tile::Status::Baking);

        auto send_status_update_message = [&]()
        {
            if (tile->id.has_value())
            {
                auto it = _channels.find(tile->id->channel_id);
                if (it != _channels.end())
                {
                    auto& channel = it->second;
                    channel.send(
                        hrz::vt::repr::messages::TileStatusUpdate{tile->id->tile_id, true});
                }
            }
        };

        if (hrz_jobs::is_job_valid(ctx.js, tile->bake_ticket)
            && hrz_jobs::is_job_finished(ctx.js, tile->bake_ticket))
        {
            if (hrz_jobs::get_job_status(ctx.js, tile->bake_ticket)
                == hrz::job_scheduler::JobStatus::Finished_Success)
            {
                hrz::vt::FlatVectorGeometry response;
                hrz_jobs::get_job_response(ctx.js, tile->bake_ticket, response);

                TileGeometry geometry;

                geometry.sea_center = response.sea_bsphere_center;
                geometry.sea_radius = response.sea_bsphere_radius;
                geometry.clamped_center = response.sea_bsphere_center;
                geometry.clamped_radius = response.sea_bsphere_radius;
                geometry.polygon_data = std::move(response.polygon_data);
                geometry.polygon_indices = std::move(response.polygon_indices);
                geometry.polyline_data = std::move(response.polyline_data);
                geometry.point_data = std::move(response.point_data);
                geometry.feature_ids = std::move(response.feature_ids);
                geometry.max_feature_index = response.max_feature_index;
                geometry.is_animated = response.is_animated;
                geometry.origin_uv = response.origin_uv;
                geometry.origin_lat = response.origin_lat;
                geometry.lat_span = response.lat_span;
                geometry.polygon_pattern_style_data =
                    std::move(response.polygon_pattern_style_data);

                tile->geometry = std::move(geometry);

                tile->wmerc_bounds = {response.wmerc_bounds};
                compute_tile_bsphere(tile);

                tile->status = Tile::Status::FinishedBaking;
                _finished_baking.insert(handle);
            }
            else
            {
                HRZ_LOG_ERROR(
                    "Could not bake flat overlay representation for tile {}-{}-{}",
                    tile->coords.lod, tile->coords.x, tile->coords.y);
                tile->status = Tile::Status::Error;
                send_status_update_message();

                if (tile->id.has_value())
                {
                    auto it = _channels.find(tile->id->channel_id);
                    if (it != _channels.end())
                    {
                        auto& channel = it->second;
                        channel.send(
                            hrz::vt::repr::messages::TileStatusUpdate{tile->id->tile_id, true});
                    }
                }
            }

            return true;
        }
        else
        {
            return false;
        }
    }

    void work_finished_baking(hrz::Render* render, TileH handle, Tile* tile)
    {
        assert(tile->status == Tile::Status::FinishedBaking);
        assert(tile->geometry.has_value());

        auto send_status_update_message = [&]()
        {
            if (tile->id.has_value())
            {
                auto it = _channels.find(tile->id->channel_id);
                if (it != _channels.end())
                {
                    auto& channel = it->second;
                    channel.send(
                        hrz::vt::repr::messages::TileStatusUpdate{tile->id->tile_id, true});
                }
            }
        };

#define CHECK_RESOURCE_UPLOAD(resource, resource_type)                                    \
    if (resource.is_null())                                                               \
    {                                                                                     \
        HRZ_LOG_ERROR(                                                                    \
            "Could not upload flat vector {} of tile {}-{}-{} to the GPU", resource_type, \
            tile->coords.lod, tile->coords.x, tile->coords.y);                            \
        tile->status = Tile::Status::Error;                                               \
        send_status_update_message();                                                     \
        return;                                                                           \
    }
        TileGeometry& geometry = tile->geometry.value();

        bool has_polygon_data =
            (!tile->has_polygon_pattern
             && std::holds_alternative<
                 hrz::BlobArray<hrz::vt::FlatVectorGeometry::SolidColorPolygonVertex>>(
                 geometry.polygon_data)
             && !std::get<hrz::BlobArray<hrz::vt::FlatVectorGeometry::SolidColorPolygonVertex>>(
                     geometry.polygon_data)
                     .empty())
            || (tile->has_polygon_pattern
                && std::holds_alternative<
                    hrz::BlobArray<hrz::vt::FlatVectorGeometry::PatternPolygonVertex>>(
                    geometry.polygon_data)
                && !std::get<hrz::BlobArray<hrz::vt::FlatVectorGeometry::PatternPolygonVertex>>(
                        geometry.polygon_data)
                        .empty());
        bool has_polyline_data = !geometry.polyline_data.empty();
        bool has_point_data = !geometry.point_data.empty();
        if (!has_polygon_data && !has_polyline_data && !has_point_data)
        {
            // No data to render
            tile->status = Tile::Status::Ready;
            send_status_update_message();
            return;
        }

        auto tile_coords_str = fmt::to_string(tile->coords);

        RenderableFeatures renderable;

        // Selection bitmask texture
        if (tile->has_feature_ids)
        {
            renderable.selection_storage = hrz::selection::SelectionStorageUint32TextureMultiIndex(
                geometry.max_feature_index + 1,
                {hrz::monitoring::systems::FlatOverlays, tile->layer_id},
                {{"tile coords"_ss, tile_coords_str}});

            auto feature_ids_data = geometry.feature_ids.get_data();

            for (uint32_t i = 0; i <= geometry.max_feature_index; ++i)
            {
                renderable.selection_storage.register_indirection(feature_ids_data.at(i), i);
            }

            renderable.selection_texture = renderable.selection_storage.get_texture(render);
        }
        else
        {
            renderable.selection_texture = _empty_selection_storage.get_texture(render);
        }

        if (tile->has_feature_ids)
        {
            auto feature_ids_data = geometry.feature_ids.get_data();
            assert(feature_ids_data.size() % hrz::vt::DATA_TEXTURE_SIZE == 0);

            auto data = feature_ids_data.as_bytes();
            auto texture_size = hrz::vt::compute_data_texture_size(feature_ids_data.size());

            my::TextureResource tex_res;
            tex_res.layout.type = my::TextureLayout::Type2D;
            tex_res.layout.format = my::TextureFormat::RG32UI;
            tex_res.layout.width = texture_size.x;
            tex_res.layout.height = texture_size.y;
            tex_res.layout.depth = 1;
            tex_res.layout.levels = 1;
            tex_res.data = {&data, 1};
            tex_res.generate_mipmaps = false;
            tex_res.allow_allocation_failure = true;

            renderable.feature_id_texture = render->rc->alloc(
                &tex_res, hrz::monitoring::systems::FlatOverlays, tile->layer_id,
                {{"contents"_ss, "feature IDs"_ss}, {"tile coords"_ss, tile_coords_str}});
            CHECK_RESOURCE_UPLOAD(renderable.feature_id_texture, "feature ID texture");
        }
        else
        {
            renderable.feature_id_texture = _empty_feature_id_texture;
        }

        if (has_polygon_data && tile->has_polygon_pattern)
        {
            static constexpr size_t pixels_per_style =
                sizeof(hrz::vt::FlatVectorGeometry::PolygonPatternStyle) / sizeof(lm::uvec4);

            auto polygon_pattern_style_data = geometry.polygon_pattern_style_data.get_data();
            assert(
                polygon_pattern_style_data.size() < hrz::vt::DATA_TEXTURE_SIZE
                || polygon_pattern_style_data.size() % hrz::vt::DATA_TEXTURE_SIZE == 0);

            auto data = polygon_pattern_style_data.as_bytes();
            auto texture_size =
                hrz::vt::compute_data_texture_size(polygon_pattern_style_data.size());

            my::TextureResource tex_res;
            tex_res.layout.type = my::TextureLayout::Type2D;
            tex_res.layout.format = my::TextureFormat::RGBA32UI;
            tex_res.layout.width = texture_size.x * pixels_per_style;
            tex_res.layout.height = texture_size.y;
            tex_res.layout.depth = 1;
            tex_res.layout.levels = 1;
            tex_res.data = {&data, 1};
            tex_res.generate_mipmaps = false;
            tex_res.allow_allocation_failure = true;

            renderable.data.polygons_data.pattern_style_texture = render->rc->alloc(
                &tex_res, hrz::monitoring::systems::FlatOverlays, tile->layer_id,
                {{"contents"_ss, "polygon pattern styles"_ss},
                 {"tile coords"_ss, tile_coords_str}});
            CHECK_RESOURCE_UPLOAD(
                renderable.data.polygons_data.pattern_style_texture,
                "polygon pattern style texture");
        }

        renderable.data.draw_report = &_draw_report;

        if (has_polygon_data || has_polyline_data || has_point_data)
        {
            hrz::split_double(
                geometry.sea_center.x, tile->ubo.center_low.x, tile->ubo.center_high.x);
            hrz::split_double(
                geometry.sea_center.y, tile->ubo.center_low.y, tile->ubo.center_high.y);
            hrz::split_double(
                geometry.sea_center.z, tile->ubo.center_low.z, tile->ubo.center_high.z);

            if (has_polygon_data && tile->has_polygon_pattern)
            {
                hrz::split_double(
                    geometry.origin_uv.x, tile->ubo.origin_uv_low.x, tile->ubo.origin_uv_high.x);
                hrz::split_double(
                    geometry.origin_uv.y, tile->ubo.origin_uv_low.y, tile->ubo.origin_uv_high.y);

                tile->ubo.origin_lat = geometry.origin_lat;
                tile->ubo.lat_span = geometry.lat_span;
            }

            my::BufferResource ub_res(my::BufferResource::BufferType::Uniform);
            ub_res.size = sizeof(TileUniformData);
            ub_res.usage = my::UsageHint::Updatable;
            ub_res.data = &tile->ubo;

            renderable.data.ubo_buffer = render->rc->alloc(
                &ub_res, hrz::monitoring::systems::FlatOverlays, tile->layer_id,
                {{"contents"_ss, "flat geometry uniforms"_ss},
                 {"tile coords"_ss, tile_coords_str}});
        }

        if (has_polygon_data)
        {
            auto& renderable_data = renderable.data.polygons_data;

            my::BufferResource vb_res(my::BufferResource::BufferType::Vertex);
            vb_res.usage = my::UsageHint::Static;
            vb_res.allow_allocation_failure = true;

            hrz::StaticVector<my::VertexInputStream, 5> streams;

            if (tile->has_polygon_pattern)
            {
                using Vertex = hrz::vt::FlatVectorGeometry::PatternPolygonVertex;

                auto geometry_data =
                    std::get<hrz::BlobArray<hrz::vt::FlatVectorGeometry::PatternPolygonVertex>>(
                        geometry.polygon_data)
                        .get_data();

                vb_res.size = geometry_data.size_bytes();
                vb_res.data = geometry_data.data();

                my::ResourceHandle vertex_buffer = render->rc->alloc(
                    &vb_res, hrz::monitoring::systems::FlatOverlays, tile->layer_id,
                    {{"contents"_ss, "polygon vertex data"_ss},
                     {"tile coords"_ss, tile_coords_str}});
                CHECK_RESOURCE_UPLOAD(vertex_buffer, "polygon vertex data");
                renderable.polygons_vertex_buffer = vertex_buffer;

                streams.push_back(
                    {InputStreamInTilePos, vertex_buffer, my::VertexFormat::Float32_3,
                     offsetof(Vertex, position), sizeof(Vertex), my::VertexRate::PerVertex});
                streams.push_back(
                    {InputStreamPolygonUv, vertex_buffer, my::VertexFormat::Float32_2,
                     offsetof(Vertex, uv), sizeof(Vertex), my::VertexRate::PerVertex});
                streams.push_back(
                    {InputStreamPolygonInTileLat, vertex_buffer, my::VertexFormat::Float32,
                     offsetof(Vertex, in_tile_lat), sizeof(Vertex), my::VertexRate::PerVertex});
                streams.push_back(
                    {InputStreamPolygonPatternStyleIndex, vertex_buffer, my::VertexFormat::UInt32,
                     offsetof(Vertex, pattern_style_index), sizeof(Vertex),
                     my::VertexRate::PerVertex});
                streams.push_back(
                    {InputStreamPolygonFeatureIndex, vertex_buffer, my::VertexFormat::UInt32,
                     offsetof(Vertex, feature_index), sizeof(Vertex), my::VertexRate::PerVertex});
            }
            else
            {
                using Vertex = hrz::vt::FlatVectorGeometry::SolidColorPolygonVertex;

                auto geometry_data =
                    std::get<hrz::BlobArray<hrz::vt::FlatVectorGeometry::SolidColorPolygonVertex>>(
                        geometry.polygon_data)
                        .get_data();

                vb_res.size = geometry_data.size_bytes();
                vb_res.data = geometry_data.data();

                my::ResourceHandle vertex_buffer = render->rc->alloc(
                    &vb_res, hrz::monitoring::systems::FlatOverlays, tile->layer_id,
                    {{"contents"_ss, "polygon vertex data"_ss},
                     {"tile coords"_ss, tile_coords_str}});
                CHECK_RESOURCE_UPLOAD(vertex_buffer, "polygon vertex data");
                renderable.polygons_vertex_buffer = vertex_buffer;

                streams.push_back(
                    {InputStreamInTilePos, vertex_buffer, my::VertexFormat::Float32_3,
                     offsetof(Vertex, position), sizeof(Vertex), my::VertexRate::PerVertex});
                streams.push_back(
                    {InputStreamColor, vertex_buffer, my::VertexFormat::UInt8Norm_4,
                     offsetof(Vertex, color), sizeof(Vertex), my::VertexRate::PerVertex});
                streams.push_back(
                    {InputStreamPolygonFeatureIndex, vertex_buffer, my::VertexFormat::UInt32,
                     offsetof(Vertex, feature_index), sizeof(Vertex), my::VertexRate::PerVertex});
            }

            auto index_data = geometry.polygon_indices.get_cdata();

            my::BufferResource ib_res(my::BufferResource::BufferType::Index);
            ib_res.usage = my::UsageHint::Static;
            ib_res.size = index_data.size_bytes();
            ib_res.data = index_data.data();

            my::ResourceHandle index_buffer = render->rc->alloc(
                &ib_res, hrz::monitoring::systems::FlatOverlays, tile->layer_id,
                {{"contents"_ss, "polygon index data"_ss}, {"tile coords"_ss, tile_coords_str}});
            CHECK_RESOURCE_UPLOAD(index_buffer, "polygon index data");
            renderable.polygons_index_buffer = index_buffer;

            my::VertexInputResource vi_res;
            vi_res.indices = index_buffer;
            vi_res.attrib_count = streams.size();
            vi_res.attribs = streams.data();
            my::ResourceHandle vertex_input = render->rc->alloc(
                &vi_res, hrz::monitoring::systems::FlatOverlays, tile->layer_id,
                {{"tile coords"_ss, tile_coords_str}});

            renderable_data.vertex_input = vertex_input;
            renderable_data.vertex_count = index_data.size();

            if (tile->has_polygon_pattern)
            {
                renderable_data.shader = _polygons_pattern_visual_shader;
                renderable_data.picking_shader = _polygons_pattern_picking_shader;
                renderable_data.selection_shader = _polygons_pattern_selection_shader;

                renderable_data.pattern_texture = tile->pattern_texture;
                renderable_data.pattern_sampler = _pattern_sampler;
            }
            else
            {
                renderable_data.shader = _polygons_solid_color_visual_shader;
                renderable_data.picking_shader = _polygons_solid_color_picking_shader;
                renderable_data.selection_shader = _polygons_solid_color_selection_shader;
            }

            renderable_data.metadata_sampler = _metadata_sampler;
            renderable_data.feature_id_texture = renderable.feature_id_texture;
            renderable_data.selection_texture = renderable.selection_texture;
        }

        if (has_polyline_data)
        {
            using Instance = hrz::vt::FlatVectorGeometry::PolylineInstance;

            auto& renderable_data = renderable.data.polylines_data;
            auto geometry_data = geometry.polyline_data.get_data();

            my::BufferResource vb_res(my::BufferResource::BufferType::Vertex);
            vb_res.size = geometry_data.size_bytes();
            vb_res.usage = my::UsageHint::Static;
            vb_res.data = geometry_data.data();
            vb_res.allow_allocation_failure = true;

            my::ResourceHandle instance_buffer = render->rc->alloc(
                &vb_res, hrz::monitoring::systems::FlatOverlays, tile->layer_id,
                {{"contents"_ss, "polyline instance data"_ss},
                 {"tile coords"_ss, tile_coords_str}});
            CHECK_RESOURCE_UPLOAD(instance_buffer, "polyline instance data");
            renderable.polylines_instance_buffer = instance_buffer;

            my::VertexInputStream streams[] = {
                {InputStreamInMeshPos, _line_vertex_buffer, my::VertexFormat::Float32_3, 0, 0,
                 my::VertexRate::PerVertex},
                {InputStreamColor, instance_buffer, my::VertexFormat::UInt8Norm_4,
                 offsetof(Instance, color), sizeof(Instance), my::VertexRate::PerInstance},
                {InputStreamLineWidth, instance_buffer, my::VertexFormat::Float32,
                 offsetof(Instance, line_width), sizeof(Instance), my::VertexRate::PerInstance},
                {InputStreamLineGeometry, instance_buffer, my::VertexFormat::Float32_4,
                 offsetof(Instance, progress_at_start), sizeof(Instance),
                 my::VertexRate::PerInstance},
                {InputStreamLineFeatureIndex, instance_buffer, my::VertexFormat::UInt32,
                 offsetof(Instance, feature_index), sizeof(Instance), my::VertexRate::PerInstance},
                {InputStreamLinePos0, instance_buffer, my::VertexFormat::Float32_3,
                 offsetof(Instance, position0), sizeof(Instance), my::VertexRate::PerInstance},
                {InputStreamLinePos1, instance_buffer, my::VertexFormat::Float32_3,
                 offsetof(Instance, position1), sizeof(Instance), my::VertexRate::PerInstance},
                {InputStreamLineNormal0, instance_buffer, my::VertexFormat::UInt32,
                 offsetof(Instance, normal0), sizeof(Instance), my::VertexRate::PerInstance},
                {InputStreamLineNormal1, instance_buffer, my::VertexFormat::UInt32,
                 offsetof(Instance, normal1), sizeof(Instance), my::VertexRate::PerInstance},
                {InputStreamLineAnimationSpeed, instance_buffer, my::VertexFormat::Float32,
                 offsetof(Instance, animation_speed), sizeof(Instance),
                 my::VertexRate::PerInstance},
                {InputStreamLineTotalLength, instance_buffer, my::VertexFormat::Float32,
                 offsetof(Instance, line_total_length), sizeof(Instance),
                 my::VertexRate::PerInstance},
                {InputStreamLineEmptyColor, instance_buffer, my::VertexFormat::UInt8Norm_4,
                 offsetof(Instance, empty_color), sizeof(Instance), my::VertexRate::PerInstance},
            };

            my::VertexInputResource vi_res;
            vi_res.attrib_count = HRZ_ARRAY_COUNT(streams);
            vi_res.attribs = streams;
            my::ResourceHandle vertex_input = render->rc->alloc(
                &vi_res, hrz::monitoring::systems::FlatOverlays, tile->layer_id,
                {{"tile coords"_ss, tile_coords_str}});

            renderable_data.vertex_input = vertex_input;

            renderable_data.vertex_count = 6;
            renderable_data.instance_count = geometry_data.size();

            renderable_data.shader =
                tile->round_tips ? _polylines_round_visual_shader : _polylines_square_visual_shader;
            renderable_data.picking_shader = tile->round_tips ? _polylines_round_picking_shader
                                                              : _polylines_square_picking_shader;
            renderable_data.selection_shader = tile->round_tips
                ? _polylines_round_selection_shader
                : _polylines_square_selection_shader;
            renderable_data.metadata_sampler = _metadata_sampler;
            renderable_data.feature_id_texture = renderable.feature_id_texture;
            renderable_data.selection_texture = renderable.selection_texture;
        }

        if (has_point_data)
        {
            using Instance = hrz::vt::FlatVectorGeometry::PointInstance;

            auto& renderable_data = renderable.data.points_data;
            auto geometry_data = geometry.point_data.get_data();

            my::BufferResource pb_res(my::BufferResource::BufferType::Vertex);
            pb_res.size = geometry_data.size_bytes();
            pb_res.usage = my::UsageHint::Static;
            pb_res.data = geometry_data.data();
            pb_res.allow_allocation_failure = true;

            my::ResourceHandle instance_buffer = render->rc->alloc(
                &pb_res, hrz::monitoring::systems::FlatOverlays, tile->layer_id,
                {{"contents"_ss, "point instance data"_ss}, {"tile coords"_ss, tile_coords_str}});
            CHECK_RESOURCE_UPLOAD(instance_buffer, "point instance data");
            renderable.points_instance_buffer = instance_buffer;

            my::VertexInputStream streams[] = {
                {InputStreamInMeshPos, _point_vertex_buffer, my::VertexFormat::Float32_2, 0, 0,
                 my::VertexRate::PerVertex},
                {InputStreamPointsPosition, instance_buffer, my::VertexFormat::Float32_3,
                 offsetof(Instance, position), sizeof(Instance), my::VertexRate::PerInstance},
                {InputStreamPointsColor, instance_buffer, my::VertexFormat::UInt8Norm_4,
                 offsetof(Instance, color), sizeof(Instance), my::VertexRate::PerInstance},
                {InputStreamPointsRadius, instance_buffer, my::VertexFormat::Float32,
                 offsetof(Instance, disc_radius), sizeof(Instance), my::VertexRate::PerInstance},
                {InputStreamPointsFeatureIndex, instance_buffer, my::VertexFormat::UInt32,
                 offsetof(Instance, feature_index), sizeof(Instance), my::VertexRate::PerInstance},
            };

            my::VertexInputResource vi_res;
            vi_res.attrib_count = HRZ_ARRAY_COUNT(streams);
            vi_res.attribs = streams;
            my::ResourceHandle vertex_input = render->rc->alloc(
                &vi_res, hrz::monitoring::systems::FlatOverlays, tile->layer_id,
                {{"tile coords"_ss, tile_coords_str}});

            renderable_data.vertex_input = vertex_input;
            renderable_data.vertex_count = 4;
            renderable_data.instance_count = geometry_data.size();

            renderable_data.shader = _points_visual_shader;
            renderable_data.picking_shader = _points_picking_shader;
            renderable_data.selection_shader = _points_selection_shader;
            renderable_data.metadata_sampler = _metadata_sampler;
            renderable_data.feature_id_texture = renderable.feature_id_texture;
            renderable_data.selection_texture = renderable.selection_texture;
        }

        bool is_really_animated =
            geometry.is_animated && tile->ubo.dash_mode != (uint32_t)hrz_proto::DASH_DISABLED;

        renderable.data.is_animated = is_really_animated;
        renderable.data.scene_views = tile->scene_views;
        renderable.z_index = tile->z_index;
        renderable.bin_mask = hrz::RenderFlatOverlayBin;
        renderable.sea_center = geometry.sea_center;
        renderable.sea_radius = geometry.sea_radius;
        renderable.clamped_center = geometry.clamped_center;
        renderable.clamped_radius = geometry.clamped_radius;

        tile->geometry = std::nullopt;
        tile->renderable = std::move(renderable);
        tile->status = Tile::Status::Ready;

        send_status_update_message();

#undef CHECK_RESOURCE_UPLOAD
    }

    hrz::RenderRequest work_removed_tiles(WorkCtx& ctx)
    {
        hrz::RenderRequest render_request;

        for (TileH handle : _removed)
        {
            Tile* tile = _tiles.get_object(handle);
            if (!tile) continue;

            work_remove_tile(ctx, handle, tile);
            render_request.schedule_flat_overlay_render();
        }
        _removed.clear();

        return render_request;
    }

    hrz::RenderRequest work(WorkCtx& ctx) override
    {
        hrz::RenderRequest render_request;

        _channels.work();

        for (auto& it : _channels)
        {
            auto channel_id = it.first;
            auto& channel = it.second;

            for (auto& message : channel.receive())
            {
                std::visit(
                    [&](auto& message)
                    {
                        using MessageType = std::decay_t<decltype(message)>;
                        if constexpr (std::is_same_v<
                                          MessageType, hrz::vt::repr::messages::RegisterStyle>)
                        {
                            decltype(hrz::vt::repr::messages::StyleRegistrationResult::
                                         registered_properties) registered_properties;

                            auto handle = register_style(
                                message.repr, message.layer_id,
                                [repr_reg = ctx.repr_reg, layer_id = message.layer_id,
                                 &registered_properties = registered_properties](
                                    std::string_view name,
                                    const hrz::vector_data::OwnedAttributeValue& default_value)
                                    -> uint64_t
                                {
                                    uint64_t prp_id = repr_reg->register_property(layer_id, name);
                                    registered_properties.push_back({prp_id, default_value});
                                    return prp_id;
                                },
                                ConfigId{channel_id, message.style_id});

                            channel.send(hrz::vt::repr::messages::StyleRegistrationResult{
                                message.style_id, handle.has_value(),
                                std::move(registered_properties)});
                        }
                        else if constexpr (std::is_same_v<
                                               MessageType,
                                               hrz::vt::repr::messages::UnregisterStyle>)
                        {
                            auto it = _configs_by_id.find(ConfigId{channel_id, message.style_id});
                            if (it != _configs_by_id.end())
                            {
                                unregister_style(
                                    it->second,
                                    [repr_reg = ctx.repr_reg,
                                     layer_id = message.layer_id](uint64_t prp_id)
                                    { repr_reg->unregister_property(layer_id, prp_id); });
                                _configs_by_id.erase(it);
                            }
                            else
                            {
                                HRZ_LOG_WARNING("Cannot unregister style: style not found");
                            }
                        }
                        else if constexpr (std::is_same_v<
                                               MessageType, hrz::vt::repr::messages::AddTile>)
                        {
                            auto it = _configs_by_id.find(ConfigId{channel_id, message.style_id});
                            if (it != _configs_by_id.end())
                            {
                                add_tile(
                                    it->second, message.coords, message.layer_id,
                                    message.object_ref, message.feature_ref, message.feature_ids,
                                    message.geometry, message.style, message.min_elevation,
                                    message.max_elevation, TileId{channel_id, message.tile_id});
                            }
                            else
                            {
                                HRZ_LOG_ERROR("Cannot add tile: style not found");
                            }
                        }
                        else if constexpr (std::is_same_v<
                                               MessageType, hrz::vt::repr::messages::RemoveTile>)
                        {
                            auto it = _tiles_by_id.find(TileId{channel_id, message.tile_id});
                            if (it != _tiles_by_id.end())
                            {
                                remove_tile(it->second);
                                _tiles_by_id.erase(it);
                            }
                            else
                            {
                                HRZ_LOG_WARNING("Cannot remove tile: tile not found");
                            }
                        }
                        else if constexpr (std::is_same_v<
                                               MessageType,
                                               hrz::vt::repr::messages::UpdateTileElevation>)
                        {
                            auto it = _tiles_by_id.find(TileId{channel_id, message.tile_id});
                            if (it != _tiles_by_id.end())
                            {
                                set_tile_elevation(
                                    it->second, message.min_elevation, message.max_elevation);
                            }
                            else
                            {
                                HRZ_LOG_WARNING("Cannot set tile elevation: tile not found");
                            }
                        }
                        else if constexpr (std::is_same_v<
                                               MessageType, hrz::vt::repr::messages::UpdateClipId>)
                        {
                            // No-op
                        }
                        else if constexpr (std::is_same_v<
                                               MessageType,
                                               hrz::vt::repr::messages::UpdateLighting>)
                        {
                            // No-op
                        }
                        else if constexpr (std::is_same_v<
                                               MessageType,
                                               hrz::vt::repr::messages::UpdateSelection>)
                        {
                            auto it = _tiles_by_id.find(TileId{channel_id, message.tile_id});
                            if (it != _tiles_by_id.end())
                            {
                                update_selection(it->second, message.selected_objects);
                            }
                            else
                            {
                                HRZ_LOG_ERROR("Cannot update selection: tile not found");
                            }
                        }
                        else
                        {
                            static_assert(hrz::always_false<MessageType>, "Unhandled case");
                        }
                    },
                    message);
            }
        }

        for (auto it = _loading_configs.begin(); it != _loading_configs.end();)
        {
            ConfigH handle = *it;
            Config* config = _configs.get_object(handle);
            bool erase = false;

            if (config)
            {
                erase = work_load_config(ctx, handle, config);
            }
            else
            {
                erase = true;
            }

            if (erase)
            {
                _loading_configs.erase(it++);
            }
            else
            {
                ++it;
            }
        }

        render_request |= work_removed_tiles(ctx);

        for (auto it = _waiting_for_config.begin(); it != _waiting_for_config.end();)
        {
            TileH tile_handle = it->first;
            ConfigH config_handle = it->second;

            Tile* tile = _tiles.get_object(tile_handle);
            Config* config = _configs.get_object(config_handle);

            bool erase = false;

            if (tile)
            {
                erase = work_waiting_for_config(ctx, tile_handle, tile, config);
            }
            else
            {
                erase = true;
            }

            if (erase)
            {
                _waiting_for_config.erase(it++);
            }
            else
            {
                ++it;
            }
        }

        for (TileH handle : _to_bake)
        {
            Tile* tile = _tiles.get_object(handle);
            if (!tile) continue;

            work_start_baking(ctx, handle, tile);
        }
        _to_bake.clear();

        for (auto it = _baking.begin(); it != _baking.end();)
        {
            TileH handle = *it;
            Tile* tile = _tiles.get_object(handle);
            bool erase = false;

            if (tile)
            {
                erase = work_continue_baking(ctx, handle, tile);
            }
            else
            {
                erase = true;
            }

            if (erase)
            {
                _baking.erase(it++);
            }
            else
            {
                ++it;
            }
        }

        // Animated features require a constant redrawing of the flat overlays as long as they are
        // visible on screen.
        // Note: We track the amount of consecutive frames drawn without animated features because
        // there is a one frame delay between a flat overlay render request and the actual render.
        // This information is necessary to avoid standby frames between flat overlay renders.
        if (_draw_report.animated_features_drawn > 0)
        {
            _frames_since_last_animation_draw = 0;
        }
        else
        {
            _frames_since_last_animation_draw++;
        }

        if (_frames_since_last_animation_draw < 2)
        {
            render_request.request_visual_render(hrz::RenderRequest::VisualCause::Animation);
            render_request.schedule_flat_overlay_render();
        }

        _draw_report.reset();

        return render_request;
    }

    hrz::RenderRequest work_gpu(WorkGpuCtx& ctx) override
    {
        hrz::RenderRequest render_request;

        _empty_selection_storage.work_gpu(ctx.render);

        for (TileH handle : _finished_baking)
        {
            Tile* tile = _tiles.get_object(handle);
            if (!tile) continue;

            work_finished_baking(ctx.render, handle, tile);
        }
        _finished_baking.clear();

        for (auto image : _images_to_free)
        {
            hrz::vt::image_loader::release_image(ctx.il, image);
        }
        _images_to_free.clear();

        for (my::ResourceHandle res : _resources_to_free)
        {
            ctx.render->rc->dealloc(res);
        }
        _resources_to_free.clear();

        return render_request;
    }

    void draw(DrawCtx& ctx) override
    {
        for (const auto& entry : _drawn_tiles)
        {
            Tile* tile = _tiles.get_object(entry.first);
            if (!tile) continue;

            if (tile->renderable.has_value())
            {
                RenderableFeatures& renderable = tile->renderable.value();
                renderable.data.scene_views = tile->scene_views & entry.second;
                renderable.main_views = ctx.render->main_views;

                if (tile->has_feature_ids)
                {
                    renderable.selection_storage.work_gpu(ctx.render);
                }

                ctx.render->rd->collect_renderable(renderable);
            }
        }
        _drawn_tiles.clear();
    }

    void update_selection(
        TileH handle,
        const hrz::flat_hash_set<hrz::vector_data::FeatureIdHash>& selected_objects)
    {
        Tile* tile = _tiles.get_object(handle);
        if (tile && tile->status == Tile::Status::Ready && tile->renderable.has_value()
            && tile->has_feature_ids)
        {
            RenderableFeatures& renderable = tile->renderable.value();

            renderable.selection_storage.update_selection(selected_objects);

            bool has_selected_features = renderable.selection_storage.has_any_selected();
            renderable.data.polygons_data.has_selected_features = has_selected_features;
            renderable.data.polylines_data.has_selected_features = has_selected_features;
            renderable.data.points_data.has_selected_features = has_selected_features;
        }
    }

    std::pair<uint64_t, Channel> create_channel() override { return _channels.create_channel(); }
};

} // namespace

namespace hrz::vt
{
std::unique_ptr<ReprSystem> create_flat_overlay_repr_system()
{
    return std::unique_ptr<ReprSystem>(new FlatOverlayReprSystem());
}

void collect_flat_overlay_shaders(hrz::GpuResourceContext* rc)
{
    FlatOverlayReprSystem::collect_shaders(rc);
}
} // namespace hrz::vt
