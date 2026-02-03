#include "hrz/common/blob_array.h"
#include "hrz/common/fmt.h" // IWYU pragma: keep
#include "hrz/common/monitoring_defs.h"
#include "hrz/common/palette.h"
#include "hrz/core/channel_group.h"
#include "hrz/core/jobs/jobs_tickets.h"
#include "hrz/core/jobs/vector_tiles_jobs_params.h"
#include "hrz/core/render/context.h"
#include "hrz/core/render/defs.h"
#include "hrz/core/render/resource_context.h"
#include "hrz/core/render/resources.h"
#include "hrz/core/selection_storage.h"
#include "hrz/core/shaders/collection.h"
#include "hrz/core/vector/flat_overlay.h"
#include "hrz/core/vector/heatmaps.h"
#include "hrz/core/vector/repr.h"
#include "hrz/fnd/gen_object_pool.h"
#include "hrz/fnd/mem.h"
#include "hrz/fnd/meta.h"

#include <optional>

namespace
{
enum
{
    UboTileParams = hrz::vector_flat_overlay::UboVectorOverlayPass + 1,

    InputStreamInMeshPos = 0,

    InputStreamPointsPosition = 1,
    InputStreamPointsValue = 2,
    InputStreamPointsRadius = 3,
};

struct TileUniformData
{
    lm::vec4 center_low;
    lm::vec4 center_high;
    hrz::bool32 size_in_meters;
    float blur_size;
    uint32_t padding[2];
};

HRZ_CHECK_UBO_SIZE(TileUniformData);

struct RenderableFeatures : public my::Renderer::Renderable
{
    my::Renderer::BinMask bin_mask;
    my::Renderer::ViewMask main_views;
    hrz::BSphere<double> bsphere;

    my::ResourceHandle points_instance_buffer = my::ResourceHandle::null();

    struct FeatureRenderData
    {
        my::ResourceHandle vertex_input = my::ResourceHandle::null();
        my::ResourceHandle shader = my::ResourceHandle::null();
        my::ResourceHandle ubo_buffer = my::ResourceHandle::null();
        hrz::heatmaps::ReprId heatmap_id = 0;
        uint32_t instance_count = 0;
        uint32_t vertex_count = 0;
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
        const auto* user_data = (const hrz::heatmaps::RenderUserData*)user_data_raw;

        if (user_data->repr_id != data->heatmap_id) return;

        if (((1 << user_data->scene_view_data->scene_view) & data->scene_views) == 0) return;

        if (render_type != hrz::RenderVisual) return;

        if (data->instance_count == 0) return;

        auto points_batch = my::DrawBatchInfo(my::PrimitiveType::TriangleStrip, data->vertex_count)
                                .instanced(data->instance_count);

        rb->push_state();

        my::UboBinding ubo_bindings[] = {
            {UboTileParams, data->ubo_buffer, 0, sizeof(TileUniformData)},
        };
        rb->bind(ubo_bindings);

        auto state = rb->get_current_state();
        r->draw(points_batch, data->shader, data->vertex_input, state.ubos, state.textures);

        rb->pop_state();
    }

    void collect_render_info(my::Renderer::Queue& queue, const my::Renderer::Culler& culler)
        const override
    {
        if (culler.is_visible_in_some_views(bsphere.center, bsphere.radius, main_views))
        {
            queue.enqueue(bin_mask, render_callback, data, bsphere.center, bsphere.radius, 0);
        }
    }
};

struct Config
{
    uint64_t layer_id;
    uint32_t repr_id;
    hrz::heatmaps::ReprId heatmap_id;

    uint64_t value_prp = hrz::style::Parser::INVALID_PROPERTY;
    uint64_t disc_radius_prp = hrz::style::Parser::INVALID_PROPERTY;

