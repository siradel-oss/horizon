#include "hrz_core_channel_group.h"
#include "hrz_core_render.h"
#include "hrz_core_selection_storage.h"
#include "hrz_core_shaders.h"
#include "hrz_core_shadows.h"
#include "hrz_core_sky.h"
#include "hrz_core_viewsheds.h"
#include "vector/hrz_core_vector_repr.h"

#include <hrz_common_blob_allocator.h>
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

namespace
{
enum
{
    UboTileParams = hrz::UboCustomStart,

    InputStreamPosition = 0,
    InputStreamNormal = 1,
    InputStreamColor = 2,
    InputStreamFeatureIndex = 3,

    SamplerSelection = hrz::SamplerCustomStart,
    SamplerFeatureIds,
};

struct TileUniformData
{
    lm::vec4 center_low;
    lm::vec4 center_high;
    lm::uvec3 feature_reference;
    int32_t clip_id;
    lm::uvec2 object_reference;
    hrz::bool32 lighting_enabled;
    hrz::bool32 receive_shadows;
};

HRZ_CHECK_UBO_SIZE(TileUniformData);

struct RenderableFeatures : public my::Renderer::Renderable
{
    my::Renderer::BinMask bin_mask;
    lm::dvec3 center;
    double radius;

    my::ResourceHandle vertex_buffer = my::ResourceHandle::null();
    my::ResourceHandle index_buffer = my::ResourceHandle::null();

    hrz::selection::SelectionStorageUint32TextureMultiIndex selection_storage;

    struct RenderData
    {
        my::ResourceHandle vertex_input = my::ResourceHandle::null();
        my::ResourceHandle shader = my::ResourceHandle::null();
        my::ResourceHandle picking_shader = my::ResourceHandle::null();
        my::ResourceHandle depth_shader = my::ResourceHandle::null();
        my::ResourceHandle selection_shader = my::ResourceHandle::null();
        my::ResourceHandle ubo_buffer = my::ResourceHandle::null();
        my::ResourceHandle metadata_sampler = my::ResourceHandle::null();
        my::ResourceHandle feature_id_texture = my::ResourceHandle::null();
        my::ResourceHandle selection_texture = my::ResourceHandle::null();
        uint32_t vertex_count;
        uint32_t scene_views;
        bool has_selected_features = false;
        bool cast_shadows = false;
    } data;

    static void render_callback(
        uint32_t render_type,
        my::RenderContext* r,
        my::ResourceBinder* rb,
        const void* user_data_raw,
        const void* raw_data)
    {
        auto data = (const RenderData*)raw_data;
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
                if (!data->has_selected_features)
                {
                    return;
                }
                shader = data->selection_shader;
                break;
            }
            default: return;
        }

        auto batch = my::DrawBatchInfo(my::PrimitiveType::TriangleList, data->vertex_count)
                         .indexed(my::IndexType::UInt);

        rb->push_state();

        my::UboBinding ubo_bindings[] = {
            {UboTileParams, data->ubo_buffer, 0, sizeof(TileUniformData)}};
        rb->bind(HRZ_ARRAY_COUNT(ubo_bindings), ubo_bindings);

        if (render_type == hrz::RenderVisual)
        {
            my::TextureBinding texture_bindings[] = {
                {SamplerFeatureIds, data->feature_id_texture, data->metadata_sampler}};
            rb->bind(HRZ_ARRAY_COUNT(texture_bindings), texture_bindings);
        }
        else if (render_type == hrz::RenderSelection)
        {
            my::TextureBinding texture_bindings[] = {
                {SamplerSelection, data->selection_texture, data->metadata_sampler}};
            rb->bind(HRZ_ARRAY_COUNT(texture_bindings), texture_bindings);
        }

        auto state = rb->get_current_state();

        r->draw(
            batch, shader, data->vertex_input, state.ubo_count, state.ubos, state.texture_count,
            state.textures);

        rb->pop_state();
    }

    void collect_render_info(my::Renderer::Queue& queue, const my::Renderer::Culler& culler)
        const override
    {
        if (culler.is_visible_in_any_view(center, radius, bin_mask))
        {
            queue.enqueue(bin_mask, render_callback, &data, center, radius);
        }
    }
};

struct Config
{
    uint32_t repr_id = -1;

