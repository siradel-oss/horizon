// SPDX-FileCopyrightText: Copyright 2021 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/core/camera_height.h"

#include "hrz/common/geo.h"
#include "hrz/common/monitoring_defs.h"
#include "hrz/common/profiling.h"
#include "hrz/core/download_buffer_pool.h"
#include "hrz/core/global_flags.h"
#include "hrz/core/planet/geometry.h"
#include "hrz/core/render/common_ubos.h"
#include "hrz/core/render/defs.h"
#include "hrz/core/render/double_buffered_uniform_buffer.h"
#include "hrz/core/render/resources.h"
#include "hrz/core/render/timed_render_pass.h"
#include "hrz/core/shaders/collection.h"
#include "hrz/fnd/mem.h"

#include <lin_maths.h>
#include <mycelium/render_graph.h>

#include <cassert>
#include <deque>
#include <memory>

namespace
{

enum
{
    PlanetParamsUbo = hrz::UboCustomStart,
    CameraPositionUbo,

    DepthSampler = 0,
    DtmIndirectionSampler = 1,
    DtmAtlasSampler = 2,
};

struct CameraPositionUniformData
{
    lm::vec2 wmerc_high;
    lm::vec2 wmerc_low;
};

class CameraHeightComputationPass : public hrz::render::TimedRenderPass
{
    my::Renderer::ViewId _view = 0;
    hrz::render::DoubleBufferedUniformBuffer<hrz::AuxViewUniformData> _view_ubo;

    const char* _depth_texture_name = "camera_height_depth";
    my::ResourceHandle _depth_texture = my::ResourceHandle::null();
    my::ResourceHandle _depth_fbo = my::ResourceHandle::null();

    const char* _height_texture_name = "camera_height";
    my::ResourceHandle _height_texture = my::ResourceHandle::null();
    my::ResourceHandle _height_fbo = my::ResourceHandle::null();

    hrz::render::DoubleBufferedUniformBuffer<CameraPositionUniformData> _camera_ubo;

    hrz::planet::GeometryResources _planet_resources;

    my::ResourceHandle _depth_sampler = my::ResourceHandle::null();
    my::ResourceHandle _depth_to_height_vb = my::ResourceHandle::null();
    my::ResourceHandle _depth_to_height_vi = my::ResourceHandle::null();
    my::ResourceHandle _depth_to_height_shader = my::ResourceHandle::null();

    struct Download
    {
        uint64_t id;
        hrz::DownloadBuffer buffer;
    };

    size_t _download_buffer_size;
    std::deque<Download> _downloads;
    hrz::DownloadBufferPool _download_buffer_pool;

    float _last_downloaded_height = std::numeric_limits<float>::max();

public:
    CameraHeightComputationPass() : TimedRenderPass("camera height") {}

    constexpr float last_downloaded_height() const { return _last_downloaded_height; }

    void set_view(my::Renderer::ViewId view) { _view = view; }

    void set_planet_resources(const hrz::planet::GeometryResources& planet_resources)
    {
        _planet_resources = planet_resources;
    }

    const char* camera_height_output() const { return _height_texture_name; }

    void setup_pass(my::RenderGraph::SetupContext& ctx) override
    {
        {
            my::RenderGraph::ResourceInfo res;
            res.format = my::TextureFormat::Depth32F;
            res.size_class = my::RenderGraph::ResourceInfo::Absolute;
            res.width = 1;
            res.height = 1;

            ctx.create(_depth_texture_name, my::RenderGraph::TargetSampled, res);
        }

        {
            my::RenderGraph::ResourceInfo res;
            res.format = my::TextureFormat::R32F;
            res.size_class = my::RenderGraph::ResourceInfo::Absolute;
            res.width = 1;
            res.height = 1;

            ctx.create(_height_texture_name, my::RenderGraph::Target, res);
        }

        _download_buffer_size = 1 * 1 * sizeof(float) * 4;
    }