    hrz_proto::InWorldSizeUnit disc_radius_size_unit;
    float blur_size = 0;
    float default_value = 0;
    float default_disc_radius = 0;
    uint32_t scene_views = 0;
    hrz_proto::HeatmapAccumulationMode accumulation;
};

struct TileGeometry
{
    hrz::BSphere<double> bsphere;

    // Positions are relative to the centre of the tile.
    hrz::BlobArray<hrz_jobs::HeatmapGeometry::PointInstance> point_data;
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
        ReadyToBake,
        Baking,
        FinishedBaking,
        Ready,
        Error,
    };

    std::optional<TileId> id;
    uint64_t layer_id;
    hrz::heatmaps::ReprId heatmap_id;
    hrz::TileCoords coords;
    Status status;

    hrz_jobs::BakeHeatmapGeometryTicket bake_ticket;
    std::optional<hrz_jobs::HeatmapData> bake_data;
    std::optional<TileGeometry> geometry;
    std::optional<RenderableFeatures> renderable;

    uint32_t scene_views;
    hrz_proto::HeatmapAccumulationMode accumulation;

    hrz_proto::InWorldSizeUnit disc_radius_size_unit;
    float blur_size;
};

static const std::string kAdditiveShaderName =
    std::string(hrz_shaders::HeatmapPoints_name) + "_Additive";
static const std::string kBlendedShaderName =
    std::string(hrz_shaders::HeatmapPoints_name) + "_Blended";

class HeatmapReprSystem : public hrz::vt::ReprSystem
{
    using ConfigH = uint64_t;
    using TileH = uint64_t;

    using ConfigIndexPool = hrz::GenIndexPool<ConfigH, 32, 32>;
    using ConfigPool = hrz::GenObjectPool<Config, ConfigIndexPool, 64>;

    using TileIndexPool = hrz::GenIndexPool<TileH, 32, 32>;
    using TilePool = hrz::GenObjectPool<Tile, TileIndexPool, 64>;

    my::ResourceHandle _point_vertex_buffer;
    my::ResourceHandle _additive_shader;
    my::ResourceHandle _blended_shader;

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

    hrz::flat_hash_map<ConfigH, hrz::heatmaps::OverlayConfig> _configs_to_register;
    hrz::flat_hash_set<ConfigH> _configs_to_unregister;

    hrz::flat_hash_set<std::pair<TileH, uint32_t>> _drawn_tiles;

    hrz::flat_hash_set<TileH> _to_bake;
    hrz::flat_hash_set<TileH> _baking;
    hrz::flat_hash_set<TileH> _finished_baking;
    hrz::flat_hash_set<TileH> _removed;

    std::vector<my::ResourceHandle> _resources_to_free;

    hrz::ChannelGroup<hrz::vt::FromReprMessage, hrz::vt::ToReprMessage> _channels;

public:
    ~HeatmapReprSystem() override = default;

    void deinit(WorkCtx& ctx, hrz::Render*) override { work_removed_tiles(ctx); }