    uint64_t extrusion_prp = hrz::style::Parser::INVALID_PROPERTY;
    uint64_t upper_color_prp = hrz::style::Parser::INVALID_PROPERTY;
    uint64_t lower_color_prp = hrz::style::Parser::INVALID_PROPERTY;
    uint64_t roof_color_prp = hrz::style::Parser::INVALID_PROPERTY;
    uint64_t altitude_offset_prp = hrz::style::Parser::INVALID_PROPERTY;

    float default_extrusion = 0;
    lm::vec4 default_upper_color = {0, 0, 0, 0};
    lm::vec4 default_lower_color = {0, 0, 0, 0};
    lm::vec4 default_roof_color = {0, 0, 0, 0};
    float default_altitude_offset = 0;

    uint32_t scene_views = 0;
    bool clip_to_tile = false;
    hrz::render::LightingSettings lighting_settings;
};

struct TileGeometry
{
    lm::dvec3 center;
    double bsphere_radius;
    lm::dvec3 bsphere_center;
    bool has_transparency;
    // Positions are relative to the tile centre
    hrz::BlobArray<hrz::vt::ExtrudedVectorGeometry::Vertex> vertex_data;
    hrz::BlobArray<uint32_t> indices;
    hrz::BlobArray<hrz::vector_data::FeatureIdHash> feature_ids;
    uint32_t max_feature_index;
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

    hrz_jobs::BakeExtrudedVectorGeometryTicket bake_ticket;
    std::optional<hrz::vt::ExtrudedVectorData> bake_data;
    std::optional<TileGeometry> geometry;
    std::optional<RenderableFeatures> renderable;

    TileUniformData ubo;
    uint32_t scene_views;
    hrz::render::LightingSettings lighting_settings_on_config;
    bool ubo_dirty;
    bool cast_shadows;
};

class ExtrudedReprSystem : public hrz::vt::ReprSystem
{
    using ConfigH = uint64_t;
    using TileH = uint64_t;

    using ConfigIndexPool = hrz::GenIndexPool<ConfigH, 32, 32>;
    using ConfigPool = hrz::GenObjectPool<Config, ConfigIndexPool, 64>;

    using TileIndexPool = hrz::GenIndexPool<TileH, 32, 32>;
    using TilePool = hrz::GenObjectPool<Tile, TileIndexPool, 64>;

    my::ResourceHandle _opaque_shader;
    my::ResourceHandle _transparent_shader;
    my::ResourceHandle _picking_shader;
    my::ResourceHandle _depth_shader;
    my::ResourceHandle _selection_shader;
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

    hrz::flat_hash_set<std::pair<TileH, uint32_t>> _drawn_tiles;

    hrz::flat_hash_set<TileH> _to_bake;
    hrz::flat_hash_set<TileH> _baking;
    hrz::flat_hash_set<TileH> _finished_baking;
    hrz::flat_hash_set<TileH> _removed;

    std::vector<my::ResourceHandle> _resources_to_free;

    hrz::ChannelGroup<hrz::vt::FromReprMessage, hrz::vt::ToReprMessage> _channels;

public:
    ~ExtrudedReprSystem() override = default;

    void deinit(WorkCtx& ctx, hrz::Render*) override { work_removed_tiles(ctx); }

