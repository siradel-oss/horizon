// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/common/blob_array.h"
#include "hrz/common/color.h"
#include "hrz/common/fmt.h" // IWYU pragma: keep
#include "hrz/common/monitoring_defs.h"
#include "hrz/core/channel_group.h"
#include "hrz/core/global_flags.h"
#include "hrz/core/jobs/jobs_tickets.h"
#include "hrz/core/jobs/vector_tiles_jobs_params.h"
#include "hrz/core/render/context.h"
#include "hrz/core/render/defs.h"
#include "hrz/core/render/lighting_settings.h"
#include "hrz/core/render/resource_context.h"
#include "hrz/core/render/resources.h"
#include "hrz/core/selection_storage.h"
#include "hrz/core/shaders/collection.h"
#include "hrz/core/shadows.h"
#include "hrz/core/sky.h"
#include "hrz/core/vector/repr.h"
#include "hrz/core/viewsheds.h"
#include "hrz/fnd/gen_object_pool.h"
#include "hrz/fnd/meta.h"

#include <optional>

namespace
{

constexpr uint32_t CircleResolution = 12;

enum
{
    UboTileParams = hrz::UboCustomStart,

    InputStreamVertexPos = 0,
    InputStreamColor = 1,
    InputStreamRadii = 2,
    InputStreamInstancePos0 = 3,
    InputStreamNormal0 = 4,
    InputStreamInstancePos1 = 5,
    InputStreamNormal1 = 6,
    InputStreamGeometry = 7,
    InputStreamAnimationSpeed = 8,
    InputStreamSecondaryColor = 9,
    InputStreamTotalLength = 10,
    InputStreamFeatureIndex = 11,
    InputStreamFeatureId = 12,
    InputStreamSelection = 13,
};

struct TileUniformData
{
    lm::vec4 center_low;
    lm::vec4 center_high;
    lm::uvec2 object_ref;
    int32_t clip_id;
    hrz::bool32 lighting_enabled;
    hrz::bool32 receive_shadows;
    uint32_t dash_mode;
    uint32_t dash_period_unit;
    uint32_t dash_primary_length_unit;
    lm::uvec3 feature_ref;
    uint32_t animation_speed_unit;
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
    lm::dvec3 center;
    double radius;

    my::ResourceHandle instance_data_buffer = my::ResourceHandle::null();

    hrz::selection::SelectionStorageUint32BufferMultiIndex selection_storage;

    struct RenderData
    {
        my::ResourceHandle vertex_input = my::ResourceHandle::null();
        my::ResourceHandle shader = my::ResourceHandle::null();
        my::ResourceHandle picking_shader = my::ResourceHandle::null();
        my::ResourceHandle depth_shader = my::ResourceHandle::null();
        my::ResourceHandle selection_shader = my::ResourceHandle::null();
        my::ResourceHandle ubo_buffer = my::ResourceHandle::null();
        uint32_t vertex_count;
        uint32_t instance_count;
        uint32_t scene_views;
        bool has_any_object_selected;
        bool cast_shadows;
        bool is_animated;
        my::CullModifier cull_modifier;

        DrawReport* draw_report;
    } data;

    static void render_callback(
        uint32_t render_type,
        my::RenderContext* r,
        my::ResourceBinder* rb,
        const void* user_data_raw,
        const void* raw_data)
    {
        const auto* data = (const RenderData*)raw_data;
        const auto* user_data = (const hrz::SceneViewRenderGraphUserData*)user_data_raw;

        if (((1 << user_data->scene_view) & data->scene_views) == 0) return;

        my::ResourceHandle shader;
        switch (render_type)
        {
            case hrz::RenderVisual: shader = data->shader; break;
            case hrz::RenderPicking: shader = data->picking_shader; break;
            case hrz::RenderShadows:
            {
                if (!data->cast_shadows) return;
                shader = data->depth_shader;
                break;
            }
            case hrz::RenderViewshed: shader = data->depth_shader; break;
            case hrz::RenderSelection:
            {
                if (!data->has_any_object_selected)
                {
                    return;
                }

                shader = data->selection_shader;
                break;
            }
            default: return;
        }

        auto batch = my::DrawBatchInfo(my::PrimitiveType::TriangleList, data->vertex_count)
                         .indexed(my::IndexType::UShort)
                         .instanced(data->instance_count);
        batch.cull_modifier = data->cull_modifier;

        rb->push_state();

        my::UboBinding ubo_bindings[] = {
            {UboTileParams, data->ubo_buffer, 0, sizeof(TileUniformData)}
        };
        rb->bind(ubo_bindings);

        auto state = rb->get_current_state();

        r->draw(batch, shader, data->vertex_input, state.ubos, state.textures);

        data->draw_report->features_drawn += 1;
        data->draw_report->animated_features_drawn += data->is_animated ? 1 : 0;

        rb->pop_state();
    }

    void collect_render_info(my::Renderer::Queue& queue, const my::Renderer::Culler& culler)
        const override
    {
        if (culler.is_visible_in_any_view(center, radius, bin_mask))
        {
            queue.enqueue(bin_mask, render_callback, data, center, radius);
        }
    }
};

struct Config
{
    uint32_t repr_id = -1;

