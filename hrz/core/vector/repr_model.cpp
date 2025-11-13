#include "hrz/common/fmt.h"
#include "hrz/common/maths.h"
#include "hrz/common/monitoring_defs.h"
#include "hrz/common/profiling.h"
#include "hrz/common/proto_maths.h"
#include "hrz/common/style.h"
#include "hrz/common/vector_data.h"
#include "hrz/common/vector_tiles.h"
#include "hrz/core/assets_loader/assets_loader.h"
#include "hrz/core/channel_group.h"
#include "hrz/core/impostor_baker.h"
#include "hrz/core/jobs/jobs_tickets.h"
#include "hrz/core/loading_priorities.h"
#include "hrz/core/model/model.h"
#include "hrz/core/selection_storage.h"
#include "hrz/core/shaders/collection.h"
#include "hrz/core/sky.h"
#include "hrz/core/vector/repr.h"
#include "hrz/core/viewsheds.h"
#include "hrz/fnd/gen_object_pool.h"
#include "hrz/fnd/hash.h"
#include "hrz/fnd/mem.h"
#include "hrz/fnd/meta.h"
#include "hrz/fnd/static_vector.h"
#include "hrz/fnd/thread.h"

namespace
{
enum
{
    UboFeatureParams = hrz::UboCustomStart,
    UboImpostor,

    SamplerImpostorTexture = hrz::SamplerCustomStart,
    SamplerImpostorShadowTexture,
    SamplerImpostorScaleCoefficientsTexture,

    InputStreamPosition = 0,
    InputStreamColor = 1,
    InputStreamScale = 2,
    InputStreamImpostorPosition = 3,
    InputStreamImpostorOrientation = 4,
    InputStreamFeatureId = 5,
    InputStreamObjectId = 6,
    InputStreamSelection = 7,
};

struct ImpostorTileUniformData
{
    lm::vec4 center_low;
    lm::vec4 center_high;
    lm::uvec2 object_ref;
    int32_t clip_id = -1;
    hrz::bool32 lighting_enabled;
    uint32_t color_blend_mode;
    float color_blend_strength;
    uint32_t _padding[2];
    lm::uvec3 feature_ref;
    uint32_t _padding2[1];
};

HRZ_CHECK_UBO_SIZE(ImpostorTileUniformData);

struct ImpostorUniformData
{
    lm::vec3 offset_from_origin;
    float scale_correction;
    lm::uvec2 atlas_size;
    uint32_t _padding[2];
};

HRZ_CHECK_UBO_SIZE(ImpostorUniformData);

struct ImpostorRenderable : public my::Renderer::Renderable
{
    my::Renderer::BinMask bin_mask;
    lm::dvec3 center;
    double radius;

    my::ResourceHandle instance_buffer = my::ResourceHandle::null();
    my::ResourceHandle color_buffer = my::ResourceHandle::null();
    my::ResourceHandle scale_buffer = my::ResourceHandle::null();

    hrz::selection::SelectionStorageUint32BufferMultiIndex selection_storage;

    struct RenderData
    {
        my::ResourceHandle vertex_input = my::ResourceHandle::null();

        my::ResourceHandle impostor_texture = my::ResourceHandle::null();
        my::ResourceHandle normal_texture = my::ResourceHandle::null();
        my::ResourceHandle scale_coefficients_texture = my::ResourceHandle::null();
        my::ResourceHandle impostor_sampler = my::ResourceHandle::null();

        my::ResourceHandle visual_shader = my::ResourceHandle::null();
        my::ResourceHandle picking_shader = my::ResourceHandle::null();
        my::ResourceHandle selection_shader = my::ResourceHandle::null();

        my::ResourceHandle ubo_tile_buffer = my::ResourceHandle::null();
        my::ResourceHandle ubo_impostor_buffer = my::ResourceHandle::null();

        uint32_t vertex_count = 0;
        uint32_t instance_count = 1;

        uint32_t scene_views;
        bool has_any_selected;
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
            case hrz::RenderVisual: shader = data->visual_shader; break;
            case hrz::RenderPicking: shader = data->picking_shader; break;
            case hrz::RenderSelection:
            {
                if (!data->has_any_selected)
                {
                    return;
                }
                shader = data->selection_shader;
                break;
            }
            default: return;
        }

        auto batch = my::DrawBatchInfo(my::PrimitiveType::TriangleStrip, data->vertex_count)
                         .instanced(data->instance_count);

        rb->push_state();

        my::UboBinding ubo_bindings[] = {
            {UboFeatureParams, data->ubo_tile_buffer, 0, sizeof(ImpostorTileUniformData)},
            {UboImpostor, data->ubo_impostor_buffer, 0, sizeof(ImpostorUniformData)},
        };
        rb->bind(HRZ_ARRAY_COUNT(ubo_bindings), ubo_bindings);

        my::TextureBinding texture_bindings[] = {
            {SamplerImpostorTexture, data->impostor_texture, data->impostor_sampler},
            {SamplerImpostorShadowTexture, data->normal_texture, data->impostor_sampler},
            {SamplerImpostorScaleCoefficientsTexture, data->scale_coefficients_texture,
             data->impostor_sampler},
        };
        rb->bind(HRZ_ARRAY_COUNT(texture_bindings), texture_bindings);

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

using ConfigH = uint64_t;
using TileH = uint64_t;

struct Config
{
    enum class Status
    {
        Loading,
        BakingImpostor,
        Ready,
        Error,
    };

    Status status;

    uint32_t repr_id;
    uint64_t layer_id;

    uint64_t color_prp = hrz::style::Parser::INVALID_PROPERTY;
    uint64_t scale_x_prp = hrz::style::Parser::INVALID_PROPERTY;
    uint64_t scale_y_prp = hrz::style::Parser::INVALID_PROPERTY;
    uint64_t scale_z_prp = hrz::style::Parser::INVALID_PROPERTY;
    uint64_t world_offset_x_prp = hrz::style::Parser::INVALID_PROPERTY;
    uint64_t world_offset_y_prp = hrz::style::Parser::INVALID_PROPERTY;
    uint64_t world_offset_z_prp = hrz::style::Parser::INVALID_PROPERTY;
    uint64_t rotation_x_prp = hrz::style::Parser::INVALID_PROPERTY;
    uint64_t rotation_y_prp = hrz::style::Parser::INVALID_PROPERTY;
    uint64_t rotation_z_prp = hrz::style::Parser::INVALID_PROPERTY;

    lm::ubvec4 default_color_srgb = {0, 0, 0, 0};
    lm::vec3 default_world_offset = {0, 0, 0};
    lm::vec3 default_rotation = {0, 0, 0};
    lm::vec3 default_scale = {0, 0, 0};

    std::string url = "";
    hrz::HttpHeaders headers;
    bool preserve_query_parameters;
    lm::dmat4 frame = lm::dmat4::identity();
    hrz_proto::EulerRotationOrder rotation_order;

    bool clip_to_tile;

    hrz::model::ModelPrototype* model_prototype = nullptr;
    hrz::model::InstancedModelGeometryH model_geometry;
    hrz::model::ModelMaterialH model_material;
    hrz::model::InstancedBakedModelH baked_model;

    hrz_proto::ImpostorParams impostor_params;
    hrz::impostor::BakingTicket impostor_baking_ticket;
    hrz::impostor::BakedResources baked_resources;

    hrz::render::LightingSettings lighting_settings;

    uint32_t color_blend_mode;
    float color_blend_strength;

    hrz::SceneViewBitset scene_views_bitset;

    uint32_t used = 0;
};

struct TileGeometry
{
    lm::dvec3 origin;
    hrz::BSphere<double> bsphere;
    double average_scale;

