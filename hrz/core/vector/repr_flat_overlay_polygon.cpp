#include "hrz/common/blob_array.h"
#include "hrz/common/color.h"
#include "hrz/common/fmt.h" // IWYU pragma: keep
#include "hrz/common/monitoring_defs.h"
#include "hrz/common/proto_maths.h"
#include "hrz/common/vector_tiles/data_texture.h"
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
#include "hrz/fnd/static_vector.h"
#include "hrz/protocol/vector/flat_overlay_polygon_repr.pb.h"

#include <optional>

using namespace hrz::vt::flat_overlay;

namespace
{
enum
{
    UboTileParams = hrz::vector_flat_overlay::UboVectorOverlayPass + 1,

    InputStreamInTilePos = 0,
    InputStreamColor = 1,
    InputStreamUv = 2,
    InputStreamInTileLat = 3,
    InputStreamPatternStyleIndex = 4,
    InputStreamFeatureIndex = 5,

    SamplerSelection = hrz::SamplerCustomStart,
    SamplerFeatureIds,
    SamplerPattern,
    SamplerPatternStyle,
};

struct PolygonsTileUniformData
{
    HRZ_UBO_STRUCT_FIELD(CommonTileUniformData) base;
    lm::vec2 origin_uv_low;
    lm::vec2 origin_uv_high;
    float origin_lat;
    float lat_span;
    uint32_t polygon_pattern_size_unit;
    uint32_t polygon_pattern_tiling_type;
    uint32_t polygon_pattern_reference_latitude_type;
    float polygon_pattern_reference_lat_scale_factor_offset;
    uint32_t polygon_pattern_color_blend_mode;
    uint32_t _padding[1];
};

HRZ_CHECK_UBO_SIZE(PolygonsTileUniformData);

struct PolygonsRenderable final : public BaseRenderable
{
    my::ResourceHandle vertex_buffer = my::ResourceHandle::null();
    my::ResourceHandle index_buffer = my::ResourceHandle::null();

    struct FeatureRenderData : public BaseRenderData
    {
        my::ResourceHandle ubo_buffer = my::ResourceHandle::null();
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

        if (data->vertex_count == 0) return;

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
                const my::TextureBinding bindings[] = {
                    {SamplerFeatureIds, data->feature_id_texture, data->metadata_sampler}};
                rb->bind(bindings);
                break;
            }
            case hrz::RenderPicking: shader = data->picking_shader; break;
            case hrz::RenderSelection:
            {
                if (!data->has_selected_features) return;
                shader = data->selection_shader;
                const my::TextureBinding bindings[] = {
                    {SamplerSelection, data->selection_texture, data->metadata_sampler}};
                rb->bind(bindings);
                break;
            }
            default: return;
        }

        auto batch = my::DrawBatchInfo(my::PrimitiveType::TriangleList, data->vertex_count);
        const my::UboBinding ubo_bindings[] = {
            {UboTileParams, data->ubo_buffer, 0, sizeof(PolygonsTileUniformData)}};
        rb->bind(ubo_bindings);

        if (!data->pattern_texture.is_null())
        {
            const my::TextureBinding bindings[] = {
                {SamplerPattern, data->pattern_texture, data->pattern_sampler},
                {SamplerPatternStyle, data->pattern_style_texture, data->metadata_sampler},
            };
            rb->bind(bindings);
        }

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
        to_free.push_back(vertex_buffer);
        to_free.push_back(index_buffer);
        to_free.push_back(data.vertex_input);
        to_free.push_back(data.pattern_style_texture);
        to_free.push_back(data.ubo_buffer);
    }

    BaseRenderData& get_base_render_data() override { return data; }
};

struct Sprite
{
    std::string name;
    lm::uvec2 size;
    lm::uvec2 offset;
};

struct PolygonConfig : public BaseConfig
{
    uint64_t color_prp = hrz::style::Parser::INVALID_PROPERTY;

    lm::ubvec4 default_color_srgb = {0, 0, 0, 0};

