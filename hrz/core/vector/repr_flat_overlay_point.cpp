// SPDX-FileCopyrightText: Copyright 2026 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/common/blob_array.h"
#include "hrz/common/color.h"
#include "hrz/common/fmt.h" // IWYU pragma: keep
#include "hrz/common/monitoring_defs.h"
#include "hrz/core/jobs/vector_tiles_jobs_params.h"
#include "hrz/core/render/context.h"
#include "hrz/core/render/defs.h"
#include "hrz/core/render/resource_context.h"
#include "hrz/core/selection_storage.h"
#include "hrz/core/shaders/collection.h"
#include "hrz/core/vector/flat_overlay.h"
#include "hrz/core/vector/image_loader.h"
#include "hrz/core/vector/repr.h"
#include "hrz/core/vector/repr_flat_overlay.h"
#include "hrz/fnd/defer.h"
#include "hrz/protocol/vector/flat_overlay_point_repr.pb.h"

#include <optional>

using namespace hrz::vt::flat_overlay;

namespace
{

enum
{
    UboTileParams = hrz::vector_flat_overlay::UboVectorOverlayPass + 1,

    InputStreamInMeshPos = 0,
    InputStreamPosition = 1,
    InputStreamColor = 2,
    InputStreamRadius = 3,
    InputStreamOutlineColor = 4,
    InputStreamOutlineWidth = 5,
    InputStreamFeatureIndex = 6,

    SamplerSelection = hrz::SamplerCustomStart,
    SamplerFeatureIds,
};

struct PointsTileUniformData
{
    HRZ_UBO_STRUCT_FIELD(CommonTileUniformData) base;
    uint32_t radius_unit;
    uint32_t outline_width_unit;
    uint32_t _padding[2];
};

HRZ_CHECK_UBO_SIZE(PointsTileUniformData);

struct PointsRenderable final : public BaseRenderable
{
    my::ResourceHandle instance_buffer = my::ResourceHandle::null();

    struct FeatureRenderData : public BaseRenderData
    {
        my::ResourceHandle ubo_buffer = my::ResourceHandle::null();
        my::ResourceHandle vertex_input = my::ResourceHandle::null();
        my::ResourceHandle shader = my::ResourceHandle::null();
        my::ResourceHandle picking_shader = my::ResourceHandle::null();
        my::ResourceHandle selection_shader = my::ResourceHandle::null();
        my::ResourceHandle feature_id_texture = my::ResourceHandle::null();
        my::ResourceHandle selection_texture = my::ResourceHandle::null();
        my::ResourceHandle metadata_sampler = my::ResourceHandle::null();

        uint32_t instance_count = 0;
        uint32_t vertex_count = 0;
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

        if (data->instance_count == 0) return;

        rb->push_state();
        HRZ_DEFER[rb]()
        {
            rb->pop_state();
        };

        my::ResourceHandle shader;
        switch (render_type)
        {
            case hrz::RenderVisual:
            {
                shader = data->shader;
                const my::TextureBinding texture_bindings[] = {
                    {SamplerFeatureIds, data->feature_id_texture, data->metadata_sampler}
                };
                rb->bind(texture_bindings);
                break;
            }
            case hrz::RenderPicking: shader = data->picking_shader; break;
            case hrz::RenderSelection:
            {
                if (!data->has_selected_features) return;
                shader = data->selection_shader;
                const my::TextureBinding texture_bindings[] = {
                    {SamplerSelection, data->selection_texture, data->metadata_sampler}
                };
                rb->bind(texture_bindings);
                break;
            }
            default: return;
        }

        auto batch = my::DrawBatchInfo(my::PrimitiveType::TriangleStrip, data->vertex_count)
                         .instanced(data->instance_count);
        const my::UboBinding ubo_bindings[] = {
            {UboTileParams, data->ubo_buffer, 0, sizeof(PointsTileUniformData)},
        };
        rb->bind(ubo_bindings);

        auto state = rb->get_current_state();
        r->draw(batch, shader, data->vertex_input, state.ubos, state.textures);

        data->draw_report->features_drawn += 1;
    }

