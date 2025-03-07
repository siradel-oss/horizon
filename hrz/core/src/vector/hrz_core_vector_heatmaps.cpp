#include "vector/hrz_core_vector_heatmaps.h"

#include "hrz_core_camera_height.h"
#include "hrz_core_global_flags.h"
#include "hrz_core_palette_ubo.h"
#include "hrz_core_render.h"
#include "hrz_core_shaders.h"
#include "vector/hrz_core_vector_flat_overlay.h"

#include <hrz_common_palette.h>
#include <hrz_fnd_flat_hash_map.h>
#include <hrz_fnd_flat_hash_set.h>
#include <hrz_fnd_gen_object_pool.h>
#include <hrz_fnd_log.h>
#include <hrz_fnd_time.h>
#include <hrz_protocol_path_builder.h>

#include <memory>

namespace hrz
{
struct HeatmapPointsUniform
{
    lm::mat4 proj;
    float max_scale_factor;
    uint32_t cascade_count;
    uint32_t _padding[2];
};

HRZ_CHECK_UBO_SIZE(HeatmapPointsUniform);

struct HeatmapOverlayUniform
{
    lm::mat4 transform[HRZ_S_MAX_OVERLAY_CASCADES];
    HRZ_UBO_STRUCT_FIELD(PaletteUniformData) palette;
    float max_scale_factor;
    uint32_t cascade_count;
    float proj_translation_x;
    uint32_t _padding[1];
};

HRZ_CHECK_UBO_SIZE(HeatmapOverlayUniform);

enum
{
    InputStreamUv = 0,

    UboHeatmapQuadOverlay = hrz::vector_flat_overlay::UboVectorOverlayPass + 1,

    SamplerHeatmap = hrz::vector_flat_overlay::SamplerOverlayStart
};

struct HeatmapReprRegistry
{
    struct Repr
    {
        heatmaps::OverlayConfig config;
        heatmaps::VectorTilesInfo vector_tiles_info;
    };

    using IdPool = GenIndexPool<heatmaps::ReprId, 16, 16>;
    using ReprPool = GenObjectPool<Repr, IdPool, 4>;

    ReprPool entry_pool;

    hrz::flat_hash_set<heatmaps::ReprId> ids;
    hrz::flat_hash_set<uint64_t> vector_tiles_layers_to_render;
};

namespace heatmaps
{
HeatmapReprRegistry* create_repr_registry()
{
    return new HeatmapReprRegistry();
}

void destroy_repr_registry(HeatmapReprRegistry* reg)
{
    delete reg;
}

ReprId register_repr(
    HeatmapReprRegistry* reg,
    const VectorTilesInfo& info,
    const OverlayConfig& config)
{
    ReprId id = reg->entry_pool.alloc();
    reg->ids.insert(id);

    auto* new_entry = reg->entry_pool.get_object(id);
    new_entry->config = config;
    new_entry->vector_tiles_info = info;

    return id;
}

void unregister_repr(HeatmapReprRegistry* reg, ReprId id)
{
    reg->entry_pool.release(id);
    reg->ids.erase(id);
}

const VectorTilesInfo& get_repr_vector_tiles_info(HeatmapReprRegistry* reg, ReprId id)
{
    auto* entry = reg->entry_pool.get_object(id);
    return entry->vector_tiles_info;
}

void hide_all_layers(HeatmapReprRegistry* reg)
{
    reg->vector_tiles_layers_to_render.clear();
}

void make_layer_visible(HeatmapReprRegistry* reg, uint64_t layer_id)
{
    reg->vector_tiles_layers_to_render.insert(layer_id);
}
} // namespace heatmaps

// Note that the renderable doesn't own any of its ResourceHandles.
struct HeatmapOverlayRenderable : public my::Renderer::Renderable
{
    HeatmapOverlayUniform ubo;
    uint32_t z_index;

    struct FeatureRenderData
    {
        my::ResourceHandle shader = my::ResourceHandle::null();
        my::ResourceHandle vertex_input = my::ResourceHandle::null();
        my::ResourceHandle ubo_buffer = my::ResourceHandle::null();
        my::ResourceHandle heatmap_texture = my::ResourceHandle::null();
        my::ResourceHandle heatmap_sampler = my::ResourceHandle::null();
        size_t primitive_count;