    std::vector<hrz::vector_data::FeatureIdHash> feature_ids; // Per instance.
    std::vector<lm::vec3> positions;                          // Per instance.
    std::vector<lm::usvec4> normals;                          // Per instance.
    std::vector<lm::vec3> scales;                             // Per instance.
    std::vector<uint32_t> object_ids;                         // Per instance.
    std::vector<lm::ubvec4> colors;                           // Per instance
    std::vector<lm::vec3> impostor_positions;                 // Per instance.
    std::vector<lm::vec3> impostor_scales;                    // Per instance.
    std::vector<lm::uvec2> impostor_orientations;             // Per instance.
};

struct TileId
{
    uint64_t channel_id;
    uint64_t tile_id;

    constexpr bool operator==(const TileId& other) const = default;

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
        StartBaking,
        Baking,
        PrepareRendering,
        Ready,
        Error,
    };

    std::optional<TileId> id;
    uint64_t layer_id;

    struct Impostor
    {
        ImpostorTileUniformData ubo_data;
        bool ubo_dirty;
        std::optional<ImpostorRenderable> renderable;
        hrz::SceneViewBitset draw_in;
    };

    Status status;
    hrz::TileCoords coords;
    ConfigH config_handle;
    bool has_feature_ids;

    hrz::vt::ModelData bake_data;
    hrz_jobs::Bake3dModelGeometryTicket bake_ticket;
    TileGeometry geometry;

    std::optional<Impostor> impostor = std::nullopt;

    // @Todo Some of those states are duplicated from impostor_ubo because those
    // are used for drawing the instanced model. We should find a way of
    // deduplicating all of this one day.
    //      -slerouzic, 2021-08-24
    hrz::picking::ObjectReference object_ref;
    hrz::picking::FeatureReference feature_ref;
    hrz::render::LightingSettings lighting_settings_in_config;

    std::optional<hrz::model::InstanceGroupH> instance_group = std::nullopt;
    hrz::model::DrawProperties draw_prps;
};

struct ImpostorGpuResources
{
    bool initialized = false;
    my::ResourceHandle visual_shader = my::ResourceHandle::null();
    my::ResourceHandle picking_shader = my::ResourceHandle::null();
    my::ResourceHandle selection_shader = my::ResourceHandle::null();
    my::ResourceHandle texture_sampler = my::ResourceHandle::null();
    my::ResourceHandle vertex_buffer = my::ResourceHandle::null();

    void initialize(hrz::Render* render)
    {
        if (!initialized)
        {
            {
                my::IndexName attribs[] = {
                    {InputStreamPosition, "i_position"},
                    {InputStreamColor, "i_color"},
                    {InputStreamScale, "i_scale"},
                    {InputStreamImpostorPosition, "i_impostor_position"},
                    {InputStreamImpostorOrientation, "i_impostor_orientation"},
                    {InputStreamObjectId, "i_object_id"},
                    {InputStreamSelection, "i_selection"},
                };

                static const my::IndexName ubos[] = {
                    {hrz::UboFrame, "Frame"},
                    {UboFeatureParams, "Tile"},
                    {UboImpostor, "Impostor"},
                };

                my::IndexName atlas_sampler = {SamplerImpostorTexture, "hrz_impostor_texture"};
                my::IndexName shadow_sampler = {
                    SamplerImpostorShadowTexture, "hrz_impostor_normal_texture"};
                my::IndexName scale_sampler = {
                    SamplerImpostorScaleCoefficientsTexture,
                    "hrz_impostor_scale_coefficients_texture"};

                hrz::StaticVector<my::IndexName, 16> visual_samplers;
                visual_samplers.push_back(atlas_sampler);
                visual_samplers.push_back(shadow_sampler);
                visual_samplers.push_back(scale_sampler);

                for (int i = 0; i < HRZ_S_VIEWSHED_CNT; ++i)
                {
                    visual_samplers.push_back(
                        {hrz::SamplerViewshedShadow0 + i,
                         hrz::viewsheds::VIEWSHED_SHADOW_MAP_SAMPLER_NAMES[i]});
                }

                visual_samplers.push_back({hrz::SamplerSunColor, hrz::sky::SUN_COLOR_SAMPLER_NAME});

                hrz::StaticVector<my::IndexName, 16> non_visual_samplers;
                non_visual_samplers.push_back(atlas_sampler);
                non_visual_samplers.push_back(scale_sampler);

                static const char* outputs[] = {"o_color"};

                my::ShaderResource res{};
                res.name = hrz_shaders::Impostor_name;
                res.vertex_source_len = hrz_shaders::Impostor_vert_len;
                res.vertex_source = hrz_shaders::Impostor_vert;
                res.fragment_source_len = hrz_shaders::Impostor_frag_len;
                res.fragment_source = hrz_shaders::Impostor_frag;
                res.uniform_block_count = HRZ_ARRAY_COUNT(ubos);
                res.uniform_blocks = ubos;
                res.attribs = attribs;
                res.attrib_count = HRZ_ARRAY_COUNT(attribs);
                res.sampler_count = (uint32_t)visual_samplers.size();
                res.samplers = visual_samplers.data();
                res.output_count = HRZ_ARRAY_COUNT(outputs);
                res.outputs = outputs;

                res.initial_state.depth.test = true;

                res.initial_state.color_blend.enable = true;
                res.initial_state.color_blend.color.src = my::ColorBlendState::One;
                res.initial_state.color_blend.color.dst = my::ColorBlendState::OneMinusSrcAlpha;
                res.initial_state.color_blend.alpha.src = my::ColorBlendState::One;
                res.initial_state.color_blend.alpha.dst = my::ColorBlendState::OneMinusSrcAlpha;
                res.initial_state.rasterization.cull_mode = my::RasterizationState::None;
                visual_shader = render->rc->alloc(&res, hrz::monitoring::systems::Impostors);

                static const char* picking_color_outputs[] = {"o_object_reference", "o_depth"};

                res.name = hrz_shaders::Impostor_picking_name;
                res.vertex_source_len = hrz_shaders::Impostor_picking_vert_len;
                res.vertex_source = hrz_shaders::Impostor_picking_vert;
                res.fragment_source_len = hrz_shaders::Impostor_picking_frag_len;
                res.fragment_source = hrz_shaders::Impostor_picking_frag;
                res.output_count = HRZ_ARRAY_COUNT(picking_color_outputs);
                res.sampler_count = (uint32_t)non_visual_samplers.size();
                res.samplers = non_visual_samplers.data();
                res.outputs = picking_color_outputs;
                res.initial_state.color_blend.enable = false;
                picking_shader = render->rc->alloc(&res, hrz::monitoring::systems::Impostors);

                static const char* selection_color_outputs[] = {"o_highlight"};

                res.name = hrz_shaders::Impostor_selection_name;
                res.vertex_source_len = hrz_shaders::Impostor_selection_vert_len;
                res.vertex_source = hrz_shaders::Impostor_selection_vert;
                res.fragment_source_len = hrz_shaders::Impostor_selection_frag_len;
                res.fragment_source = hrz_shaders::Impostor_selection_frag;
                res.output_count = HRZ_ARRAY_COUNT(selection_color_outputs);
                res.outputs = selection_color_outputs;
                res.initial_state.color_blend.enable = false;
                selection_shader = render->rc->alloc(&res, hrz::monitoring::systems::Impostors);
            }

            {
                my::SamplerResource res;
                res.sampler.min_filter = my::SamplerParams::Filter::Linear;
                res.sampler.mag_filter = my::SamplerParams::Filter::Linear;
                res.sampler.wrap_x = my::SamplerParams::Wrap::Repeat;
                res.sampler.wrap_y = my::SamplerParams::Wrap::Repeat;
                res.sampler.wrap_z = my::SamplerParams::Wrap::Repeat;
                res.use_mipmaps = false;
                texture_sampler = render->rc->alloc(&res, hrz::monitoring::systems::Impostors);
            }

            {
                const lm::vec2 vertices[] = {{0.5, 0.5}, {0.5, -0.5}, {-0.5, 0.5}, {-0.5, -0.5}};

                ImpostorRenderable renderable;

                my::BufferResource vb_res(my::BufferResource::BufferType::Vertex);
                vb_res.size = sizeof(vertices);
                vb_res.usage = my::UsageHint::Static;
                vb_res.data = (void*)&vertices[0].x;

                vertex_buffer = render->rc->alloc(
                    &vb_res, hrz::monitoring::systems::Impostors, hrz::monitoring::NoLayer,
                    {{"contents"_ss, "impostor vertex positions"_ss}});
            }

            initialized = true;
        }
    }