    void collect_render_info(my::Renderer::Queue& queue, const my::Renderer::Culler& culler)
        const override
    {
        if (culler.is_visible_in_some_views(clamped_center, clamped_radius, main_views)
            && culler.is_visible_in_any_view(sea_center, sea_radius, bin_mask))
        {
            queue.enqueue(bin_mask, render_callback, data, sea_center, sea_radius, z_index);
        }
    }

    void free_resources(std::vector<my::ResourceHandle>& to_free) override
    {
        to_free.push_back(instance_buffer);
        to_free.push_back(data.vertex_input);
        to_free.push_back(data.ubo_buffer);
    }

    BaseRenderData& get_base_render_data() override { return data; }
};

struct PointConfig : public BaseConfig
{
    uint64_t color_prp = hrz::style::Parser::INVALID_PROPERTY;
    uint64_t radius_prp = hrz::style::Parser::INVALID_PROPERTY;
    uint64_t outline_color_prp = hrz::style::Parser::INVALID_PROPERTY;
    uint64_t outline_width_prp = hrz::style::Parser::INVALID_PROPERTY;

    lm::ubvec4 default_color_srgb = {0, 0, 0, 0};
    float default_radius = 0;
    hrz_proto::InWorldSizeUnit radius_unit;

    lm::ubvec4 default_outline_color_srgb = {0, 0, 0, 0};
    float default_outline_width = 0;
    hrz_proto::InWorldSizeUnit outline_width_unit;

