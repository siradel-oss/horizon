// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/core/sky.h"

#include "hrz/common/color.h"
#include "hrz/common/geo.h"
#include "hrz/common/monitoring_defs.h"
#include "hrz/common/profiling.h"
#include "hrz/common/proto_geo.h"
#include "hrz/core/download_buffer_pool.h"
#include "hrz/core/global_flags.h"
#include "hrz/core/render/common_ubos.h"
#include "hrz/core/render/context.h"
#include "hrz/core/render/double_buffered_uniform_buffer.h"
#include "hrz/core/render/resource_context.h"
#include "hrz/core/render/timed_render_pass.h"
#include "hrz/core/shaders/collection.h"
#include "hrz/fnd/mem.h"
#include "hrz/fnd/meta.h"
#include "hrz/protocol/path_builder/scene/view_settings.h"

#include <deque>
#include <variant>

namespace
{

enum
{
    UboSkyParams = hrz::UboCustomStart,
};

static constexpr my::RenderGraph::ResourceUsage Target = my::RenderGraph::ResourceUsage::Target;
static constexpr my::RenderGraph::ResourceUsage Sampled = my::RenderGraph::ResourceUsage::Sampled;
static constexpr my::RenderGraph::ResourceUsage TargetSampled =
    my::RenderGraph::ResourceUsage::TargetSampled;

struct FogParamsUboData
{
    lm::vec4 color{0};
    float density{0};
    float start_distance{0};
    float falloff_start{0};
    float falloff_end{0};
    float falloff_factor{0};
    hrz::bool32 enabled{false};
    hrz::bool32 apply_to_sky{false};
    uint32_t _padding[1];
};

HRZ_CHECK_UBO_SIZE(FogParamsUboData);

struct UboData
{
    lm::mat4 sky_box_pv{lm::mat4::identity()};
    lm::mat4 sky_box_inv_rot{lm::mat4::identity()};
    // Relative to the ground
    float altitude{0};
    // Signed angle between the plane tangent to the planet at the camera position and the vector
    // going from the camera to the sun
    float sun_horizon_angle{0};
    float cloudiness{0};
    // Signed angle between the plane tangent to the planet at the camera position and any vector
    // going from the camera to the horizon
    float horizon_horizon_angle{0};
    lm::vec3 ground_normal_view{0, 0, 0};
    float fog_min_depth{0};
    HRZ_UBO_STRUCT_FIELD(FogParamsUboData) fog[2];
    lm::vec4 atmosphere_color{lm::vec4(0.0F)};
    lm::vec4 space_color{lm::vec4(0.0F)};
    lm::vec4 underground_color{lm::vec4(0.0F)};
    float color_transition_start_horizon_angle{0};
    float color_transition_end_horizon_angle{0};
    hrz::bool32 oklab_gradient{false};
    uint32_t _padding;
};

HRZ_CHECK_UBO_SIZE(UboData);

class SkyPrecomputePass : public hrz::render::TimedRenderPass
{
    const char* _sky_view_camera_name;
    const char* _trans_name;
    const char* _aerial_name;
    const char* _sun_color_name;

    my::ResourceHandle _sky_view_camera_lut;
    my::ResourceHandle _sky_view_sea_level_lut;
    my::ResourceHandle _sky_view_fbo;
    my::ResourceHandle _sky_view_shader;
    my::ResourceHandle _sky_view_sampler;

    const hrz::render::DoubleBufferedUniformBuffer<UboData>& _sky_ubo;

    my::ResourceHandle _sun_color;
    my::ResourceHandle _trans_lut;
    my::ResourceHandle _trans_fbo;
    my::ResourceHandle _trans_shader;
    my::ResourceHandle _trans_lut_sampler;

    my::ResourceHandle _aerial_lut;
    my::ResourceHandle _aerial_fbo;
    my::ResourceHandle _aerial_shader;

    my::ResourceHandle _quad_vb;
    my::ResourceHandle _quad_vi;

    my::ResourceHandle _sh_sampler;
    my::ResourceHandle _sh_initial_copy_texture;
    my::ResourceHandle _sh_initial_copy_fbo;
    my::ResourceHandle _sh_initial_copy_shader;
    my::ResourceHandle _sh_first_subsample_texture;
    my::ResourceHandle _sh_first_subsample_fbo;
    my::ResourceHandle _sh_subsample_shader;
    my::ResourceHandle _sh_final_texture;
    my::ResourceHandle _sh_final_fbo;

    bool _enabled = true;
    bool _refresh_transmittance = true;
    bool _refresh_sky_view = true;
    bool _refresh_aerial = true;

    hrz::DownloadBufferPool _sh_readbacks_pool;
    std::deque<std::pair<uint64_t, hrz::DownloadBuffer>> _in_flight_sh_readbacks;

    lm::vec3 _sh_coeffs[9];

public:
    enum
    {
        TransSize = HRZ_S_SKY_TRANSMITTANCE_LUT_SIZE,
        SkyViewSize = HRZ_S_SKY_VIEW_SIZE,
        AerialWidth = HRZ_S_SKY_AERIAL_WIDTH,
        AerialHeight = HRZ_S_SKY_AERIAL_HEIGHT * HRZ_S_SKY_AERIAL_DEPTH,
        ShInitSize = 64 * 3,
    };

    explicit SkyPrecomputePass(const hrz::render::DoubleBufferedUniformBuffer<UboData>& ubo) :
        TimedRenderPass("sky precompute"),
        _sky_view_camera_name("sky_view_camera_lut"),
        _trans_name("sky_transmittance_lut"),
        _aerial_name("aerial_perspective_lut"),
        _sun_color_name("sun_color_lut"),
        _sky_ubo(ubo)
    {
    }

    void set_enabled(bool enabled) { _enabled = enabled; }

    const char* output_sky_view_camera() const { return _sky_view_camera_name; }

    const char* output_transmittance() const { return _trans_name; }

    const char* output_aerial() const { return _aerial_name; }

    const char* output_sun_color() const { return _sun_color_name; }

    const lm::vec3* get_sh_coeffs() const { return _sh_coeffs; }

    void setup_pass(my::RenderGraph::SetupContext& ctx) override
    {
        {
            my::RenderGraph::ResourceInfo color;
            color.format = my::TextureFormat::RGBA16F;
            color.size_class = my::RenderGraph::ResourceInfo::Absolute;
            color.width = SkyViewSize;
            color.height = SkyViewSize;
            ctx.create(_sky_view_camera_name, TargetSampled, color);
        }

        {
            my::RenderGraph::ResourceInfo color;
            color.format = my::TextureFormat::RGBA16F;
            color.size_class = my::RenderGraph::ResourceInfo::Absolute;
            color.width = TransSize;
            color.height = TransSize;
            ctx.create(_trans_name, TargetSampled, color);
            ctx.create(_sun_color_name, Target, color);
        }

        {
            my::RenderGraph::ResourceInfo color;
            color.format = my::TextureFormat::RGBA16F;
            color.size_class = my::RenderGraph::ResourceInfo::Absolute;
            color.width = AerialWidth;
            color.height = AerialHeight;
            ctx.create(_aerial_name, Target, color);
        }
    }

    void retrieve_resources(
        my::Instance* my,
        my::ResourceContext* rc,
        const my::RenderGraph::ResourceContext& ctx) override
    {
        if (!hrz::get_flag(hrz::Flag::EnableAtmosphere)) return;

        _sky_view_camera_lut = ctx.retrieve(_sky_view_camera_name);
        _trans_lut = ctx.retrieve(_trans_name);
        _sun_color = ctx.retrieve(_sun_color_name);
        _aerial_lut = ctx.retrieve(_aerial_name);

        // Sky view LUT at sea level (not created as part of the scene graph because only used
        // locally).
        {
            my::TextureResource res;
            res.data = {};
            res.generate_mipmaps = false;
            res.layout.type = my::TextureLayout::Type::Type2D;
            res.layout.format = my::TextureFormat::RGBA16F;
            res.layout.levels = 1;
            res.layout.width = SkyViewSize;
            res.layout.height = SkyViewSize;
            res.layout.depth = 1;

            _sky_view_sea_level_lut =
                ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::Sky);
        }

        // Sky view framebuffer
        {
            my::FramebufferAttachment attachments[] = {
                {my::Attachment::Color0, _sky_view_camera_lut},
                {my::Attachment::Color1, _sky_view_sea_level_lut},
            };

            my::FramebufferResource res;
            res.attachments = attachments;

            _sky_view_fbo =
                ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::Sky);
        }

        // Fullscreen quad
        {
            lm::vec2 vertices[] = {
                {-1, -1},
                {3, -1},
                {-1, 3},
            };

            my::BufferResource buf_res(my::BufferResource::Vertex);
            buf_res.size = sizeof(vertices);
            buf_res.data = vertices;
            buf_res.usage = my::UsageHint::Static;

            _quad_vb =
                ((hrz::GpuResourceContext*)rc)->alloc(&buf_res, hrz::monitoring::systems::Sky);

            my::VertexInputStream streams[] = {
                {0, _quad_vb, my::VertexFormat::Float32_2, 0, 0, my::VertexRate::PerVertex}
            };

            my::VertexInputResource vi_res;
            vi_res.attribs = streams;

            _quad_vi =
                ((hrz::GpuResourceContext*)rc)->alloc(&vi_res, hrz::monitoring::systems::Sky);
        }

        _sky_view_shader = rc->retrieve_shader(hrz_shaders::SkyViewPrecompute_name);
        _trans_shader = rc->retrieve_shader(hrz_shaders::SkyTransmittancePrecompute_name);