    void free_gpu_resources(std::vector<my::ResourceHandle>& gpu_resources)
    {
        if (initialized)
        {
            gpu_resources.push_back(texture_sampler);

            visual_shader = my::ResourceHandle::null();
            picking_shader = my::ResourceHandle::null();
            selection_shader = my::ResourceHandle::null();
            texture_sampler = my::ResourceHandle::null();

            initialized = false;
        }
    }
};

class ModelVectorReprSystem : public hrz::vt::ReprSystem
{
    using ConfigIndexPool = hrz::GenIndexPool<ConfigH, 32, 32>;
    using ConfigPool = hrz::GenObjectPool<Config, ConfigIndexPool, 64>;

    using TileIndexPool = hrz::GenIndexPool<TileH, 32, 32>;
    using TilePool = hrz::GenObjectPool<Tile, TileIndexPool, 64>;

    ConfigPool _configs;

    struct ConfigId
    {
        uint64_t channel_id;
        uint64_t config_id;

        constexpr bool operator==(const ConfigId& other) const = default;

        template<typename H>
        friend H AbslHashValue(H h, const ConfigId& request)
        {
            return H::combine(std::move(h), request.channel_id, request.config_id);
        }
    };

    hrz::flat_hash_map<ConfigId, ConfigH> _configs_by_id;

    TilePool _tiles;

    hrz::flat_hash_map<TileId, TileH> _tiles_by_id;

    hrz::model::SharedResources* shared_resources;

    hrz::ImpostorBaker* _impostor_baker = nullptr;
    ImpostorGpuResources _impostor_gpu_resources;

    hrz::flat_hash_set<TileH> _tiles_to_bake;
    hrz::flat_hash_set<TileH> _tiles_baking;
    hrz::flat_hash_set<TileH> _tiles_prepare_rendering;
    hrz::flat_hash_set<TileH> _tiles_ready;
    std::vector<std::pair<TileH, hrz::SceneViewBitset>> _tiles_to_render;
    std::vector<TileH> _tiles_to_remove;
    std::vector<TileH> _work_selection;

    hrz::flat_hash_set<ConfigH> _configs_loading;
    hrz::flat_hash_set<ConfigH> _configs_baking_impostor;
    hrz::flat_hash_set<ConfigH> _configs_unregistered;

    hrz::flat_hash_set<ConfigH> _models_to_work;

    std::vector<my::ResourceHandle> _resources_to_free;

    hrz::ChannelGroup<hrz::vt::FromReprMessage, hrz::vt::ToReprMessage> _channels;

public:
    ~ModelVectorReprSystem() override = default;

    void deinit(WorkCtx& ctx, hrz::Render* render) override
    {
        _work_tiles_to_remove(render);
        _work_unregistered_configs(ctx);

        hrz::impostor::destroy_system(_impostor_baker, render);
    }

    void init_render(hrz::Render* render) override
    {
        shared_resources = hrz::model::create_shared_resources_instanced(render);
        _impostor_baker = hrz::impostor::create_system();
    }

    void deinit_render(hrz::Render* render) override
    {
        hrz::model::destroy_shared_resources(shared_resources, render);
        _impostor_gpu_resources.free_gpu_resources(_resources_to_free);

        for (auto handle : _resources_to_free)
        {
            render->rc->dealloc(handle);
        }
        _resources_to_free.clear();
    }

    std::optional<ConfigH> register_style(
        const hrz_proto::VectorRepr& repr,
        uint64_t layer_id,
        const std::function<
            uint64_t(std::string_view name, const hrz::vector_data::OwnedAttributeValue&)>&
            register_prp,
        ConfigId style_id)
    {
        if (repr.type() != hrz_proto::VectorReprType::MODEL_VECTOR_REPR)
        {
            HRZ_LOG_ERROR("Unexpected vector representation type, expected 3D model.");
            return std::nullopt;
        }

        if (_configs_by_id.contains(style_id))
        {
            HRZ_LOG_ERROR(
                "Cannot register style: ID {}-{} already in use", style_id.channel_id,
                style_id.config_id);
            return std::nullopt;
        }

        std::string_view color_prp_name = repr.model().color().name();
        std::string_view scale_prp_name = repr.model().scale().name();
        std::string scale_prp_x_name = fmt::format("{}_x", scale_prp_name.data());
        std::string scale_prp_y_name = fmt::format("{}_y", scale_prp_name.data());
        std::string scale_prp_z_name = fmt::format("{}_z", scale_prp_name.data());
        std::string_view world_offset_prp_name = repr.model().world_offset().name();
        std::string world_offset_x_prp_name = fmt::format("{}_x", world_offset_prp_name.data());
        std::string world_offset_y_prp_name = fmt::format("{}_y", world_offset_prp_name.data());
        std::string world_offset_z_prp_name = fmt::format("{}_z", world_offset_prp_name.data());
        std::string_view rotation_prp_name = repr.model().rotation().name();
        std::string rotation_x_prp_name = fmt::format("{}_x", rotation_prp_name.data());
        std::string rotation_y_prp_name = fmt::format("{}_y", rotation_prp_name.data());
        std::string rotation_z_prp_name = fmt::format("{}_z", rotation_prp_name.data());

        Config config;
        config.repr_id = repr.id();
        config.layer_id = layer_id;

        config.default_color_srgb =
            hrz::convert_proto_color_to_bytes(repr.model().color().default_value());
        config.default_scale = hrz::to_lm(repr.model().scale().default_value());
        config.default_world_offset =
            lm::vec3(hrz::to_lm(repr.model().world_offset().default_value()));
        config.default_rotation = lm::vec3(hrz::to_lm(repr.model().rotation().default_value()));
        config.url = repr.model().url();
        config.headers = hrz::assets_loader::from_proto(repr.model().http_headers());
        config.preserve_query_parameters = repr.model().preserve_query_parameters();
        config.clip_to_tile = repr.model().clip_to_tile();
        config.frame = hrz::to_lm(repr.model().frame());
        config.rotation_order = repr.model().rotation_order();
        config.status = Config::Status::Loading;
        config.impostor_params = repr.model().impostor_params();
        config.scene_views_bitset = repr.scene_views().bits();
        config.lighting_settings = hrz::render::from_proto(repr.model().lighting());
        config.color_blend_mode = repr.model().feature_color_blend_mode();
        config.color_blend_strength = repr.model().feature_color_blend_strength();
        config.model_prototype = nullptr;

        config.color_prp = register_prp(
            color_prp_name,
            hrz::vector_data::attr_from_color<hrz::vector_data::OwnedAttributeValue>(
                config.default_color_srgb));
        config.scale_x_prp = register_prp(scale_prp_x_name, config.default_scale.x);
        config.scale_y_prp = register_prp(scale_prp_y_name, config.default_scale.y);
        config.scale_z_prp = register_prp(scale_prp_z_name, config.default_scale.z);
        config.world_offset_x_prp =
            register_prp(world_offset_x_prp_name, config.default_world_offset.x);
        config.world_offset_y_prp =
            register_prp(world_offset_y_prp_name, config.default_world_offset.y);
        config.world_offset_z_prp =
            register_prp(world_offset_z_prp_name, config.default_world_offset.z);
        config.rotation_x_prp = register_prp(rotation_x_prp_name, config.default_rotation.x);
        config.rotation_y_prp = register_prp(rotation_y_prp_name, config.default_rotation.y);
        config.rotation_z_prp = register_prp(rotation_z_prp_name, config.default_rotation.z);

        ConfigH config_handle = _configs.alloc(std::move(config));
        _configs_loading.insert(config_handle);

        _configs_by_id.insert({style_id, config_handle});

        return config_handle;
    }