    uint32_t z_index = 0;
    bool clip_to_tile = false;
};

struct PointTileGeometry : public BaseTileGeometry
{
    hrz::BlobArray<hrz_jobs::FlatPointGeometry::PointInstance> baked_data;
};

struct PointTile : public BaseTile<PointTileGeometry, hrz_jobs::FlatPointData>
{
    hrz_jobs::BakeFlatPointGeometryTicket bake_ticket;
    std::optional<PointsRenderable> renderable;
    PointsTileUniformData ubo_template;
};

struct PointFlatOverlayTraits
{
    using Config = PointConfig;
    using TileGeometry = PointTileGeometry;
    using Tile = PointTile;
    using BakingData = hrz_jobs::FlatPointData;
    using BakedData = hrz_jobs::FlatPointGeometry;
};

class FlatOverlayPointReprSystem final :
    public hrz::vt::flat_overlay::FlatOverlayReprSystem<PointFlatOverlayTraits>
{
    using Base = hrz::vt::flat_overlay::FlatOverlayReprSystem<PointFlatOverlayTraits>;
    using Config = PointConfig;
    using Tile = PointTile;
    using TileGeometry = PointTileGeometry;
    using BakedData = hrz_jobs::FlatPointGeometry;

    my::ResourceHandle _visual_shader;
    my::ResourceHandle _picking_shader;
    my::ResourceHandle _selection_shader;

    my::ResourceHandle _vertex_buffer;

public:
    static void collect_shaders(hrz::GpuResourceContext* rc)
    {
        static const my::IndexName ubos[] = {
            {hrz::UboFrame, "Frame"},
            {hrz::vector_flat_overlay::UboVectorOverlayCameras, "OverlayCamerasUniform"},
            {hrz::vector_flat_overlay::UboVectorOverlayPass, "OverlayPasses"},
            {UboTileParams, "Tile"},
        };

        static const char* const color_outputs[] = {"o_color"};
        static const char* const picking_outputs[] = {"o_object_reference"};
        static const char* const selection_outputs[] = {"o_highlight"};

        static const my::IndexName attribs[] = {
            {InputStreamInMeshPos, "i_in_mesh_pos"},
            {InputStreamPosition, "i_position"},
            {InputStreamColor, "i_color"},
            {InputStreamRadius, "i_radius"},
            {InputStreamOutlineColor, "i_outline_color"},
            {InputStreamOutlineWidth, "i_outline_width"},
            {InputStreamFeatureIndex, "i_feature_index"},
        };

        static const my::IndexName visual_samplers[] = {
            {hrz::SamplerCameraHeight, "u_camera_height"},
            {SamplerFeatureIds, "u_feature_ids"},
        };

        static const my::IndexName picking_samplers[] = {
            {hrz::SamplerCameraHeight, "u_camera_height"},
        };

        static const my::IndexName selection_samplers[] = {
            {hrz::SamplerCameraHeight, "u_camera_height"},
            {SamplerSelection, "u_selection"},
        };

        my::ShaderResource res{};
        res.name = hrz_shaders::FlatPoints_name;
        res.vertex_source_len = hrz_shaders::FlatPoints_vert_len;
        res.vertex_source = hrz_shaders::FlatPoints_vert;
        res.fragment_source_len = hrz_shaders::FlatPoints_frag_len;
        res.fragment_source = hrz_shaders::FlatPoints_frag;
        res.attribs = attribs;
        res.uniform_blocks = ubos;
        res.samplers = visual_samplers;
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
        res.samplers = picking_samplers;
        res.outputs = picking_outputs;
        res.initial_state.color_blend.enable = false;

        rc->alloc(&res, hrz::monitoring::systems::FlatOverlays);

        res.name = hrz_shaders::FlatPoints_selection_name;
        res.vertex_source_len = hrz_shaders::FlatPoints_selection_vert_len;
        res.vertex_source = hrz_shaders::FlatPoints_selection_vert;
        res.fragment_source_len = hrz_shaders::FlatPoints_selection_frag_len;
        res.fragment_source = hrz_shaders::FlatPoints_selection_frag;
        res.samplers = selection_samplers;
        res.outputs = selection_outputs;

        rc->alloc(&res, hrz::monitoring::systems::FlatOverlays);
    }

    void init_render(hrz::Render* render) override
    {
        Base::init_render(render);

        _visual_shader = render->rc->retrieve_shader(hrz_shaders::FlatPoints_name);
        _picking_shader = render->rc->retrieve_shader(hrz_shaders::FlatPoints_picking_name);
        _selection_shader = render->rc->retrieve_shader(hrz_shaders::FlatPoints_selection_name);

        {
            const lm::vec2 positions[] = {{0.5, 0.5}, {0.5, -0.5}, {-0.5, 0.5}, {-0.5, -0.5}};

            my::BufferResource vertex_buffer(my::BufferResource::BufferType::Vertex);
            vertex_buffer.size = sizeof(positions);
            vertex_buffer.usage = my::UsageHint::Static;
            vertex_buffer.data = &positions[0].x;

            _vertex_buffer = render->rc->alloc(
                &vertex_buffer, hrz::monitoring::systems::FlatOverlays, hrz::monitoring::NoLayer,
                {{"contents"_ss, "point vertex positions"_ss}});
        }
    }

    void deinit_render(hrz::Render* render) override
    {
        render->rc->dealloc(_vertex_buffer);

        Base::deinit_render(render);
    }

    bool initialize_config(
        const hrz_proto::VectorRepr& repr,
        Config* config,
        const hrz::function_ref<
            uint64_t(std::string_view name, const hrz::vector_data::OwnedAttributeValue&)
        >& register_prp) override
    {
        if (repr.repr_type_case() != hrz_proto::VectorRepr::ReprTypeCase::kFlatOverlayPoint)
            return false;

        const std::string_view color_prp_name = repr.flat_overlay_point().color().name();
        const std::string_view radius_prp_name = repr.flat_overlay_point().radius().name();
        const std::string_view outline_color_prp_name =
            repr.flat_overlay_point().outline_color().name();
        const std::string_view outline_width_prp_name =
            repr.flat_overlay_point().outline_width().name();

        config->default_color_srgb =
            hrz::convert_proto_color_to_bytes(repr.flat_overlay_point().color().default_value());
        config->color_prp = register_prp(
            color_prp_name,
            hrz::vector_data::attr_from_color<hrz::vector_data::OwnedAttributeValue>(
                config->default_color_srgb));

        config->default_radius = repr.flat_overlay_point().radius().default_value();
        config->radius_prp = register_prp(radius_prp_name, config->default_radius);
        config->radius_unit = repr.flat_overlay_point().radius_unit();

        config->default_outline_color_srgb = hrz::convert_proto_color_to_bytes(
            repr.flat_overlay_point().outline_color().default_value());
        config->outline_color_prp = register_prp(
            outline_color_prp_name,
            hrz::vector_data::attr_from_color<hrz::vector_data::OwnedAttributeValue>(
                config->default_outline_color_srgb));

        config->default_outline_width = repr.flat_overlay_point().outline_width().default_value();
        config->outline_width_prp =
            register_prp(outline_width_prp_name, config->default_outline_width);
        config->outline_width_unit = repr.flat_overlay_point().outline_width_unit();

        config->z_index = repr.flat_overlay_point().z_index();
        config->clip_to_tile = repr.flat_overlay_point().clip_to_tile();

        return true;
    }

    void unregister_config(
        const Config* cfg,
        const hrz::function_ref<void(uint64_t prp_id)>& unregister_property) override
    {
        unregister_property(cfg->color_prp);
        unregister_property(cfg->radius_prp);
    }

    void initialize_tile_with_config(const Config& config, Tile* tile) override
    {
        tile->ubo_template.base.object_reference = tile->object_ref.to_uvec2();
        tile->ubo_template.base.feature_reference = tile->feature_ref.to_uvec3();
        tile->ubo_template.base.has_feature_ids = tile->has_feature_ids;

        tile->ubo_template.radius_unit = config.radius_unit;
        tile->ubo_template.outline_width_unit = config.outline_width_unit;

        auto& bake_data = tile->bake_data.value();

        bake_data.default_color_srgb = config.default_color_srgb;
        bake_data.color_prp = config.color_prp;

        bake_data.default_radius = config.default_radius;
        bake_data.radius_prp = config.radius_prp;

        bake_data.default_outline_color_srgb = config.default_outline_color_srgb;
        bake_data.outline_color_prp = config.outline_color_prp;

        bake_data.default_outline_width = config.default_outline_width;
        bake_data.outline_width_prp = config.outline_width_prp;

        bake_data.clip_to_tile = config.clip_to_tile;
    }

    void work_load_config(WorkCtx& ctx, Config* config) override
    {
        config->status = Config::Status::Ready;
    }

    BaseRenderable* get_renderable(Tile* tile) const override
    {
        return tile->renderable.has_value() ? &tile->renderable.value() : nullptr;
    }

    void initialize_tile_geometry_with_baked_data(
        TileGeometry* geometry,
        hrz_jobs::FlatPointGeometry& baked_data) override
    {
        geometry->baked_data = std::move(baked_data.point_data);
    }

    bool tile_geometry_has_necessary_baked_data(const Tile& tile) const override
    {
        return tile.geometry.has_value() && !tile.geometry->baked_data.empty();
    }

    bool initialize_renderable(hrz::Render* render, Tile* tile) override
    {
#define CHECK_RESOURCE_UPLOAD(resource, resource_type)                                    \
    if (resource.is_null())                                                               \
    {                                                                                     \
        HRZ_LOG_ERROR(                                                                    \
            "Could not upload flat vector {} of tile {}-{}-{} to the GPU", resource_type, \
            tile->coords.lod, tile->coords.x, tile->coords.y);                            \
        return false;                                                                     \
    }

        const auto& geometry = tile->geometry.value();
        auto tile_coords_str = fmt::to_string(tile->coords);

        using Instance = hrz_jobs::FlatPointGeometry::PointInstance;

        PointsRenderable points_renderable;
        auto& renderable_data = points_renderable.data;
        auto geometry_data = geometry.baked_data.get_data();
        auto ubo = tile->ubo_template;

        my::BufferResource pb_res(my::BufferResource::BufferType::Vertex);
        pb_res.size = geometry_data.size_bytes();
        pb_res.usage = my::UsageHint::Static;
        pb_res.data = geometry_data.data();
        pb_res.allow_allocation_failure = true;

        my::ResourceHandle instance_buffer = render->rc->alloc(
            &pb_res, hrz::monitoring::systems::FlatOverlays, tile->layer_id,
            {{"contents"_ss, "point instance data"_ss}, {"tile coords"_ss, tile_coords_str}});
        CHECK_RESOURCE_UPLOAD(instance_buffer, "point instance data");
        points_renderable.instance_buffer = instance_buffer;

        const my::VertexInputStream streams[] = {
            {InputStreamInMeshPos, _vertex_buffer, my::VertexFormat::Float32_2, 0, 0,
             my::VertexRate::PerVertex},
            {InputStreamPosition, instance_buffer, my::VertexFormat::Float32_3,
             offsetof(Instance, position), sizeof(Instance), my::VertexRate::PerInstance},
            {InputStreamColor, instance_buffer, my::VertexFormat::UInt8Norm_4,
             offsetof(Instance, color), sizeof(Instance), my::VertexRate::PerInstance},
            {InputStreamRadius, instance_buffer, my::VertexFormat::Float32,
             offsetof(Instance, disc_radius), sizeof(Instance), my::VertexRate::PerInstance},
            {InputStreamOutlineColor, instance_buffer, my::VertexFormat::UInt8Norm_4,
             offsetof(Instance, outline_color), sizeof(Instance), my::VertexRate::PerInstance},
            {InputStreamOutlineWidth, instance_buffer, my::VertexFormat::Float32,
             offsetof(Instance, outline_width), sizeof(Instance), my::VertexRate::PerInstance},
            {InputStreamFeatureIndex, instance_buffer, my::VertexFormat::UInt32,
             offsetof(Instance, feature_index), sizeof(Instance), my::VertexRate::PerInstance},
        };

        my::VertexInputResource vi_res;
        vi_res.attribs = streams;
        const my::ResourceHandle vertex_input = render->rc->alloc(
            &vi_res, hrz::monitoring::systems::FlatOverlays, tile->layer_id,
            {{"tile coords"_ss, tile_coords_str}});

        hrz::split_double(geometry.sea_center, ubo.base.center_low.xyz, ubo.base.center_high.xyz);

        my::BufferResource ub_res(my::BufferResource::BufferType::Uniform);
        ub_res.size = sizeof(PointsTileUniformData);
        ub_res.usage = my::UsageHint::Updatable;
        ub_res.data = &ubo;

        renderable_data.ubo_buffer = render->rc->alloc(
            &ub_res, hrz::monitoring::systems::FlatOverlays, tile->layer_id,
            {{"contents"_ss, "flat points geometry uniforms"_ss},
             {"tile coords"_ss, tile_coords_str}});

        renderable_data.vertex_input = vertex_input;
        renderable_data.vertex_count = 4;
        renderable_data.instance_count = geometry_data.size();

        renderable_data.shader = _visual_shader;
        renderable_data.picking_shader = _picking_shader;
        renderable_data.selection_shader = _selection_shader;
        renderable_data.metadata_sampler = _metadata_sampler;
        renderable_data.feature_id_texture = tile->feature_id_texture;
        renderable_data.selection_texture = tile->selection_texture;

        tile->renderable.emplace(std::move(points_renderable));
        return true;
    }

    void start_baking_job(WorkCtx& ctx, Tile* tile) override
    {
        tile->bake_ticket = hrz_jobs::add_job_bake_flat_point_geometry(
            ctx.js, std::move(tile->bake_data).value(),
            {hrz::monitoring::systems::FlatOverlays, tile->layer_id});
    }

    bool is_baking_job_finished(WorkCtx& ctx, Tile* tile) override
    {
        return hrz_jobs::is_job_valid(ctx.js, tile->bake_ticket)
            && hrz_jobs::is_job_finished(ctx.js, tile->bake_ticket);
    }

    bool has_baking_job_succeeded(WorkCtx& ctx, Tile* tile) override
    {
        return hrz_jobs::get_job_status(ctx.js, tile->bake_ticket)
            == hrz::job_scheduler::JobStatus::Finished_Success;
    }

    void get_baking_job_response(WorkCtx& ctx, Tile* tile, BakedData& out_baked_data) override
    {
        out_baked_data = hrz_jobs::get_job_response(ctx.js, tile->bake_ticket);
    }

    void cancel_baking_job(WorkCtx& ctx, Tile* tile) override
    {
        hrz_jobs::cancel_job(ctx.js, tile->bake_ticket);
    }
};

} // anonymous namespace

namespace hrz::vt
{

std::unique_ptr<ReprSystem> create_flat_overlay_point_repr_system()
{
    return std::make_unique<FlatOverlayPointReprSystem>();
}

void collect_flat_overlay_point_shaders(hrz::GpuResourceContext* rc)
{
    FlatOverlayPointReprSystem::collect_shaders(rc);
}

} // namespace hrz::vt