        // Transmittance precomputation framebuffer
        {
            my::FramebufferAttachment attachments[] = {
                {my::Attachment::Color0, _trans_lut},
                {my::Attachment::Color1, _sun_color},
            };

            my::FramebufferResource res;
            res.attachments = attachments;

            _trans_fbo = ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::Sky);
        }

        // Transmittance sampler
        {
            my::SamplerResource res;
            res.use_mipmaps = false;
            res.sampler.min_filter = my::SamplerParams::Filter::Linear;
            res.sampler.mag_filter = my::SamplerParams::Filter::Linear;
            res.sampler.wrap_x = my::SamplerParams::Wrap::Clamp;
            res.sampler.wrap_y = my::SamplerParams::Wrap::Clamp;
            _trans_lut_sampler =
                ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::Sky);
        }

        _aerial_shader = rc->retrieve_shader(hrz_shaders::SkyAerialPrecompute_name);

        // Aerial perspective precomputation framebuffer
        {
            my::FramebufferAttachment attachments[] = {
                {my::Attachment::Color0, _aerial_lut},
            };

            my::FramebufferResource res;
            res.attachments = attachments;

            _aerial_fbo =
                ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::Sky);
        }

        // Sky view & SH samplers
        {
            my::SamplerResource res;
            res.use_mipmaps = false;
            res.sampler.is_shadow = false;
            res.sampler.mag_filter = my::SamplerParams::Filter::Nearest;
            res.sampler.min_filter = my::SamplerParams::Filter::Nearest;
            res.sampler.wrap_x = my::SamplerParams::Wrap::Mirror;
            res.sampler.wrap_y = my::SamplerParams::Wrap::Mirror;

            _sh_sampler =
                ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::Sky);

            if (my->get_info().has_texture_float_linear)
            {
                res.sampler.mag_filter = my::SamplerParams::Filter::Linear;
                res.sampler.min_filter = my::SamplerParams::Filter::Linear;
            }
            _sky_view_sampler =
                ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::Sky);
        }

        // SH textures & framebuffers
        {
            my::TextureResource res;
            res.data = {};
            res.generate_mipmaps = false;
            res.layout.type = my::TextureLayout::Type::Type2D;
            res.layout.format = my::TextureFormat::RGBA32F;
            res.layout.levels = 1;
            res.layout.width = ShInitSize;
            res.layout.height = ShInitSize;
            res.layout.depth = 1;

            _sh_initial_copy_texture =
                ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::Sky);

            my::FramebufferAttachment attachments[] = {
                {my::Attachment::Color0, _sh_initial_copy_texture},
            };

            my::FramebufferResource res_fbo;
            res_fbo.attachments = attachments;

            _sh_initial_copy_fbo =
                ((hrz::GpuResourceContext*)rc)->alloc(&res_fbo, hrz::monitoring::systems::Sky);

            res.layout.width = ShInitSize / 4;
            res.layout.height = ShInitSize / 4;
            _sh_first_subsample_texture =
                ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::Sky);
            attachments[0].texture_or_renderbuffer = _sh_first_subsample_texture;
            _sh_first_subsample_fbo =
                ((hrz::GpuResourceContext*)rc)->alloc(&res_fbo, hrz::monitoring::systems::Sky);

            res.layout.width = ShInitSize / 16;
            res.layout.height = ShInitSize / 16;
            _sh_final_texture =
                ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::Sky);
            attachments[0].texture_or_renderbuffer = _sh_final_texture;
            _sh_final_fbo =
                ((hrz::GpuResourceContext*)rc)->alloc(&res_fbo, hrz::monitoring::systems::Sky);
        }

        _sh_initial_copy_shader = rc->retrieve_shader(hrz_shaders::SkyEnvShInit_name);
        _sh_subsample_shader = rc->retrieve_shader(hrz_shaders::SkyEnvShSubsample_name);
    }

    static void collect_shaders(hrz::GpuResourceContext* rc)
    {
        if (!hrz::get_flag(hrz::Flag::EnableAtmosphere))
        {
            return;
        }

        // Sky view shader
        {
            static const my::IndexName attribs[] = {
                {0, "i_pos"},
            };

            static const my::IndexName samplers[] = {
                {0, "u_transmittance_lut"},
            };

            static const my::IndexName uniform_blocks[] = {
                {UboSkyParams, "SkyParams"},
            };

            static const char* outputs[] = {"o_color_camera", "o_color_sea_level"};

            my::ShaderResource res{};
            res.name = hrz_shaders::SkyViewPrecompute_name;
            res.link_hint = my::ShaderLinkHint::Initial;
            res.vertex_source_len = hrz_shaders::SkyViewPrecompute_vert_len;
            res.vertex_source = hrz_shaders::SkyViewPrecompute_vert;
            res.fragment_source_len = hrz_shaders::SkyViewPrecompute_frag_len;
            res.fragment_source = hrz_shaders::SkyViewPrecompute_frag;
            res.attribs = attribs;
            res.uniform_blocks = uniform_blocks;
            res.samplers = samplers;
            res.outputs = outputs;
            res.initial_state.rasterization.cull_mode = my::RasterizationState::None;
            res.initial_state.depth.test = false;
            res.initial_state.depth.write = false;
            res.initial_state.stencil.enable = false;
            res.initial_state.color_blend.enable = false;
            res.initial_state.color_blend.mask = my::ColorBlendState::RGBA;

            rc->alloc(&res, hrz::monitoring::systems::Sky);
        }

        // Transmittance shader
        {
            static const my::IndexName attribs[] = {
                {0, "i_pos"},
            };

            static const char* outputs[] = {"o_transmittance_lut", "o_sun_color"};

            static const my::IndexName uniform_blocks[] = {
                {UboSkyParams, "SkyParams"},
            };

            my::ShaderResource res{};
            res.name = hrz_shaders::SkyTransmittancePrecompute_name;
            res.link_hint = my::ShaderLinkHint::Initial;
            res.vertex_source_len = hrz_shaders::SkyTransmittancePrecompute_vert_len;
            res.vertex_source = hrz_shaders::SkyTransmittancePrecompute_vert;
            res.fragment_source_len = hrz_shaders::SkyTransmittancePrecompute_frag_len;
            res.fragment_source = hrz_shaders::SkyTransmittancePrecompute_frag;
            res.attribs = attribs;
            res.uniform_blocks = uniform_blocks;
            res.samplers = {};
            res.outputs = outputs;
            res.initial_state.rasterization.cull_mode = my::RasterizationState::None;
            res.initial_state.depth.test = false;
            res.initial_state.depth.write = false;
            res.initial_state.stencil.enable = false;
            res.initial_state.color_blend.enable = false;
            res.initial_state.color_blend.mask = my::ColorBlendState::RGBA;

            rc->alloc(&res, hrz::monitoring::systems::Sky);
        }

        // Aerial perspective shader
        {
            static const my::IndexName attribs[] = {
                {0, "i_pos"},
            };

            static const char* outputs[] = {"o_color"};

            static const my::IndexName uniform_blocks[] = {
                {hrz::UboFrame, "Frame"},
                {UboSkyParams, "SkyParams"},
            };

            static const my::IndexName samplers[] = {
                {0, "u_transmittance_lut"},
            };

            my::ShaderResource res{};
            res.name = hrz_shaders::SkyAerialPrecompute_name;
            res.link_hint = my::ShaderLinkHint::Initial;
            res.vertex_source_len = hrz_shaders::SkyAerialPrecompute_vert_len;
            res.vertex_source = hrz_shaders::SkyAerialPrecompute_vert;
            res.fragment_source_len = hrz_shaders::SkyAerialPrecompute_frag_len;
            res.fragment_source = hrz_shaders::SkyAerialPrecompute_frag;
            res.attribs = attribs;
            res.uniform_blocks = uniform_blocks;
            res.samplers = samplers;
            res.outputs = outputs;
            res.initial_state.rasterization.cull_mode = my::RasterizationState::None;
            res.initial_state.depth.test = false;
            res.initial_state.depth.write = false;
            res.initial_state.stencil.enable = false;
            res.initial_state.color_blend.enable = false;
            res.initial_state.color_blend.mask = my::ColorBlendState::RGBA;

            rc->alloc(&res, hrz::monitoring::systems::Sky);
        }

        // Environment spherical harmonics initial copy shader
        {
            static const my::IndexName attribs[] = {
                {0, "i_pos"},
            };

            static const char* outputs[] = {"o_color"};

            static const my::IndexName uniform_blocks[] = {
                {UboSkyParams, "SkyParams"},
            };

            static const my::IndexName samplers[] = {
                {0, "u_sky_view"},
            };

            my::ShaderResource res{};
            res.name = hrz_shaders::SkyEnvShInit_name;
            res.link_hint = my::ShaderLinkHint::Initial;
            res.vertex_source_len = hrz_shaders::SkyEnvShInit_vert_len;
            res.vertex_source = hrz_shaders::SkyEnvShInit_vert;
            res.fragment_source_len = hrz_shaders::SkyEnvShInit_frag_len;
            res.fragment_source = hrz_shaders::SkyEnvShInit_frag;
            res.attribs = attribs;
            res.uniform_blocks = uniform_blocks;
            res.samplers = samplers;
            res.outputs = outputs;
            res.initial_state.rasterization.cull_mode = my::RasterizationState::None;
            res.initial_state.depth.test = false;
            res.initial_state.depth.write = false;
            res.initial_state.stencil.enable = false;
            res.initial_state.color_blend.enable = false;
            res.initial_state.color_blend.mask = my::ColorBlendState::RGBA;

            rc->alloc(&res, hrz::monitoring::systems::Sky);
        }

        // Environment spherical harmonics subsample shader
        {
            static const my::IndexName attribs[] = {
                {0, "i_pos"},
            };

            static const char* outputs[] = {"o_color"};

            static const my::IndexName samplers[] = {
                {0, "u_previous"},
            };

            my::ShaderResource res{};
            res.name = hrz_shaders::SkyEnvShSubsample_name;
            res.link_hint = my::ShaderLinkHint::Initial;
            res.vertex_source_len = hrz_shaders::SkyEnvShSubsample_vert_len;
            res.vertex_source = hrz_shaders::SkyEnvShSubsample_vert;
            res.fragment_source_len = hrz_shaders::SkyEnvShSubsample_frag_len;
            res.fragment_source = hrz_shaders::SkyEnvShSubsample_frag;
            res.attribs = attribs;
            res.uniform_blocks = {};
            res.samplers = samplers;
            res.outputs = outputs;
            res.initial_state.rasterization.cull_mode = my::RasterizationState::None;
            res.initial_state.depth.test = false;
            res.initial_state.depth.write = false;
            res.initial_state.stencil.enable = false;
            res.initial_state.color_blend.enable = false;
            res.initial_state.color_blend.mask = my::ColorBlendState::RGBA;

            rc->alloc(&res, hrz::monitoring::systems::Sky);
        }
    }

    void destroy(hrz::Render* render)
    {
        render->rc->dealloc(_sky_view_fbo);
        render->rc->dealloc(_sky_view_sea_level_lut);
        render->rc->dealloc(_trans_fbo);
        render->rc->dealloc(_trans_lut_sampler);
        render->rc->dealloc(_quad_vb);
        render->rc->dealloc(_quad_vi);
        render->rc->dealloc(_aerial_fbo);
        render->rc->dealloc(_sh_sampler);
        render->rc->dealloc(_sh_initial_copy_texture);
        render->rc->dealloc(_sh_initial_copy_fbo);
        render->rc->dealloc(_sh_first_subsample_texture);
        render->rc->dealloc(_sh_first_subsample_fbo);
        render->rc->dealloc(_sh_final_texture);
        render->rc->dealloc(_sh_final_fbo);
        render->rc->dealloc(_sky_view_sampler);

        for (const auto& entry : _in_flight_sh_readbacks)
        {
            render->my->cancel_texture_download(entry.first);
            _sh_readbacks_pool.release(entry.second);
        }

        _in_flight_sh_readbacks.clear();
        _sh_readbacks_pool.free_all(render->my);
    }

    void schedule_refresh_sky_view()
    {
        _refresh_sky_view = true;
        _refresh_aerial = true;
    }

    void schedule_refresh_transmittance()
    {
        _refresh_sky_view = true;
        _refresh_transmittance = true;
        _refresh_aerial = true;
    }

    void schedule_refresh_aerial() { _refresh_aerial = true; }

    hrz::DownloadBuffer acquire_sh_readback_buffer(const my::RenderGraph::ExecutionContext& ctx)
    {
        return _sh_readbacks_pool.acquire(
            (hrz::GpuResourceContext*)ctx.resource,
            sizeof(lm::vec4) * ShInitSize * ShInitSize / 16 / 16, {hrz::monitoring::systems::Sky});
    }

    void request_render(hrz::RenderRequest& request) const
    {
        if (_enabled && (_refresh_aerial || _refresh_sky_view || _refresh_transmittance))
        {
            request.request_visual_render();
            return;
        }

        if (!_in_flight_sh_readbacks.empty())
        {
            request.request_visual_render();
            return;
        }
    }

    void execute_timed(const my::RenderGraph::ExecutionContext& ctx) override
    {
        HRZ_SCOPED_SAMPLE("precompute sky");

        if (!_enabled) return;

        if (_refresh_transmittance)
        {
            HRZ_SCOPED_SAMPLE("refresh transmittance");

            ctx.render->set_framebuffer(
                _trans_fbo,
                my::ViewportState{{0, 0, TransSize, TransSize}, {0, 0, TransSize, TransSize}});

            static const auto batch_info = my::DrawBatchInfo(my::PrimitiveType::TriangleList, 3);

            const my::UboBinding ubo_bindings[] = {
                {UboSkyParams, _sky_ubo.get_for_gpu(), 0, sizeof(UboData)},
            };

            ctx.render->draw(batch_info, _trans_shader, _quad_vi, ubo_bindings, {});

            _refresh_transmittance = false;
        }

        if (_refresh_sky_view)
        {
            HRZ_SCOPED_SAMPLE("refresh sky view");

            {
                HRZ_SCOPED_SAMPLE("sky view");

                ctx.render->set_framebuffer(
                    _sky_view_fbo,
                    my::ViewportState{
                        {0, 0, SkyViewSize, SkyViewSize},
                        {0, 0, SkyViewSize, SkyViewSize}
                    });

                static const auto batch_info =
                    my::DrawBatchInfo(my::PrimitiveType::TriangleList, 3);

                const my::TextureBinding texture_bindings[] = {
                    {0, _trans_lut, _trans_lut_sampler},
                };

                const my::UboBinding ubo_bindings[] = {
                    {UboSkyParams, _sky_ubo.get_for_gpu(), 0, sizeof(UboData)},
                };

                ctx.render->draw(
                    batch_info, _sky_view_shader, _quad_vi, ubo_bindings, texture_bindings);

                _refresh_sky_view = false;
            }

            {
                ctx.render->set_framebuffer(
                    _sh_initial_copy_fbo,
                    my::ViewportState{
                        {0, 0, ShInitSize, ShInitSize},
                        {0, 0, ShInitSize, ShInitSize}
                    });

                static const auto batch_info =
                    my::DrawBatchInfo(my::PrimitiveType::TriangleList, 3);

                const my::TextureBinding texture_bindings[] = {
                    {0, _sky_view_sea_level_lut, _sky_view_sampler},
                };

                const my::UboBinding ubo_bindings[] = {
                    {UboSkyParams, _sky_ubo.get_for_gpu(), 0, sizeof(UboData)},
                };

                ctx.render->draw(
                    batch_info, _sh_initial_copy_shader, _quad_vi, ubo_bindings, texture_bindings);
            }

            {
                ctx.render->set_framebuffer(
                    _sh_first_subsample_fbo,
                    my::ViewportState{
                        {0, 0, ShInitSize / 4, ShInitSize / 4},
                        {0, 0, ShInitSize / 4, ShInitSize / 4}
                    });

                static const auto batch_info =
                    my::DrawBatchInfo(my::PrimitiveType::TriangleList, 3);

                const my::TextureBinding texture_bindings[] = {
                    {0, _sh_initial_copy_texture, _sh_sampler},
                };

                ctx.render->draw(batch_info, _sh_subsample_shader, _quad_vi, {}, texture_bindings);
            }

            {
                ctx.render->set_framebuffer(
                    _sh_final_fbo,
                    my::ViewportState{
                        {0, 0, ShInitSize / 16, ShInitSize / 16},
                        {0, 0, ShInitSize / 16, ShInitSize / 16}
                    });

                static const auto batch_info =
                    my::DrawBatchInfo(my::PrimitiveType::TriangleList, 3);

                const my::TextureBinding texture_bindings[] = {
                    {0, _sh_first_subsample_texture, _sh_sampler},
                };

                ctx.render->draw(batch_info, _sh_subsample_shader, _quad_vi, {}, texture_bindings);
            }

            {
                HRZ_SCOPED_SAMPLE("sh download");
                hrz::DownloadBuffer readback_buffer = acquire_sh_readback_buffer(ctx);
                uint64_t download_id = hrz::render::acquire_texture_download_id();

                ctx.render->color_texture_download_async(
                    download_id, _sh_final_fbo, my::Attachment::Color0,
                    my::Rect{0, 0, ShInitSize / 16, ShInitSize / 16},
                    my::TextureDownloadFormat::RGBA32F, readback_buffer.buffer);

                _in_flight_sh_readbacks.push_back(std::make_pair(download_id, readback_buffer));
            }
        }

        if (_refresh_aerial)
        {
            HRZ_SCOPED_SAMPLE("refresh aerial");

            ctx.render->set_framebuffer(
                _aerial_fbo,
                my::ViewportState{
                    {0, 0, AerialWidth, AerialHeight},
                    {0, 0, AerialWidth, AerialHeight}
                });

            static const auto batch_info = my::DrawBatchInfo(my::PrimitiveType::TriangleList, 3);

            const my::TextureBinding texture_bindings[] = {
                {0, _trans_lut, _trans_lut_sampler},
            };

            const my::UboBinding ubo_bindings[] = {
                {UboSkyParams, _sky_ubo.get_for_gpu(), 0, sizeof(UboData)},
            };

            ctx.binder->push_state();
            ctx.binder->bind(ubo_bindings);
            auto state = ctx.binder->get_current_state();

            ctx.render->draw(batch_info, _aerial_shader, _quad_vi, state.ubos, texture_bindings);

            ctx.binder->pop_state();

            _refresh_aerial = false;
        }
    }

    bool update(hrz::Render* render)
    {
        bool needs_render_request = false;

        while (!_in_flight_sh_readbacks.empty())
        {
            const std::pair<uint64_t, hrz::DownloadBuffer>& download =
                _in_flight_sh_readbacks.front();

            if (render->my->is_texture_download_ready(download.first))
            {
                my::TextureDownloadData dl_data =
                    render->my->retrieve_texture_download(download.first);

                // We need to do a final sum on the 4x4 tiles, because it was not worth dispatching
                // another draw call for so few pixels...

                std::unique_ptr<lm::vec4[]> data(new lm::vec4[ShInitSize * ShInitSize / 16 / 16]);
                memcpy(
                    data.get(), dl_data.data.get(),
                    sizeof(lm::vec4) * ShInitSize / 16 * ShInitSize / 16);

                for (int tile = 0; tile < 9; ++tile)
                {
                    int tile_x = tile % 3;
                    int tile_y = tile / 3;

                    lm::vec4 result(0.0F);

                    for (int y = 0; y < 4; ++y)
                    {
                        const lm::vec4* ptr = data.get() + tile_x * 4 + (tile_y * 4 + y) * 12;
                        result += ptr[0] + ptr[1] + ptr[2] + ptr[3];
                    }

                    _sh_coeffs[tile] = result.rgb / 16.0F;
                }

                _sh_readbacks_pool.release(download.second);
                _in_flight_sh_readbacks.pop_front();
                needs_render_request = true;
            }
            else
            {
                break;
            }
        }

        return needs_render_request;
    }
};