    static void collect_shaders(hrz::GpuResourceContext* rc)
    {
        static const my::IndexName ubos[] = {
            {hrz::UboFrame, "Frame"},
            {hrz::vector_flat_overlay::UboVectorOverlayCameras, "OverlayCamerasUniform"},
            {hrz::heatmaps::UboHeatmapPoints, "HeatmapPass"},
            {UboTileParams, "Tile"},
        };

        const char* color_outputs[] = {"o_value"};

        my::IndexName visual_samplers[] = {{hrz::SamplerCameraHeight, "u_camera_height"}};

        {
            my::IndexName attribs[] = {
                {InputStreamInMeshPos, "i_in_mesh_pos"},
                {InputStreamPointsPosition, "i_position"},
                {InputStreamPointsValue, "i_value"},
                {InputStreamPointsRadius, "i_radius"},
            };

            my::ShaderResource res{};
            res.name = kAdditiveShaderName.c_str();
            res.vertex_source_len = hrz_shaders::HeatmapPoints_vert_len;
            res.vertex_source = hrz_shaders::HeatmapPoints_vert;
            res.fragment_source_len = hrz_shaders::HeatmapPoints_frag_len;
            res.fragment_source = hrz_shaders::HeatmapPoints_frag;
            res.attribs = attribs;
            res.uniform_blocks = ubos;
            res.samplers = visual_samplers;
            res.outputs = color_outputs;

            res.initial_state.depth.test = true;

            res.initial_state.color_blend.enable = true;
            res.initial_state.color_blend.color.op = my::ColorBlendState::Add;
            res.initial_state.color_blend.color.src = my::ColorBlendState::One;
            res.initial_state.color_blend.color.dst = my::ColorBlendState::One;
            res.initial_state.rasterization.cull_mode = my::RasterizationState::CullMode::None;

            my::ResourceHandle additive_shader =
                rc->alloc(&res, hrz::monitoring::systems::Heatmaps);

            my::ShaderDerivativeResource res2(additive_shader, res, kBlendedShaderName.c_str());
            res2.initial_state.color_blend.color.dst = my::ColorBlendState::OneMinusSrcColor;

            rc->alloc(&res2, hrz::monitoring::systems::Heatmaps);
        }
    }

    void init_render(hrz::Render* render) override
    {
        assert(render);

        _additive_shader = render->rc->retrieve_shader(kAdditiveShaderName.c_str());
        _blended_shader = render->rc->retrieve_shader(kBlendedShaderName.c_str());

        {
            const lm::vec2 positions[] = {{1.0, 1.0}, {1.0, -1.0}, {-1.0, 1.0}, {-1.0, -1.0}};

            my::BufferResource vertex_buffer(my::BufferResource::BufferType::Vertex);
            vertex_buffer.size = sizeof(positions);
            vertex_buffer.usage = my::UsageHint::Static;
            vertex_buffer.data = &positions[0].x;

            _point_vertex_buffer = render->rc->alloc(
                &vertex_buffer, hrz::monitoring::systems::Heatmaps, hrz::monitoring::NoLayer,
                {{"contents"_ss, "point vertex positions"_ss}});
        }
    }

    void deinit_render(hrz::Render* render) override
    {
        assert(render);

        render->rc->dealloc(_point_vertex_buffer);
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
        if (repr.type() != hrz_proto::VectorReprType::HEATMAP_VECTOR_REPR)
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

        std::string_view value_prp_name = repr.heatmap().value().name();
        std::string_view disc_radius_prp_name = repr.heatmap().disc_radius().name();

        Config config;
        config.layer_id = layer_id;
        config.repr_id = repr.id();

        config.default_value = repr.heatmap().value().default_value();
        config.default_disc_radius = repr.heatmap().disc_radius().default_value();
        config.disc_radius_size_unit = repr.heatmap().disc_radius_size_unit();
        config.blur_size = repr.heatmap().blur_size();
        config.scene_views = repr.scene_views().bits();
        config.accumulation = repr.heatmap().accumulation();

        config.value_prp = register_prp(value_prp_name, config.default_value);
        config.disc_radius_prp = register_prp(disc_radius_prp_name, config.default_disc_radius);

        hrz::heatmaps::OverlayConfig quad_config;
        quad_config.palette = hrz::palette::from_proto(repr.heatmap().numeric_palette());
        quad_config.z_index = repr.heatmap().z_index();

        ConfigH handle = _configs.alloc(std::move(config));
        _configs_to_register.insert({handle, quad_config});

        _configs_by_id.insert({style_id, handle});

        return handle;
    }

    void unregister_style(
        ConfigH config_handle,
        const std::function<void(uint64_t prp_id)>& unregister_property)
    {
        if (!_configs.is_valid(config_handle)) return;

        const Config* cfg = _configs.get_object(config_handle);

        unregister_property(cfg->value_prp);
        unregister_property(cfg->disc_radius_prp);

        _configs_to_unregister.insert(config_handle);
    }