    std::string pattern_image_url;
    hrz_proto::HttpHeaderList pattern_image_headers;
    std::vector<Sprite> pattern_sprites;
    hrz::vt::image_loader::ImageH pattern_image = 0;
    my::ResourceHandle pattern_texture = my::ResourceHandle::null();
    lm::uvec2 pattern_texture_size = {0, 0};

    uint32_t default_pattern_sprite_index = 0;
    uint64_t pattern_sprite_index_prp = hrz::style::Parser::INVALID_PROPERTY;
    std::string default_pattern_sprite_name;
    uint64_t pattern_sprite_name_prp = hrz::style::Parser::INVALID_PROPERTY;
    lm::vec2 default_pattern_size = {0, 0};
    lm::ulvec2 pattern_size_prp = {
        hrz::style::Parser::INVALID_PROPERTY, hrz::style::Parser::INVALID_PROPERTY};
    hrz_proto::PolygonPatternSizeUnit pattern_size_unit;
    float default_pattern_rotation = 0.0F;
    uint64_t pattern_rotation_prp = hrz::style::Parser::INVALID_PROPERTY;
    hrz_proto::PolygonPatternTilingType pattern_tiling_type;
    hrz_proto::PolygonPatternReferenceLatitudeType pattern_reference_latitude_type;
    float pattern_reference_latitude = 0.0F;
    lm::ubvec4 default_pattern_color_srgb = {0, 0, 0, 0};
    uint64_t pattern_color_prp = hrz::style::Parser::INVALID_PROPERTY;
    hrz_proto::BlendMode pattern_color_blend_mode;
    float default_pattern_color_blend_strength = 0.0F;
    uint64_t pattern_color_blend_strength_prp = hrz::style::Parser::INVALID_PROPERTY;

    bool clip_to_tile = false;
    uint32_t z_index = 0;
};

struct PolygonTileGeometry : public BaseTileGeometry
{
    std::variant<
        hrz::BlobArray<hrz_jobs::FlatPolygonGeometry::SolidColorPolygonVertex>,
        hrz::BlobArray<hrz_jobs::FlatPolygonGeometry::PatternPolygonVertex>>
        baked_data;
    hrz::BlobArray<hrz_jobs::FlatPolygonGeometry::PolygonPatternStyle> pattern_style_data;
    hrz::BlobArray<uint32_t> indices;
    lm::dvec2 origin_uv;
    double origin_lat{};
    double lat_span{};
};

struct PolygonTile : public BaseTile<PolygonTileGeometry, hrz_jobs::FlatPolygonData>
{
    hrz_jobs::BakeFlatPolygonGeometryTicket bake_ticket;
    std::optional<PolygonsRenderable> renderable;
    PolygonsTileUniformData ubo_template;
    my::ResourceHandle pattern_texture;
    bool has_pattern;
};

struct PolygonFlatOverlayTraits
{
    using Config = PolygonConfig;
    using TileGeometry = PolygonTileGeometry;
    using Tile = PolygonTile;
    using BakingData = hrz_jobs::FlatPolygonData;
    using BakedData = hrz_jobs::FlatPolygonGeometry;
};

