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
#include "hrz/fnd/mem.h"
#include "hrz/protocol/vector/flat_overlay_polyline_repr.pb.h"

#include <optional>

using namespace hrz::vt::flat_overlay;

namespace
{

enum
{
    UboTileParams = hrz::vector_flat_overlay::UboVectorOverlayPass + 1,

    InputStreamInMeshPos = 0,
    InputStreamColor = 1,
    InputStreamWidth = 2,
    // In the geometry vector:
    // x, y = progress of first and second point
    // z = dash period
    // w = dash length
    InputStreamGeometry = 3,
    InputStreamPos0 = 4,
    InputStreamPos1 = 5,
    InputStreamNormal0 = 6,
    InputStreamNormal1 = 7,
    InputStreamAnimationSpeed = 8,
    InputStreamSecondaryColor = 9,
    InputStreamTotalLength = 10,
    InputStreamFeatureIndex = 11,

    SamplerSelection = hrz::SamplerCustomStart,
    SamplerFeatureIds,
};

struct PolylinesTileUniformData
{
    HRZ_UBO_STRUCT_FIELD(CommonTileUniformData) base;
    uint32_t polyline_sides;
    uint32_t line_width_unit;
    uint32_t dash_mode;
    uint32_t dash_period_unit;
    uint32_t dash_primary_length_unit;
    uint32_t animation_speed_unit;
    uint32_t _padding[2];
};

HRZ_CHECK_UBO_SIZE(PolylinesTileUniformData);

struct PolylinesRenderable final : public BaseRenderable
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
        bool is_animated = false;
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

        auto batch = my::DrawBatchInfo(my::PrimitiveType::TriangleList, data->vertex_count)
                         .instanced(data->instance_count);

        const my::UboBinding ubo_bindings[] = {
            {UboTileParams, data->ubo_buffer, 0, sizeof(PolylinesTileUniformData)}
        };
        rb->bind(ubo_bindings);

        auto state = rb->get_current_state();
        r->draw(batch, shader, data->vertex_input, state.ubos, state.textures);

        data->draw_report->features_drawn += 1;
        data->draw_report->animated_features_drawn += data->is_animated ? 1 : 0;
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

struct PolylineConfig : public BaseConfig
{
    uint64_t width_prp = hrz::style::Parser::INVALID_PROPERTY;
    uint64_t color_prp = hrz::style::Parser::INVALID_PROPERTY;
    uint64_t dash_primary_length_prp = hrz::style::Parser::INVALID_PROPERTY;
    uint64_t dash_period_prp = hrz::style::Parser::INVALID_PROPERTY;
    uint64_t animation_speed_prp = hrz::style::Parser::INVALID_PROPERTY;
    uint64_t secondary_color_prp = hrz::style::Parser::INVALID_PROPERTY;

    lm::ubvec4 default_color_srgb = {0, 0, 0, 0};

    double default_width = 0;
    bool round_tips = false;
    hrz_proto::DashMode dash_mode;
    hrz_proto::DashSizeUnit dash_period_unit;
    hrz_proto::DashSizeUnit dash_primary_length_unit;
    hrz_proto::DashSizeUnit animation_speed_unit;
    float default_dash_period = 0.0F;
    float default_primary_dash_length = 0.0F;
    float default_animation_speed = 0.0F;
    lm::ubvec4 default_secondary_color_srgb = {0, 0, 0, 0};
    hrz_proto::PolylineSide side;
    hrz_proto::InWorldSizeUnit width_unit;