class BackgroundSkyRenderPass : public hrz::render::TimedRenderPass
{
    const char* _world_color_target_name;
    const char* _sky_view_lut_name;
    const char* _transmittance_lut_name;
    const char* _color_output_target_name;

    my::ResourceHandle _color_target;
    my::ResourceHandle _sky_view_lut;
    my::ResourceHandle _transmittance_lut;

    my::ResourceHandle _fbo;
    my::ResourceHandle _sky_box_vi;
    my::ResourceHandle _sky_box_vb;
    my::ResourceHandle _sky_box_ib;
    const hrz::render::DoubleBufferedUniformBuffer<UboData>& _sky_ubo;
    my::ResourceHandle _sky_view_sampler;
    my::ResourceHandle _sky_view_background_shader;
    my::ResourceHandle _simple_background_shader;
    my::ResourceHandle _transmittance_sampler;

    bool _atmosphere_enabled = true;

public:
    BackgroundSkyRenderPass(
        const hrz::render::DoubleBufferedUniformBuffer<UboData>& ubo,
        const char* world_color_target_name,
        const char* sky_view_lut_name,
        const char* transmittance_lut_name) :
        TimedRenderPass("sky background"),
        _world_color_target_name(world_color_target_name),
        _sky_view_lut_name(sky_view_lut_name),
        _transmittance_lut_name(transmittance_lut_name),
        _color_output_target_name("background with sky"),
        _sky_ubo(ubo)
    {
    }

    void set_atmosphere_enabled(bool enabled) { _atmosphere_enabled = enabled; }

    const char* output_color() { return _color_output_target_name; }

    void setup_pass(my::RenderGraph::SetupContext& ctx) override
    {
        ctx.read(_sky_view_lut_name, Sampled);
        ctx.read(_transmittance_lut_name, Sampled);
        ctx.read_write(_world_color_target_name, Target, _color_output_target_name);
    }

    void retrieve_resources(
        my::Instance* my,
        my::ResourceContext* rc,
        const my::RenderGraph::ResourceContext& ctx) override
    {
        _color_target = ctx.retrieve(_color_output_target_name);
        _sky_view_lut = ctx.retrieve(_sky_view_lut_name);
        _transmittance_lut = ctx.retrieve(_transmittance_lut_name);

        // Framebuffer
        {
            my::FramebufferAttachment attachments[] = {
                {my::Attachment::Color0, _color_target},
            };

            my::FramebufferResource res;
            res.attachments = attachments;

            _fbo = ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::Sky);
        }

        // Sky box
        {
            lm::vec3 vertices[] = {
                {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
                {-1, -1, 1},  {1, -1, 1},  {1, 1, 1},  {-1, 1, 1},
            };

            uint8_t indices[] = {
                0, 1, 2, 0, 2, 3, 0, 4, 5, 0, 5, 1, 1, 6, 2, 1, 5, 6,
                3, 2, 6, 3, 6, 7, 0, 3, 7, 0, 7, 4, 4, 6, 5, 4, 7, 6,
            };

            my::BufferResource buf_res(my::BufferResource::Vertex);
            buf_res.size = sizeof(vertices);
            buf_res.data = vertices;
            buf_res.usage = my::UsageHint::Static;
            _sky_box_vb =
                ((hrz::GpuResourceContext*)rc)->alloc(&buf_res, hrz::monitoring::systems::Sky);

            my::BufferResource ind_res(my::BufferResource::Index);
            ind_res.size = sizeof(indices);
            ind_res.data = indices;
            ind_res.usage = my::UsageHint::Static;
            _sky_box_ib =
                ((hrz::GpuResourceContext*)rc)->alloc(&ind_res, hrz::monitoring::systems::Sky);

            my::VertexInputStream streams[] = {
                {0, _sky_box_vb, my::VertexFormat::Float32_3, 0, 0, my::VertexRate::PerVertex}
            };

            my::VertexInputResource vi_res;
            vi_res.indices = _sky_box_ib;
            vi_res.attribs = streams;
            _sky_box_vi =
                ((hrz::GpuResourceContext*)rc)->alloc(&vi_res, hrz::monitoring::systems::Sky);
        }

        // Sky view sampler
        {
            my::SamplerResource res;
            res.use_mipmaps = false;
            res.sampler.min_filter = my::SamplerParams::Filter::Linear;
            res.sampler.mag_filter = my::SamplerParams::Filter::Linear;
            res.sampler.wrap_x = my::SamplerParams::Wrap::Mirror;
            res.sampler.wrap_y = my::SamplerParams::Wrap::Mirror;
            _sky_view_sampler =
                ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::Sky);
        }