    void retrieve_resources(
        my::Instance* my,
        my::ResourceContext* rc,
        const my::RenderGraph::ResourceContext& ctx) override
    {
        hrz::Render render;
        render.my = my;
        render.rc = (hrz::GpuResourceContext*)rc;

        _view_ubo.initialize(
            &render, hrz::monitoring::systems::CameraHeight,
            {{"contents"_ss, "camera height view ubo"_ss}});
        _camera_ubo.initialize(
            &render, hrz::monitoring::systems::CameraHeight,
            {{"contents"_ss, "camera height camera ubo"_ss}});

        _depth_texture = ctx.retrieve(_depth_texture_name);

        {
            my::FramebufferAttachment attachments[] = {
                {my::Attachment::Depth, _depth_texture},
            };

            my::FramebufferResource res;
            res.attachments = attachments;

            _depth_fbo = render.rc->alloc(&res, hrz::monitoring::systems::CameraHeight);
        }

        _height_texture = ctx.retrieve(_height_texture_name);

        {
            my::FramebufferAttachment attachments[] = {
                {my::Attachment::Color0, _height_texture},
            };

            my::FramebufferResource res;
            res.attachments = attachments;

            _height_fbo = render.rc->alloc(&res, hrz::monitoring::systems::CameraHeight);
        }

        {
            my::SamplerResource res;
            res.sampler.mag_filter = my::SamplerParams::Filter::Nearest;
            res.sampler.min_filter = my::SamplerParams::Filter::Nearest;
            res.sampler.wrap_x = my::SamplerParams::Wrap::Clamp;
            res.sampler.wrap_y = my::SamplerParams::Wrap::Clamp;
            res.sampler.is_shadow = false;
            res.use_mipmaps = false;

            _depth_sampler = render.rc->alloc(&res, hrz::monitoring::systems::CameraHeight);
        }

        {
            static const lm::vec2 triangle_data[] = {
                lm::vec2(-1, -1),
                lm::vec2(3, -1),
                lm::vec2(-1, 3),
            };

            my::BufferResource res(my::BufferResource::Vertex);
            res.usage = my::UsageHint::Static;
            res.size = sizeof(lm::vec2) * HRZ_ARRAY_COUNT(triangle_data);
            res.data = triangle_data;

            _depth_to_height_vb = render.rc->alloc(&res, hrz::monitoring::systems::CameraHeight);
        }

        {
            const my::VertexInputStream streams[] = {
                {0, _depth_to_height_vb, my::VertexFormat::Float32_2, 0, 0,
                 my::VertexRate::PerVertex},
            };

            my::VertexInputResource res;
            res.attribs = streams;

            _depth_to_height_vi = render.rc->alloc(&res, hrz::monitoring::systems::CameraHeight);
        }

        _depth_to_height_shader = render.rc->retrieve_shader(hrz_shaders::CameraDepthToHeight_name);
    }

    static void collect_shaders(hrz::GpuResourceContext* rc)
    {
        my::IndexName attribs[] = {{0, "i_pos"}};

        my::IndexName ubos[] = {
            {hrz::UboFrame, "Frame"},
            {PlanetParamsUbo, "PlanetParams"},
            {CameraPositionUbo, "CameraPosition"},
        };

        my::IndexName samplers[] = {
            {DepthSampler, "u_camera_depth"},
            {DtmIndirectionSampler, "u_dtm_indirection"},
            {DtmAtlasSampler, "u_dtm_atlas"},
        };

        const char* outputs[] = {"o_height"};

        my::ShaderResource res{};
        res.name = hrz_shaders::CameraDepthToHeight_name;
        res.link_hint = my::ShaderLinkHint::Initial;
        res.vertex_source_len = hrz_shaders::CameraDepthToHeight_vert_len;
        res.vertex_source = hrz_shaders::CameraDepthToHeight_vert;
        res.fragment_source_len = hrz_shaders::CameraDepthToHeight_frag_len;
        res.fragment_source = hrz_shaders::CameraDepthToHeight_frag;
        res.attribs = attribs;
        res.uniform_blocks = ubos;
        res.samplers = samplers;
        res.outputs = outputs;
        res.initial_state.color_blend.enable = false;
        res.initial_state.color_blend.mask = my::ColorBlendState::RGBA;
        res.initial_state.depth.test = false;
        res.initial_state.rasterization.cull_mode = my::RasterizationState::None;
        res.initial_state.stencil.enable = false;

        rc->alloc(&res, hrz::monitoring::systems::CameraHeight);
    }

    void destroy(my::ResourceContext* rc)
    {
        _view_ubo.destroy(rc);
        _camera_ubo.destroy(rc);
        rc->dealloc(_depth_fbo);
        rc->dealloc(_depth_sampler);
        rc->dealloc(_depth_to_height_vi);
        rc->dealloc(_depth_to_height_vb);
        rc->dealloc(_height_fbo);

        for (const auto& d : _downloads)
        {
            _download_buffer_pool.release(d.buffer);
            hrz::render::release_texture_download_id(d.id);
        }
        _downloads.clear();
        _download_buffer_pool.free_all(rc);
    }