    uint64_t radius_prp = hrz::style::Parser::INVALID_PROPERTY;
    uint64_t altitude_offset_prp = hrz::style::Parser::INVALID_PROPERTY;
    uint64_t color_prp = hrz::style::Parser::INVALID_PROPERTY;
    uint64_t secondary_color_prp = hrz::style::Parser::INVALID_PROPERTY;
    uint64_t dash_period_prp = hrz::style::Parser::INVALID_PROPERTY;
    uint64_t dash_primary_length_prp = hrz::style::Parser::INVALID_PROPERTY;
    uint64_t animation_speed_prp = hrz::style::Parser::INVALID_PROPERTY;

    lm::ubvec4 default_color_srgb = {0, 0, 0, 0};
    lm::ubvec4 default_secondary_color_srgb = {0, 0, 0, 0};
    float default_radius = 0;
    float default_altitude_offset = 0;
    hrz_proto::DashMode dash_mode{};
    hrz_proto::DashSizeUnit dash_period_unit{};
    hrz_proto::DashSizeUnit dash_primary_length_unit{};
    hrz_proto::DashSizeUnit animation_speed_unit{};
    float default_dash_period = 0;
    float default_primary_dash_length = 0;
    float default_animation_speed = 0;
    bool disable_face_culling = false;
    uint32_t scene_views = 0;
    hrz::render::LightingSettings lighting_settings;
};

struct TileGeometry
{
    lm::dvec3 center;
    double bsphere_radius;
    lm::dvec3 bsphere_center;
    bool has_transparency;
    bool is_animated;
    hrz::BlobArray<hrz_jobs::CylinderVectorGeometry::Instance> instance_data;
};

struct TileId
{
    uint64_t channel_id;
    uint64_t tile_id;

    constexpr bool operator ==(const TileId& other) const = default;

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

    hrz_jobs::BakeCylinderVectorGeometryTicket bake_ticket;
    std::optional<hrz_jobs::CylinderVectorData> bake_data;
    std::optional<TileGeometry> geometry;
    std::optional<RenderableFeatures> renderable;

    bool ubo_dirty;
    TileUniformData ubo;
    bool cast_shadows;
    bool disable_face_culling;
    uint32_t scene_views;
    hrz::render::LightingSettings lighting_settings_on_config;
};

class CylinderReprSystem : public hrz::vt::ReprSystem
{
    using ConfigH = uint64_t;
    using TileH = uint64_t;

    using ConfigIndexPool = hrz::GenIndexPool<ConfigH, 32, 32>;
    using ConfigPool = hrz::GenObjectPool<Config, ConfigIndexPool, 64>;

    using TileIndexPool = hrz::GenIndexPool<TileH, 32, 32>;
    using TilePool = hrz::GenObjectPool<Tile, TileIndexPool, 64>;

    my::ResourceHandle _cylinder_vertex_buffer;
    my::ResourceHandle _cylinder_index_buffer;

    my::ResourceHandle _opaque_shader;
    my::ResourceHandle _transparent_shader;
    my::ResourceHandle _picking_shader;
    my::ResourceHandle _depth_shader;
    my::ResourceHandle _selection_shader;

    ConfigPool _configs;

    struct ConfigId
    {
        uint64_t channel_id;
        uint64_t config_id;

        constexpr bool operator ==(const ConfigId& other) const = default;

        template<typename H>
        friend H AbslHashValue(H h, const ConfigId& request)
        {
            return H::combine(std::move(h), request.channel_id, request.config_id);
        }
    };

    hrz::flat_hash_map<ConfigId, ConfigH> _configs_by_id;

    TilePool _tiles;

    hrz::flat_hash_map<TileId, TileH> _tiles_by_id;

    DrawReport _draw_report;

    hrz::flat_hash_set<std::pair<TileH, uint32_t>> _drawn_tiles;

    hrz::flat_hash_set<TileH> _to_bake;
    hrz::flat_hash_set<TileH> _baking;
    hrz::flat_hash_set<TileH> _finished_baking;
    hrz::flat_hash_set<TileH> _removed;

    std::vector<my::ResourceHandle> _resources_to_free;

    hrz::ChannelGroup<hrz::vt::FromReprMessage, hrz::vt::ToReprMessage> _channels;

public:
    ~CylinderReprSystem() override = default;

    void deinit(WorkCtx& ctx, hrz::Render*) override { _work_removed(ctx); }