        // Transmittance sampler
        {
            my::SamplerResource res;
            res.use_mipmaps = false;
            res.sampler.min_filter = my::SamplerParams::Filter::Linear;
            res.sampler.mag_filter = my::SamplerParams::Filter::Linear;
            res.sampler.wrap_x = my::SamplerParams::Wrap::Clamp;
            res.sampler.wrap_y = my::SamplerParams::Wrap::Clamp;
            _transmittance_sampler =
                ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::Sky);
        }

        if (hrz::get_flag(hrz::Flag::EnableAtmosphere))
        {
            _sky_view_background_shader =
                rc->retrieve_shader(hrz_shaders::SkyRenderBackgroundSkyView_name);
        }
        _simple_background_shader =
            rc->retrieve_shader(hrz_shaders::SkyRenderBackgroundSimple_name);
    }

    static void collect_shaders(hrz::GpuResourceContext* rc)
    {
        // Sky view shader
        if (hrz::get_flag(hrz::Flag::EnableAtmosphere))
        {
            static const my::IndexName attribs[] = {
                {0, "i_pos"},
            };

            static const my::IndexName samplers[] = {
                {0, "u_sky_view"},
                {1, "u_transmittance_lut"},
            };

            static const char* outputs[] = {"o_color"};

            static const my::IndexName uniform_blocks[] = {
                {hrz::UboFrame, "Frame"},
                {UboSkyParams, "SkyParams"},
            };

            my::ShaderResource res{};
            res.name = hrz_shaders::SkyRenderBackgroundSkyView_name;
            res.link_hint = my::ShaderLinkHint::Initial;
            res.vertex_source_len = hrz_shaders::SkyRenderBackgroundSkyView_vert_len;
            res.vertex_source = hrz_shaders::SkyRenderBackgroundSkyView_vert;
            res.fragment_source_len = hrz_shaders::SkyRenderBackgroundSkyView_frag_len;
            res.fragment_source = hrz_shaders::SkyRenderBackgroundSkyView_frag;
            res.attribs = attribs;
            res.uniform_blocks = uniform_blocks;
            res.samplers = samplers;
            res.outputs = outputs;
            res.initial_state.rasterization.cull_mode = my::RasterizationState::None;
            res.initial_state.depth.test = false;
            res.initial_state.depth.write = false;
            res.initial_state.stencil.enable = false;
            res.initial_state.color_blend.enable = false;

            rc->alloc(&res, hrz::monitoring::systems::Sky);
        }

        {
            static const my::IndexName attribs[] = {
                {0, "i_pos"},
            };

            static const char* outputs[] = {"o_color"};

            static const my::IndexName uniform_blocks[] = {
                {hrz::UboFrame, "Frame"},
                {UboSkyParams, "SkyParams"},
            };

            my::ShaderResource res{};
            res.name = hrz_shaders::SkyRenderBackgroundSimple_name;
            res.link_hint = my::ShaderLinkHint::Initial;
            res.vertex_source_len = hrz_shaders::SkyRenderBackgroundSimple_vert_len;
            res.vertex_source = hrz_shaders::SkyRenderBackgroundSimple_vert;
            res.fragment_source_len = hrz_shaders::SkyRenderBackgroundSimple_frag_len;
            res.fragment_source = hrz_shaders::SkyRenderBackgroundSimple_frag;
            res.attribs = attribs;
            res.uniform_blocks = uniform_blocks;
            res.samplers = {};
            res.outputs = outputs;
            res.initial_state.rasterization.cull_mode = my::RasterizationState::None;
            res.initial_state.depth.test = false;
            res.initial_state.depth.write = false;
            res.initial_state.stencil.enable = false;
            res.initial_state.color_blend.enable = false;
            res.initial_state.color_blend.mask = my::ColorBlendState::RGBA;

            rc->alloc(&res, hrz::monitoring::systems::Sky);
        }
    }

    void destroy(hrz::Render* render)
    {
        render->rc->dealloc(_fbo);
        render->rc->dealloc(_sky_box_vb);
        render->rc->dealloc(_sky_box_vi);
        render->rc->dealloc(_sky_box_ib);
        render->rc->dealloc(_sky_view_sampler);
        render->rc->dealloc(_transmittance_sampler);
    }

    void execute_timed(const my::RenderGraph::ExecutionContext& ctx) override
    {
        HRZ_SCOPED_SAMPLE("render sky");

        const my::ViewportState viewport_state = {
            {0, 0, ctx.backbuffer_width, ctx.backbuffer_height},
            {0, 0, ctx.backbuffer_width, ctx.backbuffer_height},
        };

        ctx.render->set_framebuffer(_fbo, viewport_state);

        static const auto batch_info =
            my::DrawBatchInfo(my::PrimitiveType::TriangleList, 36).indexed(my::IndexType::UByte);

        ctx.binder->push_state();

        const my::TextureBinding texture_bindings[] = {
            {0, _sky_view_lut, _sky_view_sampler},
            {1, _transmittance_lut, _transmittance_sampler},
        };

        const my::UboBinding ubo_bindings[] = {
            {UboSkyParams, _sky_ubo.get_for_gpu(), 0, sizeof(UboData)},
        };

        if (_atmosphere_enabled)
        {
            ctx.binder->bind(texture_bindings);
        }

        ctx.binder->bind(ubo_bindings);

        auto state = ctx.binder->get_current_state();

        auto shader =
            (_atmosphere_enabled) ? _sky_view_background_shader : _simple_background_shader;

        ctx.render->draw(batch_info, shader, _sky_box_vi, state.ubos, state.textures);

        ctx.binder->pop_state();
    }
};

class WorldSkyRenderPass : public hrz::render::TimedRenderPass
{
    const char* _color_input_name;
    const char* _depth_input_name;
    const char* _sky_view_lut_name;
    const char* _aerial_lut_name;
    const char* _color_output_name;
    const char* _depth_output_name;

    my::ResourceHandle _color_input;
    my::ResourceHandle _color_output;
    my::ResourceHandle _depth_input;
    my::ResourceHandle _sky_view_lut;
    my::ResourceHandle _aerial_lut;

    my::ResourceHandle _fbo;
    my::ResourceHandle _blit_src_fbo;
    my::ResourceHandle _sky_box_vi;
    my::ResourceHandle _sky_box_vb;
    my::ResourceHandle _sky_box_ib;
    const hrz::render::DoubleBufferedUniformBuffer<UboData>& _sky_ubo;
    my::ResourceHandle _sky_view_sampler;
    my::ResourceHandle _sky_view_shader;
    my::ResourceHandle _fog_shader;
    my::ResourceHandle _aerial_sampler;
    my::ResourceHandle _depth_color_sampler;

    bool _fog_enabled = false;
    bool _sky_view_enabled = true;

public:
    WorldSkyRenderPass(
        const hrz::render::DoubleBufferedUniformBuffer<UboData>& ubo,
        const char* color_input_name,
        const char* depth_input_name,
        const char* sky_view_lut_name,
        const char* aerial_lut_name) :
        TimedRenderPass("sky world"),
        _color_input_name(color_input_name),
        _depth_input_name(depth_input_name),
        _sky_view_lut_name(sky_view_lut_name),
        _aerial_lut_name(aerial_lut_name),
        _color_output_name("sky render color output"),
        _depth_output_name("sky render depth output"),
        _sky_ubo(ubo)
    {
    }

    void set_fog_enabled(bool enabled) { _fog_enabled = enabled; }

    void set_sky_view_enabled(bool enabled) { _sky_view_enabled = enabled; }

    const char* output_color() { return _color_output_name; }

    const char* output_depth() { return _depth_output_name; }

    void setup_pass(my::RenderGraph::SetupContext& ctx) override
    {
        ctx.read(_sky_view_lut_name, Sampled);
        ctx.read(_aerial_lut_name, Sampled);
        ctx.read(_color_input_name, Sampled);
        ctx.read_write(_depth_input_name, Sampled, _depth_output_name);

        my::RenderGraph::ResourceInfo color;
        color.format = my::TextureFormat::SRGBA8;
        color.size_class = my::RenderGraph::ResourceInfo::BackbufferRelative;
        color.width = 1;
        color.height = 1;

        ctx.create(_color_output_name, Target, color);
    }

    void retrieve_resources(
        my::Instance* my,
        my::ResourceContext* rc,
        const my::RenderGraph::ResourceContext& ctx) override
    {
        _color_output = ctx.retrieve(_color_output_name);
        _color_input = ctx.retrieve(_color_input_name);
        _depth_input = ctx.retrieve(_depth_input_name);
        _sky_view_lut = ctx.retrieve(_sky_view_lut_name);
        _aerial_lut = ctx.retrieve(_aerial_lut_name);

        // Framebuffer
        {
            my::FramebufferAttachment attachments[] = {
                {my::Attachment::Color0, _color_output},
            };

            my::FramebufferResource res;
            res.attachments = attachments;

            _fbo = ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::Sky);
        }

        // Source framebuffer for blitting when atmosphere is disabled
        {
            my::FramebufferAttachment attachments[] = {
                {my::Attachment::Color0, _color_input},
            };

            my::FramebufferResource res;
            res.attachments = attachments;

            _blit_src_fbo =
                ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::Sky);
        }

        // Sky box
        {
            lm::vec3 vertices[] = {
                {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
                {-1, -1, 1},  {1, -1, 1},  {1, 1, 1},  {-1, 1, 1},
            };

            uint8_t indices[] = {
                0, 1, 2, 0, 2, 3, 0, 4, 5, 0, 5, 1, 1, 6, 2, 1, 5, 6,
                3, 2, 6, 3, 6, 7, 0, 3, 7, 0, 7, 4, 4, 6, 5, 4, 7, 6,
            };

            my::BufferResource buf_res(my::BufferResource::Vertex);
            buf_res.size = sizeof(vertices);
            buf_res.data = vertices;
            buf_res.usage = my::UsageHint::Static;
            _sky_box_vb =
                ((hrz::GpuResourceContext*)rc)->alloc(&buf_res, hrz::monitoring::systems::Sky);

            my::BufferResource ind_res(my::BufferResource::Index);
            ind_res.size = sizeof(indices);
            ind_res.data = indices;
            ind_res.usage = my::UsageHint::Static;
            _sky_box_ib =
                ((hrz::GpuResourceContext*)rc)->alloc(&ind_res, hrz::monitoring::systems::Sky);

            my::VertexInputStream streams[] = {
                {0, _sky_box_vb, my::VertexFormat::Float32_3, 0, 0, my::VertexRate::PerVertex}
            };

            my::VertexInputResource vi_res;
            vi_res.indices = _sky_box_ib;
            vi_res.attribs = streams;
            _sky_box_vi =
                ((hrz::GpuResourceContext*)rc)->alloc(&vi_res, hrz::monitoring::systems::Sky);
        }

        // Sky view sampler
        {
            my::SamplerResource res;
            res.use_mipmaps = false;
            res.sampler.min_filter = my::SamplerParams::Filter::Linear;
            res.sampler.mag_filter = my::SamplerParams::Filter::Linear;
            res.sampler.wrap_x = my::SamplerParams::Wrap::Mirror;
            res.sampler.wrap_y = my::SamplerParams::Wrap::Mirror;
            _sky_view_sampler =
                ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::Sky);
        }

        // Aerial perspective sampler
        {
            my::SamplerResource res;
            res.use_mipmaps = false;
            res.sampler.min_filter = my::SamplerParams::Filter::Linear;
            res.sampler.mag_filter = my::SamplerParams::Filter::Linear;
            res.sampler.wrap_x = my::SamplerParams::Wrap::Clamp;
            res.sampler.wrap_y = my::SamplerParams::Wrap::Clamp;
            _aerial_sampler =
                ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::Sky);
        }

        // Depth color sampler
        {
            my::SamplerResource res;
            res.use_mipmaps = false;
            res.sampler.min_filter = my::SamplerParams::Filter::Nearest;
            res.sampler.mag_filter = my::SamplerParams::Filter::Nearest;
            res.sampler.wrap_x = my::SamplerParams::Wrap::Clamp;
            res.sampler.wrap_y = my::SamplerParams::Wrap::Clamp;
            _depth_color_sampler =
                ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::Sky);
        }

        _fog_shader = rc->retrieve_shader(hrz_shaders::SkyRenderWorldFog_name);
        if (hrz::get_flag(hrz::Flag::EnableAtmosphere))
        {
            _sky_view_shader = rc->retrieve_shader(hrz_shaders::SkyRenderWorldSkyView_name);
        }
    }

    static void collect_shaders(hrz::GpuResourceContext* rc)
    {
        if (hrz::get_flag(hrz::Flag::EnableAtmosphere))
        {
            static const my::IndexName attribs[] = {
                {0, "i_pos"},
            };

            static const my::IndexName samplers[] = {
                {0, "u_sky_view"},
                {1, "u_aerial_lut"},
                {2, "u_depth"},
                {3, "u_color"},
            };

            static const char* outputs[] = {"o_color"};

            static const my::IndexName uniform_blocks[] = {
                {hrz::UboFrame, "Frame"},
                {UboSkyParams, "SkyParams"},
            };

            my::ShaderResource res{};
            res.name = hrz_shaders::SkyRenderWorldSkyView_name;
            res.link_hint = my::ShaderLinkHint::Initial;
            res.vertex_source_len = hrz_shaders::SkyRenderWorldSkyView_vert_len;
            res.vertex_source = hrz_shaders::SkyRenderWorldSkyView_vert;
            res.fragment_source_len = hrz_shaders::SkyRenderWorldSkyView_frag_len;
            res.fragment_source = hrz_shaders::SkyRenderWorldSkyView_frag;
            res.attribs = attribs;
            res.uniform_blocks = uniform_blocks;
            res.samplers = samplers;
            res.outputs = outputs;
            res.initial_state.rasterization.cull_mode = my::RasterizationState::None;
            res.initial_state.depth.test = false;
            res.initial_state.depth.write = false;
            res.initial_state.depth.compare = my::DepthState::LessEqual;
            res.initial_state.stencil.enable = false;
            res.initial_state.color_blend.enable = false;
            res.initial_state.color_blend.mask = my::ColorBlendState::RGBA;

            rc->alloc(&res, hrz::monitoring::systems::Sky);
        }

        {
            static const my::IndexName attribs[] = {
                {0, "i_pos"},
            };

            static const my::IndexName samplers[] = {
                {0, "u_depth"},
                {1, "u_color"},
            };

            static const char* outputs[] = {"o_color"};

            static const my::IndexName uniform_blocks[] = {
                {hrz::UboFrame, "Frame"},
                {UboSkyParams, "SkyParams"},
            };

            my::ShaderResource res{};
            res.name = hrz_shaders::SkyRenderWorldFog_name;
            res.link_hint = my::ShaderLinkHint::Initial;
            res.vertex_source_len = hrz_shaders::SkyRenderWorldFog_vert_len;
            res.vertex_source = hrz_shaders::SkyRenderWorldFog_vert;
            res.fragment_source_len = hrz_shaders::SkyRenderWorldFog_frag_len;
            res.fragment_source = hrz_shaders::SkyRenderWorldFog_frag;
            res.attribs = attribs;
            res.uniform_blocks = uniform_blocks;
            res.samplers = samplers;
            res.outputs = outputs;
            res.initial_state.rasterization.cull_mode = my::RasterizationState::None;
            res.initial_state.depth.test = false;
            res.initial_state.depth.write = false;
            res.initial_state.depth.compare = my::DepthState::LessEqual;
            res.initial_state.stencil.enable = false;
            res.initial_state.color_blend.enable = false;
            res.initial_state.color_blend.mask = my::ColorBlendState::RGBA;

            rc->alloc(&res, hrz::monitoring::systems::Sky);
        }
    }

    void destroy(hrz::Render* render)
    {
        render->rc->dealloc(_fbo);
        render->rc->dealloc(_blit_src_fbo);
        render->rc->dealloc(_sky_box_vb);
        render->rc->dealloc(_sky_box_vi);
        render->rc->dealloc(_sky_box_ib);
        render->rc->dealloc(_sky_view_sampler);
        render->rc->dealloc(_aerial_sampler);
        render->rc->dealloc(_depth_color_sampler);
    }

    void execute_timed(const my::RenderGraph::ExecutionContext& ctx) override
    {
        HRZ_SCOPED_SAMPLE("render sky");

        if (_sky_view_enabled || _fog_enabled)
        {
            const my::ViewportState viewport_state = {
                {0, 0, ctx.backbuffer_width, ctx.backbuffer_height},
                {0, 0, ctx.backbuffer_width, ctx.backbuffer_height},
            };

            ctx.render->set_framebuffer(_fbo, viewport_state);

            static const auto batch_info = my::DrawBatchInfo(my::PrimitiveType::TriangleList, 36)
                                               .indexed(my::IndexType::UByte);

            ctx.binder->push_state();

            const my::UboBinding ubo_bindings[] = {
                {UboSkyParams, _sky_ubo.get_for_gpu(), 0, sizeof(UboData)},
            };
            ctx.binder->bind(ubo_bindings);

            my::ResourceHandle shader{};
            if (_fog_enabled)
            {
                shader = _fog_shader;

                const my::TextureBinding texture_bindings[] = {
                    {0, _depth_input, _depth_color_sampler},
                    {1, _color_input, _depth_color_sampler},
                };
                ctx.binder->bind(texture_bindings);
            }
            else
            {
                assert(_sky_view_enabled);
                shader = _sky_view_shader;

                const my::TextureBinding texture_bindings[] = {
                    {0, _sky_view_lut, _sky_view_sampler},
                    {1, _aerial_lut, _aerial_sampler},
                    {2, _depth_input, _depth_color_sampler},
                    {3, _color_input, _depth_color_sampler},
                };
                ctx.binder->bind(texture_bindings);
            }

            auto state = ctx.binder->get_current_state();

            ctx.render->draw(batch_info, shader, _sky_box_vi, state.ubos, state.textures);

            ctx.binder->pop_state();
        }
        else
        {
            const my::Attachment dst_attachments[] = {my::Attachment::Color0};

            const my::ViewportState viewport_state = {
                {0, 0, ctx.backbuffer_width, ctx.backbuffer_height},
                {0, 0, ctx.backbuffer_width, ctx.backbuffer_height},
            };

            ctx.render->set_framebuffer(_fbo, viewport_state);

            ctx.render->blit_framebuffers_colors(
                _blit_src_fbo, my::Rect{0, 0, ctx.backbuffer_width, ctx.backbuffer_height},
                my::Rect{0, 0, ctx.backbuffer_width, ctx.backbuffer_height}, my::Attachment::Color0,
                dst_attachments, my::SamplerParams::Filter::Nearest);
        }
    }
};

} // namespace