        uint32_t scene_views;
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

        if (render_type != hrz::RenderVisual) return;

        auto points_batch =
            my::DrawBatchInfo(my::PrimitiveType::TriangleStrip, data->primitive_count);

        rb->push_state();

        my::UboBinding ubo_bindings[] = {
            {UboHeatmapQuadOverlay, data->ubo_buffer, 0, sizeof(HeatmapOverlayUniform)},
        };
        rb->bind(HRZ_ARRAY_COUNT(ubo_bindings), ubo_bindings);

        my::TextureBinding texture_bindings[] = {
            {SamplerHeatmap, data->heatmap_texture, data->heatmap_sampler}};
        rb->bind(HRZ_ARRAY_COUNT(texture_bindings), texture_bindings);

        auto state = rb->get_current_state();
        r->draw(
            points_batch, data->shader, data->vertex_input, state.ubo_count, state.ubos,
            state.texture_count, state.textures);

        rb->pop_state();
    }

    void collect_render_info(my::Renderer::Queue& queue, const my::Renderer::Culler& culler)
        const override
    {
        queue.enqueue(
            hrz::RenderFlatOverlayBin, render_callback, &data, {0, 0, 0}, HRZ_S_EARTH_RADIUS,
            z_index);
    }
};

struct ReprData
{
    my::ResourceHandle pass_output_target;
    my::ResourceHandle pass_fbo;

    my::ResourceHandle quad_ubo_buffer;

    uint64_t vector_tiles_repr_scene_views;
    uint64_t vector_tiles_layer_id;

    HeatmapOverlayRenderable renderable;
};

void destroy_repr_data(ReprData& repr_data, Render* render)
{
    render->rc->dealloc(repr_data.pass_output_target);
    render->rc->dealloc(repr_data.pass_fbo);
    render->rc->dealloc(repr_data.quad_ubo_buffer);
}

class HeatmapRenderPass : public hrz::render::TimedRenderPass
{
    struct Data
    {
        const hrz::flat_hash_map<heatmaps::ReprId, ReprData>* repr_data;
        my::Renderer::ViewId view_id;
        float max_scale_factor;
        uint32_t cascade_count;
        lm::mat4 proj;
    };

    Data _data;

    uint32_t _texture_size;
    render::DoubleBufferedUniformBuffer<HeatmapPointsUniform> _points_ubo;

    // This target is never rendered to, as eahc heatmap representation has its dedicated texture.
    // It is used to schedule the pass execution in the render graph.
    const char* _output_dummy_target;

    const char* _input_camera_height;
    my::ResourceHandle _camera_height_texture;
    my::ResourceHandle _camera_height_sampler;

    bool _render_requested;

public:
    HeatmapRenderPass(
        const char* name,
        const char* output_dummy_target,
        const char* input_camera_height,
        size_t texture_size,
        bool is_dummy) :
        TimedRenderPass(name),
        _points_ubo(1),
        _output_dummy_target(output_dummy_target),
        _input_camera_height(input_camera_height),
        _render_requested(false)
    {
        _texture_size = is_dummy ? 1 : texture_size;
    }

    const char* get_dummy_output_name() { return _output_dummy_target; }

    void request_render() { _render_requested = true; };

    bool is_render_requested() { return _render_requested; }

    void update(
        const hrz::flat_hash_map<heatmaps::ReprId, ReprData>* repr_data,
        const hrz::OverlayCamerasInfo& info,
        float max_scale_factor)
    {
        _data.repr_data = repr_data;
        _data.max_scale_factor = max_scale_factor;
        _data.cascade_count = info.cascade_count;
        _data.proj = (lm::mat4)info.heatmap_proj;
    }

    void setup_pass(my::RenderGraph::SetupContext& ctx) override
    {
        ctx.read(_input_camera_height, my::RenderGraph::Sampled);
        ctx.set_invocation_count(1);

        my::RenderGraph::ResourceInfo res;
        res.format = my::TextureFormat::R8;
        res.size_class = my::RenderGraph::ResourceInfo::Absolute;
        res.width = 1;
        res.height = 1;

        ctx.create(_output_dummy_target, my::RenderGraph::Target, res);
    }