    void unregister_style(
        ConfigH config_handle,
        const std::function<void(uint64_t prp_id)>& unregister_property)
    {
        if (!_configs.is_valid(config_handle)) return;

        const Config* cfg = _configs.get_object(config_handle);

        unregister_property(cfg->color_prp);
        unregister_property(cfg->scale_x_prp);
        unregister_property(cfg->scale_y_prp);
        unregister_property(cfg->scale_z_prp);
        unregister_property(cfg->world_offset_x_prp);
        unregister_property(cfg->world_offset_y_prp);
        unregister_property(cfg->world_offset_z_prp);
        unregister_property(cfg->rotation_x_prp);
        unregister_property(cfg->rotation_y_prp);
        unregister_property(cfg->rotation_z_prp);

        _configs_unregistered.insert(config_handle);
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

        Config cfg;
        Config* cfg_src = _configs.get_object(config_handle);
        if (cfg_src)
        {
            cfg = *cfg_src;

            cfg_src->used++;
            tile.config_handle = config_handle;
        }
        else
        {
            HRZ_LOG_ERROR("Unknown config. Using default values.");
            tile.config_handle = 0;
        }

        hrz::vt::ModelData bake_data;

        bake_data.frame = cfg.frame;
        bake_data.rotation_order = cfg.rotation_order;
        bake_data.default_color_srgb = cfg.default_color_srgb;
        bake_data.default_scale = cfg.default_scale;
        bake_data.default_world_offset = cfg.default_world_offset;
        bake_data.default_rotation = cfg.default_rotation;

        bake_data.repr_id = cfg.repr_id;
        bake_data.color_prp = cfg.color_prp;
        bake_data.scale_x_prp = cfg.scale_x_prp;
        bake_data.scale_y_prp = cfg.scale_y_prp;
        bake_data.scale_z_prp = cfg.scale_z_prp;
        bake_data.world_offset_x_prp = cfg.world_offset_x_prp;
        bake_data.world_offset_y_prp = cfg.world_offset_y_prp;
        bake_data.world_offset_z_prp = cfg.world_offset_z_prp;
        bake_data.rotation_x_prp = cfg.rotation_x_prp;
        bake_data.rotation_y_prp = cfg.rotation_y_prp;
        bake_data.rotation_z_prp = cfg.rotation_z_prp;

        bake_data.feature_ids = feature_ids.hashes();
        bake_data.geometry = geometry.geometry;

        bake_data.tile_coords = tile.coords;
        bake_data.clip_to_tile = cfg.clip_to_tile;

        bake_data.clamping.CopyFrom(geometry.clamping);
        bake_data.clamps = geometry.clamps;

        auto& dst_style = bake_data.style;
        dst_style.prps = style.prps;
        dst_style.values = style.values;
        dst_style.instances = style.instances;

        tile.bake_data = std::move(bake_data);
        tile.status = Tile::Status::StartBaking;

        tile.object_ref = object_ref;
        tile.feature_ref = feature_ref;

        if (cfg.impostor_params.use_impostor())
        {
            Tile::Impostor impostor;
            impostor.ubo_data.object_ref = object_ref.to_uvec2();
            impostor.ubo_data.feature_ref = feature_ref.to_uvec3();
            impostor.ubo_data.clip_id = -1;
            impostor.ubo_data.lighting_enabled = cfg.lighting_settings.lighting_enabled;
            impostor.ubo_dirty = true;
            impostor.draw_in.reset();
            tile.impostor = std::move(impostor);
        }

        tile.lighting_settings_in_config = cfg.lighting_settings;

        tile.draw_prps.lighting = cfg.lighting_settings;
        tile.draw_prps.clip_id = -1;
        tile.draw_prps.transform = lm::dmat4::identity();
        tile.draw_prps.color = lm::vec4(1.0f);
        tile.draw_prps.apply_feature_color_to_overlay = false;
        tile.draw_prps.overlay_material_enabled = false;
        tile.draw_prps.overlay_material_opacity = 0.0;
        tile.draw_prps.feature_color_blend_mode = cfg.color_blend_mode;
        tile.draw_prps.feature_color_blend_strength = cfg.color_blend_strength;
        tile.draw_prps.draw_under_flat_overlays = false;

        TileH handle = _tiles.alloc(std::move(tile));
        _tiles_to_bake.insert(handle);

        _tiles_by_id.insert({tile_id, handle});

        return handle;
    }

    void schedule_draw_now(uint64_t channel_id, uint64_t tile_id, uint32_t views_bitset) override
    {
        auto it = _tiles_by_id.find({channel_id, tile_id});
        if (it != _tiles_by_id.end())
        {
            auto tile_handle = it->second;
            Tile* tile = _tiles.get_object(tile_handle);
            if (!tile) return;

            Config* cfg = _configs.get_object(tile->config_handle);
            if (!cfg)
            {
                // No need to keep the tile if there is no configuration associated.
                _tiles_to_remove.push_back(tile_handle);
                return;
            }

            if (cfg->status == Config::Status::Ready && tile->status == Tile::Status::Ready)
            {
                _tiles_to_render.push_back(std::make_pair(tile_handle, views_bitset));
            }
        }
    }

    bool tile_is_ready(const Tile* tile) const
    {
        return tile->status == Tile::Status::Ready || tile->status == Tile::Status::Error;
    }

    void remove_tile(TileH tile_handle) { _tiles_to_remove.push_back(tile_handle); }

    void _destroy_model(WorkCtx& ctx, Config* cfg)
    {
        hrz::model::destroy(cfg->model_prototype, cfg->baked_model);
        hrz::model::destroy(cfg->model_prototype, cfg->model_geometry);
        hrz::model::destroy(cfg->model_prototype, cfg->model_material);
        hrz::model::destroy(cfg->model_prototype, ctx.al, ctx.js, ctx.ba, _resources_to_free);
        cfg->model_prototype = nullptr;
    }