namespace hrz
{

struct SkySystem
{
    using SunDirectionParams = std::variant<
        hrz_proto::SolarDate,
        hrz_proto::CalendarDate,
        int64_t,
        hrz_proto::AngularDirection
    >;

    render::DoubleBufferedUniformBuffer<UboData> ubo;
    lm::dvec3 ecef_sun_direction;

    bool model_updated = false;
    hrz_proto::SceneViewIndex model_scene_view;

    bool enable_simulated_sun_lighting = true;
    bool enable_simulated_ambient_lighting = true;
    bool enable_simulated_sky = true;

    float atmosphere_attenuation = 0.0F;
    float cloudiness = 0.5F;
    SunDirectionParams sun_direction_params = hrz_proto::SolarDate();
    float sun_ambient_balance = 0.5F;
    float lighting_strength = 1.0F;
    float wrap_lighting = 0.0F;
    lm::vec3 sun_color_linear = lm::vec3(1.0F);
    lm::vec3 ambient_color_linear = lm::vec3(0.42F);
    lm::vec4 underground_color_linear = hrz::srgb_to_linear(lm::vec4(0.8F, 0.8F, 0.8F, 1.0F));
    lm::vec4 underground_color_oklab = hrz::srgb_to_oklab(lm::vec4(0.8F, 0.8F, 0.8F, 1.0F));
    lm::vec4 atmosphere_color_linear = hrz::srgb_to_linear(lm::vec4(0.9F, 0.9F, 0.9F, 1.0F));
    lm::vec4 atmosphere_color_oklab = hrz::srgb_to_oklab(lm::vec4(0.9F, 0.9F, 0.9F, 1.0F));
    lm::vec4 space_color_linear = hrz::srgb_to_linear(lm::vec4(0.0F, 0.0F, 0.0F, 1.0F));
    lm::vec4 space_color_oklab = hrz::srgb_to_oklab(lm::vec4(0.0F, 0.0F, 0.0F, 1.0F));
    float color_transition_start_distance = 0.0F;
    float color_transition_end_distance = 0.0F;
    hrz_proto::StaticSkyColorTransitionUnit color_transition_distance_unit =
        hrz_proto::StaticSkyColorTransitionUnit::STATIC_SKY_COLOR_TRANSITION_UNIT_METERS;

    struct Fog
    {
        float density;
        float start_distance;
        float falloff_start;
        float falloff_end;
        bool apply_to_sky;
        lm::vec4 color_linear;
    };

    Fog fog[2];

    hrz::GeoPosition3 camera_position;

    lm::mat4 sh_rotation;

