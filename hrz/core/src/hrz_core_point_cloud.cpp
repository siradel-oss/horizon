#include "hrz_core_point_cloud.h"

#include "hrz_core_data_texture.h"
#include "hrz_core_selection_storage.h"
#include "hrz_core_shaders.h"
#include "hrz_core_shadows.h"
#include "hrz_core_sky.h"
#include "hrz_core_viewsheds.h"

#include <hrz_common_geo.h>
#include <hrz_fnd_log.h>
#include <hrz_fnd_mem.h>
#include <hrz_fnd_static_vector.h>

namespace
{

using namespace hrz;

enum
{
    InputStreamPosition,
    InputStreamColor,
    InputStreamCompressedNormal,
    InputStreamBatchId,
};

enum
{
    UboPointCloud = hrz::UboCustomStart,
};

enum
{
    SamplerFeatureIds = hrz::SamplerCustomStart,
    SamplerSelection,
    SamplerFeatureColors,
};

const std::string PointCloud_visual_transparent_name =
    std::string(hrz_shaders::PointCloud_visual_name) + "_transparent";

class PointCloudImpl : public PointCloud
{
    struct CommonResources
    {
        my::ResourceHandle data_sampler = my::ResourceHandle::null();
        my::ResourceHandle visual_shader_opaque = my::ResourceHandle::null();
        my::ResourceHandle visual_shader_transparent = my::ResourceHandle::null();
        my::ResourceHandle picking_shader = my::ResourceHandle::null();
        my::ResourceHandle selection_shader = my::ResourceHandle::null();
    };

    static inline std::optional<CommonResources> _common_resources = std::nullopt;
    static inline const CommonResources* _queued_common_resources = nullptr;
    static inline uint64_t _last_frame_common_resources_queued = 0;

    static void _init_common_resources(hrz::Render* render)
    {
        // Very not OK for multithreading, but it's fine for now, this shouldn't be instantiated
        // anywhere but from the main thead.

        if (!_common_resources)
        {
            CommonResources res;
            res.visual_shader_opaque =
                render->rc->retrieve_shader(hrz_shaders::PointCloud_visual_name);
            res.visual_shader_transparent =
                render->rc->retrieve_shader(PointCloud_visual_transparent_name.c_str());
            res.picking_shader = render->rc->retrieve_shader(hrz_shaders::PointCloud_picking_name);
            res.selection_shader =
                render->rc->retrieve_shader(hrz_shaders::PointCloud_selection_name);

            my::SamplerResource sampler_res;
            sampler_res.sampler.min_filter = my::SamplerParams::Filter::Nearest;
            sampler_res.sampler.mag_filter = my::SamplerParams::Filter::Nearest;
            sampler_res.sampler.mipmap_filter = my::SamplerParams::Filter::Nearest;
            sampler_res.sampler.wrap_x = my::SamplerParams::Wrap::Repeat;
            sampler_res.sampler.wrap_y = my::SamplerParams::Wrap::Repeat;
            sampler_res.sampler.wrap_z = my::SamplerParams::Wrap::Repeat;
            sampler_res.use_mipmaps = false;
            res.data_sampler = render->rc->alloc(&sampler_res);

            _common_resources = res;
        }
    }

    struct Renderable : public my::Renderer::Renderable
    {
        struct RenderData
        {
            const CommonResources* common{};

            uint32_t point_count{};
            uint32_t scene_views{};

            bool has_any_selected = false;
            bool has_transparency = false;

            my::ResourceHandle vertex_input = my::ResourceHandle::null();
            my::ResourceHandle ubo = my::ResourceHandle::null();
            my::ResourceHandle feature_ids_texture = my::ResourceHandle::null();
            my::ResourceHandle feature_colors_texture = my::ResourceHandle::null();
            my::ResourceHandle selection_texture = my::ResourceHandle::null();
        };

        my::ResourceHandle vbo = my::ResourceHandle::null();

        RenderData render_data;