    void update_ubos(
        hrz::Render* render,
        const lm::dmat4& pv_from_main_view,
        const lm::dmat4& pv_cc,
        const lm::dvec2& view_wmerc)
    {
        {
            hrz::AuxViewUniformData view;
            view.pv_from_main_view = lm::mat4(pv_from_main_view);
            view.pv_cc = lm::mat4(pv_cc);

            _view_ubo.set(0, view);
        }

        {
            CameraPositionUniformData camera_position;
            hrz::split_double(
                view_wmerc.x, camera_position.wmerc_low.x, camera_position.wmerc_high.x);
            hrz::split_double(
                view_wmerc.y, camera_position.wmerc_low.y, camera_position.wmerc_high.y);

            _camera_ubo.set(0, camera_position);
        }
    }

    void render_depth(const my::RenderGraph::ExecutionContext& ctx)
    {
        ctx.render->set_framebuffer(_depth_fbo, my::ViewportState{{0, 0, 1, 1}, {0, 0, 1, 1}});

        ctx.binder->push_state();

        static const my::ClearTarget clear_targets[] = {
            {my::Attachment::Depth, my::ClearValue::make_depth(1.0)},
        };
        ctx.render->clear(clear_targets);

        my::UboBinding ubo_binding{
            hrz::UboView, _view_ubo.get_for_gpu(), 0, sizeof(hrz::AuxViewUniformData)
        };
        ctx.binder->bind({&ubo_binding, 1});

        static const my::Renderer::BinMask pass_masks[] = {
            hrz::RenderPlanetBin,
        };

        ctx.renderer->draw(
            hrz::RenderDepth, _view, pass_masks, ctx.render, ctx.binder, ctx.user_data);

        ctx.binder->pop_state();
    }

    void render_height(const my::RenderGraph::ExecutionContext& ctx)
    {
        ctx.render->set_framebuffer(_height_fbo, my::ViewportState{{0, 0, 1, 1}, {0, 0, 1, 1}});

        ctx.binder->push_state();

        my::UboBinding ubo_bindings[] = {
            {PlanetParamsUbo, _planet_resources.planet_params.buffer,
             _planet_resources.planet_params.offset, _planet_resources.planet_params.size},
            {CameraPositionUbo, _camera_ubo.get_for_gpu(), 0, sizeof(CameraPositionUniformData)},
        };
        ctx.binder->bind(ubo_bindings);

        my::TextureBinding texture_bindings[] = {
            {DepthSampler, _depth_texture, _depth_sampler},
            {DtmIndirectionSampler, _planet_resources.dtm_indirection.texture,
             _planet_resources.dtm_indirection.sampler},
            {DtmAtlasSampler, _planet_resources.dtm_atlas.texture,
             _planet_resources.dtm_atlas.sampler},
        };
        ctx.binder->bind(texture_bindings);

        auto state = ctx.binder->get_current_state();

        static const auto info = my::DrawBatchInfo(my::PrimitiveType::TriangleList, 3);

        ctx.render->draw(
            info, _depth_to_height_shader, _depth_to_height_vi, state.ubos, state.textures);

        ctx.binder->pop_state();
    }

    void prepare_download(const my::RenderGraph::ExecutionContext& ctx)
    {
        auto rc = (hrz::GpuResourceContext*)ctx.resource;

        uint64_t id = hrz::render::acquire_texture_download_id();
        hrz::DownloadBuffer buffer = _download_buffer_pool.acquire(
            rc, _download_buffer_size, hrz::monitoring::systems::CameraHeight);

        ctx.render->color_texture_download_async(
            id, _height_fbo, my::Attachment::Color0, {0, 0, 1, 1},
            my::TextureDownloadFormat::RGBA32F, buffer.buffer);

        _downloads.push_back(Download{id, buffer});
    }

    void handle_downloads(const my::RenderGraph::ExecutionContext& ctx)
    {
        while (!_downloads.empty())
        {
            Download front = _downloads.front();
            if (ctx.instance->is_texture_download_ready(front.id))
            {
                my::TextureDownloadData data = ctx.instance->retrieve_texture_download(front.id);

                assert(data.format == my::TextureFormat::RGBA32F);
                _last_downloaded_height = *(float*)data.data.get();

                hrz::render::release_texture_download_id(front.id);
                _download_buffer_pool.release(front.buffer);

                _downloads.pop_front();
            }
            else
            {
                break;
            }
        }
    }

