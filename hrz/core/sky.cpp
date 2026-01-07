#include "hrz/core/sky.h"

#include "hrz/common/color.h"
#include "hrz/common/geo.h"
#include "hrz/common/monitoring_defs.h"
#include "hrz/common/profiling.h"
#include "hrz/core/download_buffer_pool.h"
#include "hrz/core/global_flags.h"
#include "hrz/core/render/common_ubos.h"
#include "hrz/core/render/context.h"
#include "hrz/core/render/double_buffered_uniform_buffer.h"
#include "hrz/core/render/resource_context.h"
#include "hrz/core/render/timed_render_pass.h"
#include "hrz/core/shaders/collection.h"
#include "hrz/fnd/mem.h"
#include "hrz/protocol/path_builder/scene/view_settings.h"

#include <deque>

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
    lm::vec4 atmosphere_color{lm::vec4(0.0f)};
    lm::vec4 space_color{lm::vec4(0.0f)};
    lm::vec4 underground_color{lm::vec4(0.0f)};
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
                {0, _quad_vb, my::VertexFormat::Float32_2, 0, 0, my::VertexRate::PerVertex}};

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
                        {0, 0, SkyViewSize, SkyViewSize}});

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
                        {0, 0, ShInitSize, ShInitSize}});

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
                        {0, 0, ShInitSize / 4, ShInitSize / 4}});

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
                        {0, 0, ShInitSize / 16, ShInitSize / 16}});

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
                    {0, 0, AerialWidth, AerialHeight}});

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

                    lm::vec4 result(0.0f);

                    for (int y = 0; y < 4; ++y)
                    {
                        const lm::vec4* ptr = data.get() + tile_x * 4 + (tile_y * 4 + y) * 12;
                        result += ptr[0] + ptr[1] + ptr[2] + ptr[3];
                    }

                    _sh_coeffs[tile] = result.rgb / 16.0f;
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
                {0, _sky_box_vb, my::VertexFormat::Float32_3, 0, 0, my::VertexRate::PerVertex}};

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
                {0, _sky_box_vb, my::VertexFormat::Float32_3, 0, 0, my::VertexRate::PerVertex}};

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
    render::DoubleBufferedUniformBuffer<UboData> ubo;
    lm::dvec3 ecef_sun_direction;

    bool model_updated = false;
    hrz_proto::SceneViewIndex model_scene_view;

    bool enable_simulated_sun_lighting = true;
    bool enable_simulated_ambient_lighting = true;
    bool enable_simulated_sky = true;

    float atmosphere_attenuation = 0.0f;
    float cloudiness = 0.5f;
    hrz_proto::SunDirectionMode sun_direction_mode =
        hrz_proto::SunDirectionMode::SUN_DIRECTION_RELATIVE_TO_DATE;
    float solar_time = 0.0f;
    float day = 0.0f;
    float sun_azimuth = 0.0f;
    float sun_altitude = 0.0;
    float sun_ambient_balance = 0.5f;
    float lighting_strength = 1.0f;
    float wrap_lighting = 0.0f;
    lm::vec3 sun_color_linear = lm::vec3(1.0f);
    lm::vec3 ambient_color_linear = lm::vec3(0.42f);
    lm::vec4 underground_color_linear = hrz::srgb_to_linear(lm::vec4(0.8f, 0.8f, 0.8f, 1.0f));
    lm::vec4 underground_color_oklab = hrz::srgb_to_oklab(lm::vec4(0.8f, 0.8f, 0.8f, 1.0f));
    lm::vec4 atmosphere_color_linear = hrz::srgb_to_linear(lm::vec4(0.9f, 0.9f, 0.9f, 1.0f));
    lm::vec4 atmosphere_color_oklab = hrz::srgb_to_oklab(lm::vec4(0.9f, 0.9f, 0.9f, 1.0f));
    lm::vec4 space_color_linear = hrz::srgb_to_linear(lm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    lm::vec4 space_color_oklab = hrz::srgb_to_oklab(lm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    float color_transition_start_distance = 0.0f;
    float color_transition_end_distance = 0.0f;
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
    ubo_data.underground_color = lm::vec4(0.0f);
    ubo_data.atmosphere_color = lm::vec4(0.0f);
    ubo_data.space_color = lm::vec4(0.0f);
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

        sky->day = settings.sun().direction().day_of_year();
        sky->sun_direction_mode = settings.sun().direction().mode();
        sky->solar_time = settings.sun().direction().local_solar_time();
        sky->sun_ambient_balance = settings.sun_ambient_balance();
        sky->lighting_strength = settings.lighting_strength();
        sky->wrap_lighting = settings.wrap_lighting();
        sky->sun_color_linear = hrz::srgb_to_linear(
            hrz::convert_proto_color_to_float(settings.sun().static_color()).rgb);
        sky->sun_azimuth = settings.sun().direction().azimuth();
        sky->sun_altitude = settings.sun().direction().altitude();
        sky->ambient_color_linear = hrz::srgb_to_linear(
            hrz::convert_proto_color_to_float(settings.ambient_lighting().static_color()).rgb);
        sky->underground_color_linear = hrz::premultiply_alpha(
            hrz::srgb_to_linear(hrz::convert_proto_color_to_float(settings.underground_color())));
        sky->underground_color_oklab = hrz::premultiply_alpha(
            hrz::srgb_to_oklab(hrz::convert_proto_color_to_float(settings.underground_color())));
        sky->atmosphere_color_linear = hrz::premultiply_alpha(hrz::srgb_to_linear(
            hrz::convert_proto_color_to_float(settings.sky().static_atmosphere_color())));
        sky->atmosphere_color_oklab = hrz::premultiply_alpha(hrz::srgb_to_oklab(
            hrz::convert_proto_color_to_float(settings.sky().static_atmosphere_color())));
        sky->space_color_linear = hrz::premultiply_alpha(hrz::srgb_to_linear(
            hrz::convert_proto_color_to_float(settings.sky().static_space_color())));
        sky->space_color_oklab = hrz::premultiply_alpha(hrz::srgb_to_oklab(
            hrz::convert_proto_color_to_float(settings.sky().static_space_color())));
        sky->color_transition_start_distance =
            settings.sky().static_color_transition_start_distance();
        sky->color_transition_end_distance = std::max(
            (float)settings.sky().static_color_transition_end_distance(),
            sky->color_transition_start_distance);
        sky->color_transition_distance_unit =
            settings.sky().static_color_transition_distance_unit();
        sky->atmosphere_attenuation = hrz::clamp(settings.sky().attenuation(), 0.0f, 1.0f);

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
            && sky->underground_color_linear.a == 1.0f && sky->atmosphere_color_linear.a == 1.0f
            && sky->space_color_linear.a == 1.0f;
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

                ubo_data.space_color = sky->space_color_oklab;
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
        lm::dvec3 normal = lm::normalize(lm::dvec3{
            camera.cam.pos.x, camera.cam.pos.y, camera.cam.pos.z / hrz::WGS84_AXES_LENGTH_RATIO_2});
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
            / std::max(1.0f, ubo_data.fog[0].falloff_end - ubo_data.fog[0].falloff_start);
        ubo_data.fog[1].falloff_factor = std::log(FogValueAtFalloffEnd)
            / std::max(1.0f, ubo_data.fog[1].falloff_end - ubo_data.fog[1].falloff_start);

        sky->world_pass->set_fog_enabled(ubo_data.fog[0].enabled || ubo_data.fog[1].enabled);
        render_request.request_visual_render();
    }

    float new_sun_horizon_angle = 0;
    float new_sun_azimuth = 0;

    switch (sky->sun_direction_mode)
    {
        case hrz_proto::SunDirectionMode::SUN_DIRECTION_RELATIVE_TO_DATE:
        {
            // In all computations below, we completely ignore axial precession, which is
            // the rotation of the polar axis in the frame of the solar system.
            // It happens slowly enough to ignore it for our usecases, and pretend that the
            // solstices happen at fixed times.

            const double daily_rotation = -(sky->solar_time - 12.0) / 12.0 * lm::PI;

            // We offset the day number by the fraction of the day that passed so that
            // the computations below are time-continuous.
            const double day = std::floor(sky->day) + sky->solar_time / 24.0;

            // We then compute the angle the earth has made so far in the year around the sun
            // by considering that the summer solstice is 0°.
            // Also we pretend the summer solstice happens at noon, which is not always
            // the case but that's fine.
            static const double DAYS_PER_YEAR = 365.0;
            static const double SUMMER_SOLSTICE = 171.5f;
            const double angle_around_sun = (day - SUMMER_SOLSTICE) / DAYS_PER_YEAR * 2.0 * lm::PI;

            static const double EARTH_AXIAL_TILT = 0.40910517666747085283;

            // We're going to compute the transformation from the local tangential frame
            // to the ecliptic space centered at the Earth center. This space has X towards
            // the sun and XY is the solar system ecliptic plane.
            // We're pretty lucky because all of this can be modeled with rotations only
            // since we only care about the direction of the sun. So quaternions galore!
            // Note that when adjacent rotations use the same axis, we merge them.
            // Also, longitude doesn't appear here because we pretend that the sun is at
            // its maximum elevation where the camera is at 12, always. If we applied
            // the longitude, it would mean that our time is UTC.

            lm::dquat ecef_to_ecliptic = lm::axis_angle({0, 0, 1}, -angle_around_sun)
                * lm::axis_angle({0, 1, 0}, EARTH_AXIAL_TILT)
                * lm::axis_angle({0, 0, 1}, angle_around_sun - daily_rotation);

            lm::dquat to_longitude = lm::axis_angle({0, 0, 1}, -geo.lon);

            lm::dquat local_to_ecef = lm::axis_angle({0, 1, 0}, -geo.lat + lm::PI / 2)
                * lm::axis_angle({0, 0, 1}, lm::PI / 2);

            // We know the quaternions are unit, so inverse = conjugate
            lm::dvec3 sun_dir_local =
                lm::conjugate(ecef_to_ecliptic * local_to_ecef) * lm::dvec3(1, 0, 0);
            sky->ecef_sun_direction =
                lm::conjugate(ecef_to_ecliptic * to_longitude) * lm::dvec3(1, 0, 0);

            new_sun_horizon_angle = lm::PI / 2 - acos(sun_dir_local.z);
            new_sun_azimuth = -atan2(sun_dir_local.x, sun_dir_local.y);
        }
        break;

        case hrz_proto::SunDirectionMode::SUN_DIRECTION_RELATIVE_TO_CARDINAL_FRAME:
        {
            new_sun_horizon_angle = sky->sun_altitude;
            new_sun_azimuth = sky->sun_azimuth;

            lm::dmat4 enu_to_ecef = hrz::enu_to_ecef_rotation_matrix_for_geo(geo.latlon());
            lm::dvec3 sun_direction_enu = lm::axis_angle({0, 0, 1}, new_sun_azimuth)
                * lm::axis_angle({1, 0, 0}, new_sun_horizon_angle) * lm::dvec3(0, 1, 0);
            sky->ecef_sun_direction = (enu_to_ecef * lm::dvec4{sun_direction_enu, 1.0}).xyz;
        }
        break;

        case hrz_proto::SunDirectionMode::SUN_DIRECTION_RELATIVE_TO_TANGENTIAL_FRAME:
        {
            new_sun_horizon_angle = sky->sun_altitude;

            lm::dmat4 ecef_to_enu = hrz::ecef_to_enu_rotation_matrix_for_geo(geo.latlon());
            lm::dvec3 forward_enu = (ecef_to_enu * lm::dvec4{camera.cam.forward(), 1.0}).xyz;

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

            double forward_azimuth = atan2(forward_enu.y, forward_enu.x) - lm::PI / 2.0;
            new_sun_azimuth = sky->sun_azimuth + forward_azimuth;

            lm::dmat4 enu_to_ecef = hrz::enu_to_ecef_rotation_matrix_for_geo(geo.latlon());
            lm::dvec3 sun_direction_enu = lm::axis_angle({0, 0, 1}, new_sun_azimuth)
                * lm::axis_angle({1, 0, 0}, new_sun_horizon_angle) * lm::dvec3(0, 1, 0);
            sky->ecef_sun_direction = (enu_to_ecef * lm::dvec4{sun_direction_enu, 1.0}).xyz;
        }
        break;

        default: assert(false && "Unhandled");
    }

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

    // Build the matrix that places the skybox at the correct angle.
    // The skybox is a tangential frame, with the sun always in the XZ plane.
    lm::dvec3 z_axis = lm::normalize(camera.cam.pos);
    lm::dvec3 y_axis = lm::normalize(lm::cross(z_axis, lm::dvec3(0, 0, 1)));
    lm::dvec3 x_axis = lm::normalize(lm::cross(y_axis, z_axis));

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
        lm::vec4(view_to_sky.x, 0.0f), lm::vec4(view_to_sky.y, 0.0f), lm::vec4(view_to_sky.z, 0.0f),
        lm::vec4(0.0f, 0.0f, 0.0f, 1.0f));

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

        L[0] = 0.282095f * sky_color * 0.5f;  // 1
        L[1] = 0.488608f * sky_color * 0.0f;  // y
        L[2] = 0.488603f * sky_color * 0.25f; // z
        L[3] = 0.488608f * sky_color * 0.0f;  // x
        L[4] = 1.092548f * sky_color * 0.0f;  // x * y
        L[5] = 1.092548f * sky_color * 0.0f;  // y * z
        L[6] = 0.315392f * sky_color * 0.0f;  // 3 * z * z - 1
        L[7] = 1.092548f * sky_color * 0.0f;  // x * z
        L[8] = 0.546270f * sky_color * 0.0f;  // x * x - y * y
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
        std::min(2.0f - sky->sun_ambient_balance * 2.0, 1.0) * sky->lighting_strength;
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