    static void collect_shaders(hrz::GpuResourceContext* rc)
    {
        my::IndexName attribs[] = {
            {InputStreamVertexPos, "i_vertex_pos"},
            {InputStreamColor, "i_color"},
            {InputStreamRadii, "i_radii"},
            {InputStreamInstancePos0, "i_instance_pos0"},
            {InputStreamInstancePos1, "i_instance_pos1"},
            {InputStreamNormal0, "i_normal0"},
            {InputStreamNormal1, "i_normal1"},
            {InputStreamGeometry, "i_geometry"},
            {InputStreamAnimationSpeed, "i_animation_speed"},
            {InputStreamSecondaryColor, "i_secondary_color"},
            {InputStreamTotalLength, "i_total_length"},
            {InputStreamFeatureIndex, "i_feature_index"},
            {InputStreamFeatureId, "i_feature_id"},
            {InputStreamSelection, "i_selection"},
        };

        static const my::IndexName ubos[] = {
            {hrz::UboFrame, "Frame"},
            {UboTileParams, "Tile"},
        };

        static const my::IndexName ubos_depth[] = {
            {hrz::UboFrame, "Frame"},
            {hrz::UboView, "View"},
            {UboTileParams, "Tile"},
        };

        const char* color_outputs[] = {"o_color"};

        std::vector<my::IndexName> samplers;

        samplers.push_back({hrz::SamplerCameraHeight, "u_camera_height"});
        uint32_t non_visual_sampler_count = (uint32_t)samplers.size();

        if (get_flag(hrz::Flag::EnableAtmosphere))
        {
            samplers.push_back({hrz::SamplerSunColor, hrz::sky::SUN_COLOR_SAMPLER_NAME});
        }

        if (get_flag(hrz::Flag::EnableShadows))
        {
            for (int i = 0; i < HRZ_S_MAX_SUN_CASCADES; ++i)
            {
                samplers.push_back(
                    {hrz::SamplerSunShadow0 + i, hrz::shadows::SUN_SHADOW_MAP_SAMPLER_NAMES[i]});
            }
        }

        for (int i = 0; i < HRZ_S_VIEWSHED_CNT; ++i)
        {
            samplers.push_back(
                {hrz::SamplerViewshedShadow0 + i,
                 hrz::viewsheds::VIEWSHED_SHADOW_MAP_SAMPLER_NAMES[i]});
        }

        my::ShaderResource res{};
        res.name = hrz_shaders::Cylinders_name;
        res.vertex_source_len = hrz_shaders::Cylinders_vert_len;
        res.vertex_source = hrz_shaders::Cylinders_vert;
        res.fragment_source_len = hrz_shaders::Cylinders_frag_len;
        res.fragment_source = hrz_shaders::Cylinders_frag;
        res.attribs = attribs;
        res.uniform_blocks = ubos;
        res.samplers = samplers;
        res.outputs = color_outputs;
        res.initial_state.color_blend.enable = false;
        auto opaque_shader = rc->alloc(&res, hrz::monitoring::systems::Cylinders);

        my::ShaderDerivativeResource res_d(opaque_shader, res, "Cylinders_transparent");
        res_d.initial_state.color_blend.enable = true;
        res_d.initial_state.color_blend.color.src = my::ColorBlendState::One;
        res_d.initial_state.color_blend.color.dst = my::ColorBlendState::OneMinusSrcAlpha;
        res_d.initial_state.color_blend.alpha.src = my::ColorBlendState::One;
        res_d.initial_state.color_blend.alpha.dst = my::ColorBlendState::OneMinusSrcAlpha;
        rc->alloc(&res_d, hrz::monitoring::systems::Cylinders);

        const char* picking_color_outputs[] = {"o_object_reference", "o_depth"};

        res.name = hrz_shaders::Cylinders_picking_name;
        res.vertex_source_len = hrz_shaders::Cylinders_picking_vert_len;
        res.vertex_source = hrz_shaders::Cylinders_picking_vert;
        res.fragment_source_len = hrz_shaders::Cylinders_picking_frag_len;
        res.fragment_source = hrz_shaders::Cylinders_picking_frag;
        res.outputs = picking_color_outputs;
        res.samplers = {samplers.data(), non_visual_sampler_count};
        res.initial_state.color_blend.enable = false;
        rc->alloc(&res, hrz::monitoring::systems::Cylinders);

        const char* selection_outputs[] = {"o_highlight"};

        res.name = hrz_shaders::Cylinders_selection_name;
        res.vertex_source_len = hrz_shaders::Cylinders_selection_vert_len;
        res.vertex_source = hrz_shaders::Cylinders_selection_vert;
        res.fragment_source_len = hrz_shaders::Cylinders_selection_frag_len;
        res.fragment_source = hrz_shaders::Cylinders_selection_frag;
        res.outputs = selection_outputs;
        rc->alloc(&res, hrz::monitoring::systems::Cylinders);

        res.name = hrz_shaders::Cylinders_depth_name;
        res.vertex_source_len = hrz_shaders::Cylinders_depth_vert_len;
        res.vertex_source = hrz_shaders::Cylinders_depth_vert;
        res.fragment_source_len = hrz_shaders::Cylinders_depth_frag_len;
        res.fragment_source = hrz_shaders::Cylinders_depth_frag;
        res.outputs = {};
        res.uniform_blocks = ubos_depth;
        res.initial_state.rasterization.depth_bias_factor = 1.0F;
        res.initial_state.rasterization.depth_bias_units = 1.0F;
        rc->alloc(&res, hrz::monitoring::systems::Cylinders);
    }