    bool clip_to_tile = false;
    uint32_t z_index = 0;
};

struct PolylineTileGeometry : public BaseTileGeometry
{
    hrz::BlobArray<hrz_jobs::FlatPolylineGeometry::PolylineInstance> baked_data;
    bool is_animated{};
};

struct PolylineTile : public BaseTile<PolylineTileGeometry, hrz_jobs::FlatPolylineData>
{
    hrz_jobs::BakeFlatPolylineGeometryTicket bake_ticket;
    std::optional<PolylinesRenderable> renderable;
    PolylinesTileUniformData ubo_template;
    bool round_tips;
};

struct PolylineFlatOverlayTraits
{
    using Config = PolylineConfig;
    using TileGeometry = PolylineTileGeometry;
    using Tile = PolylineTile;
    using BakingData = hrz_jobs::FlatPolylineData;
    using BakedData = hrz_jobs::FlatPolylineGeometry;
};

class FlatOverlayPolylineReprSystem final :
    public hrz::vt::flat_overlay::FlatOverlayReprSystem<PolylineFlatOverlayTraits>
{
    using Base = hrz::vt::flat_overlay::FlatOverlayReprSystem<PolylineFlatOverlayTraits>;
    using Config = PolylineConfig;
    using Tile = PolylineTile;
    using TileGeometry = PolylineTileGeometry;
    using BakedData = hrz_jobs::FlatPolylineGeometry;

    my::ResourceHandle _square_visual_shader;
    my::ResourceHandle _square_picking_shader;
    my::ResourceHandle _square_selection_shader;
    my::ResourceHandle _round_visual_shader;
    my::ResourceHandle _round_picking_shader;
    my::ResourceHandle _round_selection_shader;

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
            {InputStreamColor, "i_color"},
            {InputStreamWidth, "i_width"},
            {InputStreamGeometry, "i_geometry"},
            {InputStreamPos0, "i_pos0"},
            {InputStreamPos1, "i_pos1"},
            {InputStreamNormal0, "i_normal0"},
            {InputStreamNormal1, "i_normal1"},
            {InputStreamAnimationSpeed, "i_animation_speed"},
            {InputStreamSecondaryColor, "i_secondary_color"},
            {InputStreamTotalLength, "i_total_length"},
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
        res.name = hrz_shaders::FlatPolylinesSquare_name;
        res.vertex_source_len = hrz_shaders::FlatPolylinesSquare_vert_len;
        res.vertex_source = hrz_shaders::FlatPolylinesSquare_vert;
        res.fragment_source_len = hrz_shaders::FlatPolylinesSquare_frag_len;
        res.fragment_source = hrz_shaders::FlatPolylinesSquare_frag;
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
        res.samplers = picking_samplers;
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
        res.samplers = selection_samplers;
        res.outputs = selection_outputs;

        rc->alloc(&res, hrz::monitoring::systems::FlatOverlays);

        res.name = hrz_shaders::FlatPolylinesRound_selection_name;
        res.vertex_source_len = hrz_shaders::FlatPolylinesRound_selection_vert_len;
        res.vertex_source = hrz_shaders::FlatPolylinesRound_selection_vert;
        res.fragment_source_len = hrz_shaders::FlatPolylinesRound_selection_frag_len;
        res.fragment_source = hrz_shaders::FlatPolylinesRound_selection_frag;

        rc->alloc(&res, hrz::monitoring::systems::FlatOverlays);
    }