    static void collect_shaders(hrz::GpuResourceContext* rc)
    {
        my::IndexName attribs[] = {
            {InputStreamPosition, "i_position"},
            {InputStreamNormal, "i_normal"},
            {InputStreamColor, "i_color"},
            {InputStreamFeatureIndex, "i_feature_index"},
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

        hrz::StaticVector<my::IndexName, 32> visual_samplers;
        visual_samplers.push_back({hrz::SamplerSunColor, hrz::sky::SUN_COLOR_SAMPLER_NAME});

        for (int i = 0; i < HRZ_S_MAX_SUN_CASCADES; ++i)
        {
            visual_samplers.push_back(
                {hrz::SamplerSunShadow0 + i, hrz::shadows::SUN_SHADOW_MAP_SAMPLER_NAMES[i]});
        }

        for (int i = 0; i < HRZ_S_VIEWSHED_CNT; ++i)
        {
            visual_samplers.push_back(
                {hrz::SamplerViewshedShadow0 + i,
                 hrz::viewsheds::VIEWSHED_SHADOW_MAP_SAMPLER_NAMES[i]});
        }

        visual_samplers.push_back({SamplerFeatureIds, "u_feature_ids"});

        {
            my::ShaderResource res{};
            res.name = hrz_shaders::ExtrudedVectors_name;
            res.vertex_source_len = hrz_shaders::ExtrudedVectors_vert_len;
            res.vertex_source = hrz_shaders::ExtrudedVectors_vert;
            res.fragment_source_len = hrz_shaders::ExtrudedVectors_frag_len;
            res.fragment_source = hrz_shaders::ExtrudedVectors_frag;
            res.attrib_count = HRZ_ARRAY_COUNT(attribs);
            res.attribs = attribs;
            res.uniform_block_count = HRZ_ARRAY_COUNT(ubos);
            res.uniform_blocks = ubos;
            res.sampler_count = (uint32_t)visual_samplers.size();
            res.samplers = visual_samplers.data();
            res.output_count = HRZ_ARRAY_COUNT(color_outputs);
            res.outputs = color_outputs;
            res.initial_state.depth.test = true;
            res.initial_state.color_blend.enable = false;
            auto opaque_shader = rc->alloc(&res, hrz::monitoring::systems::ExtrudedVectors);

            my::ShaderDerivativeResource res_d(opaque_shader, res, "ExtrudedVectors_transparent");
            res_d.initial_state.color_blend.enable = true;
            res_d.initial_state.color_blend.color.src = my::ColorBlendState::SrcAlpha;
            res_d.initial_state.color_blend.color.dst = my::ColorBlendState::OneMinusSrcAlpha;
            rc->alloc(&res_d, hrz::monitoring::systems::ExtrudedVectors);

            const char* picking_color_outputs[] = {"o_object_reference", "o_depth"};

            res.name = hrz_shaders::ExtrudedVectors_picking_name;
            res.vertex_source_len = hrz_shaders::ExtrudedVectors_picking_vert_len;
            res.vertex_source = hrz_shaders::ExtrudedVectors_picking_vert;
            res.fragment_source_len = hrz_shaders::ExtrudedVectors_picking_frag_len;
            res.fragment_source = hrz_shaders::ExtrudedVectors_picking_frag;
            res.output_count = HRZ_ARRAY_COUNT(picking_color_outputs);
            res.outputs = picking_color_outputs;
            res.initial_state.color_blend.enable = false;
            res.sampler_count = 0;
            rc->alloc(&res, hrz::monitoring::systems::ExtrudedVectors);

            const char* selection_color_outputs[] = {"o_highlight"};

            my::IndexName selection_samplers[] = {
                {SamplerSelection, "u_selection"},
            };

            res.name = hrz_shaders::ExtrudedVectors_selection_name;
            res.vertex_source_len = hrz_shaders::ExtrudedVectors_selection_vert_len;
            res.vertex_source = hrz_shaders::ExtrudedVectors_selection_vert;
            res.fragment_source_len = hrz_shaders::ExtrudedVectors_selection_frag_len;
            res.fragment_source = hrz_shaders::ExtrudedVectors_selection_frag;
            res.output_count = HRZ_ARRAY_COUNT(selection_color_outputs);
            res.outputs = selection_color_outputs;
            res.initial_state.color_blend.enable = false;
            res.sampler_count = HRZ_ARRAY_COUNT(selection_samplers);
            res.samplers = selection_samplers;
            rc->alloc(&res, hrz::monitoring::systems::ExtrudedVectors);

            res.name = hrz_shaders::ExtrudedVectors_depth_name;
            res.vertex_source_len = hrz_shaders::ExtrudedVectors_depth_vert_len;
            res.vertex_source = hrz_shaders::ExtrudedVectors_depth_vert;
            res.fragment_source_len = hrz_shaders::ExtrudedVectors_depth_frag_len;
            res.fragment_source = hrz_shaders::ExtrudedVectors_depth_frag;
            res.output_count = 0;
            res.uniform_block_count = HRZ_ARRAY_COUNT(ubos_depth);
            res.uniform_blocks = ubos_depth;
            res.sampler_count = 0;
            res.initial_state.color_blend.enable = false;
            res.initial_state.rasterization.depth_bias_factor = 1.0f;
            res.initial_state.rasterization.depth_bias_units = 1.0f;
            rc->alloc(&res, hrz::monitoring::systems::ExtrudedVectors);
        }
    }

    void init_render(hrz::Render* render) override
    {
        assert(render);

        _opaque_shader = render->rc->retrieve_shader(hrz_shaders::ExtrudedVectors_name);
        _transparent_shader = render->rc->retrieve_shader("ExtrudedVectors_transparent");
        _picking_shader = render->rc->retrieve_shader(hrz_shaders::ExtrudedVectors_picking_name);
        _selection_shader =
            render->rc->retrieve_shader(hrz_shaders::ExtrudedVectors_selection_name);
        _depth_shader = render->rc->retrieve_shader(hrz_shaders::ExtrudedVectors_depth_name);

        {
            my::SamplerResource res;
            res.use_mipmaps = false;
            res.sampler.min_filter = my::SamplerParams::Filter::Nearest;
            res.sampler.mag_filter = my::SamplerParams::Filter::Nearest;
            res.sampler.mipmap_filter = my::SamplerParams::Filter::Nearest;

            _metadata_sampler = render->rc->alloc(&res, hrz::monitoring::systems::ExtrudedVectors);
        }
    }

    void deinit_render(hrz::Render* render) override
    {
        assert(render);

        render->rc->dealloc(_metadata_sampler);
    }

    std::optional<ConfigH> register_style(
        const hrz_proto::VectorRepr& repr,
        uint64_t,
        const std::function<
            uint64_t(std::string_view name, const hrz::vector_data::OwnedAttributeValue&)>&
            register_prp,
        ConfigId style_id)
    {
        if (repr.type() != hrz_proto::VectorReprType::EXTRUDED_GEOMETRY_VECTOR_REPR)
        {
            HRZ_LOG_ERROR("Unexpected vector representation type, expected extruded...");
            return std::nullopt;
        }

        if (_configs_by_id.contains(style_id))
        {
            HRZ_LOG_ERROR(
                "Cannot register style: ID {}-{} already in use", style_id.channel_id,
                style_id.config_id);
            return std::nullopt;
        }

        std::string_view extrusion_prp_name = repr.extruded_geometry().extrusion().name();
        std::string_view upper_color_prp_name = repr.extruded_geometry().upper_color().name();
        std::string_view lower_color_prp_name = repr.extruded_geometry().lower_color().name();
        std::string_view roof_color_prp_name = repr.extruded_geometry().roof_color().name();
        std::string_view altitude_offset_prp_name =
            repr.extruded_geometry().altitude_offset().name();

        Config config;
        config.repr_id = repr.id();

        config.default_extrusion = repr.extruded_geometry().extrusion().default_value();
        config.default_upper_color =
            hrz::to_lm(repr.extruded_geometry().upper_color().default_value());
        config.default_lower_color =
            hrz::to_lm(repr.extruded_geometry().lower_color().default_value());
        config.default_roof_color =
            hrz::to_lm(repr.extruded_geometry().roof_color().default_value());
        config.default_altitude_offset = repr.extruded_geometry().altitude_offset().default_value();
        config.scene_views = repr.scene_views().bits();
        config.clip_to_tile = repr.extruded_geometry().clip_to_tile();
        config.lighting_settings = hrz::render::from_proto(repr.extruded_geometry().lighting());

        config.extrusion_prp = register_prp(extrusion_prp_name, config.default_extrusion);
        config.upper_color_prp = register_prp(
            upper_color_prp_name,
            hrz::vector_data::attr_from_color<hrz::vector_data::OwnedAttributeValue>(
                config.default_upper_color));
        config.lower_color_prp = register_prp(
            lower_color_prp_name,
            hrz::vector_data::attr_from_color<hrz::vector_data::OwnedAttributeValue>(
                config.default_lower_color));
        config.roof_color_prp = register_prp(
            roof_color_prp_name,
            hrz::vector_data::attr_from_color<hrz::vector_data::OwnedAttributeValue>(
                config.default_roof_color));
        config.altitude_offset_prp =
            register_prp(altitude_offset_prp_name, config.default_altitude_offset);

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

        unregister_property(cfg->extrusion_prp);
        unregister_property(cfg->upper_color_prp);
        unregister_property(cfg->lower_color_prp);
        unregister_property(cfg->roof_color_prp);
        unregister_property(cfg->altitude_offset_prp);

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

        hrz::vt::ExtrudedVectorData bake_data;

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

        bake_data.default_upper_color = cfg.default_upper_color;
        bake_data.default_lower_color = cfg.default_lower_color;
        bake_data.default_roof_color = cfg.default_roof_color;
        bake_data.default_extrusion = cfg.default_extrusion;
        bake_data.default_altitude_offset = cfg.default_altitude_offset;
        bake_data.repr_id = cfg.repr_id;
        bake_data.upper_color_prp = cfg.upper_color_prp;
        bake_data.lower_color_prp = cfg.lower_color_prp;
        bake_data.roof_color_prp = cfg.roof_color_prp;
        bake_data.extrusion_prp = cfg.extrusion_prp;
        bake_data.altitude_offset_prp = cfg.altitude_offset_prp;
        bake_data.clip_to_tile = cfg.clip_to_tile;

        bake_data.feature_ids = feature_ids.hashes();
        bake_data.geometry = geometry.geometry;

        bake_data.clamping.CopyFrom(geometry.clamping);
        bake_data.clamps = geometry.clamps;
        bake_data.coords = coords;

        auto& dst_style = bake_data.style;
        dst_style.prps = style.prps;
        dst_style.values = style.values;
        dst_style.out_of_line_data = style.out_of_line_data;
        dst_style.instances = style.instances;

        tile.ubo.object_reference = object_ref.to_uvec2();
        tile.ubo.feature_reference = feature_ref.to_uvec3();
        tile.ubo.clip_id = -1;
        tile.ubo.lighting_enabled = cfg.lighting_settings.lighting_enabled;
        tile.ubo.receive_shadows = cfg.lighting_settings.receive_shadows;
        tile.ubo_dirty = true;

        tile.lighting_settings_on_config = cfg.lighting_settings;
        tile.cast_shadows = cfg.lighting_settings.cast_shadows;
        tile.scene_views = cfg.scene_views;
        tile.bake_data = std::move(bake_data);
        tile.status = Tile::Status::ReadyToBake;
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

    bool tile_is_ready(const Tile* tile) const
    {
        return tile->status == Tile::Status::Ready || tile->status == Tile::Status::Error;
    }

    void remove_tile(TileH handle) { _removed.insert(handle); }

    void remove_from_set(TileH handle, Tile::Status status)
    {
        switch (status)
        {
            case Tile::Status::ReadyToBake: _to_bake.erase(handle); break;
            case Tile::Status::Baking: _baking.erase(handle); break;
            case Tile::Status::FinishedBaking: _finished_baking.erase(handle); break;
            default: break;
        }
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
            _resources_to_free.push_back(renderable.vertex_buffer);
            _resources_to_free.push_back(renderable.index_buffer);
            _resources_to_free.push_back(renderable.data.vertex_input);
            _resources_to_free.push_back(renderable.data.ubo_buffer);
            _resources_to_free.push_back(renderable.data.feature_id_texture);
            renderable.selection_storage.free_gpu_resources(_resources_to_free);
        }

        _tiles.release(handle);
    }

    void work_start_baking(WorkCtx& ctx, TileH handle, Tile* tile)
    {
        assert(tile->status == Tile::Status::ReadyToBake);
        assert(tile->bake_data.has_value());

        tile->bake_ticket = hrz_jobs::add_job_bake_extruded_vector_geometry(
            ctx.js, tile->bake_data.value(),
            {hrz::monitoring::systems::ExtrudedVectors, tile->layer_id});
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
                hrz::vt::ExtrudedVectorGeometry response;
                hrz_jobs::get_job_response(ctx.js, tile->bake_ticket, response);

                TileGeometry geometry;

                geometry.center = response.center;
                geometry.bsphere_radius = response.bsphere_radius;
                geometry.bsphere_center = response.bsphere_center;
                geometry.has_transparency = response.has_transparency;
                geometry.vertex_data = std::move(response.vertex_data);
                geometry.indices = std::move(response.indices);
                geometry.feature_ids = std::move(response.feature_ids);
                geometry.max_feature_index = response.max_feature_index;

                tile->geometry = std::move(geometry);
                tile->status = Tile::Status::FinishedBaking;
                _finished_baking.insert(handle);
            }
            else
            {
                HRZ_LOG_ERROR(
                    "Could not bake extruded vector representation for tile {}-{}-{}",
                    tile->coords.lod, tile->coords.x, tile->coords.y);
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

#define CHECK_RESOURCE_UPLOAD(resource, resource_type)                                        \
    if (resource.is_null())                                                                   \
    {                                                                                         \
        HRZ_LOG_ERROR(                                                                        \
            "Could not upload extruded vector {} of tile {}-{}-{} to the GPU", resource_type, \
            tile->coords.lod, tile->coords.x, tile->coords.y);                                \
        tile->status = Tile::Status::Error;                                                   \
        send_status_update_message();                                                         \
        return;                                                                               \
    }

        auto tile_coords_str = fmt::to_string(tile->coords);

        auto& geometry = tile->geometry.value();
        RenderableFeatures renderable;

        auto vertex_data = geometry.vertex_data.get_data();
        my::BufferResource vb_res(my::BufferResource::BufferType::Vertex);
        vb_res.size = vertex_data.size_bytes();
        vb_res.usage = my::UsageHint::Static;
        vb_res.data = vertex_data.data();
        vb_res.allow_allocation_failure = true;

        my::ResourceHandle vertex_buffer = render->rc->alloc(
            &vb_res, hrz::monitoring::systems::ExtrudedVectors, tile->layer_id,
            {{"contents"_ss, "vertex data"_ss}, {"tile coords"_ss, tile_coords_str}});
        CHECK_RESOURCE_UPLOAD(vertex_buffer, "vertex buffer");
        renderable.vertex_buffer = vertex_buffer;

        auto index_data = geometry.indices.get_data();
        my::BufferResource ib_res(my::BufferResource::BufferType::Index);
        ib_res.size = index_data.size_bytes();
        ib_res.usage = my::UsageHint::Static;
        ib_res.data = index_data.data();
        ib_res.allow_allocation_failure = true;

        my::ResourceHandle index_buffer = render->rc->alloc(
            &ib_res, hrz::monitoring::systems::ExtrudedVectors, tile->layer_id,
            {{"tile coords"_ss, tile_coords_str}});
        CHECK_RESOURCE_UPLOAD(index_buffer, "index buffer");
        renderable.index_buffer = index_buffer;

        using Vertex = hrz::vt::ExtrudedVectorGeometry::Vertex;

        my::VertexInputStream streams[] = {
            {InputStreamPosition, vertex_buffer, my::VertexFormat::Float32_3,
             offsetof(Vertex, position), sizeof(Vertex), my::VertexRate::PerVertex},
            {InputStreamNormal, vertex_buffer, my::VertexFormat::UInt32, offsetof(Vertex, normal),
             sizeof(Vertex), my::VertexRate::PerVertex},
            {InputStreamColor, vertex_buffer, my::VertexFormat::UInt8Norm_4,
             offsetof(Vertex, color), sizeof(Vertex), my::VertexRate::PerVertex},
            {InputStreamFeatureIndex, vertex_buffer, my::VertexFormat::UInt32,
             offsetof(Vertex, feature_index), sizeof(Vertex), my::VertexRate::PerVertex},
        };

        my::VertexInputResource vi_res;
        vi_res.indices = index_buffer;
        vi_res.attrib_count = HRZ_ARRAY_COUNT(streams);
        vi_res.attribs = streams;
        my::ResourceHandle vertex_input = render->rc->alloc(
            &vi_res, hrz::monitoring::systems::ExtrudedVectors, tile->layer_id,
            {{"tile coords"_ss, tile_coords_str}});

        renderable.data.vertex_input = vertex_input;
        renderable.data.vertex_count = geometry.indices.size();

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

            auto feature_id_texture = render->rc->alloc(
                &tex_res, hrz::monitoring::systems::ExtrudedVectors, tile->layer_id,
                {{"contents"_ss, "feature IDs"_ss}, {"tile coords"_ss, tile_coords_str}});
            CHECK_RESOURCE_UPLOAD(feature_id_texture, "feature ID texture");
            renderable.data.feature_id_texture = feature_id_texture;
        }

        hrz::split_double(geometry.center.x, tile->ubo.center_low.x, tile->ubo.center_high.x);
        hrz::split_double(geometry.center.y, tile->ubo.center_low.y, tile->ubo.center_high.y);
        hrz::split_double(geometry.center.z, tile->ubo.center_low.z, tile->ubo.center_high.z);
        tile->ubo_dirty = true;

        my::BufferResource ub_res(my::BufferResource::BufferType::Uniform);
        ub_res.size = sizeof(TileUniformData);
        ub_res.usage = my::UsageHint::Updatable;
        ub_res.data = &tile->ubo;

        my::ResourceHandle uniform_buffer = render->rc->alloc(
            &ub_res, hrz::monitoring::systems::ExtrudedVectors, tile->layer_id,
            {{"tile coords"_ss, tile_coords_str}});

        renderable.data.ubo_buffer = uniform_buffer;
        renderable.data.shader = geometry.has_transparency ? _transparent_shader : _opaque_shader;
        renderable.data.picking_shader = _picking_shader;
        renderable.data.depth_shader = _depth_shader;
        renderable.data.selection_shader = _selection_shader;
        renderable.data.metadata_sampler = _metadata_sampler;
        renderable.bin_mask =
            geometry.has_transparency ? hrz::RenderWorldTransparentBin : hrz::RenderWorldOpaqueBin;
        renderable.data.scene_views = tile->scene_views;

        // Selection bitmask texture & stuff
        {
            auto feature_ids_data = geometry.feature_ids.get_data();

            renderable.selection_storage = hrz::selection::SelectionStorageUint32TextureMultiIndex(
                geometry.max_feature_index + 1,
                {hrz::monitoring::systems::ExtrudedVectors, tile->layer_id},
                {{"tile coords"_ss, tile_coords_str}});

            if (tile->has_feature_ids)
            {
                for (uint32_t i = 0; i <= geometry.max_feature_index; ++i)
                {
                    renderable.selection_storage.register_indirection(feature_ids_data.at(i), i);
                }
            }

            renderable.data.selection_texture = renderable.selection_storage.get_texture(render);
        }

        renderable.center = geometry.bsphere_center;
        renderable.radius = geometry.bsphere_radius;

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
                                    message.geometry, message.style,
                                    TileId{channel_id, message.tile_id});
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
                            // No-op
                        }
                        else if constexpr (std::is_same_v<
                                               MessageType, hrz::vt::repr::messages::UpdateClipId>)
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
                        }
                        else if constexpr (std::is_same_v<
                                               MessageType,
                                               hrz::vt::repr::messages::UpdateLighting>)
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

        render_request |= work_removed_tiles(ctx);

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
                renderable.selection_storage.work_gpu(ctx.render);
                renderable.data.scene_views = tile->scene_views & entry.second;
                renderable.data.cast_shadows = tile->cast_shadows;

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
        if (tile && tile->status == Tile::Status::Ready)
        {
            assert(tile->renderable.has_value());
            RenderableFeatures& renderable = tile->renderable.value();

            renderable.selection_storage.update_selection(selected_objects);
            renderable.data.has_selected_features = renderable.selection_storage.has_any_selected();
        }
    }

    void update_clip_id(TileH handle, int32_t clip_id)
    {
        Tile* tile = _tiles.get_object(handle);
        if (tile)
        {
            tile->ubo_dirty = true;
            tile->ubo.clip_id = clip_id;
        }
    }

    void update_lighting(TileH handle, const hrz::render::LightingSettings& lighting)
    {
        Tile* tile = _tiles.get_object(handle);
        if (tile)
        {
            tile->ubo_dirty = true;
            tile->ubo.lighting_enabled =
                lighting.lighting_enabled && tile->lighting_settings_on_config.lighting_enabled;
            tile->cast_shadows =
                lighting.cast_shadows && tile->lighting_settings_on_config.cast_shadows;
            tile->ubo.receive_shadows =
                lighting.receive_shadows && tile->lighting_settings_on_config.receive_shadows;
        }
    }

    std::pair<uint64_t, Channel> create_channel() override { return _channels.create_channel(); }
};

} // namespace

namespace hrz::vt
{
std::unique_ptr<ReprSystem> create_extruded_repr_system()
{
    return std::unique_ptr<ReprSystem>(new ExtrudedReprSystem());
}

void collect_extruded_shaders(hrz::GpuResourceContext* rc)
{
    ExtrudedReprSystem::collect_shaders(rc);
}
} // namespace hrz::vt