    void init_render(hrz::Render* render) override
    {
        assert(render);

        // We generate a cylinder with 1 radius and 1 length, extruded along the Z
        // axis that we'll then instantiate along each segment of our polylines.
        // Technically we could generate the geometry in the vertex shaders with
        // gl_VertexId but anyway it's cheap.

        // In a normalized circle, the normals are the coordinates or the points,
        // so that's nice, we don't need to compute normals.
        std::vector<lm::vec3> points;
        std::vector<uint16_t> indices;

        points.reserve(CircleResolution * 2);
        indices.reserve(CircleResolution * 6);

        for (uint32_t i = 0; i < CircleResolution; ++i)
        {
            float x = (float)std::cos((double)i * 2.0 * lm::PI / CircleResolution);
            float y = (float)std::sin((double)i * 2.0 * lm::PI / CircleResolution);

            points.push_back(lm::vec3(x, y, 0.0F));
            points.push_back(lm::vec3(x, y, 1.0F));
        }

        for (uint32_t i = 0; i < CircleResolution; ++i)
        {
            int p0 = i;
            int p1 = (i + 1) % CircleResolution;

            indices.push_back(p0 * 2 + 0);
            indices.push_back(p1 * 2 + 0);
            indices.push_back(p1 * 2 + 1);

            indices.push_back(p0 * 2 + 0);
            indices.push_back(p1 * 2 + 1);
            indices.push_back(p0 * 2 + 1);
        }

        my::BufferResource vertex_buffer(my::BufferResource::BufferType::Vertex);
        vertex_buffer.size = points.size() * sizeof(lm::vec3);
        vertex_buffer.usage = my::UsageHint::Static;
        vertex_buffer.data = &points.data()->x;

        my::BufferResource index_buffer(my::BufferResource::BufferType::Index);
        index_buffer.size = indices.size() * sizeof(uint16_t);
        index_buffer.usage = my::UsageHint::Static;
        index_buffer.data = indices.data();

        _cylinder_vertex_buffer = render->rc->alloc(
            &vertex_buffer, hrz::monitoring::systems::Cylinders,
            {{"contents"_ss, "vertex positions"_ss}});
        _cylinder_index_buffer = render->rc->alloc(
            &index_buffer, hrz::monitoring::systems::Cylinders, {{"contents"_ss, "indices"_ss}});

        _opaque_shader = render->rc->retrieve_shader(hrz_shaders::Cylinders_name);
        _transparent_shader = render->rc->retrieve_shader("Cylinders_transparent");
        _picking_shader = render->rc->retrieve_shader(hrz_shaders::Cylinders_picking_name);
        _selection_shader = render->rc->retrieve_shader(hrz_shaders::Cylinders_selection_name);
        _depth_shader = render->rc->retrieve_shader(hrz_shaders::Cylinders_depth_name);
    }

    void deinit_render(hrz::Render* render) override
    {
        assert(render);

        render->rc->dealloc(_cylinder_vertex_buffer);
        render->rc->dealloc(_cylinder_index_buffer);
    }

    std::optional<ConfigH> register_style(
        const hrz_proto::VectorRepr& repr,
        uint64_t,
        const std::function<
            uint64_t(std::string_view name, const hrz::vector_data::OwnedAttributeValue&)
        >& register_prp,
        ConfigId style_id)
    {
        if (repr.repr_type_case() != hrz_proto::VectorRepr::ReprTypeCase::kCylinder)
        {
            HRZ_LOG_ERROR("Unexpected vector representation type, expected cylinder...");
            return std::nullopt;
        }

        if (_configs_by_id.contains(style_id))
        {
            HRZ_LOG_ERROR(
                "Cannot register style: ID {}-{} already in use", style_id.channel_id,
                style_id.config_id);
            return std::nullopt;
        }

        std::string_view radius_prp_name = repr.cylinder().radius().name();
        std::string_view altitude_offset_prp_name = repr.cylinder().altitude_offset().name();
        std::string_view color_prp_name = repr.cylinder().color().name();
        std::string_view secondary_color_prp_name =
            repr.cylinder().dashes().secondary_color().name();
        std::string_view dash_period_prp_name = repr.cylinder().dashes().period().name();
        std::string_view dash_primary_length_prp_name =
            repr.cylinder().dashes().primary_segment_length().name();
        std::string_view animation_speed_prp_name =
            repr.cylinder().dashes().animation_speed().name();

        Config config;
        config.repr_id = repr.id();

        config.default_radius = repr.cylinder().radius().default_value();
        config.default_color_srgb =
            hrz::convert_proto_color_to_bytes(repr.cylinder().color().default_value());
        config.default_altitude_offset = repr.cylinder().altitude_offset().default_value();
        config.default_dash_period = repr.cylinder().dashes().period().default_value();
        config.default_primary_dash_length =
            repr.cylinder().dashes().primary_segment_length().default_value();
        config.default_animation_speed = repr.cylinder().dashes().animation_speed().default_value();
        config.default_secondary_color_srgb = hrz::convert_proto_color_to_bytes(
            repr.cylinder().dashes().secondary_color().default_value());

        config.radius_prp = register_prp(radius_prp_name, config.default_radius);
        config.altitude_offset_prp =
            register_prp(altitude_offset_prp_name, config.default_altitude_offset);
        config.color_prp = register_prp(
            color_prp_name,
            hrz::vector_data::attr_from_color<hrz::vector_data::OwnedAttributeValue>(
                config.default_color_srgb));
        config.secondary_color_prp = register_prp(
            secondary_color_prp_name,
            hrz::vector_data::attr_from_color<hrz::vector_data::OwnedAttributeValue>(
                config.default_secondary_color_srgb));
        config.dash_period_prp = register_prp(dash_period_prp_name, config.default_dash_period);
        config.dash_primary_length_prp =
            register_prp(dash_primary_length_prp_name, config.default_primary_dash_length);
        config.animation_speed_prp =
            register_prp(animation_speed_prp_name, config.default_animation_speed);

        config.dash_mode = repr.cylinder().dashes().mode();
        config.dash_period_unit = repr.cylinder().dashes().period_unit();
        config.dash_primary_length_unit = repr.cylinder().dashes().primary_segment_length_unit();
        config.animation_speed_unit = repr.cylinder().dashes().animation_speed_unit();
        config.disable_face_culling = repr.cylinder().disable_face_culling();
        config.scene_views = repr.scene_views().bits();
        config.lighting_settings = hrz::render::from_proto(repr.cylinder().lighting());

        auto handle = _configs.alloc(std::move(config));

        _configs_by_id.insert({style_id, handle});

        return handle;
    }