    void _work_messages(WorkCtx& ctx)
    {
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

                            channel.send(hrz::vt::repr::messages::StyleRegistrationResult{
                                message.style_id, handle.has_value(),
                                std::move(registered_properties)});
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
                        [](const hrz::vt::repr::messages::UpdateTileElevation&)
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
                        }},
                    generic_message);
            }
        }
    }

    void _work_unregistered_configs(WorkCtx& ctx)
    {
        for (auto it = _configs_unregistered.begin(); it != _configs_unregistered.end();)
        {
            Config* cfg = _configs.get_object(*it);
            if (cfg->used == 0)
            {
                if (cfg->impostor_params.use_impostor())
                {
                    hrz::impostor::destroy_impostor(
                        _impostor_baker, cfg->model_prototype, cfg->impostor_baking_ticket);
                }

                if (cfg->model_prototype)
                {
                    _destroy_model(ctx, cfg);
                }

                switch (cfg->status)
                {
                    case Config::Status::Loading: _configs_loading.erase(*it); break;
                    case Config::Status::BakingImpostor: _configs_baking_impostor.erase(*it); break;
                    case Config::Status::Ready:
                    case Config::Status::Error: break;
                }

                _configs.release(*it);
                _models_to_work.erase(*it);
                _configs_unregistered.erase(it++);
            }
            else
            {
                it++;
            }
        }
    }

    hrz::RenderRequest work(WorkCtx& ctx) override
    {
        HRZ_SCOPED_SAMPLE("model repr work");

        _work_messages(ctx);
        _work_unregistered_configs(ctx);
        _work_loading_models(ctx);
        _work_impostors(ctx);
        _work_tiles(ctx);

        return {};
    }

    hrz::RenderRequest _work_tiles_to_remove(hrz::Render* render)
    {
        hrz::RenderRequest render_request;

        for (auto handle : _tiles_to_remove)
        {
            Tile* tile = _tiles.get_object(handle);
            if (!tile)
            {
                continue;
            }

            Config* cfg = _configs.get_object(tile->config_handle);
            assert(cfg && cfg->used > 0);

            if (cfg->impostor_params.use_impostor() && tile->impostor.has_value()
                && tile->impostor->renderable.has_value())
            {
                auto& renderable = tile->impostor->renderable.value();
                _resources_to_free.push_back(renderable.instance_buffer);
                _resources_to_free.push_back(renderable.color_buffer);
                _resources_to_free.push_back(renderable.scale_buffer);
                _resources_to_free.push_back(renderable.data.vertex_input);
                _resources_to_free.push_back(renderable.data.ubo_tile_buffer);
                _resources_to_free.push_back(renderable.data.ubo_impostor_buffer);
                renderable.selection_storage.free_gpu_resources(_resources_to_free);
            }

            if (tile->instance_group.has_value())
            {
                assert(cfg->model_prototype);
                hrz::model::destroy(cfg->model_prototype, tile->instance_group.value());
            }

            switch (tile->status)
            {
                case Tile::Status::StartBaking: _tiles_to_bake.erase(handle); break;
                case Tile::Status::Baking: _tiles_baking.erase(handle); break;
                case Tile::Status::PrepareRendering: _tiles_prepare_rendering.erase(handle); break;
                case Tile::Status::Ready:
                case Tile::Status::Error: _tiles_ready.erase(handle); break;
            }

            _tiles.release(handle);
            cfg->used--;

            render_request.request_visual_render();
        }
        _tiles_to_remove.clear();

        return render_request;
    }

    hrz::RenderRequest work_gpu(WorkGpuCtx& ctx) override
    {
        hrz::RenderRequest render_request;

        render_request |= _work_tiles_to_remove(ctx.render);

        for (auto handle : _models_to_work)
        {
            Config* cfg = _configs.get_object(handle);
            if (cfg)
            {
                hrz::model::work_gpu(cfg->model_prototype, ctx.ba, ctx.render);
                hrz::model::work_gpu(
                    cfg->model_prototype, cfg->baked_model, shared_resources, ctx.render);
            }
        }

        if (_configs_baking_impostor.size() > 0 && !_impostor_gpu_resources.initialized)
        {
            _impostor_gpu_resources.initialize(ctx.render);
        }

        hrz::impostor::work_gpu(_impostor_baker, ctx.render);

        for (auto it = _configs_baking_impostor.begin(); it != _configs_baking_impostor.end();)
        {
            Config* cfg = _configs.get_object(*it);
            if (!cfg)
            {
                _configs_baking_impostor.erase(it++);
                continue;
            }

            assert(cfg->status == Config::Status::BakingImpostor);

            hrz::impostor::advance_baking(
                _impostor_baker, ctx.render, cfg->model_prototype, shared_resources,
                cfg->impostor_baking_ticket);
            it++;
        }

        for (auto it = _tiles_prepare_rendering.begin(); it != _tiles_prepare_rendering.end();)
        {
            Tile* tile = _tiles.get_object(*it);
            Config* cfg = _configs.get_object(tile->config_handle);
            if (!cfg)
            {
                it++;
                continue;
            }

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

            if (cfg->status == Config::Status::Ready)
            {
                if (!tile->instance_group.has_value())
                {
                    _prepare_tile_for_rendering(ctx.render, tile);
                }

                if (tile->instance_group.has_value())
                {
                    hrz::model::work_gpu(
                        cfg->model_prototype, tile->instance_group.value(), ctx.render);

                    auto group_status = hrz::model::get_instance_group_status(
                        cfg->model_prototype, tile->instance_group.value());
                    if (group_status == hrz::model::InstanceGroupStatus::Ready)
                    {
                        _tiles_ready.insert(*it);
                        _tiles_prepare_rendering.erase(it++);
                        tile->status = Tile::Status::Ready;
                        send_status_update_message();
                    }
                    else if (group_status == hrz::model::InstanceGroupStatus::Error)
                    {
                        _tiles_ready.insert(*it);
                        _tiles_prepare_rendering.erase(it++);
                        tile->status = Tile::Status::Error;
                        send_status_update_message();
                    }
                }
                else
                {
                    // The tile was probably empty
                    _tiles_ready.insert(*it);
                    _tiles_prepare_rendering.erase(it++);
                    tile->status = Tile::Status::Error;
                    send_status_update_message();
                }
            }
            else if (cfg->status == Config::Status::Error)
            {
                _tiles_ready.insert(*it);
                _tiles_prepare_rendering.erase(it++);
                tile->status = Tile::Status::Error;
                send_status_update_message();
            }
            else
            {
                it++;
            }
        }

        if (!_work_selection.empty())
        {
            for (auto tile_handle : _work_selection)
            {
                Tile* tile = _tiles.get_object(tile_handle);
                if (!tile) continue;
                Config* cfg = _configs.get_object(tile->config_handle);
                if (!cfg) continue;

                if (tile->impostor.has_value())
                {
                    tile->impostor->renderable->selection_storage.work_gpu(ctx.render);
                }

                if (tile->instance_group.has_value())
                {
                    hrz::model::work_gpu(
                        cfg->model_prototype, tile->instance_group.value(), ctx.render);
                }
            }
            _work_selection.clear();
        }

        for (my::ResourceHandle res : _resources_to_free)
        {
            ctx.render->rc->dealloc(res);
        }
        _resources_to_free.clear();

        return render_request;
    }

    void draw(DrawCtx& ctx) override
    {
        for (const auto& entry : _tiles_to_render)
        {
            Tile* tile = _tiles.get_object(entry.first);
            if (!tile) continue;

            Config* cfg = _configs.get_object(tile->config_handle);
            if (!cfg) continue;

            hrz::SceneViewBitset scene_views_visibility = cfg->scene_views_bitset & entry.second;
            hrz::SceneViewBitset draw_models_in = scene_views_visibility;
            hrz::SceneViewBitset draw_impostors_in;
            if (cfg->impostor_params.use_impostor())
            {
                draw_models_in &= (!tile->impostor->draw_in);
                draw_impostors_in = scene_views_visibility & tile->impostor->draw_in;
            }

            if (draw_impostors_in.any() && tile->impostor->renderable.has_value())
            {
                if (tile->impostor->ubo_dirty)
                {
                    ctx.render->my->update_buffer(
                        tile->impostor->renderable->data.ubo_tile_buffer, 0,
                        sizeof(ImpostorTileUniformData), &tile->impostor->ubo_data);
                    tile->impostor->ubo_dirty = false;
                }

                hrz::attribution::use_this_frame(
                    ctx.attributions, hrz::model::get_attribution(cfg->model_prototype));
                tile->impostor->renderable->data.scene_views = draw_impostors_in.bits();
                ctx.render->rd->collect_renderable(tile->impostor->renderable.value());
            }

            if (draw_models_in.any())
            {
                hrz::model::draw(
                    cfg->model_prototype, cfg->baked_model, tile->instance_group.value(),
                    tile->draw_prps, draw_models_in.bits(), shared_resources, ctx.render,
                    ctx.attributions);
            }
        }
        _tiles_to_render.clear();
    }

    void update_selection(
        TileH tile_handle,
        const hrz::flat_hash_set<hrz::vector_data::FeatureIdHash>& selected_objects)
    {
        Tile* tile = _tiles.get_object(tile_handle);
        assert(tile);

        if (tile->status != Tile::Status::Ready) return;

        if (tile)
        {
            Config* cfg = _configs.get_object(tile->config_handle);

            if (cfg && cfg->impostor_params.use_impostor() && tile->impostor.has_value()
                && tile->impostor->renderable.has_value())
            {
                tile->impostor->renderable->selection_storage.update_selection(selected_objects);
                tile->impostor->renderable->data.has_any_selected =
                    tile->impostor->renderable->selection_storage.has_any_selected();
            }

            if (cfg && tile->instance_group.has_value())
            {
                hrz::model::set_instance_group_selection(
                    cfg->model_prototype, tile->instance_group.value(), selected_objects);
            }

            _work_selection.push_back(tile_handle);
        }
    }

    void update_clip_id(TileH handle, int32_t clip_id)
    {
        Tile* tile = _tiles.get_object(handle);
        if (tile)
        {
            if (tile->impostor.has_value() && tile->impostor->ubo_data.clip_id != clip_id)
            {
                tile->impostor->ubo_dirty = true;
                tile->impostor->ubo_data.clip_id = clip_id;
            }

            tile->draw_prps.clip_id = clip_id;
        }
    }

    void update_lighting(TileH handle, const hrz::render::LightingSettings& lighting)
    {
        Tile* tile = _tiles.get_object(handle);
        if (tile)
        {
            bool lighting_enabled =
                lighting.lighting_enabled && tile->lighting_settings_in_config.lighting_enabled;
            bool cast_shadows =
                lighting.cast_shadows && tile->lighting_settings_in_config.cast_shadows;
            bool receive_shadows =
                lighting.receive_shadows && tile->lighting_settings_in_config.receive_shadows;

            if (tile->impostor.has_value()
                && tile->impostor->ubo_data.lighting_enabled != (hrz::bool32)lighting_enabled)
            {
                tile->impostor->ubo_dirty = true;
                tile->impostor->ubo_data.lighting_enabled = lighting_enabled;
            }

            tile->draw_prps.lighting.lighting_enabled = lighting_enabled;
            tile->draw_prps.lighting.cast_shadows = cast_shadows;
            tile->draw_prps.lighting.receive_shadows = receive_shadows;
        }
    }