    void retrieve_resources(
        my::Instance* my,
        my::ResourceContext* rc,
        const my::RenderGraph::ResourceContext& ctx) override
    {
        Render render;
        render.my = my;
        render.rc = (hrz::GpuResourceContext*)rc;

        _camera_height_texture = ctx.retrieve(_input_camera_height);

        my::SamplerResource sampler_res;
        sampler_res.sampler.wrap_x = my::SamplerParams::Wrap::Clamp;
        sampler_res.sampler.wrap_y = my::SamplerParams::Wrap::Clamp;
        sampler_res.sampler.wrap_z = my::SamplerParams::Wrap::Clamp;
        sampler_res.sampler.mag_filter = my::SamplerParams::Filter::Nearest;
        sampler_res.sampler.min_filter = my::SamplerParams::Filter::Nearest;
        sampler_res.sampler.is_shadow = false;
        sampler_res.use_mipmaps = false;
        _camera_height_sampler = render.rc->alloc(&sampler_res, hrz::monitoring::systems::Heatmaps);

        _points_ubo.initialize(
            &render, monitoring::systems::Heatmaps, {{"contents"_ss, "pass ubos"_ss}});
    }

    void set_view(my::Renderer::ViewId view_id) { _data.view_id = view_id; }

    void destroy(Render* render)
    {
        render->my->dealloc(_camera_height_sampler);
        _points_ubo.destroy(render->my);
    }

    void create_repr_texture_and_framebuffer(hrz::Render* render, ReprData* repr_data)
    {
        my::TextureResource output_res;
        output_res.generate_mipmaps = false;
        output_res.layout.type = my::TextureLayout::Type2D;
        output_res.layout.format = my::TextureFormat::R32F;
        output_res.layout.width = _texture_size;
        output_res.layout.height = _texture_size;
        output_res.layout.depth = 1;
        output_res.layout.levels = 1;

        repr_data->pass_output_target = render->rc->alloc(
            &output_res, hrz::monitoring::systems::Heatmaps, repr_data->vector_tiles_layer_id);

        my::FramebufferAttachment attachment{my::Attachment::Color0, repr_data->pass_output_target};
        my::FramebufferResource fb_res;
        fb_res.attachment_count = 1;
        fb_res.attachments = &attachment;

        repr_data->pass_fbo = render->rc->alloc(
            &fb_res, hrz::monitoring::systems::Heatmaps, repr_data->vector_tiles_layer_id);
    }

    void execute_timed(const my::RenderGraph::ExecutionContext& ctx) override
    {
        if (!_render_requested) return;

        ctx.binder->push_state();

        HeatmapPointsUniform uniform;
        uniform.max_scale_factor = _data.max_scale_factor;
        uniform.cascade_count = _data.cascade_count;
        uniform.proj = _data.proj;
        _points_ubo.set(0, uniform);
        _points_ubo.update(ctx.render);

        my::UboBinding binding = {
            heatmaps::UboHeatmapPoints, _points_ubo.get_for_gpu(), 0, sizeof(HeatmapPointsUniform)};
        ctx.binder->bind(1, &binding);

        const my::TextureBinding texture_bindings[] = {
            {hrz::SamplerCameraHeight, _camera_height_texture, _camera_height_sampler},
        };
        ctx.binder->bind(HRZ_ARRAY_COUNT(texture_bindings), texture_bindings);

        const my::ViewportState viewport_state = {
            {0, 0, _texture_size, _texture_size},
            {0, 0, _texture_size, _texture_size},
        };

        const my::ClearTarget clear_values[] = {
            {my::Attachment::Color0, my::ClearValue::make_color_float(0, 0, 0, 0)}};

        my::Renderer::BinMask to_render = RenderHeatmapBin;

        heatmaps::RenderUserData user_data{};
        user_data.scene_view_data = (const SceneViewRenderGraphUserData*)ctx.user_data;

        for (const auto& pair : *_data.repr_data)
        {
            const heatmaps::ReprId repr_id = pair.first;
            const auto& repr_data = pair.second;

            ctx.render->set_framebuffer(repr_data.pass_fbo, viewport_state);
            ctx.render->clear(1, clear_values);

            user_data.repr_id = repr_id;

            ctx.renderer->draw(
                hrz::RenderType::RenderVisual, _data.view_id, 1, &to_render, ctx.render, ctx.binder,
                (void*)&user_data);
        }

        ctx.binder->pop_state();
        _render_requested = false;
    }
};

struct HeatmapSystem
{
    std::unique_ptr<HeatmapRenderPass> pass;
    bool disabled;