        void collect_render_info(my::Renderer::Queue& queue, const my::Renderer::Culler& culler)
            const override
        {
            auto bin_mask = render_data.has_transparency ? hrz::RenderWorldTransparentBin
                                                         : hrz::RenderWorldOpaqueBin;
            queue.enqueue(
                bin_mask, render_callback, &render_data, lm::dvec3(0), hrz::EARTH_RADIUS + 10'000);
        }

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
                case hrz::RenderVisual:
                    shader = data->has_transparency ? data->common->visual_shader_transparent
                                                    : data->common->visual_shader_opaque;
                    break;
                case hrz::RenderPicking: shader = data->common->picking_shader; break;
                case hrz::RenderSelection:
                {
                    if (!data->has_any_selected)
                    {
                        return;
                    }
                    shader = data->common->selection_shader;
                    break;
                }
                default: return;
            }

            auto batch = my::DrawBatchInfo(my::PrimitiveType::PointList, data->point_count);

            rb->push_state();

            my::UboBinding ubo_bindings[] = {{UboPointCloud, data->ubo, 0, sizeof(UniformData)}};
            rb->bind(HRZ_ARRAY_COUNT(ubo_bindings), ubo_bindings);

            my::TextureBinding texture_bindings[] = {
                {SamplerFeatureIds, data->feature_ids_texture, data->common->data_sampler},
                {SamplerFeatureColors, data->feature_colors_texture, data->common->data_sampler},
                {SamplerSelection, data->selection_texture, data->common->data_sampler},
            };
            rb->bind(HRZ_ARRAY_COUNT(texture_bindings), texture_bindings);

            auto state = rb->get_current_state();

            r->draw(
                batch, shader, data->vertex_input, state.ubo_count, state.ubos, state.texture_count,
                state.textures);

            rb->pop_state();
        }
    };

    bool _has_transparent_color = false;
    bool _has_transparent_feature = false;

    Renderable _renderable;

    hrz::DataTexture<
        HRZ_S_POINT_CLOUD_DATA_TEXTURE_WIDTH,
        my::TextureFormat::RG32UI,
        uint64_t,
        uint64_t,
        false>
        _feature_ids_texture;

    hrz::DataTexture<
        HRZ_S_POINT_CLOUD_DATA_TEXTURE_WIDTH,
        my::TextureFormat::RGBA8,
        lm::ubvec4,
        lm::ubvec4,
        false>
        _feature_colors_texture;

    hrz::selection::SelectionStorageUint32TextureMultiIndex _selection_texture;

    std::optional<UniformData> _uniform_data_to_update;

public:
    PointCloudImpl(
        hrz::Render* render,
        const hrz::monitoring::ResourceOwner& owner,
        const Geometry& geometry,
        const UniformData& uniform_data) :
        _has_transparent_color(geometry.has_transparent_color),
        _feature_ids_texture(owner, {{"content", "point cloud feature ids"}}),
        _feature_colors_texture(owner, {{"content", "point cloud feature colors"}}),
        _selection_texture(geometry.batch_count, owner, {{"content", "point cloud selection"}})
    {
        _init_common_resources(render);

        _renderable.render_data.point_count = (uint32_t)geometry.point_count;

        {
            auto positions = std::visit(
                hrz::overload{
                    [](const hrz::BlobArray<lm::vec3>& a) { return a.blob(); },
                    [](const hrz::BlobArray<lm::usvec3>& a) { return a.blob(); }},
                geometry.positions);

            auto colors = std::visit<std::variant<blobs::BlobHandle, lm::ubvec4>>(
                hrz::overload{
                    [](const hrz::BlobArray<lm::ubvec4>& a) { return a.blob(); },
                    [](const hrz::BlobArray<lm::ubvec3>& a) { return a.blob(); },
                    [](lm::ubvec4 c) { return c; }},
                geometry.colors);

            auto batch_ids = std::visit<std::variant<blobs::BlobHandle, uint32_t>>(
                hrz::overload{
                    [](const hrz::BlobArray<uint8_t>& a) { return a.blob(); },
                    [](const hrz::BlobArray<uint16_t>& a) { return a.blob(); },
                    [](const hrz::BlobArray<uint32_t>& a) { return a.blob(); },
                    [](uint32_t c) { return c; }},
                geometry.batch_ids);

            hrz::render::VertexInputBuilder builder;
            builder.add_input_stream(
                InputStreamPosition, positions, geometry.positions_format,
                my::VertexRate::PerVertex);
            builder.add_input_stream(
                InputStreamColor, colors, geometry.colors_format, geometry.colors_rate);
            builder.add_input_stream(
                InputStreamCompressedNormal, geometry.compressed_normals, my::VertexFormat::UInt16,
                geometry.compressed_normals_rate);
            builder.add_input_stream(
                InputStreamBatchId, batch_ids, geometry.batch_ids_format, geometry.batch_ids_rate);

            auto [vbo, vi] =
                builder.build(render, owner.system, owner.layer_id, {{"content", "point cloud"}});

            _renderable.render_data.vertex_input = vi;
            _renderable.vbo = vbo;
        }

        {
            my::BufferResource ubo_res(my::BufferResource::BufferType::Uniform);
            ubo_res.size = sizeof(hrz::PointCloud::UniformData);
            ubo_res.usage = my::UsageHint::Static;
            ubo_res.data = &uniform_data;

            _renderable.render_data.ubo =
                render->rc->alloc(&ubo_res, owner, {{"contents", "point cloud uniforms"}});
        }

        auto feature_ids_data = geometry.feature_ids.get_cdata();
        auto feature_ids = feature_ids_data.as_span();

        _feature_ids_texture.set(feature_ids);
        _feature_ids_texture.update(render);
        _renderable.render_data.feature_ids_texture = _feature_ids_texture.get_resource();

        if (feature_ids_data.size() == geometry.batch_count)
        {
            for (uint32_t i = 0; i < geometry.batch_count; ++i)
            {
                _selection_texture.register_indirection(feature_ids[i], i);
            }
        }

        _renderable.render_data.selection_texture = _selection_texture.get_texture(render);
    }