private:
    void _prepare_tile_for_rendering(hrz::Render* render, Tile* tile)
    {
        Config* cfg = _configs.get_object(tile->config_handle);
        if (!cfg) return;

        // Prepare 3D model.

        auto tile_coords_str = fmt::to_string(tile->coords);

        TileGeometry& geometry = tile->geometry;
        uint32_t feature_id_count = geometry.feature_ids.size();
        uint32_t instance_count = geometry.positions.size();
        uint32_t color_count = geometry.colors.size();

        assert(feature_id_count == instance_count);
        assert(instance_count == color_count || color_count == 1);
        assert(instance_count == geometry.object_ids.size());

        if (instance_count > 0)
        {
            double base_transform_det =
                lm::determinant(cfg->frame * lm::scaling(cfg->default_scale));
            my::CullModifier cull_modifier =
                (base_transform_det < 0) ? my::CullModifier::Swap : my::CullModifier::DontChange;

            hrz::model::InstanceGroupData group_data;
            group_data.use_enu_orientation = false;
            group_data.transform = lm::translation(geometry.origin);
            group_data.cull_modifier = cull_modifier;
            group_data.positions = {(lm::vec3*)&geometry.positions[0], geometry.positions.size()};
            group_data.compressed_normals = {
                (lm::usvec4*)&geometry.normals[0], geometry.normals.size()};
            group_data.scales = {(lm::vec3*)&geometry.scales[0], geometry.scales.size()};
            group_data.colors = {(lm::ubvec4*)&geometry.colors[0], color_count};
            group_data.feature_id_per_instance = {&geometry.feature_ids[0], feature_id_count};
            group_data.object_ids = {&geometry.object_ids[0], feature_id_count};
            group_data.position_compression.type = hrz::model::DracoCompressionType::None;
            group_data.normal_compression.type = hrz::model::DracoCompressionType::OctEncoded;
            group_data.normal_compression.quantization_scale = lm::vec3(2.0f / 65535.0f);

            tile->instance_group = hrz::model::create_instance_group(
                cfg->model_prototype, tile->object_ref, tile->feature_ref, 0, group_data);
        }

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

        auto prepare_impostors = [&]()
        {
#define CHECK_RESOURCE_UPLOAD(resource, resource_type)                                 \
    if (resource.is_null())                                                            \
    {                                                                                  \
        HRZ_LOG_ERROR(                                                                 \
            "Could not upload impostor {} of tile {}-{}-{} to the GPU", resource_type, \
            tile->coords.lod, tile->coords.x, tile->coords.y);                         \
        tile->status = Tile::Status::Error;                                            \
        send_status_update_message();                                                  \
        return;                                                                        \
    }
            ImpostorRenderable renderable;

            size_t instance_count = feature_id_count;
            constexpr size_t instance_size = sizeof(lm::vec3) + sizeof(lm::uvec2)
                + sizeof(hrz::vector_data::FeatureIdHash) + sizeof(uint32_t);
            size_t instance_array_size = instance_count * instance_size;
            std::vector<std::byte> instance_array(instance_array_size);

            bool has_unique_color =
                instance_count != geometry.colors.size() && geometry.colors.size() == 1;
            bool has_unique_scale =
                instance_count != geometry.scales.size() && geometry.scales.size() == 1;

            size_t dst_offset = 0;
            auto copy_attribute = [&](const std::byte* src, size_t element_size)
            {
                std::byte* dst = instance_array.data() + dst_offset;
                for (size_t i = 0; i < instance_count; ++i)
                {
                    std::memcpy(dst, src, element_size);
                    src += element_size;
                    dst += instance_size;
                }
                dst_offset += element_size;
            };

            copy_attribute((std::byte*)geometry.positions.data(), sizeof(lm::vec3));
            copy_attribute((std::byte*)geometry.impostor_orientations.data(), sizeof(lm::uvec2));
            copy_attribute(
                (std::byte*)geometry.feature_ids.data(), sizeof(hrz::vector_data::FeatureIdHash));
            copy_attribute((std::byte*)geometry.object_ids.data(), sizeof(uint32_t));

            {
                my::BufferResource vb_res(my::BufferResource::BufferType::Vertex);
                vb_res.size = instance_array_size;
                vb_res.usage = my::UsageHint::Static;
                vb_res.data = (void*)instance_array.data();
                vb_res.allow_allocation_failure = true;

                my::ResourceHandle instance_buffer = render->rc->alloc(
                    &vb_res, hrz::monitoring::systems::Impostors, tile->layer_id,
                    {{"contents"_ss, "impostor instance data"_ss},
                     {"tile coords"_ss, tile_coords_str}});
                CHECK_RESOURCE_UPLOAD(instance_buffer, "instance data");
                renderable.instance_buffer = instance_buffer;
            }

            {
                my::BufferResource vb_color_res(my::BufferResource::BufferType::Vertex);
                vb_color_res.size = geometry.colors.size() * sizeof(lm::ubvec4);
                vb_color_res.usage = my::UsageHint::Static;
                vb_color_res.data = (void*)geometry.colors.data();
                vb_color_res.allow_allocation_failure = true;

                my::ResourceHandle color_buffer = render->rc->alloc(
                    &vb_color_res, hrz::monitoring::systems::Impostors, tile->layer_id,
                    {{"contents"_ss, "impostor color data"_ss},
                     {"tile coords"_ss, tile_coords_str}});
                CHECK_RESOURCE_UPLOAD(color_buffer, "color data");
                renderable.color_buffer = color_buffer;
            }

            {
                my::BufferResource vb_scale_res(my::BufferResource::BufferType::Vertex);
                vb_scale_res.size = geometry.impostor_scales.size() * sizeof(lm::vec3);
                vb_scale_res.usage = my::UsageHint::Static;
                vb_scale_res.data = (void*)geometry.impostor_scales.data();
                vb_scale_res.allow_allocation_failure = true;

                my::ResourceHandle scale_buffer = render->rc->alloc(
                    &vb_scale_res, hrz::monitoring::systems::Impostors, tile->layer_id,
                    {{"contents"_ss, "impostor scale data"_ss},
                     {"tile coords"_ss, tile_coords_str}});
                CHECK_RESOURCE_UPLOAD(scale_buffer, "scale data");
                renderable.scale_buffer = scale_buffer;
            }

            {
                renderable.selection_storage =
                    hrz::selection::SelectionStorageUint32BufferMultiIndex(
                        instance_count, {hrz::monitoring::systems::InstancedModels, tile->layer_id},
                        {{"contents"_ss, "impostor selection storage"_ss},
                         {"tile coords"_ss, tile_coords_str}});

                if (tile->has_feature_ids)
                {
                    for (size_t i = 0; i < geometry.feature_ids.size(); ++i)
                    {
                        renderable.selection_storage.register_indirection(
                            geometry.feature_ids.at(i), i);
                    }
                }
            }

            my::VertexInputStream impostor_color_input_stream = has_unique_color
                ? my::VertexInputStream{InputStreamColor,
                                        renderable.color_buffer,
                                        my::VertexFormat::UInt8Norm_4,
                                        0,
                                        sizeof(lm::ubvec4),
                                        my::VertexRate::Constant}
                : my::VertexInputStream{InputStreamColor,
                                        renderable.color_buffer,
                                        my::VertexFormat::UInt8Norm_4,
                                        0,
                                        sizeof(lm::ubvec4),
                                        my::VertexRate::PerInstance};

            my::VertexInputStream impostor_scale_input_stream = has_unique_scale
                ? my::VertexInputStream{InputStreamScale,
                                        renderable.scale_buffer,
                                        my::VertexFormat::Float32_3,
                                        0,
                                        sizeof(lm::vec3),
                                        my::VertexRate::Constant}
                : my::VertexInputStream{
                    InputStreamScale, renderable.scale_buffer,    my::VertexFormat::Float32_3, 0,
                    sizeof(lm::vec3), my::VertexRate::PerInstance};

            my::VertexInputStream streams[] = {
                {InputStreamPosition, _impostor_gpu_resources.vertex_buffer,
                 my::VertexFormat::Float32_2, 0, 0, my::VertexRate::PerVertex},
                impostor_color_input_stream,
                impostor_scale_input_stream,
                {InputStreamImpostorPosition, renderable.instance_buffer,
                 my::VertexFormat::Float32_3, 0, instance_size, my::VertexRate::PerInstance},
                {InputStreamImpostorOrientation, renderable.instance_buffer,
                 my::VertexFormat::UInt32_2, 12, instance_size, my::VertexRate::PerInstance},
                {InputStreamFeatureId, renderable.instance_buffer, my::VertexFormat::UInt32_2, 20,
                 instance_size, my::VertexRate::PerInstance},
                {InputStreamObjectId, renderable.instance_buffer, my::VertexFormat::UInt32, 28,
                 instance_size, my::VertexRate::PerInstance},
                renderable.selection_storage.get_vertex_input_stream(render, InputStreamSelection),
            };

            my::VertexInputResource vi_res;
            vi_res.attrib_count = HRZ_ARRAY_COUNT(streams);
            vi_res.attribs = streams;
            renderable.data.vertex_input = render->rc->alloc(
                &vi_res, hrz::monitoring::systems::Impostors, tile->layer_id,
                {{"tile coords"_ss, tile_coords_str}});
            renderable.data.vertex_count = 4;
            renderable.data.instance_count = instance_count;

            auto& ubo_data = tile->impostor->ubo_data;
            hrz::split_double(geometry.origin.x, ubo_data.center_low.x, ubo_data.center_high.x);
            hrz::split_double(geometry.origin.y, ubo_data.center_low.y, ubo_data.center_high.y);
            hrz::split_double(geometry.origin.z, ubo_data.center_low.z, ubo_data.center_high.z);
            ubo_data.object_ref = tile->object_ref.to_uvec2();
            ubo_data.feature_ref = tile->feature_ref.to_uvec3();
            ubo_data.clip_id = tile->draw_prps.clip_id;
            ubo_data.color_blend_mode = tile->draw_prps.feature_color_blend_mode;
            ubo_data.color_blend_strength = tile->draw_prps.feature_color_blend_strength;
            tile->impostor->ubo_dirty = true;

            ImpostorUniformData ubo_impostor;
            ubo_impostor.atlas_size.x = cfg->impostor_params.atlas_size().x();
            ubo_impostor.atlas_size.y = cfg->impostor_params.atlas_size().y();
            ubo_impostor.scale_correction = cfg->baked_resources.scale_correction;
            ubo_impostor.offset_from_origin = lm::vec3(cfg->baked_resources.model_bsphere.center);

            my::BufferResource ubo_res(my::BufferResource::BufferType::Uniform);
            ubo_res.size = sizeof(ImpostorTileUniformData);
            ubo_res.usage = my::UsageHint::Static;
            ubo_res.data = &ubo_data;
            renderable.data.ubo_tile_buffer = render->rc->alloc(
                &ubo_res, hrz::monitoring::systems::Impostors, tile->layer_id,
                {{"contents"_ss, "impostor tile uniforms"_ss},
                 {"tile coords"_ss, tile_coords_str}});

            ubo_res.size = sizeof(ImpostorUniformData);
            ubo_res.data = &ubo_impostor;
            renderable.data.ubo_impostor_buffer = render->rc->alloc(
                &ubo_res, hrz::monitoring::systems::Impostors, tile->layer_id,
                {{"contents"_ss, "impostor uniforms"_ss}, {"tile coords"_ss, tile_coords_str}});

            renderable.data.visual_shader = _impostor_gpu_resources.visual_shader;
            renderable.data.picking_shader = _impostor_gpu_resources.picking_shader;
            renderable.data.selection_shader = _impostor_gpu_resources.selection_shader;

            renderable.bin_mask = hrz::RenderWorldTransparentBin;
            renderable.center = geometry.bsphere.center;
            renderable.radius = geometry.bsphere.radius
                + cfg->baked_resources.model_bsphere.radius * tile->geometry.average_scale;

            renderable.data.impostor_texture = cfg->baked_resources.color_texture;
            renderable.data.normal_texture = cfg->baked_resources.normal_texture;
            renderable.data.scale_coefficients_texture =
                cfg->baked_resources.scale_coefficients_texture;
            renderable.data.impostor_sampler = _impostor_gpu_resources.texture_sampler;
            renderable.data.scene_views = cfg->scene_views_bitset.bits();

            tile->impostor->renderable = {std::move(renderable)};

#undef CHECK_RESOURCE_UPLOAD
        };

        if (instance_count > 0 && cfg->impostor_params.use_impostor()
            && cfg->status == Config::Status::Ready)
        {
            prepare_impostors();
        }

        geometry.feature_ids.clear();
        geometry.feature_ids.shrink_to_fit();
        geometry.positions.clear();
        geometry.positions.shrink_to_fit();
        geometry.normals.clear();
        geometry.normals.shrink_to_fit();
        geometry.scales.clear();
        geometry.scales.shrink_to_fit();
        geometry.colors.clear();
        geometry.colors.shrink_to_fit();
        geometry.object_ids.clear();
        geometry.object_ids.shrink_to_fit();
        geometry.impostor_positions.clear();
        geometry.impostor_positions.shrink_to_fit();
        geometry.impostor_scales.clear();
        geometry.impostor_scales.shrink_to_fit();
        geometry.impostor_orientations.clear();
        geometry.impostor_orientations.shrink_to_fit();
    }

    void _work_loading_models(WorkCtx& ctx)
    {
        for (auto handle : _models_to_work)
        {
            Config* cfg = _configs.get_object(handle);
            if (!cfg) continue;

            hrz::model::work(
                cfg->model_prototype, ctx.al, ctx.js, ctx.ba, ctx.imgdec, ctx.attributions);
            hrz::model::work(cfg->model_prototype, cfg->baked_model);
        }

        for (auto it = _configs_loading.begin(); it != _configs_loading.end();)
        {
            Config* cfg = _configs.get_object(*it);

            assert(cfg->status == Config::Status::Loading);

            if (cfg->model_prototype == nullptr)
            {
                cfg->model_prototype = hrz::model::create_from_gltf_url(
                    ctx.al, {cfg->url, cfg->preserve_query_parameters}, cfg->headers, {},
                    hrz::get_request_queue(0, hrz::assets_loader::MeshModels),
                    hrz::combine_loading_priorities(0, 0),
                    {hrz::monitoring::systems::InstancedModels, cfg->layer_id});
                cfg->model_geometry =
                    hrz::model::create_instanced_model_geometry(cfg->model_prototype);
                cfg->model_material = hrz::model::create_model_material(cfg->model_prototype, {});
                cfg->baked_model = hrz::model::create_baked_model(
                    cfg->model_prototype, cfg->model_geometry, cfg->model_material);
                _models_to_work.insert(*it);
            }

            auto model_status = hrz::model::get_status(cfg->model_prototype);
            auto baked_status = hrz::model::get_status(cfg->model_prototype, cfg->baked_model);

            if (model_status == hrz::model::ModelPrototypeStatus::Error
                || baked_status == hrz::model::BakedModelStatus::Error)
            {
                _destroy_model(ctx, cfg);
                cfg->status = Config::Status::Error;
                _models_to_work.erase(*it);
                _configs_loading.erase(it++);
            }
            else if (
                model_status == hrz::model::ModelPrototypeStatus::Ready
                && (baked_status == hrz::model::BakedModelStatus::Ready
                    || baked_status == hrz::model::BakedModelStatus::ReadyWithErrors))
            {
                if (cfg->impostor_params.use_impostor())
                {
                    hrz::model::DrawProperties draw_prps;
                    draw_prps.transform = cfg->frame;
                    draw_prps.lighting = cfg->lighting_settings;
                    draw_prps.color = lm::vec4(1.0f);
                    draw_prps.clip_id = -1;
                    draw_prps.apply_feature_color_to_overlay = false;
                    draw_prps.overlay_material_enabled = false;
                    draw_prps.overlay_material_opacity = 0;
                    draw_prps.feature_color_blend_mode = hrz_proto::BLEND_NORMAL;
                    draw_prps.feature_color_blend_strength = 0;
                    draw_prps.draw_under_flat_overlays = false;

                    cfg->impostor_baking_ticket = hrz::impostor::bake_impostor(
                        _impostor_baker, cfg->model_prototype, {}, cfg->impostor_params, draw_prps,
                        cfg->frame, cfg->layer_id);
                    cfg->status = Config::Status::BakingImpostor;
                    _configs_baking_impostor.insert(*it);
                }
                else
                {
                    cfg->status = Config::Status::Ready;
                    _models_to_work.erase(*it);
                }

                _configs_loading.erase(it++);
            }
            else
            {
                it++;
            }
        }
    }

    void _work_impostors(WorkCtx& ctx)
    {
        for (auto it = _configs_baking_impostor.begin(); it != _configs_baking_impostor.end();)
        {
            Config* cfg = _configs.get_object(*it);

            assert(cfg->status == Config::Status::BakingImpostor);

            auto baking_status =
                hrz::impostor::baking_status(_impostor_baker, cfg->impostor_baking_ticket);
            if (baking_status == hrz::impostor::BakingStatus::Ready)
            {
                auto baked_resources = hrz::impostor::get_baked_resources(
                    _impostor_baker, cfg->impostor_baking_ticket);
                assert(baked_resources.has_value());
                cfg->baked_resources = baked_resources.value();
                cfg->status = Config::Status::Ready;
                _models_to_work.erase(*it);
                _configs_baking_impostor.erase(it++);
            }
            else if (baking_status == hrz::impostor::BakingStatus::Error)
            {
                HRZ_LOG_ERROR("Couldn't bake impostor for model '{}'.", cfg->url);
                cfg->status = Config::Status::Error;
                _configs_baking_impostor.erase(it++);
            }
            else
            {
                it++;
            }
        }
    }

    void _work_tiles(WorkCtx& ctx)
    {
        for (auto handle : _tiles_to_bake)
        {
            Tile* tile = _tiles.get_object(handle);
            if (tile)
            {
                assert(tile->status == Tile::Status::StartBaking);

                tile->bake_ticket = hrz_jobs::add_job_bake_3d_model_geometry(
                    ctx.js, tile->bake_data, {hrz::monitoring::systems::Models, tile->layer_id});
                tile->status = Tile::Status::Baking;
                _tiles_baking.insert(handle);
            }
        }
        _tiles_to_bake.clear();

        for (auto it = _tiles_baking.begin(); it != _tiles_baking.end();)
        {
            Tile* tile = _tiles.get_object(*it);
            if (tile)
            {
                assert(tile->status == Tile::Status::Baking);

                if (hrz_jobs::is_job_valid(ctx.js, tile->bake_ticket)
                    && hrz_jobs::is_job_finished(ctx.js, tile->bake_ticket))
                {
                    hrz::vt::ModelGeometry response;
                    hrz_jobs::get_job_response(ctx.js, tile->bake_ticket, response);

                    TileGeometry geometry;

                    geometry.origin = response.origin;
                    geometry.bsphere = response.bsphere;
                    geometry.average_scale = response.average_scale;

                    geometry.positions = std::move(response.positions);
                    geometry.normals = std::move(response.normals);
                    geometry.scales = std::move(response.scales);

                    geometry.feature_ids = std::move(response.feature_ids);
                    geometry.colors = std::move(response.colors);
                    geometry.object_ids = std::move(response.object_ids);
                    geometry.impostor_positions = std::move(response.impostor_positions);
                    geometry.impostor_scales = std::move(response.impostor_scales);
                    geometry.impostor_orientations = std::move(response.impostor_orientations);

                    tile->geometry = std::move(geometry);
                    tile->status = Tile::Status::PrepareRendering;
                    _tiles_prepare_rendering.insert(*it);

                    _tiles_baking.erase(it++);
                    continue;
                }
            }
            else
            {
                _tiles_baking.erase(it++);
                continue;
            }

            it++;
        }

        hrz::StaticVector<hrz::render::ScreenSpaceError, hrz::SCENE_VIEW_COUNT> sses;
        for (const auto& view_info : ctx.views_info)
        {
            sses.push_back(hrz::render::ScreenSpaceError(
                view_info.cam_view_info.cam.fovy, (double)view_info.cam_view_info.viewport.size.y,
                view_info.cam_view_info.viewport.device_pixel_ratio));
        }

        for (auto handle : _tiles_ready)
        {
            Tile* tile = _tiles.get_object(handle);
            Config* cfg = _configs.get_object(tile->config_handle);

            if (!cfg) continue;

            if (cfg->impostor_params.use_impostor())
            {
                tile->impostor->draw_in.reset();

                for (size_t i = 0; i < sses.size(); ++i)
                {
                    const auto& view_info = ctx.views_info[i];

                    double distance = std::max(
                        lm::length(tile->geometry.bsphere.center - view_info.cam_view_info.cam.pos)
                            - tile->geometry.bsphere.radius,
                        0.0);

                    double world_size = tile->geometry.average_scale
                        * cfg->baked_resources.model_bsphere.radius * 2.0;

                    double size_pixels = sses[i].compute_screen_space_size(world_size, distance);

                    if (size_pixels < cfg->impostor_params.max_screen_size_pixels())
                    {
                        tile->impostor->draw_in.set((int)view_info.view);
                    }
                }
            }
        }
    }

    std::pair<uint64_t, Channel> create_channel() override { return _channels.create_channel(); }
};
} // namespace

namespace hrz::vt
{
std::unique_ptr<ReprSystem> create_model_repr_system()
{
    return std::unique_ptr<ReprSystem>(new ModelVectorReprSystem());
}
} // namespace hrz::vt