    std::unique_ptr<SkyPrecomputePass> precompute_pass;
    std::unique_ptr<BackgroundSkyRenderPass> background_pass;
    std::unique_ptr<WorldSkyRenderPass> world_pass;
};

namespace sky
{

SkySystem* create()
{
    SkySystem* sys = new SkySystem();
    return sys;
}

void init_render_precompute(SkySystem* sky, RenderView* render)
{
    sky->ubo.initialize(render, hrz::monitoring::systems::Sky, {{"contents"_ss, "sky ubo"_ss}});

    UboData ubo_data;
    ubo_data.sky_box_pv = lm::mat4::identity();
    ubo_data.sky_box_inv_rot = lm::mat4::identity();
    ubo_data.altitude = 0;
    ubo_data.sun_horizon_angle = 0;
    ubo_data.cloudiness = 0;
    ubo_data.horizon_horizon_angle = 0;
    ubo_data.fog[0] = {};
    ubo_data.fog[1] = {};
    ubo_data.fog_min_depth = 0;
    ubo_data.underground_color = lm::vec4(0.0F);
    ubo_data.atmosphere_color = lm::vec4(0.0F);
    ubo_data.space_color = lm::vec4(0.0F);
    ubo_data.oklab_gradient = false;
    ubo_data.color_transition_start_horizon_angle = 0;
    ubo_data.color_transition_end_horizon_angle = 0;
    sky->ubo.set(0, ubo_data);

    sky->precompute_pass.reset(new SkyPrecomputePass(sky->ubo));
    render->rg->add_pass("sky precompute", sky->precompute_pass.get());
}

void init_render_background(SkySystem* sky, RenderView* render, const char* input_color)
{
    sky->background_pass.reset(new BackgroundSkyRenderPass(
        sky->ubo, input_color, sky->precompute_pass->output_sky_view_camera(),
        sky->precompute_pass->output_transmittance()));

    render->rg->add_pass("sky background", sky->background_pass.get());
}

void init_render_world(
    SkySystem* sky,
    RenderView* render,
    const char* input_color,
    const char* input_depth)
{
    sky->world_pass.reset(new WorldSkyRenderPass(
        sky->ubo, input_color, input_depth, sky->precompute_pass->output_sky_view_camera(),
        sky->precompute_pass->output_aerial()));

    render->rg->add_pass("sky world", sky->world_pass.get());
}

const char* get_background_color_output(const SkySystem* sky)
{
    return sky->background_pass->output_color();
}

const char* get_world_color_output(const SkySystem* sky)
{
    return sky->world_pass->output_color();
}

const char* get_world_depth_output(const SkySystem* sky)
{
    return sky->world_pass->output_depth();
}

const char* get_sun_color_output(const SkySystem* sky)
{
    return sky->precompute_pass->output_sun_color();
}

void destroy(SkySystem* sky, Render* render)
{
    if (sky->precompute_pass)
    {
        sky->precompute_pass->destroy(render);
    }

    if (sky->background_pass)
    {
        sky->background_pass->destroy(render);
    }

    if (sky->world_pass)
    {
        sky->world_pass->destroy(render);
    }

    sky->ubo.destroy(render->rc);
    delete sky;
}

RenderRequest update(SkySystem* sky, const CameraViewInfo& camera, SceneModel* model)
{
    HRZ_SCOPED_SAMPLE("update sky");

    RenderRequest render_request;

    bool update_sky_ubo = false;

    if (sky->model_updated)
    {
        auto settings = hrz_proto::SceneViewSettingsPathBuilder<SceneModelAccessor>(
                            model, sky->model_scene_view)
                            .ambient()
                            .get();

        switch (settings.sun().direction_case())
        {
            case hrz_proto::SunSettings::DIRECTION_NOT_SET:
            case hrz_proto::SunSettings::kSolarDate:
                sky->sun_direction_params = settings.sun().solar_date();
                break;
            case hrz_proto::SunSettings::kCalendarDate:
                sky->sun_direction_params = settings.sun().calendar_date();
                break;
            case hrz_proto::SunSettings::kUnixTimeMs:
                sky->sun_direction_params = settings.sun().unix_time_ms();
                break;
            case hrz_proto::SunSettings::kAngularDirection:
                sky->sun_direction_params = settings.sun().angular_direction();
                break;
            default:
                assert(false && "Unhandled case");
                sky->sun_direction_params = hrz_proto::SolarDate();
                break;
        }

        sky->sun_ambient_balance = settings.sun_ambient_balance();
        sky->lighting_strength = settings.lighting_strength();
        sky->wrap_lighting = settings.wrap_lighting();
        sky->sun_color_linear = hrz::srgb_to_linear(
            hrz::convert_proto_color_to_float(settings.sun().static_color()).rgb);
        sky->ambient_color_linear = hrz::srgb_to_linear(
            hrz::convert_proto_color_to_float(settings.ambient_lighting().static_color()).rgb);
        sky->underground_color_linear = hrz::premultiply_alpha(
            hrz::srgb_to_linear(hrz::convert_proto_color_to_float(settings.underground_color())));
        sky->underground_color_oklab = hrz::premultiply_alpha(
            hrz::srgb_to_oklab(hrz::convert_proto_color_to_float(settings.underground_color())));
        sky->atmosphere_color_linear = hrz::premultiply_alpha(
            hrz::srgb_to_linear(
                hrz::convert_proto_color_to_float(settings.sky().static_atmosphere_color())));
        sky->atmosphere_color_oklab = hrz::premultiply_alpha(
            hrz::srgb_to_oklab(
                hrz::convert_proto_color_to_float(settings.sky().static_atmosphere_color())));
        sky->space_color_linear = hrz::premultiply_alpha(
            hrz::srgb_to_linear(
                hrz::convert_proto_color_to_float(settings.sky().static_space_color())));
        sky->space_color_oklab = hrz::premultiply_alpha(
            hrz::srgb_to_oklab(
                hrz::convert_proto_color_to_float(settings.sky().static_space_color())));
        sky->color_transition_start_distance =
            settings.sky().static_color_transition_start_distance();
        sky->color_transition_end_distance = std::max(
            (float)settings.sky().static_color_transition_end_distance(),
            sky->color_transition_start_distance);
        sky->color_transition_distance_unit =
            settings.sky().static_color_transition_distance_unit();
        sky->atmosphere_attenuation = hrz::clamp(settings.sky().attenuation(), 0.0F, 1.0F);

        sky->fog[0].density = settings.primary_fog().density();
        sky->fog[0].start_distance = settings.primary_fog().start_distance();
        sky->fog[0].falloff_start = settings.primary_fog().falloff_start();
        sky->fog[0].falloff_end = settings.primary_fog().falloff_end();
        sky->fog[0].apply_to_sky = settings.primary_fog().apply_to_sky();
        sky->fog[0].color_linear =
            hrz::srgb_to_linear(hrz::convert_proto_color_to_float(settings.primary_fog().color()));

        sky->fog[1].density = settings.secondary_fog().density();
        sky->fog[1].start_distance = settings.secondary_fog().start_distance();
        sky->fog[1].falloff_start = settings.secondary_fog().falloff_start();
        sky->fog[1].falloff_end = settings.secondary_fog().falloff_end();
        sky->fog[1].apply_to_sky = settings.secondary_fog().apply_to_sky();
        sky->fog[1].color_linear = hrz::srgb_to_linear(
            hrz::convert_proto_color_to_float(settings.secondary_fog().color()));

        if (hrz::get_flag(hrz::Flag::EnableAtmosphere))
        {
            sky->enable_simulated_sun_lighting =
                settings.sun().mode() == hrz_proto::SUN_LIGHTING_SIMULATED;
            sky->enable_simulated_ambient_lighting =
                settings.ambient_lighting().mode() == hrz_proto::AMBIENT_LIGHTING_SIMULATED;
            sky->enable_simulated_sky = settings.sky().mode() == hrz_proto::SKY_SIMULATED;

            sky->precompute_pass->set_enabled(
                sky->enable_simulated_sky || sky->enable_simulated_ambient_lighting
                || sky->enable_simulated_sun_lighting);
            sky->background_pass->set_atmosphere_enabled(sky->enable_simulated_sky);
            sky->world_pass->set_sky_view_enabled(sky->enable_simulated_sky);
        }
        else
        {
            sky->enable_simulated_sun_lighting = false;
            sky->enable_simulated_ambient_lighting = false;
            sky->enable_simulated_sky = false;
            sky->precompute_pass->set_enabled(false);
            sky->background_pass->set_atmosphere_enabled(false);
            sky->world_pass->set_sky_view_enabled(false);
        }

        sky->model_updated = false;
        render_request.request_visual_render();
        update_sky_ubo = true;
    }

    hrz::GeoPosition3 geo = hrz::ecef_to_geo3(camera.cam.pos);

    UboData ubo_data = sky->ubo.get();

    if (update_sky_ubo)
    {
        ubo_data.oklab_gradient = !sky->enable_simulated_sky
            && sky->underground_color_linear.a == 1.0F && sky->atmosphere_color_linear.a == 1.0F
            && sky->space_color_linear.a == 1.0F;
    }

    if (sky->camera_position != geo || update_sky_ubo)
    {
        sky->camera_position = geo;

        double altitude_for_static_sky = geo.alt;

        // Altitudes close to 0 present artifacts in the sky view texture (black horizontal
        // lines are present at the top of the texture), which in turn results in black circles
        // in the sky when looking up.
        if (geo.alt > 1.0)
        {
            const double radius_at_lat = hrz::earth_radius_at_latitude(geo.lat);

            ubo_data.altitude = (float)geo.alt;
            ubo_data.horizon_horizon_angle =
                (float)-acos(radius_at_lat / (geo.alt + radius_at_lat));
        }
        else
        {
            ubo_data.altitude = 1.0;
            ubo_data.horizon_horizon_angle = 0.0;

            altitude_for_static_sky = 1.0;
        }

        switch (sky->color_transition_distance_unit)
        {
            case hrz_proto::StaticSkyColorTransitionUnit::STATIC_SKY_COLOR_TRANSITION_UNIT_METERS:
            {
                const auto& atmosphere_color = ubo_data.oklab_gradient
                    ? sky->atmosphere_color_oklab
                    : sky->atmosphere_color_linear;
                const auto& space_color =
                    ubo_data.oklab_gradient ? sky->space_color_oklab : sky->space_color_linear;

                if (sky->color_transition_start_distance < altitude_for_static_sky)
                {
                    ubo_data.color_transition_start_horizon_angle = (float)-acos(
                        (HRZ_S_EARTH_RADIUS + sky->color_transition_start_distance)
                        / (HRZ_S_EARTH_RADIUS + altitude_for_static_sky));

                    if (sky->color_transition_end_distance < altitude_for_static_sky)
                    {
                        ubo_data.color_transition_end_horizon_angle = (float)-acos(
                            (HRZ_S_EARTH_RADIUS + sky->color_transition_end_distance)
                            / (HRZ_S_EARTH_RADIUS + altitude_for_static_sky));
                        ubo_data.space_color = space_color;
                    }
                    else
                    {
                        ubo_data.color_transition_end_horizon_angle = 0;
                        ubo_data.space_color = lm::mix(
                            atmosphere_color, space_color,
                            (float)((altitude_for_static_sky - sky->color_transition_start_distance)
                                    / (sky->color_transition_end_distance
                                       - sky->color_transition_start_distance)));
                    }
                }
                else
                {
                    ubo_data.color_transition_start_horizon_angle = 0;
                    ubo_data.space_color = atmosphere_color;
                }
                break;
            }
            case hrz_proto::StaticSkyColorTransitionUnit::STATIC_SKY_COLOR_TRANSITION_UNIT_PIXELS:
            {
                double distance_to_horizon = std::sqrt(
                    2.0 * hrz::EARTH_RADIUS * altitude_for_static_sky
                    + altitude_for_static_sky * altitude_for_static_sky);
                // metres per pixel
                double resolution =
                    ((std::abs(distance_to_horizon) * std::tan(camera.cam.fovy / 2.0))
                     / (camera.viewport.size.y / 2.0));
                double transition_start_distance = sky->color_transition_start_distance * resolution
                    * camera.viewport.device_pixel_ratio;
                double transition_end_distance = sky->color_transition_end_distance * resolution
                    * camera.viewport.device_pixel_ratio;

                double transition_start_angle =
                    std::atan2(transition_start_distance, distance_to_horizon);
                ubo_data.color_transition_start_horizon_angle =
                    ubo_data.horizon_horizon_angle + (float)transition_start_angle;
                double transition_end_angle =
                    std::atan2(transition_end_distance, distance_to_horizon);
                ubo_data.color_transition_end_horizon_angle =
                    ubo_data.horizon_horizon_angle + (float)transition_end_angle;

                ubo_data.space_color =
                    ubo_data.oklab_gradient ? sky->space_color_oklab : sky->space_color_linear;
                break;
            }
            default: assert(false && "Unhandled case"); break;
        }

        // Compute the normal of the planet at the position of the camera (or rather, at the
        // intersection of the position vector of the camera and the planet surface).
        //
        // * ϕ is the angle between the normal and the "XY" ECEF plane
        // * θ is the angle between the position vector and the "XY" ECEF plane
        //
        // We have tan(ϕ) = (a/b)² * tan(θ)         (1)
        // (Source: https://math.stackexchange.com/a/990013)
        //
        // We work in the "XZ" plane (or "YZ" plane if the x coordinate of our position is 0):
        // * (x0, z0) is the normalized position vector
        // * (x1, z1) is a normal vector
        //
        // Which means
        // * tan(ϕ) = z1 / x1
        // * tan(θ) = z0 / x0
        //
        // Replacing in (1) gives
        // z1 = z0 * (a/b)² * (x1/x0)
        //
        // For a normal vector scaled such that x1 = x0, this gives us
        // z1 = z0 * (a/b)²
        //
        // Then we just have to normalize this to get a correct normal vector.
        lm::dvec3 normal = lm::normalize(
            lm::dvec3{
                camera.cam.pos.x, camera.cam.pos.y,
                camera.cam.pos.z / hrz::WGS84_AXES_LENGTH_RATIO_2
            });
        ubo_data.ground_normal_view = (lm::vec3)(camera.cam.view_cc * lm::vec4(normal, 1.0)).xyz;

        sky->precompute_pass->schedule_refresh_sky_view();
        render_request.request_visual_render();
    }

    if (ubo_data.fog[0].color != sky->fog[0].color_linear
        || ubo_data.fog[0].density != sky->fog[0].density
        || ubo_data.fog[0].start_distance != sky->fog[0].start_distance
        || ubo_data.fog[0].falloff_start != sky->fog[0].falloff_start
        || ubo_data.fog[0].falloff_end != sky->fog[0].falloff_end
        || (bool)ubo_data.fog[0].apply_to_sky != sky->fog[0].apply_to_sky
        || ubo_data.fog[1].color != sky->fog[1].color_linear
        || ubo_data.fog[1].density != sky->fog[1].density
        || ubo_data.fog[1].start_distance != sky->fog[1].start_distance
        || ubo_data.fog[1].falloff_start != sky->fog[1].falloff_start
        || ubo_data.fog[1].falloff_end != sky->fog[1].falloff_end
        || (bool)ubo_data.fog[1].apply_to_sky != sky->fog[1].apply_to_sky)
    {
        ubo_data.fog[0].color = sky->fog[0].color_linear;
        ubo_data.fog[1].color = sky->fog[1].color_linear;
        ubo_data.fog[0].density = sky->fog[0].density;
        ubo_data.fog[1].density = sky->fog[1].density;
        ubo_data.fog[0].start_distance = sky->fog[0].start_distance;
        ubo_data.fog[1].start_distance = sky->fog[1].start_distance;
        ubo_data.fog[0].falloff_start = sky->fog[0].falloff_start;
        ubo_data.fog[1].falloff_start = sky->fog[1].falloff_start;
        ubo_data.fog[0].falloff_end = sky->fog[0].falloff_end;
        ubo_data.fog[1].falloff_end = sky->fog[1].falloff_end;
        ubo_data.fog[0].apply_to_sky = sky->fog[0].apply_to_sky;
        ubo_data.fog[1].apply_to_sky = sky->fog[1].apply_to_sky;
        ubo_data.fog[0].enabled = ubo_data.fog[0].color.a > 0.0 && ubo_data.fog[0].density > 0.0
            && (ubo_data.fog[0].falloff_end - ubo_data.fog[0].falloff_start) > 0.0;
        ubo_data.fog[1].enabled = ubo_data.fog[1].color.a > 0.0 && ubo_data.fog[1].density > 0.0
            && (ubo_data.fog[1].falloff_end - ubo_data.fog[1].falloff_start) > 0.0;

        static constexpr float FogValueAtFalloffEnd = 0.1;
        ubo_data.fog[0].falloff_factor = std::log(FogValueAtFalloffEnd)
            / std::max(1.0F, ubo_data.fog[0].falloff_end - ubo_data.fog[0].falloff_start);
        ubo_data.fog[1].falloff_factor = std::log(FogValueAtFalloffEnd)
            / std::max(1.0F, ubo_data.fog[1].falloff_end - ubo_data.fog[1].falloff_start);

        sky->world_pass->set_fog_enabled(ubo_data.fog[0].enabled || ubo_data.fog[1].enabled);
        render_request.request_visual_render();
    }

    // In all computations below, we completely ignore axial precession, which is
    // the rotation of the polar axis in the frame of the solar system.
    // It happens slowly enough to ignore it for our usecases, and pretend that the
    // solstices happen at fixed times.

    // IAU 2006 obliquity of the ecliptic at J2000: 84381.406" = 23° 26' 21.406", from the
    // IERS conventions (2010), technical note 36, chapter 5.
    //
    // See https://syrte.obspm.fr/iauWGnfa/NFA_Glossary.html
    static const double EARTH_AXIAL_TILT = 0.40909260060058288966; // 23.43927944°

    // Eccentricity of the orbit of the Earth around the Sun, and the ecliptic longitude
    // of the Sun at perihelion (J2000). As with the axial tilt, we ignore the slow
    // precession of the apsides and treat both as constants.
    //
    // Both come from the Keplerian elements of the Earth-Moon barycentre at J2000 listed
    // by JPL, which give ϖ = 102.93768193° for the perihelion of the Earth, hence 180°
    // away from the one of the Sun.
    //
    // See https://ssd.jpl.nasa.gov/planets/approx_pos.html
    static const double EARTH_ORBIT_ECCENTRICITY = 0.01671123;
    static const double SUN_LONGITUDE_AT_PERIHELION = 4.93819412763896448126; // 282.93768°

    // The true ecliptic longitude of the Sun does not advance uniformly over the year,
    // because the Earth moves faster near perihelion. Mean solar time, on the other hand,
    // is tied to a fictitious sun that does advance uniformly, i.e. to the mean
    // longitude. Going from the true to the mean longitude is the easy direction of
    // Kepler's equation, and needs no iteration:
    //   ν = λ - ϖ                                            true anomaly
    //   E = 2 * atan(sqrt((1 - e) / (1 + e)) * tan(ν / 2))   eccentric anomaly
    //   M = E - e * sin(E)                                   mean anomaly
    //   L = M + ϖ                                            mean longitude
    //
    // The two middle steps are equations 2.3.17c and 9.6.5 of Tatum, Celestial Mechanics,
    // section 9.5. The first and the last one are the definitions of the true and of the
    // mean longitude, which are the true and the mean anomaly counted from the equinox
    // rather than from the perihelion. JPL walks the same chain the other way around, in
    // steps 2 and 3 of the algorithm at the link given with the constants above.
    //
    // See https://phys.libretexts.org/@go/page/6845
    //     https://en.wikipedia.org/wiki/True_longitude
    //     https://en.wikipedia.org/wiki/Mean_longitude
    auto mean_solar_longitude = [](double true_longitude)
    {
        double true_anomaly = true_longitude - SUN_LONGITUDE_AT_PERIHELION;
        double eccentric_anomaly = 2.0
            * std::atan2(std::sqrt(1.0 - EARTH_ORBIT_ECCENTRICITY) * std::sin(true_anomaly / 2.0),
                         std::sqrt(1.0 + EARTH_ORBIT_ECCENTRICITY) * std::cos(true_anomaly / 2.0));

        return eccentric_anomaly - EARTH_ORBIT_ECCENTRICITY * std::sin(eccentric_anomaly)
            + SUN_LONGITUDE_AT_PERIHELION;
    };

    // The other way around. This is the hard direction of Kepler's equation, but the
    // equation of center series converges quickly at such a small eccentricity, so we
    // don't need to iterate either. Each order in e is worth about e² = 1 / 3600 of the
    // previous one, so the fourth order terms we drop cost two hundredths of an arcsecond
    // at this eccentricity.
    //
    // See Moulton, An Introduction to Celestial Mechanics (1914), pages 171 and 172
    //     https://archive.org/details/anintroductiont04moulgoog
    //     https://en.wikipedia.org/wiki/Equation_of_the_center
    auto true_solar_longitude = [](double mean_longitude)
    {
        double mean_anomaly = mean_longitude - SUN_LONGITUDE_AT_PERIHELION;
        double e = EARTH_ORBIT_ECCENTRICITY;
        double e2 = e * e;
        double e3 = e2 * e;

        // ν - M, to the third order in e, which leaves less than a twentieth of an
        // arcsecond of error.
        double equation_of_center = (2.0 * e - e3 / 4.0) * std::sin(mean_anomaly)
            + 1.25 * e2 * std::sin(2.0 * mean_anomaly)
            + (13.0 / 12.0) * e3 * std::sin(3.0 * mean_anomaly);

        return mean_longitude + equation_of_center;
    };

    // The mean longitude at the March equinox, where λ is zero by definition.
    static const double MEAN_LONGITUDE_AT_MARCH_EQUINOX = mean_solar_longitude(0.0);

    // Our idealised year lasts exactly one mean tropical year, which is the time it takes
    // for the true solar longitude to increase by 360°, taken at J2000.
    static const double DAYS_PER_YEAR = 365.24219;

    // The day of the year at which the March equinox happens in our idealised year: the
    // 20th of March at midday, counting the 1st of January at midnight as day zero. Both
    // equinoxes and both solstices then land within an hour of their average date over a
    // leap cycle.
    static const double MARCH_EQUINOX_DAY = 78.5;

    // λ at a given day of our idealised year. Days may be fractional.
    auto solar_longitude_at_day_of_year = [&true_solar_longitude](double day)
    {
        return true_solar_longitude(
            MEAN_LONGITUDE_AT_MARCH_EQUINOX
            + (day - MARCH_EQUINOX_DAY) / DAYS_PER_YEAR * 2.0 * lm::PI);
    };

    // Days elapsed since J2000.0, i.e. the 1st of January 2000 at 12:00 UT, for a date on
    // the proleptic Gregorian calendar and a universal time in hours.
    //
    // See https://aa.usno.navy.mil/faq/sun_approx
    auto days_since_j2000 = [](const hrz_proto::CalendarDate& date, double universal_time)
    {
        // Julian day number, through the usual integer arithmetic of Fliegel and van
        // Flandern, Communications of the ACM 11 (1968), page 657. All the divisions here
        // are meant to truncate.
        //
        // See https://dl.acm.org/doi/10.1145/364096.364097
        //     https://aa.usno.navy.mil/faq/JD_formula
        int64_t leap_offset = (14 - date.month()) / 12;
        int64_t years = date.year() + 4800 - leap_offset;
        int64_t months = date.month() + 12 * leap_offset - 3;
        int64_t julian_day = date.day() + (153 * months + 2) / 5 + 365 * years + years / 4
            - years / 100 + years / 400 - 32045;

        // Julian days start at midday, which is also where J2000.0 sits.
        return (double)(julian_day - 2451545) + (universal_time - 12.0) / 24.0;
    };

    // The same, for an instant given as Unix time, i.e. milliseconds elapsed since the
    // 1st of January 1970 at 00:00 UTC. That epoch is JD 2440587.5, which sits 10957.5
    // days before J2000.0.
    //
    // Unix time counts UTC days of 86400 seconds, so every leap second pushes it one
    // second further away from atomic time. That gap does not concern us, because what
    // we need here is universal time, i.e. the rotation of the Earth, and inserting those
    // leap seconds is precisely what keeps UTC within 0.9 seconds of it. The error is
    // therefore bounded rather than accumulating, and is worth at most thirteen arc-
    // seconds of rotation.
    auto days_since_j2000_for_unix_time = [](int64_t unix_time_ms)
    { return (double)unix_time_ms / 86400000.0 - 10957.5; };

    // λ at a given number of days since J2000.0.
    //
    // Unlike the idealised year above, an actual date has to keep up with the real orbit
    // over centuries, so both the mean longitude and the mean anomaly are taken from the
    // low precision solar coordinates of the Astronomical Almanac, which stay within
    // about an arcminute of the true position for two centuries around 2000.
    //
    // See https://aa.usno.navy.mil/faq/sun_approx
    auto solar_longitude_at_j2000_day = [](double days)
    {
        double mean_longitude = lm::radians(280.459 + 0.98564736 * days);
        double mean_anomaly = lm::radians(357.529 + 0.98560028 * days);

        return mean_longitude + lm::radians(1.915) * std::sin(mean_anomaly)
            + lm::radians(0.020) * std::sin(2.0 * mean_anomaly);
    };

    // `solar_longitude` is λ, in radians, and `time` is a time of day, in hours.
    //
    // The two describe the same instant from two angles: λ places the Earth on its orbit,
    // and `time` tells how far it has spun on its axis. A time of day therefore belongs
    // in both, and callers that derive one from the other must keep them in step rather
    // than split the day between them.
    auto compute_sun_from_longitude_and_time =
        [&, mean_solar_longitude](double solar_longitude, float time, bool time_is_local_solar)
    {
        static const lm::dquat identity = lm::dquat();

        // In all computations below, we completely ignore axial precession, which is
        // the rotation of the polar axis in the frame of the solar system.
        // It happens slowly enough to ignore it for our usecases, and pretend that the
        // solstices happen at fixed times.

        double daily_rotation = -(time - 12.0) / 12.0 * lm::PI;

        // We then compute the angle the Earth has made so far in the year around the Sun
        // by considering that the summer solstice is 0°.
        double solar_longitude_rotation = solar_longitude - lm::PI / 2.0;

        // The same angle for the mean longitude, which is what the Earth rotation is
        // referenced to, since the given time is a mean solar time.
        double mean_longitude_rotation = mean_solar_longitude(solar_longitude) - lm::PI / 2.0;

        // We're going to compute the transformation from the local tangential frame
        // to the ecliptic space centered at the Earth center. This space has X towards
        // the sun and XY is the solar system ecliptic plane.
        // We're pretty lucky because all of this can be modeled with rotations only
        // since we only care about the direction of the sun. So quaternions galore!
        // Note that when adjacent rotations use the same axis, we merge them.

        lm::dquat ecef_to_ecliptic = lm::axis_angle({0, 0, 1}, -solar_longitude_rotation)
            * lm::axis_angle({0, 1, 0}, EARTH_AXIAL_TILT)
            * lm::axis_angle({0, 0, 1}, mean_longitude_rotation - daily_rotation);

        lm::dquat from_longitude = lm::axis_angle({0, 0, 1}, geo.lon);

        // We know the quaternions are unit, so inverse = conjugate.
        sky->ecef_sun_direction = (time_is_local_solar ? from_longitude : identity)
            * lm::conjugate(ecef_to_ecliptic) * lm::dvec3(1, 0, 0);
    };

    // λ for a solar date, whichever way its time of year is expressed.
    auto solar_longitude_for_date =
        [&solar_longitude_at_day_of_year](const hrz_proto::SolarDate& date)
    {
        if (date.has_day_of_year())
        {
            // The time of day is part of the date, so that the Sun keeps moving along its
            // orbit while the time of day is animated.
            return solar_longitude_at_day_of_year(date.day_of_year() + date.solar_time() / 24.0);
        }

        // This also covers the case where the time of year is not set at all, which
        // leaves the Sun at the March equinox.
        return lm::radians((double)date.solar_longitude());
    };

    std::visit(
        hrz::overload{
            [&](const hrz_proto::SolarDate& solar_date)
            {
                compute_sun_from_longitude_and_time(
                    solar_longitude_for_date(solar_date), solar_date.solar_time(),
                    !solar_date.at_prime_meridian());
            },
            [&](const hrz_proto::CalendarDate& calendar_date)
            {
                float universal_time = calendar_date.time() - calendar_date.utc_offset();

                compute_sun_from_longitude_and_time(
                    solar_longitude_at_j2000_day(days_since_j2000(calendar_date, universal_time)),
                    universal_time, false);
            },
            [&](int64_t unix_time_ms)
            {
                double days = days_since_j2000_for_unix_time(unix_time_ms);

                // Days since J2000.0 are counted from a midday, so half a day of offset is needed
                // to make the day fraction start at midnight.
                double day_fraction = lm::fract(days + 0.5);

                compute_sun_from_longitude_and_time(
                    solar_longitude_at_j2000_day(days), (float)(day_fraction * 24.0), false);
            },
            [sky, &geo, &camera](const hrz_proto::AngularDirection& angular_direction)
            {
                if (angular_direction.has_geographic_position())
                {
                    auto ref_pos =
                        hrz::from_proto(angular_direction.geographic_position()).latlon();

                    lm::dquat ecef_to_ecliptic =
                        lm::axis_angle({0, 1, 0}, -angular_direction.altitude() + lm::PI / 2)
                        * lm::axis_angle({1, 0, 0}, angular_direction.azimuth())
                        * lm::axis_angle({0, 1, 0}, ref_pos.lat)
                        * lm::axis_angle({0, 0, 1}, -ref_pos.lon);

                    // We know the quaternions are unit, so inverse = conjugate
                    sky->ecef_sun_direction = lm::conjugate(ecef_to_ecliptic) * lm::dvec3(1, 0, 0);
                    return;
                }

                double altitude = angular_direction.altitude();
                double azimuth = -angular_direction.azimuth();

                switch (angular_direction.camera_frame())
                {
                    case hrz_proto::AngularDirection::FRAME_ENU: break;
                    case hrz_proto::AngularDirection::FRAME_CAMERA_HEADING:
                    {
                        lm::dmat4 ecef_to_enu =
                            hrz::ecef_to_enu_rotation_matrix_for_geo(geo.latlon());
                        lm::dvec3 forward_enu =
                            (ecef_to_enu * lm::dvec4{camera.cam.forward(), 1.0}).xyz;

                        if (forward_enu.z > 0.999)
                        {
                            // Looking up, use the down camera vector instead
                            forward_enu = (ecef_to_enu * lm::dvec4{-camera.cam.up(), 1.0}).xyz;
                        }
                        else if (forward_enu.z < -0.999)
                        {
                            // Looking down, use the up camera vector instead
                            forward_enu = (ecef_to_enu * lm::dvec4{camera.cam.up(), 1.0}).xyz;
                        }

                        double forward_azimuth =
                            std::atan2(forward_enu.y, forward_enu.x) - lm::PI / 2.0;
                        azimuth += forward_azimuth;
                    }
                    break;
                    case hrz_proto::AngularDirection::FRAME_CAMERA:
                    {
                        sky->ecef_sun_direction =
                            lm::axis_angle(camera.cam.up(), -(double)angular_direction.azimuth())
                            * (lm::axis_angle(
                                   camera.cam.right(), (double)angular_direction.altitude())
                               * camera.cam.forward());
                        return;
                    }
                    default: assert(false && "Unhandled case"); break;
                }

                lm::dmat4 enu_to_ecef = hrz::enu_to_ecef_rotation_matrix_for_geo(geo.latlon());
                lm::dvec3 sun_direction_enu = lm::axis_angle({0, 0, 1}, azimuth)
                    * lm::axis_angle({1, 0, 0}, altitude) * lm::dvec3(0, 1, 0);
                sky->ecef_sun_direction = (enu_to_ecef * lm::dvec4{sun_direction_enu, 1.0}).xyz;
            },
        },
        sky->sun_direction_params);

    // The atmosphere is rendered on a sphere centred on the Earth, and not an ellipsoid,
    // so the angles we need here are slightly different from the ones computed above.
    // We might not even have computed angles before, depending on the case. So in all
    // cases new angles are recomputed from the direction.
    lm::dvec3 z_axis = lm::normalize(camera.cam.pos);
    lm::dvec3 y_axis = lm::normalize(lm::cross(z_axis, lm::dvec3(0, 0, 1)));
    lm::dvec3 x_axis = lm::normalize(lm::cross(y_axis, z_axis));

    float new_sun_horizon_angle = (float)std::asin(lm::dot(sky->ecef_sun_direction, z_axis));
    float new_sun_azimuth = (float)std::atan2(
        lm::dot(sky->ecef_sun_direction, y_axis), lm::dot(sky->ecef_sun_direction, x_axis));

    if (ubo_data.sun_horizon_angle != new_sun_horizon_angle)
    {
        ubo_data.sun_horizon_angle = new_sun_horizon_angle;
        sky->precompute_pass->schedule_refresh_sky_view();
        render_request.request_visual_render();
    }

    if (ubo_data.cloudiness != sky->cloudiness)
    {
        ubo_data.cloudiness = sky->cloudiness;
        sky->precompute_pass->schedule_refresh_transmittance();
        render_request.request_visual_render();
    }

    // We rotate the skybox along the local vertical axis so that the sun is
    // at the correct azimuth.
    lm::dquat sun_azimuth_rotation = lm::axis_angle(z_axis, (double)new_sun_azimuth);
    y_axis = sun_azimuth_rotation * y_axis;
    x_axis = sun_azimuth_rotation * x_axis;

    lm::dmat4 rot(
        lm::dvec4(x_axis, 0.0), lm::dvec4(y_axis, 0.0), lm::dvec4(z_axis, 0.0),
        lm::dvec4(0.0, 0.0, 0.0, 1.0));

    lm::mat4 new_sky_box_pv = lm::mat4(camera.pv_cc * rot);

    if (ubo_data.sky_box_pv != new_sky_box_pv)
    {
        sky->precompute_pass->schedule_refresh_aerial();
        render_request.request_visual_render();
    }

    ubo_data.sky_box_pv = new_sky_box_pv;
    ubo_data.sky_box_inv_rot = lm::mat4(lm::transpose(rot));

    float fog_min_distance =
        std::min(ubo_data.fog[0].start_distance, ubo_data.fog[1].start_distance);
    ubo_data.fog_min_depth = cos(camera.cam.fovy) * fog_min_distance;

    // Compute the rotation matrix for the spherical harmonics.
    lm::mat3 sky_to_world(x_axis, y_axis, z_axis);
    lm::mat3 world_to_view(camera.cam.view.x.xyz, camera.cam.view.y.xyz, camera.cam.view.z.xyz);
    lm::mat3 view_to_sky = lm::transpose(world_to_view * sky_to_world);
    sky->sh_rotation = lm::mat4(
        lm::vec4(view_to_sky.x, 0.0F), lm::vec4(view_to_sky.y, 0.0F), lm::vec4(view_to_sky.z, 0.0F),
        lm::vec4(0.0F, 0.0F, 0.0F, 1.0F));

    if (update_sky_ubo)
    {
        ubo_data.underground_color =
            ubo_data.oklab_gradient ? sky->underground_color_oklab : sky->underground_color_linear;
        ubo_data.atmosphere_color =
            ubo_data.oklab_gradient ? sky->atmosphere_color_oklab : sky->atmosphere_color_linear;

        render_request.request_visual_render();
    }

    sky->precompute_pass->request_render(render_request);

    sky->ubo.set(0, ubo_data);

    return render_request;
}

RenderRequest work_gpu(SkySystem* sky, Render* render)
{
    RenderRequest rr;
    sky->ubo.update(render->my);
    if (sky->precompute_pass->update(render))
    {
        rr.request_visual_render();
    }

    return rr;
}

lm::dvec3 get_ecef_sun_direction(const SkySystem* sky)
{
    return sky->ecef_sun_direction;
}

bool is_dynamic_sun_lighting_enabled(const SkySystem* sky)
{
    return sky->enable_simulated_sun_lighting;
}

bool is_dynamic_ambient_lighting_enabled(const SkySystem* sky)
{
    return sky->enable_simulated_ambient_lighting;
}

void notify_model_update(
    SkySystem* sky,
    scene_model::UpdateType update_type,
    const scene_model::SceneViewSettingsPath& path)
{
    if (path.leaf() || path.is_ambient())
    {
        sky->model_updated = true;
        sky->model_scene_view = path.get_root();
    }
}

void fill_frame_uniform_data(const SkySystem* sky, FrameUniformData* ubo)
{
    lm::vec3 L[9] = {};

    if (!sky->enable_simulated_ambient_lighting)
    {
        lm::vec3 sky_color = sky->ambient_color_linear;

        // We simulate a hemisphere of sky_color, and black everywhere else.
        // This is done by integrating the functions commented below on the hemisphere.

        L[0] = 0.282095F * sky_color * 0.5F;  // 1
        L[1] = 0.488608F * sky_color * 0.0F;  // y
        L[2] = 0.488603F * sky_color * 0.25F; // z
        L[3] = 0.488608F * sky_color * 0.0F;  // x
        L[4] = 1.092548F * sky_color * 0.0F;  // x * y
        L[5] = 1.092548F * sky_color * 0.0F;  // y * z
        L[6] = 0.315392F * sky_color * 0.0F;  // 3 * z * z - 1
        L[7] = 1.092548F * sky_color * 0.0F;  // x * z
        L[8] = 0.546270F * sky_color * 0.0F;  // x * x - y * y
    }
    else
    {
        std::copy_n(sky->precompute_pass->get_sh_coeffs(), 9, L);
    }

    // See
    // Ramamoorthi, Ravi, and Pat Hanrahan. "An efficient representation for
    // irradiance environment maps." Proceedings of the 28th annual conference on
    // Computer graphics and interactive techniques. 2001.

    const float c1 = 0.429043;
    const float c2 = 0.511664;
    const float c3 = 0.743125;
    const float c4 = 0.886227;
    const float c5 = 0.247708;

    lm::mat4 M = sky->sh_rotation;
    lm::mat4 Mt = lm::transpose(sky->sh_rotation);

    auto& matrices = ubo->env_sh;

#define COMPUTE_MATRIX(INDEX, COMPONENT)                                                   \
    do                                                                                     \
    {                                                                                      \
        matrices[INDEX] = Mt                                                               \
            * lm::mat4(lm::vec4(                                                           \
                           c1 * L[8].COMPONENT, c1 * L[4].COMPONENT, c1 * L[7].COMPONENT,  \
                           c2 * L[3].COMPONENT),                                           \
                       lm::vec4(                                                           \
                           c1 * L[4].COMPONENT, -c1 * L[8].COMPONENT, c1 * L[5].COMPONENT, \
                           c2 * L[1].COMPONENT),                                           \
                       lm::vec4(                                                           \
                           c1 * L[7].COMPONENT, c1 * L[5].COMPONENT, c3 * L[6].COMPONENT,  \
                           c2 * L[2].COMPONENT),                                           \
                       lm::vec4(                                                           \
                           c2 * L[3].COMPONENT, c2 * L[1].COMPONENT, c2 * L[2].COMPONENT,  \
                           c4 * L[0].COMPONENT - c5 * L[6].COMPONENT))                     \
            * M;                                                                           \
    } while (0)

    COMPUTE_MATRIX(0, r);
    COMPUTE_MATRIX(1, g);
    COMPUTE_MATRIX(2, b);

    ubo->sun_strength = std::min(sky->sun_ambient_balance * 2.0, 1.0) * sky->lighting_strength;
    ubo->ambient_strength =
        std::min(2.0F - sky->sun_ambient_balance * 2.0, 1.0) * sky->lighting_strength;
    ubo->sun_color = sky->sun_color_linear;
    ubo->wrap_lighting = sky->wrap_lighting;

    double altitude = sky->ubo.get().altitude;
    if (sky->enable_simulated_sky)
    {
        // By default the fade start is at depth 0. At max attenuation, it starts at the same
        // distance as the camera elevation.

        // By default the fade ends at the distance to the planet, vertically from the camera. So by
        // default it's the camera elevation.
        // At max attenuation the fade ends at the horizon.
        // Between them we compute the end distance by the angle between the vertical and the
        // horizon. There is a power applied to the attenuation parameter so that it "feels" linear.

        double attenuation = std::pow((double)sky->atmosphere_attenuation, (double)0.1);
        double center_distance = hrz::EARTH_RADIUS + altitude;
        double horizon_angle = std::asin(hrz::EARTH_RADIUS / center_distance);
        double atmosphere_limit_angle = horizon_angle * attenuation;
        double a = center_distance * sin(atmosphere_limit_angle);
        double b = center_distance * cos(atmosphere_limit_angle);
        ubo->atmosphere_fade_end =
            b - std::sqrt(std::max(0.0, hrz::EARTH_RADIUS * hrz::EARTH_RADIUS - a * a));
        ubo->atmosphere_fade_start = sky->atmosphere_attenuation * altitude;
    }
}

void collect_shaders(hrz::GpuResourceContext* rc)
{
    BackgroundSkyRenderPass::collect_shaders(rc);
    SkyPrecomputePass::collect_shaders(rc);
    WorldSkyRenderPass::collect_shaders(rc);
}

} // namespace sky
} // namespace hrz