    uint32_t scene_view_index;

    my::ResourceHandle quad_vertex_buffer;
    my::ResourceHandle quad_vertex_input;
    my::ResourceHandle quad_heatmap_sampler;
    my::ResourceHandle quad_shader;

    hrz::flat_hash_map<heatmaps::ReprId, ReprData> repr_data;
    hrz::flat_hash_set<heatmaps::ReprId> reprs_to_initialize;
    hrz::flat_hash_set<heatmaps::ReprId> reprs_to_remove;
    hrz::flat_hash_set<heatmaps::ReprId> reprs_to_update_owner;
    hrz::flat_hash_set<heatmaps::ReprId> reprs_to_draw;

    int previous_cascade_count;
};

namespace heatmaps
{
// This factor is be used to scale the contents of the texture storing the heatmap values
// on the X axis according to their vertical position, so that the points of the heatmap
// that are closer to the camera are larger than the ones that are further away,
// which will make it possible to draw them at a higher resolution on the flat overlays.
//
//
//                          ------*               * ---------------------*
//                    ------      |               |                      |
//              ------            |               |                      |
//            *                   |               |                      |
//         /  |                   |               |                      |
//     []<    |                   |      <=>      |                      |
//         \  |                   |               |                      |
//            *                   |               |                      |
//              ------            |               |                      |
//                    ------      |               |                      |
//                          ------*               * ---------------------*
//
//            The goal : "Stretching" the main view frustum to improve
//                    the resolution of the closest points
//
// Ideally we'd want to scale the points so that only the ones that are visible in the
// camera view's frustum are renderered to the texture, but in practice, when rendering
// the flat overlays on the planet, the DTM and the curvature of the planet make it hard to
// match exactly. And trying to scale too much will result in pixels missing on the edges of
// the main view frustum.
// This is the reason why the factor is divided by 2: to ensure the full frustum is covered
// in most cases. There may be a way to compute a better factor, but this one works
// fine enough.
//      -aleclerc, 2023-04-24
float compute_heatmaps_max_scale_factor(const hrz::OverlayCamerasInfo& info)
{
    // Since the heatmap and flat overlay projections are orthographic, we can easily fetch
    // the extents of their "view box" as a world size.
    const double heatmap_max_side_size_half =
        std::max(1.0 / info.heatmap_proj.x.x, 1.0 / info.heatmap_proj.y.y);
    const double first_cascade_max_side_size_half =
        std::max(1.0 / info.proj[0].x.x, 1.0 / info.proj[0].y.y);

    return heatmap_max_side_size_half / first_cascade_max_side_size_half / 2.0;
}

// This function must match exactly the transformation done in the heatmap shaders.
lm::vec2 warp_heatmap_texture_coordinates(
    const lm::vec2& in_uv,
    const hrz::OverlayCamerasInfo& info)
{
    lm::vec2 out_uv;

    if (info.cascade_count <= 1) return in_uv;

    out_uv.y = 1.0f - (1.0f - in_uv.y) * (1.0f - in_uv.y);

    const double max_scale_factor = compute_heatmaps_max_scale_factor(info);
    const float x_origin = (info.heatmap_proj.w.x + 1.0) / 2.0;
    const float x_scaling = max_scale_factor + out_uv.y * (1.0f - max_scale_factor);
    out_uv.x = (in_uv.x - x_origin) * x_scaling + x_origin;

    return out_uv;
}

HeatmapSystem* create_system(uint32_t scene_view_index)
{
    HeatmapSystem* system = new HeatmapSystem();
    system->scene_view_index = scene_view_index;
    system->disabled = !get_flag(hrz::Flag::EnableTerrain);
    return system;
}

void destroy_system(HeatmapSystem* system, Render* render)
{
    system->pass->destroy(render);

    render->rc->dealloc(system->quad_vertex_buffer);
    render->rc->dealloc(system->quad_vertex_input);
    render->rc->dealloc(system->quad_heatmap_sampler);

    for (auto& pair : system->repr_data)
    {
        destroy_repr_data(pair.second, render);
    }

    delete system;
}

void initialize_rendering(
    HeatmapSystem* system,
    size_t texture_size,
    CameraHeightSystem* camera_height_system,
    Render* render)
{
    system->pass.reset(new HeatmapRenderPass(
        "heatmaps visual", "heatmaps dummy output",
        hrz::camera_height::get_target_name(camera_height_system), texture_size, system->disabled));

    {
        const lm::vec2 uvs[] = {{0.0, 0.0}, {0.0, 1.0}, {1.0, 0.0}, {1.0, 1.0}};

        my::BufferResource vb_res(my::BufferResource::BufferType::Vertex);
        vb_res.size = sizeof(uvs);
        vb_res.usage = my::UsageHint::Static;
        vb_res.data = &uvs[0].x;

        system->quad_vertex_buffer = render->rc->alloc(
            &vb_res, hrz::monitoring::systems::Heatmaps, hrz::monitoring::NoLayer,
            {{"contents"_ss, "heatmap quad uvs"_ss}});
    }

    {
        my::VertexInputStream streams[] = {
            {InputStreamUv, system->quad_vertex_buffer, my::VertexFormat::Float32_2, 0, 0,
             my::VertexRate::PerVertex}};

        my::VertexInputResource vi_res;
        vi_res.attrib_count = HRZ_ARRAY_COUNT(streams);
        vi_res.attribs = streams;

        system->quad_vertex_input = render->rc->alloc(
            &vi_res, hrz::monitoring::systems::Heatmaps, 0, {{"contents", "flat heatmap quad"}});
    }

    {
        my::SamplerResource sampler_res;
        sampler_res.sampler.wrap_x = my::SamplerParams::Wrap::Clamp;
        sampler_res.sampler.wrap_y = my::SamplerParams::Wrap::Clamp;
        sampler_res.sampler.wrap_z = my::SamplerParams::Wrap::Clamp;
        sampler_res.sampler.mag_filter = my::SamplerParams::Filter::Linear;
        sampler_res.sampler.min_filter = my::SamplerParams::Filter::Linear;
        sampler_res.sampler.is_shadow = false;
        sampler_res.use_mipmaps = false;

        system->quad_heatmap_sampler =
            render->rc->alloc(&sampler_res, monitoring::systems::Heatmaps);
    }

    system->quad_shader = render->rc->retrieve_shader(hrz_shaders::FlatHeatmapQuad_name);
}

const char* get_output_target_name(const HeatmapSystem* system)
{
    return system->pass->get_dummy_output_name();
}

void add_passes_to_render_graph(HeatmapSystem* system, RenderView* render)
{
    render->rg->add_pass("heatmaps visual", system->pass.get());
}

RenderRequest work(
    HeatmapSystem* system,
    const HeatmapReprRegistry* reg,
    const VectorFlatOverlaySystem* flat_overlay)
{
    RenderRequest render_request;

    if (!system->disabled && vector_flat_overlay::is_about_to_render(flat_overlay))
    {
        system->pass->request_render();

        const auto& info = vector_flat_overlay::get_latest_overlay_cameras_info(flat_overlay);
        const double scale_factor = compute_heatmaps_max_scale_factor(info);

        // We check which representation has been unregistered since the previous frame, in order
        // to know which representation's data can be recycled.
        std::vector<ReprId> unused_repr_data_ids;
        for (const auto& pair : system->repr_data)
        {
            if (!reg->ids.contains(pair.first))
            {
                unused_repr_data_ids.push_back(pair.first);
            }
        }

        for (auto id : reg->ids)
        {
            if (!system->repr_data.contains(id))
            {
                const auto* reg_entry = reg->entry_pool.get_object(id);

                if (!unused_repr_data_ids.empty())
                {
                    // A new representation has been registered, but another was removed, which
                    // means that we can recycle the resources of the old representation for
                    // the new one.
                    auto it = system->repr_data.find(unused_repr_data_ids.back());
                    assert(it != system->repr_data.end());

                    ReprData recycled_data = std::move(it->second);

                    palette::fill_ubo_data(
                        &recycled_data.renderable.ubo.palette, reg_entry->config.palette);
                    recycled_data.renderable.z_index = reg_entry->config.z_index;
                    recycled_data.vector_tiles_layer_id = reg_entry->vector_tiles_info.layer_id;
                    recycled_data.vector_tiles_repr_scene_views =
                        reg_entry->vector_tiles_info.repr_scene_views;

                    system->repr_data.erase(it);
                    system->repr_data.insert({id, recycled_data});

                    system->reprs_to_update_owner.insert(id);

                    unused_repr_data_ids.pop_back();
                }
                else
                {
                    // There is no data that can be recycled: we have to create new data.
                    ReprData repr = {};

                    palette::fill_ubo_data(&repr.renderable.ubo.palette, reg_entry->config.palette);
                    repr.renderable.z_index = reg_entry->config.z_index;
                    repr.vector_tiles_layer_id = reg_entry->vector_tiles_info.layer_id;
                    repr.vector_tiles_repr_scene_views =
                        reg_entry->vector_tiles_info.repr_scene_views;

                    repr.renderable.data.vertex_input = system->quad_vertex_input;
                    repr.renderable.data.shader = system->quad_shader;
                    repr.renderable.data.heatmap_sampler = system->quad_heatmap_sampler;
                    repr.renderable.data.scene_views = 1 << system->scene_view_index;
                    repr.renderable.data.primitive_count = 4;

                    system->repr_data.insert({id, repr});
                    system->reprs_to_initialize.insert(id);
                }
            }
        }

        // If there is still some unused data that was not recycled, then we can delete it.
        for (auto id : unused_repr_data_ids)
        {
            system->reprs_to_remove.insert(id);
        }

        system->pass->update(&system->repr_data, info, scale_factor);

        if (system->repr_data.size() > 0)
        {
            static lm::mat4 transforms[HRZ_S_MAX_OVERLAY_CASCADES];
            for (int i = 0; i < HRZ_S_MAX_OVERLAY_CASCADES; i++)
            {
                lm::mat4 src_proj = (lm::mat4)info.heatmap_proj;
                lm::mat4 dst_proj = (lm::mat4)info.proj[i];

                transforms[i] = dst_proj * lm::inverse(src_proj);
            }

            for (auto& pair : system->repr_data)
            {
                if (system->reprs_to_remove.contains(pair.first)) continue;

                pair.second.renderable.ubo.max_scale_factor = scale_factor;
                pair.second.renderable.ubo.cascade_count = info.cascade_count;
                pair.second.renderable.ubo.proj_translation_x = info.heatmap_proj.w.x;
                memcpy(
                    &pair.second.renderable.ubo.transform[0], &transforms[0], sizeof(transforms));

                if (reg->vector_tiles_layers_to_render.contains(pair.second.vector_tiles_layer_id))
                {
                    system->reprs_to_draw.insert(pair.first);
                }
            }
        }
    }

    return render_request;
}

void work_gpu(HeatmapSystem* system, Render* render)
{
    for (auto id : system->reprs_to_initialize)
    {
        auto& repr = system->repr_data.at(id);

        system->pass->create_repr_texture_and_framebuffer(render, &repr);
        repr.renderable.data.heatmap_texture = repr.pass_output_target;

        my::BufferResource ub_res(my::BufferResource::BufferType::Uniform);
        ub_res.size = sizeof(HeatmapOverlayUniform);
        ub_res.usage = my::UsageHint::Static;
        ub_res.data = &repr.renderable.ubo;

        repr.quad_ubo_buffer = render->rc->alloc(
            &ub_res, hrz::monitoring::systems::Heatmaps, repr.vector_tiles_layer_id,
            {{"contents"_ss, "heatmap quad ubo"_ss}});

        repr.renderable.data.ubo_buffer = repr.quad_ubo_buffer;
    }
    system->reprs_to_initialize.clear();

    for (auto id : system->reprs_to_remove)
    {
        auto& repr = system->repr_data.at(id);
        destroy_repr_data(repr, render);

        system->repr_data.erase(id);
    }
    system->reprs_to_remove.clear();

    for (auto id : system->reprs_to_update_owner)
    {
        auto& repr = system->repr_data.at(id);
        render->rc->monitoring->update_gpu_resource_owner(
            repr.pass_fbo, monitoring::systems::Heatmaps, repr.vector_tiles_layer_id);
        render->rc->monitoring->update_gpu_resource_owner(
            repr.pass_output_target, monitoring::systems::Heatmaps, repr.vector_tiles_layer_id);
        render->rc->monitoring->update_gpu_resource_owner(
            repr.quad_ubo_buffer, monitoring::systems::Heatmaps, repr.vector_tiles_layer_id);
    }
    system->reprs_to_update_owner.clear();
}

void register_views(
    HeatmapSystem* system,
    const VectorFlatOverlaySystem* flat_overlay,
    hrz::Render* render,
    std::vector<my::Renderer::ViewId>& created_views)
{
    const hrz::OverlayCamerasInfo& info =
        vector_flat_overlay::get_latest_overlay_cameras_info(flat_overlay);

    my::View heatmap_view;
    heatmap_view.view = info.view;
    heatmap_view.projection = info.heatmap_proj;

    auto view_id = render->rd->add_auxiliary_view(heatmap_view, hrz::RenderHeatmapBin);
    created_views.push_back(view_id);

    system->pass->set_view(view_id);
}

void draw(HeatmapSystem* system, hrz::Render* render)
{
    if (system->disabled) return;

    for (auto id : system->reprs_to_draw)
    {
        auto& repr_data = system->repr_data.at(id);

        assert(!repr_data.renderable.data.heatmap_texture.is_null());

        if ((repr_data.vector_tiles_repr_scene_views & (1 << system->scene_view_index)) == 0)
        {
            continue;
        }

        render->my->update_buffer(
            repr_data.renderable.data.ubo_buffer, 0, sizeof(HeatmapOverlayUniform),
            &repr_data.renderable.ubo);
        render->rd->collect_renderable(repr_data.renderable);
    }

    system->reprs_to_draw.clear();
}

std::vector<std::pair<ReprId, my::ResourceHandle>> get_fbos(HeatmapSystem* system)
{
    std::vector<std::pair<ReprId, my::ResourceHandle>> out;
    out.reserve(system->repr_data.size());
    for (auto& repr : system->repr_data)
    {
        out.push_back({repr.first, repr.second.pass_fbo});
    }

    return out;
}

std::optional<lm::uvec2> world_position_to_heatmap_texture_coordinates(
    lm::dvec3 world_pos,
    const hrz::OverlayCamerasInfo& info,
    uint32_t texture_size)
{
    lm::dvec4 heatmap_clip_pos = info.heatmap_proj * info.view * lm::dvec4(world_pos, 1.0);
    lm::dvec2 heatmap_ndc_pos = heatmap_clip_pos.xy / heatmap_clip_pos.w;

    if (!lm::contains(lm::dbbox2({-1, -1}, {1, 1}), heatmap_ndc_pos))
    {
        return std::nullopt;
    }

    lm::vec2 uv = (lm::vec2)((heatmap_ndc_pos + lm::dvec2(1.0)) / 2.0);
    uv = warp_heatmap_texture_coordinates(uv, info);

    return (lm::uvec2)(uv * (float)texture_size);
}

void collect_shaders(hrz::GpuResourceContext* rc)
{
    static const my::IndexName ubos[] = {
        {UboFrame, "Frame"},
        {hrz::vector_flat_overlay::UboVectorOverlayPass, "OverlayPasses"},
        {UboHeatmapQuadOverlay, "HeatmapQuadOverlay"},
    };

    const char* color_outputs[] = {"o_color"};

    my::IndexName samplers[] = {{SamplerHeatmap, "u_heatmap"}};

    {
        my::IndexName attribs[] = {{InputStreamUv, "i_uv"}};

        my::ShaderResource res{};
        res.name = hrz_shaders::FlatHeatmapQuad_name;
        res.vertex_source_len = hrz_shaders::FlatHeatmapQuad_vert_len;
        res.vertex_source = hrz_shaders::FlatHeatmapQuad_vert;
        res.fragment_source_len = hrz_shaders::FlatHeatmapQuad_frag_len;
        res.fragment_source = hrz_shaders::FlatHeatmapQuad_frag;
        res.attrib_count = HRZ_ARRAY_COUNT(attribs);
        res.attribs = attribs;
        res.uniform_block_count = HRZ_ARRAY_COUNT(ubos);
        res.uniform_blocks = ubos;
        res.sampler_count = HRZ_ARRAY_COUNT(samplers);
        res.samplers = samplers;
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

        rc->alloc(&res, hrz::monitoring::systems::Heatmaps);
    }
}
} // namespace heatmaps
} // namespace hrz