    void unregister_style(
        ConfigH config_handle,
        const std::function<void(uint64_t prp_id)>& unregister_property)
    {
        if (!_configs.is_valid(config_handle)) return;

        const Config* cfg = _configs.get_object(config_handle);

        unregister_property(cfg->radius_prp);
        unregister_property(cfg->altitude_offset_prp);
        unregister_property(cfg->color_prp);
        unregister_property(cfg->secondary_color_prp);
        unregister_property(cfg->dash_period_prp);
        unregister_property(cfg->dash_primary_length_prp);
        unregister_property(cfg->animation_speed_prp);

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

        hrz_jobs::CylinderVectorData bake_data;

        Config cfg;
        Config* cfg_src = _configs.get_object(config_handle);
        if (cfg_src)
        {
            cfg = *cfg_src;
        }
        else
        {
            HRZ_LOG_WARNING("Unknown config. Using default values.");
        }

        bake_data.default_color_srgb = cfg.default_color_srgb;
        bake_data.default_radius = cfg.default_radius;
        bake_data.default_altitude_offset = cfg.default_altitude_offset;
        bake_data.dashes.default_secondary_color_srgb = cfg.default_secondary_color_srgb;
        bake_data.dashes.default_period = cfg.default_dash_period;
        bake_data.dashes.default_primary_length = cfg.default_primary_dash_length;
        bake_data.dashes.default_animation_speed = cfg.default_animation_speed;
        bake_data.repr_id = cfg.repr_id;

        bake_data.color_prp = cfg.color_prp;
        bake_data.radius_prp = cfg.radius_prp;
        bake_data.altitude_offset_prp = cfg.altitude_offset_prp;
        bake_data.dashes.secondary_color_prp = cfg.secondary_color_prp;
        bake_data.dashes.period_prp = cfg.dash_period_prp;
        bake_data.dashes.primary_length_prp = cfg.dash_primary_length_prp;
        bake_data.dashes.animation_speed_prp = cfg.animation_speed_prp;
        bake_data.dashes.mode = cfg.dash_mode;

        bake_data.feature_ids = feature_ids.hashes();
        bake_data.geometry = geometry.geometry;

        bake_data.clamping.CopyFrom(geometry.clamping);
        bake_data.clamps = geometry.clamps;

        auto& dst_style = bake_data.style;
        dst_style.prps = style.prps;
        dst_style.values = style.values;
        dst_style.out_of_line_data = style.out_of_line_data;
        dst_style.instances = style.instances;

        tile.ubo.object_ref = object_ref.to_uvec2();
        tile.ubo.feature_ref = feature_ref.to_uvec3();
        tile.ubo.clip_id = -1;
        tile.ubo.lighting_enabled = cfg.lighting_settings.lighting_enabled;
        tile.ubo.receive_shadows = cfg.lighting_settings.receive_shadows;
        tile.ubo.dash_mode = cfg.dash_mode;
        tile.ubo.dash_period_unit = cfg.dash_period_unit;
        tile.ubo.dash_primary_length_unit = cfg.dash_primary_length_unit;
        tile.ubo.animation_speed_unit = cfg.animation_speed_unit;
        tile.ubo_dirty = true;

        tile.scene_views = cfg.scene_views;
        tile.lighting_settings_on_config = cfg.lighting_settings;
        tile.cast_shadows = cfg.lighting_settings.cast_shadows;
        tile.bake_data = std::move(bake_data);
        tile.status = Tile::Status::ReadyToBake;
        tile.disable_face_culling = cfg.disable_face_culling;
        TileH handle = _tiles.alloc(std::move(tile));
        _to_bake.insert(handle);

        _tiles_by_id.insert({tile_id, handle});

        return handle;
    }

    void schedule_draw_now(uint64_t channel_id, uint64_t tile_id, uint32_t views_bitset) override
    {
        auto it = _tiles_by_id.find({channel_id, tile_id});
        if (it != _tiles_by_id.end())
        {
            _drawn_tiles.insert(std::make_pair(it->second, views_bitset));
        }
    }