    std::optional<TileH> add_tile(
        ConfigH config_handle,
        hrz::TileCoords coords,
        uint64_t layer_id,
        const hrz::vector_data::FeatureIds&,
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

        hrz_jobs::HeatmapData bake_data;
        bake_data.coords = coords;

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

        bake_data.default_value = cfg.default_value;
        bake_data.default_disc_radius = cfg.default_disc_radius;

        bake_data.repr_id = cfg.repr_id;
        bake_data.value_prp = cfg.value_prp;
        bake_data.disc_radius_prp = cfg.disc_radius_prp;

        bake_data.geometry = geometry.geometry;

        auto& dst_style = bake_data.style;
        dst_style.prps = style.prps;
        dst_style.values = style.values;
        dst_style.out_of_line_data = style.out_of_line_data;
        dst_style.instances = style.instances;

        tile.bake_data = std::move(bake_data);
        tile.heatmap_id = cfg.heatmap_id;
        tile.scene_views = cfg.scene_views;
        tile.disc_radius_size_unit = cfg.disc_radius_size_unit;
        tile.blur_size = cfg.blur_size;
        tile.accumulation = cfg.accumulation;

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
            _resources_to_free.push_back(renderable.points_instance_buffer);
            _resources_to_free.push_back(renderable.data.vertex_input);
            _resources_to_free.push_back(renderable.data.ubo_buffer);
        }