class FlatOverlayPolygonReprSystem final :
    public hrz::vt::flat_overlay::FlatOverlayReprSystem<PolygonFlatOverlayTraits>
{
    using Base = hrz::vt::flat_overlay::FlatOverlayReprSystem<PolygonFlatOverlayTraits>;
    using Config = PolygonConfig;
    using Tile = PolygonTile;
    using TileGeometry = PolygonTileGeometry;
    using BakedData = hrz_jobs::FlatPolygonGeometry;

    my::ResourceHandle _solid_color_visual_shader;
    my::ResourceHandle _solid_color_picking_shader;
    my::ResourceHandle _solid_color_selection_shader;
    my::ResourceHandle _pattern_visual_shader;
    my::ResourceHandle _pattern_picking_shader;
    my::ResourceHandle _pattern_selection_shader;
    my::ResourceHandle _pattern_sampler;

    std::vector<hrz::vt::image_loader::ImageH> _images_to_free;

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

        static const my::IndexName solid_color_attribs[] = {
            {InputStreamInTilePos, "i_in_tile_pos"},
            {InputStreamColor, "i_color"},
            {InputStreamFeatureIndex, "i_feature_index"}};

        static const my::IndexName pattern_attribs[] = {
            {InputStreamInTilePos, "i_in_tile_pos"},
            {InputStreamUv, "i_uv"},
            {InputStreamInTileLat, "i_in_tile_lat"},
            {InputStreamPatternStyleIndex, "i_pattern_style_index"},
            {InputStreamFeatureIndex, "i_feature_index"}};

        static const my::IndexName solid_color_visual_samplers[] = {
            {SamplerFeatureIds, "u_feature_ids"},
        };

        static const my::IndexName pattern_visual_samplers[] = {
            {hrz::SamplerCameraHeight, "u_camera_height"},
            {SamplerFeatureIds, "u_feature_ids"},
            {SamplerPattern, "u_pattern"},
            {SamplerPatternStyle, "u_pattern_styles"},
        };

        static const my::IndexName pattern_picking_samplers[] = {
            {hrz::SamplerCameraHeight, "u_camera_height"},
            {SamplerPattern, "u_pattern"},
            {SamplerPatternStyle, "u_pattern_styles"},
        };

        static const my::IndexName solid_color_selection_samplers[] = {
            {SamplerSelection, "u_selection"},
        };

        static const my::IndexName pattern_selection_samplers[] = {
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
        res.attribs = solid_color_attribs;
        res.uniform_blocks = ubos;
        res.samplers = solid_color_visual_samplers;
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
        res.attribs = pattern_attribs;
        res.samplers = pattern_visual_samplers;

        rc->alloc(&res, hrz::monitoring::systems::FlatOverlays);

        res.name = hrz_shaders::FlatPolygonsSolidColor_picking_name;
        res.vertex_source_len = hrz_shaders::FlatPolygonsSolidColor_picking_vert_len;
        res.vertex_source = hrz_shaders::FlatPolygonsSolidColor_picking_vert;
        res.fragment_source_len = hrz_shaders::FlatPolygonsSolidColor_picking_frag_len;
        res.fragment_source = hrz_shaders::FlatPolygonsSolidColor_picking_frag;
        res.attribs = solid_color_attribs;
        res.samplers = {};
        res.outputs = picking_outputs;
        res.initial_state.color_blend.enable = false;

        rc->alloc(&res, hrz::monitoring::systems::FlatOverlays);

        res.name = hrz_shaders::FlatPolygonsPattern_picking_name;
        res.vertex_source_len = hrz_shaders::FlatPolygonsPattern_picking_vert_len;
        res.vertex_source = hrz_shaders::FlatPolygonsPattern_picking_vert;
        res.fragment_source_len = hrz_shaders::FlatPolygonsPattern_picking_frag_len;
        res.fragment_source = hrz_shaders::FlatPolygonsPattern_picking_frag;
        res.attribs = pattern_attribs;
        res.samplers = pattern_picking_samplers;

        rc->alloc(&res, hrz::monitoring::systems::FlatOverlays);

        res.name = hrz_shaders::FlatPolygonsSolidColor_selection_name;
        res.vertex_source_len = hrz_shaders::FlatPolygonsSolidColor_selection_vert_len;
        res.vertex_source = hrz_shaders::FlatPolygonsSolidColor_selection_vert;
        res.fragment_source_len = hrz_shaders::FlatPolygonsSolidColor_selection_frag_len;
        res.fragment_source = hrz_shaders::FlatPolygonsSolidColor_selection_frag;
        res.attribs = solid_color_attribs;
        res.samplers = solid_color_selection_samplers;
        res.outputs = selection_outputs;

        rc->alloc(&res, hrz::monitoring::systems::FlatOverlays);

        res.name = hrz_shaders::FlatPolygonsPattern_selection_name;
        res.vertex_source_len = hrz_shaders::FlatPolygonsPattern_selection_vert_len;
        res.vertex_source = hrz_shaders::FlatPolygonsPattern_selection_vert;
        res.fragment_source_len = hrz_shaders::FlatPolygonsPattern_selection_frag_len;
        res.fragment_source = hrz_shaders::FlatPolygonsPattern_selection_frag;
        res.attribs = pattern_attribs;
        res.samplers = pattern_selection_samplers;

        rc->alloc(&res, hrz::monitoring::systems::FlatOverlays);
    }

    void init_render(hrz::Render* render) override
    {
        Base::init_render(render);

        _solid_color_visual_shader =
            render->rc->retrieve_shader(hrz_shaders::FlatPolygonsSolidColor_name);
        _solid_color_picking_shader =
            render->rc->retrieve_shader(hrz_shaders::FlatPolygonsSolidColor_picking_name);
        _solid_color_selection_shader =
            render->rc->retrieve_shader(hrz_shaders::FlatPolygonsSolidColor_selection_name);
        _pattern_visual_shader = render->rc->retrieve_shader(hrz_shaders::FlatPolygonsPattern_name);
        _pattern_picking_shader =
            render->rc->retrieve_shader(hrz_shaders::FlatPolygonsPattern_picking_name);
        _pattern_selection_shader =
            render->rc->retrieve_shader(hrz_shaders::FlatPolygonsPattern_selection_name);

        {
            my::SamplerResource res;
            res.use_mipmaps = false;
            res.sampler.min_filter = my::SamplerParams::Filter::Linear;
            res.sampler.mag_filter = my::SamplerParams::Filter::Linear;
            res.sampler.mipmap_filter = my::SamplerParams::Filter::Linear;

            _pattern_sampler = render->rc->alloc(&res, hrz::monitoring::systems::FlatOverlays);
        }
    }

    void deinit_render(hrz::Render* render) override
    {
        render->rc->dealloc(_pattern_sampler);

        Base::deinit_render(render);
    }

    bool initialize_config(
        const hrz_proto::VectorRepr& repr,
        Config* config,
        const hrz::function_ref<
            uint64_t(std::string_view name, const hrz::vector_data::OwnedAttributeValue&)>&
            register_prp) override
    {
        if (repr.type() != hrz_proto::FLAT_OVERLAY_POLYGON_VECTOR_REPR) return false;

        const std::string_view color_prp_name = repr.flat_overlay_polygon().color().name();

        config->default_color_srgb =
            hrz::convert_proto_color_to_bytes(repr.flat_overlay_polygon().color().default_value());

        config->color_prp = register_prp(
            color_prp_name,
            hrz::vector_data::attr_from_color<hrz::vector_data::OwnedAttributeValue>(
                config->default_color_srgb));

        config->z_index = repr.flat_overlay_polygon().z_index();
        config->clip_to_tile = repr.flat_overlay_polygon().clip_to_tile();

        config->pattern_image_url = repr.flat_overlay_polygon().pattern().image_url();
        config->pattern_image_headers = repr.flat_overlay_polygon().pattern().image_http_headers();
        config->default_pattern_sprite_index =
            (uint32_t)repr.flat_overlay_polygon().pattern().sprite_index().default_value();
        config->pattern_sprite_index_prp = register_prp(
            repr.flat_overlay_polygon().pattern().sprite_index().name(),
            (int64_t)config->default_pattern_sprite_index);
        config->default_pattern_sprite_name =
            repr.flat_overlay_polygon().pattern().sprite_name().default_value();
        config->pattern_sprite_name_prp = register_prp(
            repr.flat_overlay_polygon().pattern().sprite_name().name(),
            config->default_pattern_sprite_name);
        for (const auto& proto_sprite : repr.flat_overlay_polygon().pattern().sprites())
        {
            config->pattern_sprites.emplace_back(
                proto_sprite.name(), lm::uvec2(hrz::to_lm(proto_sprite.size())),
                lm::uvec2(hrz::to_lm(proto_sprite.offset())));
        }

        config->default_pattern_size =
            hrz::to_lm(repr.flat_overlay_polygon().pattern().size().default_value());
        {
            auto size_x_prp_name =
                fmt::format("{}_x", repr.flat_overlay_polygon().pattern().size().name());
            auto size_y_prp_name =
                fmt::format("{}_y", repr.flat_overlay_polygon().pattern().size().name());
            config->pattern_size_prp.x =
                register_prp(size_x_prp_name, config->default_pattern_size.x);
            config->pattern_size_prp.y =
                register_prp(size_y_prp_name, config->default_pattern_size.y);
        }
        config->pattern_size_unit = repr.flat_overlay_polygon().pattern().size_unit();
        config->default_pattern_rotation =
            repr.flat_overlay_polygon().pattern().rotation().default_value();
        config->pattern_rotation_prp = register_prp(
            repr.flat_overlay_polygon().pattern().rotation().name(),
            config->default_pattern_rotation);
        config->pattern_tiling_type = repr.flat_overlay_polygon().pattern().tiling_type();
        config->pattern_reference_latitude_type =
            repr.flat_overlay_polygon().pattern().reference_latitude_type();
        config->pattern_reference_latitude =
            lm::radians(repr.flat_overlay_polygon().pattern().reference_latitude());
        config->default_pattern_color_srgb = hrz::convert_proto_color_to_bytes(
            repr.flat_overlay_polygon().pattern().color().default_value());
        config->pattern_color_prp = register_prp(
            repr.flat_overlay_polygon().pattern().color().name(),
            hrz::vector_data::attr_from_color<hrz::vector_data::OwnedAttributeValue>(
                config->default_pattern_color_srgb));
        config->pattern_color_blend_mode = repr.flat_overlay_polygon().pattern().color_blend_mode();
        config->default_pattern_color_blend_strength =
            repr.flat_overlay_polygon().pattern().color_blend_strength().default_value();
        config->pattern_color_blend_strength_prp = register_prp(
            repr.flat_overlay_polygon().pattern().color_blend_strength().name(),
            config->default_pattern_color_blend_strength);

        return true;
    }

    void unregister_config(
        const Config* cfg,
        const hrz::function_ref<void(uint64_t prp_id)>& unregister_property) override
    {
        unregister_property(cfg->color_prp);
        unregister_property(cfg->pattern_sprite_index_prp);
        unregister_property(cfg->pattern_sprite_name_prp);
        unregister_property(cfg->pattern_size_prp.x);
        unregister_property(cfg->pattern_size_prp.y);
        unregister_property(cfg->pattern_rotation_prp);
        unregister_property(cfg->pattern_color_prp);
        unregister_property(cfg->pattern_color_blend_strength_prp);

        _images_to_free.push_back(cfg->pattern_image);
    }

    void initialize_tile_with_config(const Config& config, Tile* tile) override
    {
        tile->pattern_texture = config.pattern_texture;
        tile->has_pattern = !config.pattern_texture.is_null();

        tile->ubo_template.base.object_reference = tile->object_ref.to_uvec2();
        tile->ubo_template.base.feature_reference = tile->feature_ref.to_uvec3();
        tile->ubo_template.base.has_feature_ids = tile->has_feature_ids;

        tile->ubo_template.polygon_pattern_size_unit = config.pattern_size_unit;
        tile->ubo_template.polygon_pattern_tiling_type = config.pattern_tiling_type;
        tile->ubo_template.polygon_pattern_reference_latitude_type =
            config.pattern_reference_latitude_type;
        tile->ubo_template.polygon_pattern_color_blend_mode = config.pattern_color_blend_mode;

        if (config.pattern_reference_latitude_type
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
                1.0 / std::abs(std::cos(config.pattern_reference_latitude));
            tile->ubo_template.polygon_pattern_reference_lat_scale_factor_offset =
                hrz::round_to_power_of_two(reference_lat_size_factor) - reference_lat_size_factor;
        }
        else
        {
            tile->ubo_template.polygon_pattern_reference_lat_scale_factor_offset = 0.0f;
        }

        auto& bake_data = tile->bake_data.value();

        bake_data.default_color_srgb = config.default_color_srgb;

        bake_data.color_prp = config.color_prp;
        bake_data.clip_to_tile = config.clip_to_tile;

        bake_data.pattern_texture_size = config.pattern_texture_size;
        for (const auto& sprite : config.pattern_sprites)
        {
            auto index = bake_data.pattern_sprites.size();
            bake_data.pattern_sprites.push_back({sprite.size, sprite.offset});
            bake_data.pattern_sprite_name_to_index.insert({sprite.name, index});
        }

        bake_data.has_polygon_pattern = tile->has_pattern;
        bake_data.polygon_pattern_sprite_index_prp = config.pattern_sprite_index_prp;
        bake_data.polygon_pattern_sprite_name_prp = config.pattern_sprite_name_prp;
        bake_data.polygon_pattern_size_prp = config.pattern_size_prp;
        bake_data.polygon_pattern_rotation_prp = config.pattern_rotation_prp;
        bake_data.polygon_pattern_color_prp = config.pattern_color_prp;
        bake_data.polygon_pattern_color_blend_strength_prp =
            config.pattern_color_blend_strength_prp;

        bake_data.default_polygon_pattern_sprite_index = config.default_pattern_sprite_index;
        bake_data.default_polygon_pattern_sprite_name = config.default_pattern_sprite_name;
        bake_data.default_polygon_pattern_size = config.default_pattern_size;
        bake_data.default_polygon_pattern_rotation = config.default_pattern_rotation;
        bake_data.default_polygon_pattern_color_srgb = config.default_pattern_color_srgb;
        bake_data.default_polygon_pattern_color_blend_strength =
            config.default_pattern_color_blend_strength;

        bake_data.polygon_pattern_size_unit = config.pattern_size_unit;
    }

    void work_load_config(WorkCtx& ctx, Config* config) override
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

                    if (config->default_pattern_sprite_index >= config->pattern_sprites.size())
                    {
                        config->default_pattern_sprite_index = config->pattern_sprites.size() - 1;
                    }

                    config->status = Config::Status::Ready;
                }
                else if (image_status == image_loader::ImageStatus::Error)
                {
                    HRZ_LOG_ERROR("Could not load pattern image");
                    image_loader::release_image(ctx.il, config->pattern_image);
                    config->status = Config::Status::Error;
                }
            }
        }
        else
        {
            config->status = Config::Status::Ready;
        }
    }

    BaseRenderable* get_renderable(Tile* tile) const override
    {
        return tile->renderable.has_value() ? &tile->renderable.value() : nullptr;
    }

    void initialize_tile_geometry_with_baked_data(
        TileGeometry* geometry,
        hrz_jobs::FlatPolygonGeometry& baked_data) override
    {
        geometry->baked_data = std::move(baked_data.polygon_data);
        geometry->indices = std::move(baked_data.polygon_indices);
        geometry->origin_uv = baked_data.origin_uv;
        geometry->origin_lat = baked_data.origin_lat;
        geometry->lat_span = baked_data.lat_span;
        geometry->pattern_style_data = std::move(baked_data.polygon_pattern_style_data);
    }

    hrz::RenderRequest work_gpu(WorkGpuCtx& ctx) override
    {
        for (auto image : _images_to_free)
        {
            hrz::vt::image_loader::release_image(ctx.il, image);
        }
        _images_to_free.clear();

        return Base::work_gpu(ctx);
    }

    bool tile_geometry_has_necessary_baked_data(const Tile& tile) const override
    {
        if (!tile.geometry) return false;

        return (!tile.has_pattern
                && std::holds_alternative<
                    hrz::BlobArray<hrz_jobs::FlatPolygonGeometry::SolidColorPolygonVertex>>(
                    tile.geometry->baked_data)
                && !std::get<
                        hrz::BlobArray<hrz_jobs::FlatPolygonGeometry::SolidColorPolygonVertex>>(
                        tile.geometry->baked_data)
                        .empty())
            || (tile.has_pattern
                && std::holds_alternative<
                    hrz::BlobArray<hrz_jobs::FlatPolygonGeometry::PatternPolygonVertex>>(
                    tile.geometry->baked_data)
                && !std::get<hrz::BlobArray<hrz_jobs::FlatPolygonGeometry::PatternPolygonVertex>>(
                        tile.geometry->baked_data)
                        .empty());
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

        auto& geometry = tile->geometry.value();
        auto tile_coords_str = fmt::to_string(tile->coords);

        PolygonsRenderable polygons_renderable;
        auto& renderable_data = polygons_renderable.data;

        my::BufferResource vb_res(my::BufferResource::BufferType::Vertex);
        vb_res.usage = my::UsageHint::Static;
        vb_res.allow_allocation_failure = true;

        hrz::StaticVector<my::VertexInputStream, 5> streams;

        PolygonsTileUniformData ubo = tile->ubo_template;

        if (tile->has_pattern)
        {
            static constexpr size_t pixels_per_style =
                sizeof(hrz_jobs::FlatPolygonGeometry::PolygonPatternStyle) / sizeof(lm::uvec4);

            auto polygon_pattern_style_data = geometry.pattern_style_data.get_data();
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

            polygons_renderable.data.pattern_style_texture = render->rc->alloc(
                &tex_res, hrz::monitoring::systems::FlatOverlays, tile->layer_id,
                {{"contents"_ss, "polygon pattern styles"_ss},
                 {"tile coords"_ss, tile_coords_str}});
            CHECK_RESOURCE_UPLOAD(
                polygons_renderable.data.pattern_style_texture, "polygon pattern style texture");

            using Vertex = hrz_jobs::FlatPolygonGeometry::PatternPolygonVertex;

            auto geometry_data =
                std::get<hrz::BlobArray<hrz_jobs::FlatPolygonGeometry::PatternPolygonVertex>>(
                    geometry.baked_data)
                    .get_data();

            vb_res.size = geometry_data.size_bytes();
            vb_res.data = geometry_data.data();

            const my::ResourceHandle vertex_buffer = render->rc->alloc(
                &vb_res, hrz::monitoring::systems::FlatOverlays, tile->layer_id,
                {{"contents"_ss, "polygon vertex data"_ss}, {"tile coords"_ss, tile_coords_str}});
            CHECK_RESOURCE_UPLOAD(vertex_buffer, "polygon vertex data");
            polygons_renderable.vertex_buffer = vertex_buffer;

            streams.push_back(
                {InputStreamInTilePos, vertex_buffer, my::VertexFormat::Float32_3,
                 offsetof(Vertex, position), sizeof(Vertex), my::VertexRate::PerVertex});
            streams.push_back(
                {InputStreamUv, vertex_buffer, my::VertexFormat::Float32_2, offsetof(Vertex, uv),
                 sizeof(Vertex), my::VertexRate::PerVertex});
            streams.push_back(
                {InputStreamInTileLat, vertex_buffer, my::VertexFormat::Float32,
                 offsetof(Vertex, in_tile_lat), sizeof(Vertex), my::VertexRate::PerVertex});
            streams.push_back(
                {InputStreamPatternStyleIndex, vertex_buffer, my::VertexFormat::UInt32,
                 offsetof(Vertex, pattern_style_index), sizeof(Vertex), my::VertexRate::PerVertex});
            streams.push_back(
                {InputStreamFeatureIndex, vertex_buffer, my::VertexFormat::UInt32,
                 offsetof(Vertex, feature_index), sizeof(Vertex), my::VertexRate::PerVertex});

            hrz::split_double(geometry.origin_uv, ubo.origin_uv_low, ubo.origin_uv_high);

            ubo.origin_lat = geometry.origin_lat;
            ubo.lat_span = geometry.lat_span;
        }
        else
        {
            using Vertex = hrz_jobs::FlatPolygonGeometry::SolidColorPolygonVertex;

            auto geometry_data =
                std::get<hrz::BlobArray<hrz_jobs::FlatPolygonGeometry::SolidColorPolygonVertex>>(
                    geometry.baked_data)
                    .get_data();

            vb_res.size = geometry_data.size_bytes();
            vb_res.data = geometry_data.data();

            my::ResourceHandle vertex_buffer = render->rc->alloc(
                &vb_res, hrz::monitoring::systems::FlatOverlays, tile->layer_id,
                {{"contents"_ss, "polygon vertex data"_ss}, {"tile coords"_ss, tile_coords_str}});
            CHECK_RESOURCE_UPLOAD(vertex_buffer, "polygon vertex data");
            polygons_renderable.vertex_buffer = vertex_buffer;

            streams.push_back(
                {InputStreamInTilePos, vertex_buffer, my::VertexFormat::Float32_3,
                 offsetof(Vertex, position), sizeof(Vertex), my::VertexRate::PerVertex});
            streams.push_back(
                {InputStreamColor, vertex_buffer, my::VertexFormat::UInt8Norm_4,
                 offsetof(Vertex, color), sizeof(Vertex), my::VertexRate::PerVertex});
            streams.push_back(
                {InputStreamFeatureIndex, vertex_buffer, my::VertexFormat::UInt32,
                 offsetof(Vertex, feature_index), sizeof(Vertex), my::VertexRate::PerVertex});
        }

        auto index_data = geometry.indices.get_cdata();

        my::BufferResource ib_res(my::BufferResource::BufferType::Index);
        ib_res.usage = my::UsageHint::Static;
        ib_res.size = index_data.size_bytes();
        ib_res.data = index_data.data();

        const my::ResourceHandle index_buffer = render->rc->alloc(
            &ib_res, hrz::monitoring::systems::FlatOverlays, tile->layer_id,
            {{"contents"_ss, "polygon index data"_ss}, {"tile coords"_ss, tile_coords_str}});
        CHECK_RESOURCE_UPLOAD(index_buffer, "polygon index data");
        polygons_renderable.index_buffer = index_buffer;

        my::VertexInputResource vi_res;
        vi_res.indices = index_buffer;
        vi_res.attribs = streams;
        const my::ResourceHandle vertex_input = render->rc->alloc(
            &vi_res, hrz::monitoring::systems::FlatOverlays, tile->layer_id,
            {{"tile coords"_ss, tile_coords_str}});

        hrz::split_double(geometry.sea_center, ubo.base.center_low.xyz, ubo.base.center_high.xyz);

        my::BufferResource ub_res(my::BufferResource::BufferType::Uniform);
        ub_res.size = sizeof(PolygonsTileUniformData);
        ub_res.usage = my::UsageHint::Updatable;
        ub_res.data = &ubo;

        renderable_data.ubo_buffer = render->rc->alloc(
            &ub_res, hrz::monitoring::systems::FlatOverlays, tile->layer_id,
            {{"contents"_ss, "flat polygons geometry uniforms"_ss},
             {"tile coords"_ss, tile_coords_str}});

        renderable_data.vertex_input = vertex_input;
        renderable_data.vertex_count = index_data.size();

        if (tile->has_pattern)
        {
            renderable_data.shader = _pattern_visual_shader;
            renderable_data.picking_shader = _pattern_picking_shader;
            renderable_data.selection_shader = _pattern_selection_shader;

            renderable_data.pattern_texture = tile->pattern_texture;
            renderable_data.pattern_sampler = _pattern_sampler;
        }
        else
        {
            renderable_data.shader = _solid_color_visual_shader;
            renderable_data.picking_shader = _solid_color_picking_shader;
            renderable_data.selection_shader = _solid_color_selection_shader;
        }

        renderable_data.metadata_sampler = _metadata_sampler;
        renderable_data.feature_id_texture = tile->feature_id_texture;
        renderable_data.selection_texture = tile->selection_texture;

        tile->renderable.emplace(std::move(polygons_renderable));
        return true;
    }

    void start_baking_job(WorkCtx& ctx, Tile* tile) override
    {
        tile->bake_ticket = hrz_jobs::add_job_bake_flat_polygon_geometry(
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

std::unique_ptr<ReprSystem> create_flat_overlay_polygon_repr_system()
{
    return std::make_unique<FlatOverlayPolygonReprSystem>();
}

void collect_flat_overlay_polygon_shaders(hrz::GpuResourceContext* rc)
{
    FlatOverlayPolygonReprSystem::collect_shaders(rc);
}
} // namespace hrz::vt