    void remove_tile(TileH handle) { _removed.insert(handle); }

    void work_remove_tile(WorkCtx& ctx, TileH handle, Tile* tile)
    {
        if (tile->status == Tile::Status::Baking)
        {
            hrz_jobs::cancel_job(ctx.js, tile->bake_ticket);
        }

        if (tile->renderable.has_value())
        {
            auto& renderable = tile->renderable.value();
            _resources_to_free.push_back(renderable.instance_data_buffer);
            _resources_to_free.push_back(renderable.data.vertex_input);
            _resources_to_free.push_back(renderable.data.ubo_buffer);
            renderable.selection_storage.free_gpu_resources(_resources_to_free);
        }

        _tiles.release(handle);
    }

    void work_start_baking(WorkCtx& ctx, TileH handle, Tile* tile)
    {
        assert(tile->status == Tile::Status::ReadyToBake);
        assert(tile->bake_data.has_value());

        tile->bake_ticket = hrz_jobs::add_job_bake_cylinder_vector_geometry(
            ctx.js, std::move(tile->bake_data).value(),
            {hrz::monitoring::systems::Cylinders, tile->layer_id});
        tile->bake_data = std::nullopt;
        tile->status = Tile::Status::Baking;
        _baking.insert(handle);
    }

    // Returns whether this tile should be erased from the _baking set or not.
    bool work_continue_baking(WorkCtx& ctx, TileH handle, Tile* tile)
    {
        assert(tile->status == Tile::Status::Baking);

        if (hrz_jobs::is_job_valid(ctx.js, tile->bake_ticket)
            && hrz_jobs::is_job_finished(ctx.js, tile->bake_ticket))
        {
            if (hrz_jobs::get_job_status(ctx.js, tile->bake_ticket)
                == hrz::job_scheduler::JobStatus::Finished_Success)
            {
                auto response = hrz_jobs::get_job_response(ctx.js, tile->bake_ticket);

                TileGeometry geometry;

                geometry.center = response.center;
                geometry.bsphere_radius = response.bsphere_radius;
                geometry.bsphere_center = response.bsphere_center;
                geometry.has_transparency = response.has_transparency;
                geometry.is_animated = response.is_animated;
                geometry.instance_data = std::move(response.instance_data);

                tile->geometry = std::move(geometry);
                tile->status = Tile::Status::FinishedBaking;
                _finished_baking.insert(handle);
            }
            else
            {
                HRZ_LOG_ERROR(
                    "Could not bake cylinder representation for tile {}-{}-{}", tile->coords.lod,
                    tile->coords.x, tile->coords.y);
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

#define CHECK_RESOURCE_UPLOAD(resource, resource_type)                                 \
    if (resource.is_null())                                                            \
    {                                                                                  \
        HRZ_LOG_ERROR(                                                                 \
            "Could not upload cylinder {} of tile {}-{}-{} to the GPU", resource_type, \
            tile->coords.lod, tile->coords.x, tile->coords.y);                         \
        tile->status = Tile::Status::Error;                                            \
        send_status_update_message();                                                  \
        return;                                                                        \
    }

        auto tile_coords_str = fmt::to_string(tile->coords);

        auto& geometry = tile->geometry.value();
        RenderableFeatures renderable;

        using Instance = hrz_jobs::CylinderVectorGeometry::Instance;

        size_t instance_count = geometry.instance_data.size();

        auto instance_data = geometry.instance_data.get_data();
        my::BufferResource vb_res(my::BufferResource::BufferType::Vertex);
        vb_res.size = instance_data.size_bytes();
        vb_res.usage = my::UsageHint::Static;
        vb_res.data = instance_data.data();
        vb_res.allow_allocation_failure = true;

        my::ResourceHandle instance_data_buffer = render->rc->alloc(
            &vb_res, hrz::monitoring::systems::Cylinders, tile->layer_id,
            {{"contents"_ss, "instance data"_ss}, {"tile coords"_ss, tile_coords_str}});
        CHECK_RESOURCE_UPLOAD(instance_data_buffer, "instance data buffer");
        renderable.instance_data_buffer = instance_data_buffer;

        hrz::selection::SelectionStorageUint32BufferMultiIndex selection_storage(
            instance_count, {hrz::monitoring::systems::Cylinders, tile->layer_id},
            {{"tile coords"_ss, tile_coords_str}});

        my::VertexInputStream streams[] = {
            {InputStreamVertexPos, _cylinder_vertex_buffer, my::VertexFormat::Float32_3, 0, 0,
             my::VertexRate::PerVertex},

            {InputStreamColor, instance_data_buffer, my::VertexFormat::UInt8Norm_4,
             offsetof(Instance, color), sizeof(Instance), my::VertexRate::PerInstance},
            {InputStreamSecondaryColor, instance_data_buffer, my::VertexFormat::UInt8Norm_4,
             offsetof(Instance, alternative_color), sizeof(Instance), my::VertexRate::PerInstance},

            {InputStreamRadii, instance_data_buffer, my::VertexFormat::Float32_2,
             offsetof(Instance, radii), sizeof(Instance), my::VertexRate::PerInstance},
            {InputStreamTotalLength, instance_data_buffer, my::VertexFormat::Float32,
             offsetof(Instance, line_total_length), sizeof(Instance), my::VertexRate::PerInstance},

            {InputStreamInstancePos0, instance_data_buffer, my::VertexFormat::Float32_3,
             offsetof(Instance, position0), sizeof(Instance), my::VertexRate::PerInstance},
            {InputStreamNormal0, instance_data_buffer, my::VertexFormat::UInt32,
             offsetof(Instance, normal0), sizeof(Instance), my::VertexRate::PerInstance},

            {InputStreamInstancePos1, instance_data_buffer, my::VertexFormat::Float32_3,
             offsetof(Instance, position1), sizeof(Instance), my::VertexRate::PerInstance},
            {InputStreamNormal1, instance_data_buffer, my::VertexFormat::UInt32,
             offsetof(Instance, normal1), sizeof(Instance), my::VertexRate::PerInstance},

            {InputStreamGeometry, instance_data_buffer, my::VertexFormat::Float32_4,
             offsetof(Instance, progress0), sizeof(Instance), my::VertexRate::PerInstance},
            {InputStreamAnimationSpeed, instance_data_buffer, my::VertexFormat::Float32,
             offsetof(Instance, animation_speed), sizeof(Instance), my::VertexRate::PerInstance},

            {InputStreamFeatureIndex, instance_data_buffer, my::VertexFormat::UInt32,
             offsetof(Instance, feature_index), sizeof(Instance), my::VertexRate::PerInstance},
            {InputStreamFeatureId, instance_data_buffer, my::VertexFormat::UInt32_2,
             offsetof(Instance, feature_id), sizeof(Instance), my::VertexRate::PerInstance},

            selection_storage.get_vertex_input_stream(render, InputStreamSelection)
        };

        my::VertexInputResource vi_res;
        vi_res.indices = _cylinder_index_buffer;
        vi_res.attribs = streams;
        my::ResourceHandle vertex_input = render->rc->alloc(
            &vi_res, hrz::monitoring::systems::Cylinders, tile->layer_id,
            {{"tile coords"_ss, tile_coords_str}});

        renderable.data.vertex_input = vertex_input;
        renderable.data.vertex_count = CircleResolution * 6;
        renderable.data.instance_count = instance_count;

        hrz::split_double(geometry.center.x, tile->ubo.center_low.x, tile->ubo.center_high.x);
        hrz::split_double(geometry.center.y, tile->ubo.center_low.y, tile->ubo.center_high.y);
        hrz::split_double(geometry.center.z, tile->ubo.center_low.z, tile->ubo.center_high.z);
        tile->ubo_dirty = true;

        my::BufferResource ub_res(my::BufferResource::BufferType::Uniform);
        ub_res.size = sizeof(TileUniformData);
        ub_res.usage = my::UsageHint::Updatable;
        ub_res.data = &tile->ubo;

        my::ResourceHandle uniform_buffer = render->rc->alloc(
            &ub_res, hrz::monitoring::systems::Cylinders, tile->layer_id,
            {{"tile coords"_ss, tile_coords_str}});

        renderable.data.ubo_buffer = uniform_buffer;
        renderable.data.shader = geometry.has_transparency ? _transparent_shader : _opaque_shader;
        renderable.data.picking_shader = _picking_shader;
        renderable.data.depth_shader = _depth_shader;
        renderable.data.selection_shader = _selection_shader;
        renderable.data.is_animated = geometry.is_animated;
        renderable.bin_mask =
            geometry.has_transparency ? hrz::RenderWorldTransparentBin : hrz::RenderWorldOpaqueBin;
        renderable.data.cull_modifier =
            tile->disable_face_culling ? my::CullModifier::Disable : my::CullModifier::DontChange;
        renderable.data.scene_views = tile->scene_views;
        renderable.data.draw_report = &_draw_report;

        renderable.center = geometry.bsphere_center;
        renderable.radius = geometry.bsphere_radius;

        {
            renderable.selection_storage = std::move(selection_storage);

            if (tile->has_feature_ids)
            {
                for (size_t i = 0; i < instance_count; ++i)
                {
                    renderable.selection_storage.register_indirection(
                        instance_data.at(i).feature_id, i);
                }
            }
        }

        tile->geometry = std::nullopt;
        tile->renderable = std::move(renderable);
        tile->status = Tile::Status::Ready;

        send_status_update_message();

#undef CHECK_RESOURCE_UPLOAD
    }

    hrz::RenderRequest _work_removed(WorkCtx& ctx)
    {
        hrz::RenderRequest render_request;

        for (TileH handle : _removed)
        {
            Tile* tile = _tiles.get_object(handle);
            if (!tile) continue;

            work_remove_tile(ctx, handle, tile);
            render_request.request_visual_render();
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

            for (auto& generic_message : channel.receive())
            {
                std::visit(
                    hrz::overload{
                        [&](const hrz::vt::repr::messages::RegisterStyle& message)
                        {
                            decltype(hrz::vt::repr::messages::StyleRegistrationResult::
                                         registered_properties) registered_properties;

                            auto handle = register_style(
                                message.repr, message.layer_id,
                                [repr_reg = ctx.repr_reg, layer_id = message.layer_id,
                                 &registered_properties](
                                    std::string_view name,
                                    const hrz::vector_data::OwnedAttributeValue& default_value)
                                    -> uint64_t
                                {
                                    uint64_t prp_id = repr_reg->register_property(layer_id, name);
                                    registered_properties.push_back({prp_id, default_value});
                                    return prp_id;
                                },
                                ConfigId{channel_id, message.style_id});

                            channel.send(
                                hrz::vt::repr::messages::StyleRegistrationResult{
                                    message.style_id, handle.has_value(),
                                    std::move(registered_properties)
                                });
                        },
                        [&](const hrz::vt::repr::messages::UnregisterStyle& message)
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
                        },
                        [&](const hrz::vt::repr::messages::AddTile& message)
                        {
                            auto it = _configs_by_id.find(ConfigId{channel_id, message.style_id});
                            if (it != _configs_by_id.end())
                            {
                                add_tile(
                                    it->second, message.coords, message.layer_id,
                                    message.object_ref, message.feature_ref, message.feature_ids,
                                    message.geometry, message.style,
                                    TileId{channel_id, message.tile_id});
                            }
                            else
                            {
                                HRZ_LOG_ERROR("Cannot add tile: style not found");
                            }
                        },
                        [&](const hrz::vt::repr::messages::RemoveTile& message)
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
                        },
                        [&](const hrz::vt::repr::messages::UpdateTileElevation&)
                        {
                            // No-op
                        },
                        [&](const hrz::vt::repr::messages::UpdateClipId& message)
                        {
                            auto it = _tiles_by_id.find(TileId{channel_id, message.tile_id});
                            if (it != _tiles_by_id.end())
                            {
                                update_clip_id(it->second, message.clip_id);
                            }
                            else
                            {
                                HRZ_LOG_ERROR("Cannot update clip ID: tile not found");
                            }
                        },
                        [&](const hrz::vt::repr::messages::UpdateLighting& message)
                        {
                            auto it = _tiles_by_id.find(TileId{channel_id, message.tile_id});
                            if (it != _tiles_by_id.end())
                            {
                                update_lighting(it->second, message.lighting);
                            }
                            else
                            {
                                HRZ_LOG_ERROR("Cannot update lighting: tile not found");
                            }
                        },
                        [&](const hrz::vt::repr::messages::UpdateSelection& message)
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
                        },
                    },
                    generic_message);
            }
        }