    void update_selection(const hrz::flat_hash_set<uint64_t>& selected_features) override
    {
        _selection_texture.update_selection(selected_features);
    }

    void update_feature_colors(std::span<const lm::ubvec4> colors, bool has_transparent_color)
        override
    {
        _has_transparent_feature = has_transparent_color;
        _feature_colors_texture.set(colors);
    }

    void update_uniform_data(const UniformData& uniform_data) override
    {
        _uniform_data_to_update = uniform_data;
    }

    void draw(hrz::Render* render, hrz::SceneViewBitset scene_views) override
    {
        if (_last_frame_common_resources_queued < hrz::Render::CurrentFrame)
        {
            _queued_common_resources = render->rd->as_queue().write(_common_resources.value());
            _last_frame_common_resources_queued = hrz::Render::CurrentFrame;
        }

        _renderable.render_data.common = _queued_common_resources;

        if (_uniform_data_to_update)
        {
            render->my->update_buffer(
                _renderable.render_data.ubo, 0, sizeof(UniformData),
                &_uniform_data_to_update.value());
            _uniform_data_to_update.reset();
        }

        _feature_colors_texture.update(render);
        _renderable.render_data.feature_colors_texture = _feature_colors_texture.get_resource();

        _renderable.render_data.scene_views = scene_views.bits();
        _renderable.render_data.has_transparency =
            _has_transparent_feature || _has_transparent_color;

        if (_selection_texture.has_any_selected())
        {
            _selection_texture.work_gpu(render);
            _renderable.render_data.has_any_selected = true;
        }
        else
        {
            _renderable.render_data.has_any_selected = false;
        }

        render->rd->collect_renderable(_renderable);
    }

    void destroy(std::vector<my::ResourceHandle>& to_destroy) override
    {
        to_destroy.push_back(_renderable.vbo);
        to_destroy.push_back(_renderable.render_data.vertex_input);
        to_destroy.push_back(_renderable.render_data.ubo);

        _feature_ids_texture.destroy(to_destroy);
        _feature_colors_texture.destroy(to_destroy);
        _selection_texture.free_gpu_resources(to_destroy);
    }
};

} // anonymous namespace

namespace hrz
{
std::unique_ptr<PointCloud> PointCloud::create(
    hrz::Render* render,
    const hrz::monitoring::ResourceOwner& owner,
    const Geometry& geometry,
    const UniformData& uniform_data)
{
    return std::make_unique<PointCloudImpl>(render, owner, geometry, uniform_data);
}
} // namespace hrz