    void execute_timed(const my::RenderGraph::ExecutionContext& ctx) override
    {
        handle_downloads(ctx);

        _view_ubo.update(ctx.render);
        _camera_ubo.update(ctx.render);

        render_depth(ctx);
        render_height(ctx);
        prepare_download(ctx);
    }
};

} // namespace

namespace hrz
{

struct CameraHeightSystem
{
    bool disabled;

    my::Renderer::ViewId view;
    std::unique_ptr<CameraHeightComputationPass> pass;

    lm::dmat4 view_matrix;
    lm::dmat4 projection_matrix;
    lm::dmat4 pv_from_main_view;
    lm::dmat4 pv_cc;
    lm::dvec2 view_wmerc;
};

namespace camera_height
{

CameraHeightSystem* create_system()
{
    auto system = new CameraHeightSystem();
    system->disabled = !get_flag(Flag::EnableTerrain);
    system->pass.reset(new CameraHeightComputationPass());

    return system;
}

void destroy_system(CameraHeightSystem* system, Render* render)
{
    assert(system && render);

    system->pass->destroy(render->my);

    delete system;
}

void init_render(CameraHeightSystem* system, hrz::RenderView* render)
{
    assert(system && render);

    render->rg->add_pass("camera height", system->pass.get());
}

void register_view(CameraHeightSystem* system, Render* render)
{
    assert(system && render);

    system->view = render->rd->add_auxiliary_view(
        my::View{system->projection_matrix, system->view_matrix}, hrz::RenderPlanetBin);
    system->pass->set_view(system->view);
}

const char* get_target_name(const CameraHeightSystem* system)
{
    assert(system);

    return system->pass->camera_height_output();
}

void update(CameraHeightSystem* system, const CameraViewInfo& cam_view)
{
    assert(system);

    if (system->disabled) return;

    // Make the camera point directly under the camera, towards the surface
    // of the planet, and use an orthographic projection, in order to always
    // render what is under the camera.

    auto pos_geo = hrz::ecef_to_geo3(cam_view.cam.pos);
    auto enu_to_ecef = hrz::enu_to_ecef_transform_for_geo(pos_geo.latlon());
    lm::dvec3 forward = (enu_to_ecef * lm::dvec4(0, 0, -1, 0)).xyz;
    lm::dvec3 up = (enu_to_ecef * lm::dvec4(0, 1, 0, 0)).xyz;

    lm::dvec3 position = cam_view.cam.pos;

    system->view_matrix = lm::view(position, forward, up);

    // In order to maximise the precision, the far buffer is set to be a bit
    // below the lowest expected terrain elevation values.
    // Therefore the closer the camera is to the planet, the better the precision.
    system->projection_matrix = lm::orthographic_opengl<double>(
        1.0, 1.0, HRZ_S_NEAR, pos_geo.alt + HRZ_S_CAMERA_HEIGHT_FAR_OFFSET);

    lm::dmat4 inverse_cam_view = lm::inverse(cam_view.cam.view);
    lm::dmat4 cam_pos_translation = lm::translation(cam_view.cam.pos);
    lm::dmat4 pv = system->projection_matrix * system->view_matrix;

    system->pv_from_main_view = pv * inverse_cam_view;
    system->pv_cc = pv * cam_pos_translation;

    system->view_wmerc = hrz::geo_to_web_mercator_pixels(hrz::ecef_to_geo2(cam_view.cam.pos));
}

void draw(
    CameraHeightSystem* system,
    Render* render,
    const planet::GeometryResources& planet_geometry_resources)
{
    assert(system && render);
    HRZ_SCOPED_SAMPLE("camera height draw");

    if (!system->disabled)
    {
        system->pass->update_ubos(
            render, system->pv_from_main_view, system->pv_cc, system->view_wmerc);
    }

    system->pass->set_planet_resources(planet_geometry_resources);
}

void collect_shaders(hrz::GpuResourceContext* rc)
{
    CameraHeightComputationPass::collect_shaders(rc);
}

float get_last_downloaded_height(CameraHeightSystem* sys)
{
    return sys->pass->last_downloaded_height();
}

} // namespace camera_height
} // namespace hrz