    void init_render(hrz::Render* render) override
    {
        Base::init_render(render);

        _square_visual_shader = render->rc->retrieve_shader(hrz_shaders::FlatPolylinesSquare_name);
        _square_picking_shader =
            render->rc->retrieve_shader(hrz_shaders::FlatPolylinesSquare_picking_name);
        _square_selection_shader =
            render->rc->retrieve_shader(hrz_shaders::FlatPolylinesSquare_selection_name);
        _round_visual_shader = render->rc->retrieve_shader(hrz_shaders::FlatPolylinesRound_name);
        _round_picking_shader =
            render->rc->retrieve_shader(hrz_shaders::FlatPolylinesRound_picking_name);
        _round_selection_shader =
            render->rc->retrieve_shader(hrz_shaders::FlatPolylinesRound_selection_name);

        {
            // Generate a 2d rectangle extruded on z axis
            static const lm::vec3 positions[] = {{0, -0.5, 0}, {0, -0.5, 1}, {0, 0.5, 1},
                                                 {0, -0.5, 0}, {0, 0.5, 1},  {0, 0.5, 0}};

            my::BufferResource vertex_buffer(my::BufferResource::BufferType::Vertex);
            vertex_buffer.size = HRZ_ARRAY_COUNT(positions) * sizeof(lm::vec3);
            vertex_buffer.usage = my::UsageHint::Static;
            vertex_buffer.data = &positions[0].x;

            _vertex_buffer = render->rc->alloc(
                &vertex_buffer, hrz::monitoring::systems::FlatOverlays, hrz::monitoring::NoLayer,
                {{"contents"_ss, "polyline vertex positions"_ss}});
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
        if (repr.type() != hrz_proto::FLAT_OVERLAY_POLYLINE_VECTOR_REPR) return false;

        const std::string_view line_width_prp_name = repr.flat_overlay_polyline().width().name();
        const std::string_view color_prp_name = repr.flat_overlay_polyline().color().name();
        const std::string_view dash_primary_length_prp_name =
            repr.flat_overlay_polyline().dashes().primary_segment_length().name();
        const std::string_view dash_period_prp_name =
            repr.flat_overlay_polyline().dashes().period().name();
        const std::string_view animation_speed_prp_name =
            repr.flat_overlay_polyline().dashes().animation_speed().name();
        const std::string_view second_color_prp_name =
            repr.flat_overlay_polyline().dashes().secondary_color().name();

        config->default_width = (double)repr.flat_overlay_polyline().width().default_value();
        config->default_color_srgb =
            hrz::convert_proto_color_to_bytes(repr.flat_overlay_polyline().color().default_value());
        config->default_primary_dash_length =
            repr.flat_overlay_polyline().dashes().primary_segment_length().default_value();
        config->default_dash_period =
            repr.flat_overlay_polyline().dashes().period().default_value();
        config->default_animation_speed =
            repr.flat_overlay_polyline().dashes().animation_speed().default_value();
        config->default_secondary_color_srgb = hrz::convert_proto_color_to_bytes(
            repr.flat_overlay_polyline().dashes().secondary_color().default_value());

        config->width_prp = register_prp(line_width_prp_name, config->default_width);
        config->color_prp = register_prp(
            color_prp_name,
            hrz::vector_data::attr_from_color<hrz::vector_data::OwnedAttributeValue>(
                config->default_color_srgb));
        config->dash_primary_length_prp =
            register_prp(dash_primary_length_prp_name, config->default_primary_dash_length);
        config->dash_period_prp = register_prp(dash_period_prp_name, config->default_dash_period);
        config->animation_speed_prp =
            register_prp(animation_speed_prp_name, config->default_animation_speed);
        config->secondary_color_prp = register_prp(
            second_color_prp_name,
            hrz::vector_data::attr_from_color<hrz::vector_data::OwnedAttributeValue>(
                config->default_secondary_color_srgb));

        config->z_index = repr.flat_overlay_polyline().z_index();
        config->clip_to_tile = repr.flat_overlay_polyline().clip_to_tile();

        config->dash_mode = repr.flat_overlay_polyline().dashes().mode();
        config->dash_period_unit = repr.flat_overlay_polyline().dashes().period_unit();
        config->dash_primary_length_unit =
            repr.flat_overlay_polyline().dashes().primary_segment_length_unit();
        config->animation_speed_unit = repr.flat_overlay_polyline().dashes().animation_speed_unit();
        config->round_tips = repr.flat_overlay_polyline().round_tips();
        config->side = repr.flat_overlay_polyline().side();
        config->width_unit = repr.flat_overlay_polyline().width_unit();

        return true;
    }

    void unregister_config(
        const Config* cfg,
        const hrz::function_ref<void(uint64_t prp_id)>& unregister_property) override
    {
        unregister_property(cfg->width_prp);
        unregister_property(cfg->color_prp);
        unregister_property(cfg->dash_primary_length_prp);
        unregister_property(cfg->dash_period_prp);
        unregister_property(cfg->animation_speed_prp);
        unregister_property(cfg->secondary_color_prp);
    }

    void initialize_tile_with_config(const Config& config, Tile* tile) override
    {
        tile->round_tips = config.round_tips;

        tile->ubo_template.base.object_reference = tile->object_ref.to_uvec2();
        tile->ubo_template.base.feature_reference = tile->feature_ref.to_uvec3();
        tile->ubo_template.base.has_feature_ids = tile->has_feature_ids;

        tile->ubo_template.line_width_unit = config.width_unit;
        tile->ubo_template.dash_mode = config.dash_mode;
        tile->ubo_template.dash_period_unit = config.dash_period_unit;
        tile->ubo_template.dash_primary_length_unit = config.dash_primary_length_unit;
        tile->ubo_template.animation_speed_unit = config.animation_speed_unit;

        switch (config.side)
        {
            case hrz_proto::PolylineSide::SIDE_BOTH:
                tile->ubo_template.polyline_sides =
                    HRZ_S_POLYLINE_SIDE_IN | HRZ_S_POLYLINE_SIDE_OUT;
                break;
            case hrz_proto::PolylineSide::SIDE_INSIDE:
                tile->ubo_template.polyline_sides = HRZ_S_POLYLINE_SIDE_IN;
                break;
            case hrz_proto::PolylineSide::SIDE_OUTSIDE:
                tile->ubo_template.polyline_sides = HRZ_S_POLYLINE_SIDE_OUT;
                break;
            default: tile->ubo_template.polyline_sides = 0; break;
        }

        auto& bake_data = tile->bake_data.value();

        bake_data.default_color_srgb = config.default_color_srgb;
        bake_data.default_line_width = config.default_width;

        bake_data.line_width_prp = config.width_prp;
        bake_data.color_prp = config.color_prp;
        bake_data.clip_to_tile = config.clip_to_tile;
        bake_data.dashes.default_period = config.default_dash_period;
        bake_data.dashes.default_primary_length = config.default_primary_dash_length;
        bake_data.dashes.default_animation_speed = config.default_animation_speed;
        bake_data.dashes.default_secondary_color_srgb = config.default_secondary_color_srgb;
        bake_data.dashes.period_prp = config.dash_period_prp;
        bake_data.dashes.primary_length_prp = config.dash_primary_length_prp;
        bake_data.dashes.animation_speed_prp = config.animation_speed_prp;
        bake_data.dashes.secondary_color_prp = config.secondary_color_prp;
        bake_data.dashes.mode = config.dash_mode;
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
        hrz_jobs::FlatPolylineGeometry& baked_data) override
    {
        geometry->baked_data = std::move(baked_data.polyline_data);
        geometry->is_animated = baked_data.is_animated;
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

        using Instance = hrz_jobs::FlatPolylineGeometry::PolylineInstance;

        PolylinesRenderable polylines_renderable;
        auto& renderable_data = polylines_renderable.data;
        auto geometry_data = geometry.baked_data.get_data();
        auto ubo = tile->ubo_template;

        my::BufferResource vb_res(my::BufferResource::BufferType::Vertex);
        vb_res.size = geometry_data.size_bytes();
        vb_res.usage = my::UsageHint::Static;
        vb_res.data = geometry_data.data();
        vb_res.allow_allocation_failure = true;

        my::ResourceHandle instance_buffer = render->rc->alloc(
            &vb_res, hrz::monitoring::systems::FlatOverlays, tile->layer_id,
            {{"contents"_ss, "polyline instance data"_ss}, {"tile coords"_ss, tile_coords_str}});
        CHECK_RESOURCE_UPLOAD(instance_buffer, "polyline instance data");
        polylines_renderable.instance_buffer = instance_buffer;

        const my::VertexInputStream streams[] = {
            {InputStreamInMeshPos, _vertex_buffer, my::VertexFormat::Float32_3, 0, 0,
             my::VertexRate::PerVertex},
            {InputStreamColor, instance_buffer, my::VertexFormat::UInt8Norm_4,
             offsetof(Instance, color), sizeof(Instance), my::VertexRate::PerInstance},
            {InputStreamWidth, instance_buffer, my::VertexFormat::Float32,
             offsetof(Instance, line_width), sizeof(Instance), my::VertexRate::PerInstance},
            {InputStreamGeometry, instance_buffer, my::VertexFormat::Float32_4,
             offsetof(Instance, progress_at_start), sizeof(Instance), my::VertexRate::PerInstance},
            {InputStreamFeatureIndex, instance_buffer, my::VertexFormat::UInt32,
             offsetof(Instance, feature_index), sizeof(Instance), my::VertexRate::PerInstance},
            {InputStreamPos0, instance_buffer, my::VertexFormat::Float32_3,
             offsetof(Instance, position0), sizeof(Instance), my::VertexRate::PerInstance},
            {InputStreamPos1, instance_buffer, my::VertexFormat::Float32_3,
             offsetof(Instance, position1), sizeof(Instance), my::VertexRate::PerInstance},
            {InputStreamNormal0, instance_buffer, my::VertexFormat::UInt32,
             offsetof(Instance, normal0), sizeof(Instance), my::VertexRate::PerInstance},
            {InputStreamNormal1, instance_buffer, my::VertexFormat::UInt32,
             offsetof(Instance, normal1), sizeof(Instance), my::VertexRate::PerInstance},
            {InputStreamAnimationSpeed, instance_buffer, my::VertexFormat::Float32,
             offsetof(Instance, animation_speed), sizeof(Instance), my::VertexRate::PerInstance},
            {InputStreamTotalLength, instance_buffer, my::VertexFormat::Float32,
             offsetof(Instance, line_total_length), sizeof(Instance), my::VertexRate::PerInstance},
            {InputStreamSecondaryColor, instance_buffer, my::VertexFormat::UInt8Norm_4,
             offsetof(Instance, secondary_color), sizeof(Instance), my::VertexRate::PerInstance},
        };

        my::VertexInputResource vi_res;
        vi_res.attribs = streams;
        const my::ResourceHandle vertex_input = render->rc->alloc(
            &vi_res, hrz::monitoring::systems::FlatOverlays, tile->layer_id,
            {{"tile coords"_ss, tile_coords_str}});

        hrz::split_double(geometry.sea_center, ubo.base.center_low.xyz, ubo.base.center_high.xyz);

        my::BufferResource ub_res(my::BufferResource::BufferType::Uniform);
        ub_res.size = sizeof(PolylinesTileUniformData);
        ub_res.usage = my::UsageHint::Updatable;
        ub_res.data = &ubo;

        renderable_data.ubo_buffer = render->rc->alloc(
            &ub_res, hrz::monitoring::systems::FlatOverlays, tile->layer_id,
            {{"contents"_ss, "flat polylines geometry uniforms"_ss},
             {"tile coords"_ss, tile_coords_str}});

        renderable_data.vertex_input = vertex_input;

        renderable_data.vertex_count = 6;
        renderable_data.instance_count = geometry_data.size();

        renderable_data.shader = tile->round_tips ? _round_visual_shader : _square_visual_shader;
        renderable_data.picking_shader =
            tile->round_tips ? _round_picking_shader : _square_picking_shader;
        renderable_data.selection_shader =
            tile->round_tips ? _round_selection_shader : _square_selection_shader;
        renderable_data.metadata_sampler = _metadata_sampler;
        renderable_data.feature_id_texture = tile->feature_id_texture;
        renderable_data.selection_texture = tile->selection_texture;

        const bool is_really_animated =
            geometry.is_animated && ubo.dash_mode != (uint32_t)hrz_proto::DASH_DISABLED;

        polylines_renderable.data.is_animated = is_really_animated;

        tile->renderable.emplace(std::move(polylines_renderable));
        return true;
    }

    void start_baking_job(WorkCtx& ctx, Tile* tile) override
    {
        tile->bake_ticket = hrz_jobs::add_job_bake_flat_polyline_geometry(
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

std::unique_ptr<ReprSystem> create_flat_overlay_polyline_repr_system()
{
    return std::make_unique<FlatOverlayPolylineReprSystem>();
}

void collect_flat_overlay_polyline_shaders(hrz::GpuResourceContext* rc)
{
    FlatOverlayPolylineReprSystem::collect_shaders(rc);
}

} // namespace hrz::vt