        _tiles.release(handle);
    }

    void work_start_baking(WorkCtx& ctx, TileH handle, Tile* tile)
    {
        assert(tile->status == Tile::Status::ReadyToBake);
        assert(tile->bake_data.has_value());

        tile->bake_ticket = hrz_jobs::add_job_bake_heatmap_geometry(
            ctx.js, std::move(tile->bake_data).value(),
            {hrz::monitoring::systems::Heatmaps, tile->layer_id});
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
                geometry.bsphere = response.bsphere;
                geometry.point_data = std::move(response.point_data);

                tile->geometry = std::move(geometry);
                tile->status = Tile::Status::FinishedBaking;
                _finished_baking.insert(handle);
            }
            else
            {
                HRZ_LOG_ERROR(
                    "Could not bake flat overlay representation for tile {}-{}-{}",
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

        auto tile_coords_str = fmt::to_string(tile->coords);

        TileGeometry& geometry = tile->geometry.value();
        RenderableFeatures renderable;

        if (!geometry.point_data.empty())
        {
            using Instance = hrz_jobs::HeatmapGeometry::PointInstance;

            auto& renderable_data = renderable.data;
            auto geometry_data = geometry.point_data.get_data();

            my::BufferResource pb_res(my::BufferResource::BufferType::Vertex);
            pb_res.size = geometry_data.size_bytes();
            pb_res.usage = my::UsageHint::Static;
            pb_res.data = geometry_data.data();
            pb_res.allow_allocation_failure = true;

            my::ResourceHandle instance_buffer = render->rc->alloc(
                &pb_res, hrz::monitoring::systems::Heatmaps, tile->layer_id,
                {{"contents"_ss, "point instance data"_ss}, {"tile coords"_ss, tile_coords_str}});
            CHECK_RESOURCE_UPLOAD(instance_buffer, "point instance data");
            renderable.points_instance_buffer = instance_buffer;

            my::VertexInputStream streams[] = {
                {InputStreamInMeshPos, _point_vertex_buffer, my::VertexFormat::Float32_2, 0, 0,
                 my::VertexRate::PerVertex},
                {InputStreamPointsPosition, instance_buffer, my::VertexFormat::Float32_3,
                 offsetof(Instance, position), sizeof(Instance), my::VertexRate::PerInstance},
                {InputStreamPointsValue, instance_buffer, my::VertexFormat::Float32,
                 offsetof(Instance, value), sizeof(Instance), my::VertexRate::PerInstance},
                {InputStreamPointsRadius, instance_buffer, my::VertexFormat::Float32,
                 offsetof(Instance, disc_radius), sizeof(Instance), my::VertexRate::PerInstance},
            };

            my::VertexInputResource vi_res;
            vi_res.attribs = streams;
            my::ResourceHandle vertex_input = render->rc->alloc(
                &vi_res, hrz::monitoring::systems::Heatmaps, tile->layer_id,
                {{"tile coords"_ss, tile_coords_str}});

            renderable_data.vertex_input = vertex_input;
            renderable_data.vertex_count = 4;
            renderable_data.instance_count = geometry_data.size();

            TileUniformData ubo;
            hrz::split_double(geometry.bsphere.center.x, ubo.center_low.x, ubo.center_high.x);
            hrz::split_double(geometry.bsphere.center.y, ubo.center_low.y, ubo.center_high.y);
            hrz::split_double(geometry.bsphere.center.z, ubo.center_low.z, ubo.center_high.z);
            ubo.blur_size = tile->blur_size;
            ubo.size_in_meters =
                tile->disc_radius_size_unit == hrz_proto::InWorldSizeUnit::IN_WORLD_SIZE_IN_METERS;

            my::BufferResource ub_res(my::BufferResource::BufferType::Uniform);
            ub_res.size = sizeof(TileUniformData);
            ub_res.usage = my::UsageHint::Updatable;
            ub_res.data = &ubo;

            my::ResourceHandle uniform_buffer = render->rc->alloc(
                &ub_res, hrz::monitoring::systems::Heatmaps, tile->layer_id,
                {{"contents"_ss, "point uniforms"_ss}, {"tile coords"_ss, tile_coords_str}});

            renderable_data.ubo_buffer = uniform_buffer;
            renderable_data.shader = (tile->accumulation == hrz_proto::HEATMAP_WEIGHTED_BLENDED)
                ? _blended_shader
                : _additive_shader;
        }

        renderable.data.heatmap_id = tile->heatmap_id;
        renderable.data.scene_views = tile->scene_views;
        renderable.bin_mask = hrz::RenderHeatmapBin;
        renderable.main_views = 0;
        renderable.bsphere = geometry.bsphere;

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
                                    message.feature_ids, message.geometry, message.style,
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
                        [](const hrz::vt::repr::messages::UpdateTileElevation&) { /* No-op */ },
                        [](const hrz::vt::repr::messages::UpdateClipId&) { /* No-op */ },
                        [](const hrz::vt::repr::messages::UpdateLighting&) { /* No-op */ },
                        [](const hrz::vt::repr::messages::UpdateSelection&) { /* No-op */ },
                    },
                    generic_message);
            }
        }

        render_request |= work_removed_tiles(ctx);

        for (auto pair : _configs_to_register)
        {
            Config* config = _configs.get_object(pair.first);
            config->heatmap_id = hrz::heatmaps::register_repr(
                ctx.heatreg, {config->repr_id, config->scene_views, config->layer_id}, pair.second);
        }
        _configs_to_register.clear();

        for (auto handle : _configs_to_unregister)
        {
            Config* config = _configs.get_object(handle);
            hrz::heatmaps::unregister_repr(ctx.heatreg, config->heatmap_id);

            _configs.release(handle);
        }
        _configs_to_unregister.clear();

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
            render_request.schedule_flat_overlay_render();
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
                renderable.main_views = ctx.render->main_views;
                ctx.render->rd->collect_renderable(renderable);
            }
        }
        _drawn_tiles.clear();
    }

    std::pair<uint64_t, Channel> create_channel() override { return _channels.create_channel(); }
};

} // namespace

namespace hrz::vt
{
std::unique_ptr<ReprSystem> create_heatmap_repr_system()
{
    return std::unique_ptr<ReprSystem>(new HeatmapReprSystem());
}

void collect_heatmap_shaders(hrz::GpuResourceContext* rc)
{
    HeatmapReprSystem::collect_shaders(rc);
}
} // namespace hrz::vt