namespace hrz::point_cloud
{
void collect_shaders(hrz::GpuResourceContext* rc)
{
    my::IndexName attribs[] = {
        {InputStreamPosition, "i_position"},
        {InputStreamCompressedNormal, "i_compressed_normal"},
        {InputStreamColor, "i_color"},
        {InputStreamBatchId, "i_batch_id"},
    };

    static const my::IndexName ubos[] = {
        {hrz::UboFrame, "Frame"},
        {UboPointCloud, "PointCloud"},
    };

    const char* color_outputs[] = {"o_color"};
    const char* picking_outputs[] = {"o_object_reference", "o_depth_value"};
    const char* selection_outputs[] = {"o_highlight"};

    hrz::StaticVector<my::IndexName, 16> selection_samplers;
    selection_samplers.push_back({SamplerSelection, "u_selection"});
    selection_samplers.push_back({SamplerFeatureColors, "u_feature_colors"});

    hrz::StaticVector<my::IndexName, 16> picking_samplers;
    picking_samplers.push_back({SamplerFeatureColors, "u_feature_colors"});

    hrz::StaticVector<my::IndexName, 16> visual_samplers;
    visual_samplers.push_back({SamplerFeatureIds, "u_feature_ids"});
    visual_samplers.push_back({SamplerFeatureColors, "u_feature_colors"});
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

    {
        my::ShaderResource res{};
        res.name = hrz_shaders::PointCloud_visual_name;
        res.vertex_source_len = hrz_shaders::PointCloud_visual_vert_len;
        res.vertex_source = hrz_shaders::PointCloud_visual_vert;
        res.fragment_source_len = hrz_shaders::PointCloud_visual_frag_len;
        res.fragment_source = hrz_shaders::PointCloud_visual_frag;
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
        auto opaque_shader = rc->alloc(&res, hrz::monitoring::systems::PointCloud);

        my::ShaderDerivativeResource res_d(
            opaque_shader, res, PointCloud_visual_transparent_name.c_str());
        res_d.initial_state.color_blend.enable = true;
        res_d.initial_state.color_blend.color.src = my::ColorBlendState::One;
        res_d.initial_state.color_blend.color.dst = my::ColorBlendState::OneMinusSrcAlpha;
        res_d.initial_state.color_blend.alpha.src = my::ColorBlendState::One;
        res_d.initial_state.color_blend.alpha.dst = my::ColorBlendState::OneMinusSrcAlpha;
        rc->alloc(&res_d, hrz::monitoring::systems::PointCloud);
    }

    {
        my::ShaderResource res{};
        res.name = hrz_shaders::PointCloud_picking_name;
        res.vertex_source_len = hrz_shaders::PointCloud_picking_vert_len;
        res.vertex_source = hrz_shaders::PointCloud_picking_vert;
        res.fragment_source_len = hrz_shaders::PointCloud_picking_frag_len;
        res.fragment_source = hrz_shaders::PointCloud_picking_frag;
        res.attrib_count = HRZ_ARRAY_COUNT(attribs);
        res.attribs = attribs;
        res.uniform_block_count = HRZ_ARRAY_COUNT(ubos);
        res.uniform_blocks = ubos;
        res.sampler_count = (uint32_t)picking_samplers.size();
        res.samplers = picking_samplers.data();
        res.output_count = HRZ_ARRAY_COUNT(picking_outputs);
        res.outputs = picking_outputs;
        res.initial_state.depth.test = true;
        res.initial_state.color_blend.enable = false;
        rc->alloc(&res, hrz::monitoring::systems::PointCloud);
    }

    {
        my::ShaderResource res{};
        res.name = hrz_shaders::PointCloud_selection_name;
        res.vertex_source_len = hrz_shaders::PointCloud_selection_vert_len;
        res.vertex_source = hrz_shaders::PointCloud_selection_vert;
        res.fragment_source_len = hrz_shaders::PointCloud_selection_frag_len;
        res.fragment_source = hrz_shaders::PointCloud_selection_frag;
        res.attrib_count = HRZ_ARRAY_COUNT(attribs);
        res.attribs = attribs;
        res.uniform_block_count = HRZ_ARRAY_COUNT(ubos);
        res.uniform_blocks = ubos;
        res.sampler_count = (uint32_t)selection_samplers.size();
        res.samplers = selection_samplers.data();
        res.output_count = HRZ_ARRAY_COUNT(selection_outputs);
        res.outputs = selection_outputs;
        res.initial_state.depth.test = true;
        res.initial_state.color_blend.enable = false;
        rc->alloc(&res, hrz::monitoring::systems::PointCloud);
    }
}
} // namespace hrz::point_cloud