        render_request |= _work_removed(ctx);

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

        // Animated features require a constant redrawing of the screen.
        if (_draw_report.animated_features_drawn)
        {
            render_request.request_visual_render(hrz::RenderRequest::VisualCause::Animation);
        }

        _draw_report.reset();

        return render_request;
    }

    hrz::RenderRequest work_gpu(WorkGpuCtx& ctx) override
    {
        hrz::RenderRequest render_request;

        for (TileH handle : _finished_baking)
        {
            Tile* tile = _tiles.get_object(handle);
            if (!tile) continue;

            work_finished_baking(ctx.render, handle, tile);
        }
        _finished_baking.clear();

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
                renderable.data.cast_shadows = tile->cast_shadows;

                renderable.selection_storage.work_gpu(ctx.render);
                if (tile->ubo_dirty)
                {
                    ctx.render->my->update_buffer(
                        renderable.data.ubo_buffer, 0, sizeof(TileUniformData), &tile->ubo);
                    tile->ubo_dirty = false;
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
        assert(tile);

        if (tile->status != Tile::Status::Ready) return;

        assert(tile->renderable.has_value());
        RenderableFeatures& renderable = tile->renderable.value();
        renderable.selection_storage.update_selection(selected_objects);
        renderable.data.has_any_object_selected = renderable.selection_storage.has_any_selected();
    }

    void update_clip_id(TileH handle, int32_t clip_id)
    {
        Tile* tile = _tiles.get_object(handle);
        if (tile)
        {
            tile->ubo.clip_id = clip_id;
            tile->ubo_dirty = true;
        }
    }

    void update_lighting(TileH handle, const hrz::render::LightingSettings& lighting)
    {
        Tile* tile = _tiles.get_object(handle);
        if (tile)
        {
            tile->ubo.lighting_enabled =
                lighting.lighting_enabled && tile->lighting_settings_on_config.lighting_enabled;
            tile->ubo.receive_shadows =
                lighting.receive_shadows && tile->lighting_settings_on_config.receive_shadows;
            tile->cast_shadows =
                lighting.cast_shadows && tile->lighting_settings_on_config.cast_shadows;
            tile->ubo_dirty = true;
        }
    }

    std::pair<uint64_t, Channel> create_channel() override { return _channels.create_channel(); }
};

} // namespace

namespace hrz::vt
{

std::unique_ptr<ReprSystem> create_cylinder_repr_system()
{
    return std::unique_ptr<ReprSystem>(new CylinderReprSystem());
}

void collect_cylinder_shaders(hrz::GpuResourceContext* rc)
{
    CylinderReprSystem::collect_shaders(rc);
}

} // namespace hrz::vt
