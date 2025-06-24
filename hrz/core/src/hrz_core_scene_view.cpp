#include "hrz_core_scene_view.h"

#include "camera/hrz_core_camera_system.h"
#include "hrz_core_camera_height.h"
#include "hrz_core_events.h"
#include "hrz_core_gizmo_layers.h"
#include "hrz_core_global_flags.h"
#include "hrz_core_picking_system.h"
#include "hrz_core_render.h"
#include "hrz_core_scene_model.h"
#include "hrz_core_scene_path.h"
#include "hrz_core_shaders.h"
#include "hrz_core_shadows.h"
#include "hrz_core_shape_editor.h"
#include "hrz_core_sky.h"
#include "hrz_core_viewsheds.h"
#include "planet/hrz_core_planet_geometry.h"
#include "planet/hrz_core_planet_surface.h"
#include "vector/hrz_core_vector_flat_overlay.h"
#include "vector/hrz_core_vector_heatmaps.h"

#include <hrz_common_geo.h>
#include <hrz_common_monitoring_defs.h>
#include <hrz_common_profiling.h>
#include <hrz_common_proto_maths.h>
#include <hrz_core_picking_id_allocator.h>
#include <hrz_fnd_log.h>
#include <hrz_fnd_mem.h>
#include <hrz_fnd_static_vector.h>
#include <hrz_fnd_time.h>
#include <hrz_protocol_path_builder.h>

#include <mycelium_render_graph.h>

namespace
{
static constexpr my::RenderGraph::ResourceUsage Target = my::RenderGraph::ResourceUsage::Target;
static constexpr my::RenderGraph::ResourceUsage Sampled = my::RenderGraph::ResourceUsage::Sampled;
static constexpr my::RenderGraph::ResourceUsage TargetSampled =
    my::RenderGraph::ResourceUsage::TargetSampled;

class InitPass : public hrz::render::TimedRenderPass
{
    const char* _color_name;
    const char* _depth_name;
    my::ResourceHandle _color;
    my::ResourceHandle _depth;
    my::ResourceHandle _fbo;

public:
    InitPass() : TimedRenderPass("init"), _color_name("init_color"), _depth_name("init_depth") {}

    const char* output_color() const { return _color_name; }

    const char* output_depth() const { return _depth_name; }

    void setup_pass(my::RenderGraph::SetupContext& ctx) override
    {
        // This texture is of format Depth32FStencil8 because the decal pass needs a
        // stencil buffer.
        // Binding two different textures to the depth and stencil attachments fot the
        // same frame buffer object isn't permitted in OpenGL.
        // See https://www.khronos.org/registry/webgl/specs/latest/2.0/#FBO_ATTACHMENTS
        // An alternative would be to use a Depth32F texture here, copy the depth data
        // to a texture of format Depth32FStencil8 for the duration of the decal pass,
        // and then copy it back to the first texture for the remaining passes. However
        // this would cost time for copies, without decreasing the memory consumption.
        my::RenderGraph::ResourceInfo depth;
        depth.format = my::TextureFormat::Depth32FStencil8;
        depth.size_class = my::RenderGraph::ResourceInfo::BackbufferRelative;
        depth.width = 1;
        depth.height = 1;

        my::RenderGraph::ResourceInfo color;
        color.format = my::TextureFormat::RGB8;
        color.size_class = my::RenderGraph::ResourceInfo::BackbufferRelative;
        color.width = 1;
        color.height = 1;

        ctx.create(_depth_name, Target, depth);
        ctx.create(_color_name, Target, color);
    }

    void retrieve_resources(
        my::Instance* my,
        my::ResourceContext* rc,
        const my::RenderGraph::ResourceContext& ctx) override
    {
        _color = ctx.retrieve(_color_name);
        _depth = ctx.retrieve(_depth_name);

        my::FramebufferAttachment attachments[] = {
            {my::Attachment::Depth, _depth},
            {my::Attachment::Color0, _color},
        };

        my::FramebufferResource res;
        res.attachment_count = HRZ_ARRAY_COUNT(attachments);
        res.attachments = attachments;

        _fbo = ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::SceneView);
    }

    void destroy(hrz::Render* render) { render->rc->dealloc(_fbo); }

    void execute_timed(const my::RenderGraph::ExecutionContext& ctx) override
    {
        ctx.render->set_framebuffer(
            _fbo,
            my::ViewportState{
                {0, 0, ctx.backbuffer_width, ctx.backbuffer_height},
                {0, 0, ctx.backbuffer_width, ctx.backbuffer_height}});

        static const my::ClearTarget clear_targets[] = {{
            my::Attachment::Depth,
            my::ClearValue::make_depth(1.0),
        }};
        ctx.render->clear(HRZ_ARRAY_COUNT(clear_targets), clear_targets);
    }
};

// This is just used to retrieve the final color render target
class FinalPass : public hrz::render::TimedRenderPass
{
    const char* _color_name;
    my::ResourceHandle _color;

public:
    explicit FinalPass(const char* color_name) :
        TimedRenderPass("final scene view"), _color_name(color_name)
    {
    }

    my::ResourceHandle output_color() const { return _color; }

    void setup_pass(my::RenderGraph::SetupContext& ctx) override { ctx.read(_color_name, Sampled); }

    void retrieve_resources(
        my::Instance* my,
        my::ResourceContext* rc,
        const my::RenderGraph::ResourceContext& ctx) override
    {
        _color = ctx.retrieve(_color_name);
    }

    void execute_timed(const my::RenderGraph::ExecutionContext& ctx) override {}
};

class WorldForwardPass : public hrz::render::TimedRenderPass
{
    const char* _input_color;
    const char* _input_depth;
    const char* _input_overlays[HRZ_S_MAX_OVERLAY_CASCADES];
    const char* _input_viewsheds_shadow_maps[HRZ_S_VIEWSHED_CNT];
    const char* _input_sun_color_lut;
    const char* _output_color;
    const char* _output_depth;
    const char* _input_camera_height;
    my::ResourceHandle _target_color;
    my::ResourceHandle _target_depth;
    my::ResourceHandle _fbo;

    my::ResourceHandle _overlay_textures[HRZ_S_MAX_OVERLAY_CASCADES];
    my::ResourceHandle _overlay_texture_sampler;

    my::ResourceHandle _viewshed_shadow_maps[HRZ_S_MAX_SUN_CASCADES];
    my::ResourceHandle _viewshed_shadow_map_sampler;

    const char* _input_sun_shadow_maps[HRZ_S_MAX_SUN_CASCADES] = {};
    std::optional<my::ResourceHandle> _sun_shadow_maps[HRZ_S_MAX_SUN_CASCADES];
    std::optional<my::ResourceHandle> _sun_shadow_map_sampler;

    my::ResourceHandle _sun_color_lut;
    my::ResourceHandle _sun_color_lut_sampler;

    my::ResourceHandle _camera_height_texture;
    my::ResourceHandle _camera_height_sampler;

public:
    WorldForwardPass(
        const char* input_color,
        const char* input_depth,
        const char* input_camera_height,
        const hrz::VectorFlatOverlaySystem* flat_overlay,
        const std::optional<hrz::ShadowsSystem*>& shadows,
        const hrz::ViewshedsSystem* viewsheds,
        const hrz::SkySystem* sky) :
        TimedRenderPass("world forward"),
        _input_color(input_color),
        _input_depth(input_depth),
        _output_color("world_forward_color"),
        _output_depth("world_forward_depth"),
        _input_camera_height(input_camera_height)
    {
        hrz::vector_flat_overlay::get_visual_target_names(flat_overlay, _input_overlays);

        // @Note: Some devices (e.g. Huwawei MediaPad M5) complain when Horizon is used in low
        // graphics mode because there are no textures bound to units 2/3/4/5 because sun shadow
        // maps are not set. Warning example: "RENDER WARNING: there is no texture bound to the unit
        // 2".
        if (shadows.has_value())
        {
            hrz::shadows::get_shadow_map_names(shadows.value(), _input_sun_shadow_maps);
        }

        hrz::viewsheds::get_shadow_map_names(viewsheds, _input_viewsheds_shadow_maps);
        _input_sun_color_lut = hrz::sky::get_sun_color_output(sky);
    }

    const char* output_color() const { return _output_color; }

    const char* output_depth() const { return _output_depth; }

    void setup_pass(my::RenderGraph::SetupContext& ctx) override
    {
        ctx.read_write(_input_color, Target, _output_color);
        ctx.read_write(_input_depth, Target, _output_depth);
        ctx.read(_input_sun_color_lut, Sampled);
        ctx.read(_input_camera_height, Sampled);

        for (const char* name : _input_overlays)
        {
            ctx.read(name, Sampled);
        }

        if (_input_sun_shadow_maps[0])
        {
            for (const char* name : _input_sun_shadow_maps)
            {
                ctx.read(name, Sampled);
            }
        }

        for (const char* name : _input_viewsheds_shadow_maps)
        {
            ctx.read(name, Sampled);
        }
    }

    void retrieve_resources(
        my::Instance* my,
        my::ResourceContext* rc,
        const my::RenderGraph::ResourceContext& ctx) override
    {
        _target_color = ctx.retrieve(_output_color);
        _target_depth = ctx.retrieve(_output_depth);
        _sun_color_lut = ctx.retrieve(_input_sun_color_lut);
        _camera_height_texture = ctx.retrieve(_input_camera_height);

        for (unsigned int i = 0; i < HRZ_S_MAX_OVERLAY_CASCADES; i++)
        {
            _overlay_textures[i] = ctx.retrieve(_input_overlays[i]);
        }

        for (uint32_t i = 0; i < HRZ_S_VIEWSHED_CNT; i++)
        {
            _viewshed_shadow_maps[i] = ctx.retrieve(_input_viewsheds_shadow_maps[i]);
        }

        {
            my::SamplerResource res;
            res.sampler.wrap_x = my::SamplerParams::Wrap::Clamp;
            res.sampler.wrap_y = my::SamplerParams::Wrap::Clamp;
            res.sampler.wrap_z = my::SamplerParams::Wrap::Clamp;
            res.sampler.mag_filter = my::SamplerParams::Filter::Linear;
            res.sampler.min_filter = my::SamplerParams::Filter::Linear;
            res.use_mipmaps = false;
            _overlay_texture_sampler =
                ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::SceneView);
        }

        {
            my::SamplerResource res;
            res.sampler.wrap_x = my::SamplerParams::Wrap::Clamp;
            res.sampler.wrap_y = my::SamplerParams::Wrap::Clamp;
            res.sampler.wrap_z = my::SamplerParams::Wrap::Clamp;
            res.sampler.mag_filter = my::SamplerParams::Filter::Nearest;
            res.sampler.min_filter = my::SamplerParams::Filter::Nearest;
            res.sampler.is_shadow = true;
            res.sampler.compare = my::DepthState::Compare::Less;
            res.use_mipmaps = false;
            _viewshed_shadow_map_sampler =
                ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::SceneView);
        }

        {
            my::SamplerResource res;
            res.sampler.wrap_x = my::SamplerParams::Wrap::Clamp;
            res.sampler.wrap_y = my::SamplerParams::Wrap::Clamp;
            res.sampler.mag_filter = my::SamplerParams::Filter::Linear;
            res.sampler.min_filter = my::SamplerParams::Filter::Linear;
            res.sampler.is_shadow = false;
            res.use_mipmaps = false;
            _sun_color_lut_sampler =
                ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::SceneView);
        }

        {
            my::SamplerResource res;
            res.sampler.wrap_x = my::SamplerParams::Wrap::Clamp;
            res.sampler.wrap_y = my::SamplerParams::Wrap::Clamp;
            res.sampler.wrap_z = my::SamplerParams::Wrap::Clamp;
            res.sampler.mag_filter = my::SamplerParams::Filter::Nearest;
            res.sampler.min_filter = my::SamplerParams::Filter::Nearest;
            res.sampler.is_shadow = false;
            res.use_mipmaps = false;
            _camera_height_sampler =
                ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::SceneView);
        }

        if (_input_sun_shadow_maps[0])
        {
            for (unsigned int i = 0; i < HRZ_S_MAX_SUN_CASCADES; i++)
            {
                _sun_shadow_maps[i] = ctx.retrieve(_input_sun_shadow_maps[i]);
            }

            my::SamplerResource res;
            res.sampler.wrap_x = my::SamplerParams::Wrap::Clamp;
            res.sampler.wrap_y = my::SamplerParams::Wrap::Clamp;
            res.sampler.wrap_z = my::SamplerParams::Wrap::Clamp;
            res.sampler.mag_filter = my::SamplerParams::Filter::Linear;
            res.sampler.min_filter = my::SamplerParams::Filter::Linear;
            res.sampler.is_shadow = true;
            res.sampler.compare = my::DepthState::Compare::Less;
            res.use_mipmaps = false;
            _sun_shadow_map_sampler =
                ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::SceneView);
        }

        {
            my::FramebufferAttachment attachments[] = {
                {my::Attachment::Color0, _target_color},
                {my::Attachment::Depth, _target_depth},
            };

            my::FramebufferResource res;
            res.attachment_count = HRZ_ARRAY_COUNT(attachments);
            res.attachments = attachments;
            _fbo = ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::SceneView);
        }
    }

    void destroy(hrz::Render* render)
    {
        render->rc->dealloc(_fbo);
        render->rc->dealloc(_overlay_texture_sampler);
        render->rc->dealloc(_viewshed_shadow_map_sampler);
        render->rc->dealloc(_sun_color_lut_sampler);
        render->rc->dealloc(_camera_height_sampler);
        if (_sun_shadow_map_sampler.has_value())
        {
            render->rc->dealloc(_sun_shadow_map_sampler.value());
        }
    }

    void execute_timed(const my::RenderGraph::ExecutionContext& ctx) override
    {
        HRZ_SCOPED_SAMPLE("world forward draw");

        const hrz::SceneViewRenderGraphUserData* user_data =
            (const hrz::SceneViewRenderGraphUserData*)ctx.user_data;

        const my::ViewportState viewport_state = {
            {0, 0, ctx.backbuffer_width, ctx.backbuffer_height},
            {0, 0, ctx.backbuffer_width, ctx.backbuffer_height},
        };

        ctx.binder->push_state();

        hrz::StaticVector<
            my::TextureBinding,
            HRZ_S_MAX_OVERLAY_CASCADES + HRZ_S_MAX_SUN_CASCADES + HRZ_S_VIEWSHED_CNT + 2>
            bindings;

        bindings.push_back(
            {hrz::SamplerCameraHeight, _camera_height_texture, _camera_height_sampler});

        for (int i = 0; i < HRZ_S_MAX_OVERLAY_CASCADES; i++)
        {
            bindings.push_back(
                {hrz::vector_flat_overlay::SamplerOverlayStart + i, _overlay_textures[i],
                 _overlay_texture_sampler});
        }

        for (int i = 0; i < HRZ_S_VIEWSHED_CNT; i++)
        {
            bindings.push_back(
                {hrz::SamplerViewshedShadow0 + i, _viewshed_shadow_maps[i],
                 _viewshed_shadow_map_sampler});
        }

        bindings.push_back({hrz::SamplerSunColor, _sun_color_lut, _sun_color_lut_sampler});

        if (_sun_shadow_map_sampler.has_value())
        {
            for (int i = 0; i < HRZ_S_MAX_SUN_CASCADES; i++)
            {
                bindings.push_back(
                    {hrz::SamplerSunShadow0 + i, _sun_shadow_maps[i].value(),
                     _sun_shadow_map_sampler.value()});
            }
        }

        ctx.binder->bind(bindings.size(), bindings.data());

        ctx.render->set_framebuffer(_fbo, viewport_state);

        my::Renderer::BinMask pass_masks[] = {
            hrz::RenderWorldOpaqueBin,
            hrz::RenderWorldTransparentBin,
        };

        ctx.renderer->draw(
            hrz::RenderVisual, user_data->main_view, HRZ_ARRAY_COUNT(pass_masks), pass_masks,
            ctx.render, ctx.binder, ctx.user_data);

        ctx.binder->pop_state();
    }
};

class DecalPass : public hrz::render::TimedRenderPass
{
    const char* _input_color;
    const char* _input_depth_stencil;
    const char* _input_camera_height;
    const char* _output_color;
    const char* _output_depth_stencil;
    my::ResourceHandle _target_color;
    my::ResourceHandle _target_depth_stencil;
    my::ResourceHandle _camera_height_texture;
    my::ResourceHandle _camera_height_sampler;
    my::ResourceHandle _fbo;

public:
    DecalPass(const char* input_color, const char* input_depth, const char* input_camera_height) :
        TimedRenderPass("decal"),
        _input_color(input_color),
        _input_depth_stencil(input_depth),
        _input_camera_height(input_camera_height),
        _output_color("world_decal_color"),
        _output_depth_stencil("world_decal_depth_stencil")
    {
    }

    const char* output_color() const { return _output_color; }

    const char* output_depth_stencil() const { return _output_depth_stencil; }

    void setup_pass(my::RenderGraph::SetupContext& ctx) override
    {
        ctx.read_write(_input_color, Target, _output_color);
        ctx.read_write(_input_depth_stencil, Target, _output_depth_stencil);
        ctx.read(_input_camera_height, Sampled);
    }

    void retrieve_resources(
        my::Instance* my,
        my::ResourceContext* rc,
        const my::RenderGraph::ResourceContext& ctx) override
    {
        _target_color = ctx.retrieve(_output_color);
        _target_depth_stencil = ctx.retrieve(_output_depth_stencil);

        {
            my::FramebufferAttachment attachments[] = {
                {my::Attachment::Color0, _target_color},
                {my::Attachment::DepthStencil, _target_depth_stencil},
            };

            my::FramebufferResource res;
            res.attachment_count = HRZ_ARRAY_COUNT(attachments);
            res.attachments = attachments;
            _fbo = ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::SceneView);
        }

        _camera_height_texture = ctx.retrieve(_input_camera_height);

        {
            my::SamplerResource res;
            res.sampler.wrap_x = my::SamplerParams::Wrap::Clamp;
            res.sampler.wrap_y = my::SamplerParams::Wrap::Clamp;
            res.sampler.wrap_z = my::SamplerParams::Wrap::Clamp;
            res.sampler.mag_filter = my::SamplerParams::Filter::Nearest;
            res.sampler.min_filter = my::SamplerParams::Filter::Nearest;
            res.sampler.is_shadow = false;
            res.use_mipmaps = false;
            _camera_height_sampler =
                ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::SceneView);
        }
    }

    void destroy(hrz::Render* render)
    {
        render->rc->dealloc(_fbo);
        render->rc->dealloc(_camera_height_sampler);
    }

    void execute_timed(const my::RenderGraph::ExecutionContext& ctx) override
    {
        // The pass starts with the depth buffer of what has been drawn before, as well
        // as a stencil buffer that each renderable is free to use the way it wants.
        // This means that renderables are responsible for clearing the stencil buffer
        // when needed.

        HRZ_SCOPED_SAMPLE("decal draw");

        const hrz::SceneViewRenderGraphUserData* user_data =
            (const hrz::SceneViewRenderGraphUserData*)ctx.user_data;

        const my::ViewportState viewport_state = {
            {0, 0, ctx.backbuffer_width, ctx.backbuffer_height},
            {0, 0, ctx.backbuffer_width, ctx.backbuffer_height},
        };

        ctx.binder->push_state();

        ctx.render->set_framebuffer(_fbo, viewport_state);

        my::TextureBinding texture_bindings[] = {
            {hrz::SamplerCameraHeight, _camera_height_texture, _camera_height_sampler},
        };
        ctx.binder->bind(HRZ_ARRAY_COUNT(texture_bindings), texture_bindings);

        my::Renderer::BinMask pass_masks[] = {
            hrz::RenderDecalBin,
        };

        ctx.renderer->draw(
            hrz::RenderDecal, user_data->main_view, HRZ_ARRAY_COUNT(pass_masks), pass_masks,
            ctx.render, ctx.binder, ctx.user_data);

        ctx.binder->pop_state();
    }
};

class UiPass : public hrz::render::TimedRenderPass
{
public:
    explicit UiPass(const char* timed_name) : TimedRenderPass(timed_name) {}

    virtual const char* output_color() const = 0;
    virtual void destroy(hrz::Render* render) = 0;
};

class UiForwardPass : public UiPass
{
    const char* _input_color;
    const char* _input_depth;
    const char* _output_color;
    const char* _output_depth;

    // Necessary because the UI shaders check a peel depth. We set it to 1 to
    // accept all fragments. Alternative is to create more shader variants...
    my::ResourceHandle _dummy_peel_depth;

    my::ResourceHandle _target_color;
    my::ResourceHandle _target_depth;
    my::ResourceHandle _scene_depth;
    my::ResourceHandle _fbo;
    my::ResourceHandle _depth_sampler;

public:
    UiForwardPass(const char* input_color, const char* input_depth) :
        UiPass("ui forward"),
        _input_color(input_color),
        _input_depth(input_depth),
        _output_color("gui_color"),
        _output_depth("gui_depth")
    {
    }

    const char* output_color() const override { return _output_color; }

    void setup_pass(my::RenderGraph::SetupContext& ctx) override
    {
        ctx.read_write(_input_color, Target, _output_color);
        ctx.read(_input_depth, Sampled);

        my::RenderGraph::ResourceInfo depth;
        depth.format = my::TextureFormat::Depth32F;
        depth.size_class = my::RenderGraph::ResourceInfo::BackbufferRelative;
        depth.width = 1;
        depth.height = 1;

        ctx.create(_output_depth, Target, depth);
    }

    void retrieve_resources(
        my::Instance* my,
        my::ResourceContext* rc,
        const my::RenderGraph::ResourceContext& ctx) override
    {
        _target_color = ctx.retrieve(_output_color);
        _target_depth = ctx.retrieve(_output_depth);
        _scene_depth = ctx.retrieve(_input_depth);

        {
            my::FramebufferAttachment attachments[] = {
                {my::Attachment::Color0, _target_color},
                {my::Attachment::Depth, _target_depth},
            };

            my::FramebufferResource res;
            res.attachment_count = HRZ_ARRAY_COUNT(attachments);
            res.attachments = attachments;
            _fbo = ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::SceneView);
        }

        {
            my::SamplerResource res;
            res.use_mipmaps = false;
            res.sampler.mag_filter = my::SamplerParams::Filter::Nearest;
            res.sampler.min_filter = my::SamplerParams::Filter::Nearest;
            res.sampler.wrap_x = my::SamplerParams::Wrap::Clamp;
            res.sampler.wrap_y = my::SamplerParams::Wrap::Clamp;
            res.sampler.is_shadow = false;

            _depth_sampler =
                ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::SceneView);
        }

        {
            static const float data[] = {1.0f};
            gsl::span<const std::byte> data_span = {(const std::byte*)data, sizeof(float)};

            my::TextureResource res;
            res.layout.type = my::TextureLayout::Type2D;
            res.layout.format = my::TextureFormat::Depth32F;
            res.layout.width = 1;
            res.layout.height = 1;
            res.layout.depth = 1;
            res.layout.levels = 1;
            res.data = {&data_span, 1};
            res.generate_mipmaps = false;

            _dummy_peel_depth =
                ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::SceneView);
        }
    }

    void destroy(hrz::Render* render) override
    {
        render->rc->dealloc(_fbo);
        render->rc->dealloc(_depth_sampler);
        render->rc->dealloc(_dummy_peel_depth);
    }

    void execute_timed(const my::RenderGraph::ExecutionContext& ctx) override
    {
        HRZ_SCOPED_SAMPLE("ui draw");

        const hrz::SceneViewRenderGraphUserData* user_data =
            (const hrz::SceneViewRenderGraphUserData*)ctx.user_data;

        const my::ViewportState viewport_state = {
            {0, 0, ctx.backbuffer_width, ctx.backbuffer_height},
            {0, 0, ctx.backbuffer_width, ctx.backbuffer_height},
        };

        ctx.binder->push_state();

        my::TextureBinding binding[] = {
            {0, _scene_depth, _depth_sampler},
            {1, _dummy_peel_depth, _depth_sampler},
        };
        ctx.binder->bind(HRZ_ARRAY_COUNT(binding), binding);

        ctx.render->set_framebuffer(_fbo, viewport_state);

        static const my::ClearTarget clear_targets[] = {{
            my::Attachment::Depth,
            my::ClearValue::make_depth(1.0),
        }};

        my::Renderer::BinMask pass_masks[] = {
            hrz::RenderUiBin,
        };

        ctx.render->clear(1, clear_targets);
        ctx.renderer->draw(
            hrz::RenderVisual, user_data->main_view, HRZ_ARRAY_COUNT(pass_masks), pass_masks,
            ctx.render, ctx.binder, ctx.user_data);

        ctx.binder->pop_state();
    }
};

class UiDepthPeelingPass : public UiPass
{
    const char* _input_color_name;
    const char* _input_depth_name;

    const char* _output_color_name;

    const char* _depth_buffer_names[2];
    const char* _color_peel_name;
    const char* _color_accum_name;

    my::ResourceHandle _color_target;
    my::ResourceHandle _input_depth;

    my::ResourceHandle _depth_buffers[2];
    my::ResourceHandle _color_peel;
    my::ResourceHandle _color_accum;

    my::ResourceHandle _peel_fbos[2];
    my::ResourceHandle _accum_fbo;
    my::ResourceHandle _apply_accum_fbo;

    my::ResourceHandle _sampler;
    my::ResourceHandle _vi;
    my::ResourceHandle _vb;
    my::ResourceHandle _accum_shader;
    my::ResourceHandle _apply_accum_shader;

    static const int PASS_COUNT = 4;

public:
    UiDepthPeelingPass(const char* input_color, const char* input_depth) :
        UiPass("ui depth peeling"),
        _input_color_name(input_color),
        _input_depth_name(input_depth),
        _output_color_name("gui color"),
        _depth_buffer_names{"gui depth buffer 0", "gui depth buffer 1"},
        _color_peel_name("gui color peel"),
        _color_accum_name("gui color accum")
    {
    }

    const char* output_color() const override { return _output_color_name; }

    void setup_pass(my::RenderGraph::SetupContext& ctx) override
    {
        // Even invocations draw a peel, odd ones accumulate it
        ctx.set_invocation_count(PASS_COUNT);

        ctx.read_write(_input_color_name, Target, _output_color_name);
        ctx.read(_input_depth_name, Sampled);

        my::RenderGraph::ResourceInfo depth;
        depth.format = my::TextureFormat::Depth32F;
        depth.size_class = my::RenderGraph::ResourceInfo::BackbufferRelative;
        depth.width = 1;
        depth.height = 1;

        ctx.create(_depth_buffer_names[0], TargetSampled, depth);
        ctx.create(_depth_buffer_names[1], TargetSampled, depth);

        my::RenderGraph::ResourceInfo color;
        color.format = my::TextureFormat::RGBA8;
        color.size_class = my::RenderGraph::ResourceInfo::BackbufferRelative;
        color.width = 1;
        color.height = 1;

        ctx.create(_color_peel_name, TargetSampled, color);
        ctx.create(_color_accum_name, TargetSampled, color);
    }

    void retrieve_resources(
        my::Instance* my,
        my::ResourceContext* rc,
        const my::RenderGraph::ResourceContext& ctx) override
    {
        _color_target = ctx.retrieve(_output_color_name);
        _input_depth = ctx.retrieve(_input_depth_name);

        _depth_buffers[0] = ctx.retrieve(_depth_buffer_names[0]);
        _depth_buffers[1] = ctx.retrieve(_depth_buffer_names[1]);

        _color_peel = ctx.retrieve(_color_peel_name);
        _color_accum = ctx.retrieve(_color_accum_name);

        for (int i = 0; i < 2; ++i)
        {
            my::FramebufferAttachment attachments[] = {
                {my::Attachment::Color0, _color_peel},
                {my::Attachment::Depth, _depth_buffers[i]},
            };

            my::FramebufferResource res;
            res.attachment_count = HRZ_ARRAY_COUNT(attachments);
            res.attachments = attachments;
            _peel_fbos[i] =
                ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::SceneView);
        }

        {
            my::FramebufferAttachment attachments[] = {
                {my::Attachment::Color0, _color_accum},
            };

            my::FramebufferResource res;
            res.attachment_count = HRZ_ARRAY_COUNT(attachments);
            res.attachments = attachments;
            _accum_fbo =
                ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::SceneView);
        }

        {
            my::FramebufferAttachment attachments[] = {
                {my::Attachment::Color0, _color_target},
            };

            my::FramebufferResource res;
            res.attachment_count = HRZ_ARRAY_COUNT(attachments);
            res.attachments = attachments;
            _apply_accum_fbo =
                ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::SceneView);
        }

        {
            my::SamplerResource res;
            res.use_mipmaps = false;
            res.sampler.mag_filter = my::SamplerParams::Filter::Nearest;
            res.sampler.min_filter = my::SamplerParams::Filter::Nearest;
            res.sampler.wrap_x = my::SamplerParams::Wrap::Clamp;
            res.sampler.wrap_y = my::SamplerParams::Wrap::Clamp;
            res.sampler.is_shadow = false;
            _sampler =
                ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::SceneView);
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

            _vb = ((hrz::GpuResourceContext*)rc)
                      ->alloc(
                          &res, hrz::monitoring::systems::SceneView,
                          {{"contents"_ss, "UI depth-peeling full-screen triangle vertices"_ss}});
        }

        {
            const my::VertexInputStream streams[] = {
                {0, _vb, my::VertexFormat::Float32_2, 0, 0, my::VertexRate::PerVertex},
            };

            my::VertexInputResource res;
            res.attrib_count = HRZ_ARRAY_COUNT(streams);
            res.attribs = streams;

            _vi = ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::SceneView);
        }

        _accum_shader = rc->retrieve_shader(hrz_shaders::UiPeelAccum_name);
        _apply_accum_shader = rc->retrieve_shader(hrz_shaders::UiApplyAccum_name);
    }

    static void collect_shaders(hrz::GpuResourceContext* rc)
    {
        {
            my::IndexName attribs[] = {{0, "i_pos"}};

            my::IndexName samplers[] = {{0, "u_peel"}};

            const char* outputs[] = {"o_color"};

            my::ShaderResource res{};
            res.name = hrz_shaders::UiPeelAccum_name;
            res.link_hint = my::ShaderLinkHint::Initial;
            res.vertex_source_len = hrz_shaders::UiPeelAccum_vert_len;
            res.vertex_source = hrz_shaders::UiPeelAccum_vert;
            res.fragment_source_len = hrz_shaders::UiPeelAccum_frag_len;
            res.fragment_source = hrz_shaders::UiPeelAccum_frag;
            res.attrib_count = HRZ_ARRAY_COUNT(attribs);
            res.attribs = attribs;
            res.uniform_block_count = 0;
            res.uniform_blocks = nullptr;
            res.sampler_count = HRZ_ARRAY_COUNT(samplers);
            res.samplers = samplers;
            res.output_count = HRZ_ARRAY_COUNT(outputs);
            res.outputs = outputs;
            res.initial_state.color_blend.enable = true;
            res.initial_state.color_blend.mask = my::ColorBlendState::RGBA;
            // "Under" blending with pre-multiplied alpha
            res.initial_state.color_blend.color.op = my::ColorBlendState::Add;
            res.initial_state.color_blend.color.src = my::ColorBlendState::OneMinusDstAlpha;
            res.initial_state.color_blend.color.dst = my::ColorBlendState::One;
            res.initial_state.color_blend.alpha.op = my::ColorBlendState::Add;
            res.initial_state.color_blend.alpha.src = my::ColorBlendState::OneMinusDstAlpha;
            res.initial_state.color_blend.alpha.dst = my::ColorBlendState::One;
            res.initial_state.depth.test = false;
            res.initial_state.stencil.enable = false;

            rc->alloc(&res, hrz::monitoring::systems::SceneView);
        }

        {
            my::IndexName attribs[] = {{0, "i_pos"}};

            my::IndexName samplers[] = {{0, "u_accum"}};

            const char* outputs[] = {"o_color"};

            my::ShaderResource res{};
            res.name = hrz_shaders::UiApplyAccum_name;
            res.link_hint = my::ShaderLinkHint::Initial;
            res.vertex_source_len = hrz_shaders::UiApplyAccum_vert_len;
            res.vertex_source = hrz_shaders::UiApplyAccum_vert;
            res.fragment_source_len = hrz_shaders::UiApplyAccum_frag_len;
            res.fragment_source = hrz_shaders::UiApplyAccum_frag;
            res.attrib_count = HRZ_ARRAY_COUNT(attribs);
            res.attribs = attribs;
            res.uniform_block_count = 0;
            res.uniform_blocks = nullptr;
            res.sampler_count = HRZ_ARRAY_COUNT(samplers);
            res.samplers = samplers;
            res.output_count = HRZ_ARRAY_COUNT(outputs);
            res.outputs = outputs;
            res.initial_state.color_blend.enable = true;
            res.initial_state.color_blend.mask = my::ColorBlendState::RGBA;
            res.initial_state.color_blend.color.op = my::ColorBlendState::Add;
            res.initial_state.color_blend.color.src = my::ColorBlendState::One;
            res.initial_state.color_blend.color.dst = my::ColorBlendState::OneMinusSrcAlpha;
            res.initial_state.color_blend.alpha.op = my::ColorBlendState::Add;
            res.initial_state.color_blend.alpha.src = my::ColorBlendState::Zero;
            res.initial_state.color_blend.alpha.dst = my::ColorBlendState::One;
            res.initial_state.depth.test = false;
            res.initial_state.stencil.enable = false;

            rc->alloc(&res, hrz::monitoring::systems::SceneView);
        }
    }

    void destroy(hrz::Render* render) override
    {
        render->rc->dealloc(_peel_fbos[0]);
        render->rc->dealloc(_peel_fbos[1]);
        render->rc->dealloc(_accum_fbo);
        render->rc->dealloc(_apply_accum_fbo);
        render->rc->dealloc(_sampler);
        render->rc->dealloc(_vi);
        render->rc->dealloc(_vb);
    }

    void execute_timed(const my::RenderGraph::ExecutionContext& ctx) override
    {
        HRZ_SCOPED_SAMPLE("ui draw");

        const hrz::SceneViewRenderGraphUserData* user_data =
            (const hrz::SceneViewRenderGraphUserData*)ctx.user_data;

        const my::ViewportState viewport_state = {
            {0, 0, ctx.backbuffer_width, ctx.backbuffer_height},
            {0, 0, ctx.backbuffer_width, ctx.backbuffer_height},
        };

        if (ctx.invocation == 0)
        {
            // Clear the first read depth buffer to 0 so that everything passes
            // on first invocation
            ctx.render->set_framebuffer(_peel_fbos[0], viewport_state);

            static const my::ClearTarget clear_target = {
                my::Attachment::Depth,
                my::ClearValue::make_depth(0.0),
            };

            ctx.render->clear(1, &clear_target);

            // Clear the color accum buffer
            ctx.render->set_framebuffer(_accum_fbo, viewport_state);

            static const my::ClearTarget clear_target_2 = {
                my::Attachment::Color0,
                my::ClearValue::make_color_float(0.0, 0.0, 0.0, 0.0),
            };

            ctx.render->clear(1, &clear_target_2);
        }

        int peel_pass = ctx.invocation;

        // Main pass
        {
            int read_index = peel_pass % 2;
            int write_index = (peel_pass + 1) % 2;

            // Render depth peel

            ctx.binder->push_state();

            my::TextureBinding binding[] = {
                {0, _input_depth, _sampler},
                {1, _depth_buffers[read_index], _sampler},
            };
            ctx.binder->bind(HRZ_ARRAY_COUNT(binding), binding);

            ctx.render->set_framebuffer(_peel_fbos[write_index], viewport_state);

            static const my::ClearTarget clear_targets[] = {
                {
                    my::Attachment::Color0,
                    my::ClearValue::make_color_float(0.0, 0.0, 0.0, 0.0),
                },
                {
                    my::Attachment::Depth,
                    my::ClearValue::make_depth(1.0),
                }};

            my::Renderer::BinMask pass_masks[] = {
                hrz::RenderUiBin,
            };

            ctx.render->clear(HRZ_ARRAY_COUNT(clear_targets), clear_targets);
            ctx.renderer->draw(
                hrz::RenderVisual, user_data->main_view, HRZ_ARRAY_COUNT(pass_masks), pass_masks,
                ctx.render, ctx.binder, ctx.user_data);

            ctx.binder->pop_state();
        }

        // Accumulate depth peel
        {
            ctx.render->set_framebuffer(_accum_fbo, viewport_state);
            ctx.binder->push_state();

            my::TextureBinding texture_binding = {0, _color_peel, _sampler};

            ctx.binder->bind(1, &texture_binding);

            auto state = ctx.binder->get_current_state();

            static const auto info = my::DrawBatchInfo(my::PrimitiveType::TriangleList, 3);

            ctx.render->draw(
                info, _accum_shader, _vi, state.ubo_count, state.ubos, state.texture_count,
                state.textures);

            ctx.binder->pop_state();
        }

        // Apply accumulated transparent view to the input view on last pass
        if (peel_pass == PASS_COUNT - 1)
        {
            ctx.render->set_framebuffer(_apply_accum_fbo, viewport_state);
            ctx.binder->push_state();

            my::TextureBinding texture_binding = {0, _color_accum, _sampler};

            ctx.binder->bind(1, &texture_binding);

            auto state = ctx.binder->get_current_state();

            static const auto info = my::DrawBatchInfo(my::PrimitiveType::TriangleList, 3);

            ctx.render->draw(
                info, _apply_accum_shader, _vi, state.ubo_count, state.ubos, state.texture_count,
                state.textures);

            ctx.binder->pop_state();
        }
    }
};

class SymbolicForwardPass : public hrz::render::TimedRenderPass
{
    const char* _input_color;
    const char* _input_depth;
    const char* _input_camera_height;
    const char* _output_color;
    const char* _output_depth;
    const char* _output_overlay_depth;
    my::ResourceHandle _target_color;
    my::ResourceHandle _target_depth;
    my::ResourceHandle _target_overlay_depth;
    my::ResourceHandle _fbo;
    my::ResourceHandle _overlay_fbo;
    my::ResourceHandle _camera_height_texture;
    my::ResourceHandle _camera_height_sampler;

public:
    SymbolicForwardPass(
        const char* input_color,
        const char* input_depth,
        const char* input_camera_height) :
        TimedRenderPass("symbolic forward"),
        _input_color(input_color),
        _input_depth(input_depth),
        _input_camera_height(input_camera_height),
        _output_color("symbolic_forward_color"),
        _output_depth("symbolic_forward_depth"),
        _output_overlay_depth("symbolic_overlay_depth")
    {
    }

    const char* output_color() const { return _output_color; }

    const char* output_depth() const { return _output_depth; }

    const char* output_overlay_depth() const { return _output_overlay_depth; }

    void setup_pass(my::RenderGraph::SetupContext& ctx) override
    {
        ctx.set_invocation_count(2);

        ctx.read_write(_input_color, Target, _output_color);
        ctx.read_write(_input_depth, Target, _output_depth);
        ctx.read(_input_camera_height, Sampled);

        my::RenderGraph::ResourceInfo overlay_depth;
        overlay_depth.format = my::TextureFormat::Depth32F;
        overlay_depth.size_class = my::RenderGraph::ResourceInfo::BackbufferRelative;
        overlay_depth.width = 1;
        overlay_depth.height = 1;

        ctx.create(_output_overlay_depth, Target, overlay_depth);
    }

    void retrieve_resources(
        my::Instance* my,
        my::ResourceContext* rc,
        const my::RenderGraph::ResourceContext& ctx) override
    {
        _target_color = ctx.retrieve(_output_color);
        _target_depth = ctx.retrieve(_output_depth);
        _target_overlay_depth = ctx.retrieve(_output_overlay_depth);
        _camera_height_texture = ctx.retrieve(_input_camera_height);

        {
            my::FramebufferAttachment attachments[] = {
                {my::Attachment::Color0, _target_color},
                {my::Attachment::Depth, _target_depth},
            };

            my::FramebufferResource res;
            res.attachment_count = HRZ_ARRAY_COUNT(attachments);
            res.attachments = attachments;
            _fbo = ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::SceneView);
        }

        {
            my::FramebufferAttachment attachments[] = {
                {my::Attachment::Color0, _target_color},
                {my::Attachment::Depth, _target_overlay_depth},
            };

            my::FramebufferResource res;
            res.attachment_count = HRZ_ARRAY_COUNT(attachments);
            res.attachments = attachments;
            _overlay_fbo =
                ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::SceneView);
        }

        {
            my::SamplerResource res;
            res.sampler.wrap_x = my::SamplerParams::Wrap::Clamp;
            res.sampler.wrap_y = my::SamplerParams::Wrap::Clamp;
            res.sampler.wrap_z = my::SamplerParams::Wrap::Clamp;
            res.sampler.mag_filter = my::SamplerParams::Filter::Nearest;
            res.sampler.min_filter = my::SamplerParams::Filter::Nearest;
            res.sampler.is_shadow = false;
            res.use_mipmaps = false;
            _camera_height_sampler =
                ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::SceneView);
        }
    }

    void destroy(hrz::Render* render)
    {
        render->rc->dealloc(_fbo);
        render->rc->dealloc(_overlay_fbo);
        render->rc->dealloc(_camera_height_sampler);
    }

    void execute_timed(const my::RenderGraph::ExecutionContext& ctx) override
    {
        // Invocation 0 draws the RenderSymbolicBin.
        // Invocation 1 draws the RenderSymbolicOverlayBin using a separate depth buffer.

        HRZ_SCOPED_SAMPLE("symbolic forward draw");

        my::Renderer::BinMask render_bin =
            ctx.invocation == 0 ? hrz::RenderSymbolicBin : hrz::RenderSymbolicOverlayBin;
        auto fbo = ctx.invocation == 0 ? _fbo : _overlay_fbo;

        const hrz::SceneViewRenderGraphUserData* user_data =
            (const hrz::SceneViewRenderGraphUserData*)ctx.user_data;

        ctx.binder->push_state();

        my::TextureBinding texture_bindings[] = {
            {hrz::SamplerCameraHeight, _camera_height_texture, _camera_height_sampler},
        };
        ctx.binder->bind(HRZ_ARRAY_COUNT(texture_bindings), texture_bindings);

        ctx.render->set_framebuffer(
            fbo,
            {
                {0, 0, ctx.backbuffer_width, ctx.backbuffer_height},
                {0, 0, ctx.backbuffer_width, ctx.backbuffer_height},
            });

        if (ctx.invocation == 1)
        {
            static const my::ClearTarget clear_target = {
                my::Attachment::Depth, my::ClearValue::make_depth(1.0)};

            ctx.render->clear(1, &clear_target);
        }

        my::Renderer::BinMask pass_masks[] = {
            render_bin,
        };

        ctx.renderer->draw(
            hrz::RenderVisual, user_data->main_view, HRZ_ARRAY_COUNT(pass_masks), pass_masks,
            ctx.render, ctx.binder, ctx.user_data);

        ctx.binder->pop_state();
    }
};

class MergeDepthsPass : public hrz::render::TimedRenderPass
{
    const char* _input_depth_names[2];
    const char* _output_depth_name;
    my::ResourceHandle _target_depth;
    my::ResourceHandle _input_depth;
    my::ResourceHandle _fbo;
    my::ResourceHandle _sampler;
    my::ResourceHandle _vi;
    my::ResourceHandle _vb;
    my::ResourceHandle _shader;

public:
    MergeDepthsPass(const char* input_depth_1, const char* input_depth_2) :
        TimedRenderPass("merge depths"),
        _input_depth_names{input_depth_1, input_depth_2},
        _output_depth_name("merged_depth")
    {
    }

    const char* output_perceived_depth() const { return _output_depth_name; }

    void setup_pass(my::RenderGraph::SetupContext& ctx) override
    {
        ctx.read_write(_input_depth_names[0], Target, _output_depth_name);
        ctx.read(_input_depth_names[1], Sampled);
    }

    void retrieve_resources(
        my::Instance* my,
        my::ResourceContext* rc,
        const my::RenderGraph::ResourceContext& ctx) override
    {
        _target_depth = ctx.retrieve(_output_depth_name);
        _input_depth = ctx.retrieve(_input_depth_names[1]);

        {
            my::FramebufferAttachment attachments[] = {
                {my::Attachment::Depth, _target_depth},
            };

            my::FramebufferResource res;
            res.attachment_count = HRZ_ARRAY_COUNT(attachments);
            res.attachments = attachments;
            _fbo = ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::SceneView);
        }

        {
            my::SamplerResource res;
            res.sampler.wrap_x = my::SamplerParams::Wrap::Clamp;
            res.sampler.wrap_y = my::SamplerParams::Wrap::Clamp;
            res.sampler.wrap_z = my::SamplerParams::Wrap::Clamp;
            res.sampler.mag_filter = my::SamplerParams::Filter::Nearest;
            res.sampler.min_filter = my::SamplerParams::Filter::Nearest;
            res.use_mipmaps = false;
            _sampler =
                ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::SceneView);
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

            _vb = ((hrz::GpuResourceContext*)rc)
                      ->alloc(
                          &res, hrz::monitoring::systems::SceneView,
                          {{"contents"_ss, "depths merging full-screen triangle vertices"_ss}});
        }

        {
            const my::VertexInputStream streams[] = {
                {0, _vb, my::VertexFormat::Float32_2, 0, 0, my::VertexRate::PerVertex},
            };

            my::VertexInputResource res;
            res.attrib_count = HRZ_ARRAY_COUNT(streams);
            res.attribs = streams;

            _vi = ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::SceneView);
        }

        _shader = rc->retrieve_shader(hrz_shaders::MergeDepths_name);
    }

    static void collect_shaders(hrz::GpuResourceContext* rc)
    {
        my::IndexName attribs[] = {{0, "i_pos"}};

        my::IndexName samplers[] = {
            {0, "u_depth"},
        };

        my::ShaderResource res{};
        res.name = hrz_shaders::MergeDepths_name;
        res.link_hint = my::ShaderLinkHint::Initial;
        res.vertex_source_len = hrz_shaders::MergeDepths_vert_len;
        res.vertex_source = hrz_shaders::MergeDepths_vert;
        res.fragment_source_len = hrz_shaders::MergeDepths_frag_len;
        res.fragment_source = hrz_shaders::MergeDepths_frag;
        res.attrib_count = HRZ_ARRAY_COUNT(attribs);
        res.attribs = attribs;
        res.uniform_block_count = 0;
        res.sampler_count = HRZ_ARRAY_COUNT(samplers);
        res.samplers = samplers;
        res.output_count = 0;
        res.initial_state.color_blend.enable = false;
        res.initial_state.depth.test = true;
        res.initial_state.depth.write = true;
        res.initial_state.depth.compare = my::DepthState::Compare::Always;
        res.initial_state.stencil.enable = false;

        rc->alloc(&res, hrz::monitoring::systems::SceneView);
    }

    void destroy(hrz::Render* render)
    {
        render->rc->dealloc(_fbo);
        render->rc->dealloc(_sampler);
        render->rc->dealloc(_vb);
        render->rc->dealloc(_vi);
    }

    void execute_timed(const my::RenderGraph::ExecutionContext& ctx) override
    {
        HRZ_SCOPED_SAMPLE("Depth buffers merge pass");

        const my::ViewportState viewport_state = {
            {0, 0, ctx.backbuffer_width, ctx.backbuffer_height},
            {0, 0, ctx.backbuffer_width, ctx.backbuffer_height},
        };

        ctx.render->set_framebuffer(_fbo, viewport_state);

        ctx.binder->push_state();

        my::TextureBinding texture_bindings[] = {{0, _input_depth, _sampler}};
        ctx.binder->bind(HRZ_ARRAY_COUNT(texture_bindings), texture_bindings);

        auto state = ctx.binder->get_current_state();

        static const auto info = my::DrawBatchInfo(my::PrimitiveType::TriangleList, 3);

        ctx.render->draw(
            info, _shader, _vi, state.ubo_count, state.ubos, state.texture_count, state.textures);

        ctx.binder->pop_state();
    }
};

// In-world UI: Objects are depth-tested against the world objects.
// UI: Objects are not depth-tested, but they can read the world depth
//     to apply different styles if they want.
class InWorldUiPass : public hrz::render::TimedRenderPass
{
    const char* _input_color;
    const char* _input_depth;
    const char* _output_color;
    const char* _output_depth;
    my::ResourceHandle _target_color;
    my::ResourceHandle _target_depth;
    my::ResourceHandle _fbo;

public:
    InWorldUiPass(const char* input_color, const char* input_depth) :
        TimedRenderPass("in-world ui"),
        _input_color(input_color),
        _input_depth(input_depth),
        _output_color("in-world_ui_color"),
        _output_depth("in-world_ui_depth")
    {
    }

    const char* output_color() const { return _output_color; }

    const char* output_depth() const { return _output_depth; }

    void setup_pass(my::RenderGraph::SetupContext& ctx) override
    {
        ctx.read_write(_input_color, Target, _output_color);
        ctx.read_write(_input_depth, Target, _output_depth);
    }

    void retrieve_resources(
        my::Instance* my,
        my::ResourceContext* rc,
        const my::RenderGraph::ResourceContext& ctx) override
    {
        _target_color = ctx.retrieve(_output_color);
        _target_depth = ctx.retrieve(_output_depth);

        {
            my::FramebufferAttachment attachments[] = {
                {my::Attachment::Color0, _target_color},
                {my::Attachment::Depth, _target_depth},
            };

            my::FramebufferResource res;
            res.attachment_count = HRZ_ARRAY_COUNT(attachments);
            res.attachments = attachments;
            _fbo = ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::SceneView);
        }
    }

    void destroy(hrz::Render* render) { render->rc->dealloc(_fbo); }

    void execute_timed(const my::RenderGraph::ExecutionContext& ctx) override
    {
        HRZ_SCOPED_SAMPLE("in world ui draw");

        const hrz::SceneViewRenderGraphUserData* user_data =
            (const hrz::SceneViewRenderGraphUserData*)ctx.user_data;

        ctx.binder->push_state();

        ctx.render->set_framebuffer(
            _fbo,
            {
                {0, 0, ctx.backbuffer_width, ctx.backbuffer_height},
                {0, 0, ctx.backbuffer_width, ctx.backbuffer_height},
            });

        my::Renderer::BinMask pass_masks[] = {
            hrz::RenderInWorldBin,
        };

        ctx.renderer->draw(
            hrz::RenderVisual, user_data->main_view, HRZ_ARRAY_COUNT(pass_masks), pass_masks,
            ctx.render, ctx.binder, ctx.user_data);

        ctx.binder->pop_state();
    }
};

class DepthReductionPass : public hrz::render::TimedRenderPass
{
    struct DepthReductionUbo
    {
        float size;
        uint32_t _padding[3];
    };

    enum
    {
        PassCount = 5,
        MinBufferSize = 16,
        InitialBufferSize = MinBufferSize << PassCount,
        UboDepthReduction = hrz::UboCustomStart,
    };

    const char* _input_depth_name;
    my::ResourceHandle _input_depth;

    my::ResourceHandle _sampler;
    my::ResourceHandle _buffers[2];
    my::ResourceHandle _fbos[2];

    my::ResourceHandle _vb;
    my::ResourceHandle _vi;

    my::ResourceHandle _initial_shader;
    my::ResourceHandle _reduce_shader;
    my::ResourceHandle _ubo;
    my::ResourceHandle _readback_buffers[2];

    std::optional<uint64_t> _download_ids[2];

    float _last_near = HRZ_S_NEAR;
    float _last_far = HRZ_S_FAR;

    uint32_t _last_first_size = 0;
    uint32_t _iteration = 0;

public:
    explicit DepthReductionPass(const char* input_depth_name) :
        TimedRenderPass("depth reduction"), _input_depth_name(input_depth_name)
    {
    }

    void setup_pass(my::RenderGraph::SetupContext& ctx) override
    {
        ctx.read(_input_depth_name, Sampled);
    }

    void retrieve_resources(
        my::Instance* my,
        my::ResourceContext* rc,
        const my::RenderGraph::ResourceContext& ctx) override
    {
        _input_depth = ctx.retrieve(_input_depth_name);

        {
            my::TextureResource res;
            res.data = {};
            res.generate_mipmaps = false;
            res.layout.type = my::TextureLayout::Type2D;
            res.layout.width = InitialBufferSize;
            res.layout.height = InitialBufferSize;
            res.layout.depth = 1;
            res.layout.levels = 1;
            res.layout.format = my::TextureFormat::RG32F;

            my::FramebufferAttachment attachments[] = {
                {my::Attachment::Color0, my::ResourceHandle::null()},
            };

            my::FramebufferResource fb_res;
            fb_res.attachment_count = 1;
            fb_res.attachments = attachments;

            for (int i = 0; i < 2; ++i)
            {
                _buffers[i] =
                    ((hrz::GpuResourceContext*)rc)
                        ->alloc(
                            &res, hrz::monitoring::systems::SceneView,
                            {{"contents"_ss,
                              std::string("depth reduction buffer ") + std::to_string(i)}});
                attachments[0].texture_or_renderbuffer = _buffers[i];
                _fbos[i] = ((hrz::GpuResourceContext*)rc)
                               ->alloc(&fb_res, hrz::monitoring::systems::SceneView);
            }
        }

        {
            my::SamplerResource res;
            res.use_mipmaps = false;
            res.sampler.mag_filter = my::SamplerParams::Filter::Nearest;
            res.sampler.min_filter = my::SamplerParams::Filter::Nearest;
            res.sampler.wrap_x = my::SamplerParams::Wrap::Clamp;
            res.sampler.wrap_y = my::SamplerParams::Wrap::Clamp;
            res.sampler.is_shadow = false;

            _sampler =
                ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::SceneView);
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

            _vb = ((hrz::GpuResourceContext*)rc)
                      ->alloc(
                          &res, hrz::monitoring::systems::SceneView,
                          {{"contents"_ss, "depth reduction full-screen triangle vertices"_ss}});
        }

        {
            const my::VertexInputStream streams[] = {
                {0, _vb, my::VertexFormat::Float32_2, 0, 0, my::VertexRate::PerVertex},
            };

            my::VertexInputResource res;
            res.attrib_count = HRZ_ARRAY_COUNT(streams);
            res.attribs = streams;

            _vi = ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::SceneView);
        }

        _initial_shader = rc->retrieve_shader(hrz_shaders::DepthReductionInitialCopy_name);
        _reduce_shader = rc->retrieve_shader(hrz_shaders::DepthReductionReduce_name);

        {
            my::BufferResource res(my::BufferResource::Uniform);
            res.data = nullptr;
            res.size = sizeof(DepthReductionUbo);
            res.usage = my::UsageHint::Static;

            _ubo = ((hrz::GpuResourceContext*)rc)
                       ->alloc(
                           &res, hrz::monitoring::systems::SceneView,
                           {{"contents"_ss, "depth reduction parameters"_ss}});
        }

        {
            my::BufferResource res(my::BufferResource::TextureDownload);
            res.data = nullptr;
            res.size = MinBufferSize * MinBufferSize * sizeof(lm::vec4);
            res.usage = my::UsageHint::Download;

            _readback_buffers[0] = ((hrz::GpuResourceContext*)rc)
                                       ->alloc(
                                           &res, hrz::monitoring::systems::SceneView,
                                           {{"contents"_ss, "depth reduction readback buffer"_ss}});
            _readback_buffers[1] = ((hrz::GpuResourceContext*)rc)
                                       ->alloc(
                                           &res, hrz::monitoring::systems::SceneView,
                                           {{"contents"_ss, "depth reduction readback buffer"_ss}});
        }
    }

    static void collect_shaders(hrz::GpuResourceContext* rc)
    {
        {
            my::IndexName attribs[] = {{0, "i_pos"}};

            my::IndexName ubos[] = {
                {hrz::UboFrame, "Frame"},
                {UboDepthReduction, "DepthReduction"},
            };

            my::IndexName samplers[] = {{0, "u_depth"}};

            const char* outputs[] = {"o_min_max"};

            my::ShaderResource res{};
            res.name = hrz_shaders::DepthReductionInitialCopy_name;
            res.link_hint = my::ShaderLinkHint::Initial;
            res.vertex_source_len = hrz_shaders::DepthReductionInitialCopy_vert_len;
            res.vertex_source = hrz_shaders::DepthReductionInitialCopy_vert;
            res.fragment_source_len = hrz_shaders::DepthReductionInitialCopy_frag_len;
            res.fragment_source = hrz_shaders::DepthReductionInitialCopy_frag;
            res.attrib_count = HRZ_ARRAY_COUNT(attribs);
            res.attribs = attribs;
            res.uniform_block_count = HRZ_ARRAY_COUNT(ubos);
            res.uniform_blocks = ubos;
            res.sampler_count = HRZ_ARRAY_COUNT(samplers);
            res.samplers = samplers;
            res.output_count = HRZ_ARRAY_COUNT(outputs);
            res.outputs = outputs;
            res.initial_state.color_blend.enable = false;
            res.initial_state.color_blend.mask = my::ColorBlendState::RGBA;
            res.initial_state.depth.test = false;
            res.initial_state.rasterization.cull_mode = my::RasterizationState::None;
            res.initial_state.stencil.enable = false;

            rc->alloc(&res, hrz::monitoring::systems::SceneView);
        }

        {
            my::IndexName attribs[] = {{0, "i_pos"}};

            my::IndexName ubos[] = {
                {hrz::UboFrame, "Frame"},
            };

            my::IndexName samplers[] = {{0, "u_depth"}};

            const char* outputs[] = {"o_min_max"};

            my::ShaderResource res{};
            res.name = hrz_shaders::DepthReductionReduce_name;
            res.link_hint = my::ShaderLinkHint::Initial;
            res.vertex_source_len = hrz_shaders::DepthReductionReduce_vert_len;
            res.vertex_source = hrz_shaders::DepthReductionReduce_vert;
            res.fragment_source_len = hrz_shaders::DepthReductionReduce_frag_len;
            res.fragment_source = hrz_shaders::DepthReductionReduce_frag;
            res.attrib_count = HRZ_ARRAY_COUNT(attribs);
            res.attribs = attribs;
            res.uniform_block_count = HRZ_ARRAY_COUNT(ubos);
            res.uniform_blocks = ubos;
            res.sampler_count = HRZ_ARRAY_COUNT(samplers);
            res.samplers = samplers;
            res.output_count = HRZ_ARRAY_COUNT(outputs);
            res.outputs = outputs;
            res.initial_state.color_blend.enable = false;
            res.initial_state.color_blend.mask = my::ColorBlendState::RGBA;
            res.initial_state.depth.test = false;
            res.initial_state.rasterization.cull_mode = my::RasterizationState::None;
            res.initial_state.stencil.enable = false;

            rc->alloc(&res, hrz::monitoring::systems::SceneView);
        }
    }

    void destroy(hrz::Render* render)
    {
        for (int i = 0; i < 2; ++i)
        {
            if (_download_ids[i])
            {
                render->my->cancel_texture_download(_download_ids[i].value());
                hrz::render::release_texture_download_id(_download_ids[i].value());
            }

            render->rc->dealloc(_readback_buffers[i]);
        }

        render->rc->dealloc(_sampler);
        render->rc->dealloc(_vi);
        render->rc->dealloc(_vb);
        render->rc->dealloc(_ubo);

        for (auto fbo : _fbos)
        {
            render->rc->dealloc(fbo);
        }

        for (auto buffer : _buffers)
        {
            render->rc->dealloc(buffer);
        }
    }

    void execute_timed(const my::RenderGraph::ExecutionContext& ctx) override
    {
        HRZ_SCOPED_SAMPLE("depth reduction draw");

        uint32_t first_size = std::max(
            (uint32_t)MinBufferSize,
            std::min(
                (uint32_t)InitialBufferSize,
                hrz::next_power_of_two(std::max(ctx.backbuffer_width, ctx.backbuffer_height))));

        if (first_size != _last_first_size)
        {
            _last_first_size = first_size;

            DepthReductionUbo data;
            data.size = (float)first_size;

            ctx.render->update_buffer(_ubo, 0, sizeof(DepthReductionUbo), &data);
        }

        int read_fbo = 0;
        int write_fbo = 1;

        auto swap_fbos = [&]() { std::swap(read_fbo, write_fbo); };

        ctx.binder->push_state();

        {
            ctx.render->set_framebuffer(
                _fbos[write_fbo],
                {
                    {0, 0, first_size, first_size},
                    {0, 0, first_size, first_size},
                });

            my::TextureBinding texture_binding = {0, _input_depth, _sampler};

            my::UboBinding ubo_binding = {UboDepthReduction, _ubo, 0, sizeof(DepthReductionUbo)};

            ctx.binder->bind(1, &texture_binding);
            ctx.binder->bind(1, &ubo_binding);

            auto state = ctx.binder->get_current_state();

            static const auto info = my::DrawBatchInfo(my::PrimitiveType::TriangleList, 3);

            ctx.render->draw(
                info, _initial_shader, _vi, state.ubo_count, state.ubos, state.texture_count,
                state.textures);

            swap_fbos();
        }

        uint32_t size = first_size / 2;

        while (size >= MinBufferSize)
        {
            ctx.render->set_framebuffer(
                _fbos[write_fbo],
                {
                    {0, 0, size, size},
                    {0, 0, size, size},
                });

            my::TextureBinding texture_binding = {0, _buffers[read_fbo], _sampler};

            ctx.binder->bind(1, &texture_binding);

            auto state = ctx.binder->get_current_state();

            static const auto info = my::DrawBatchInfo(my::PrimitiveType::TriangleList, 3);

            ctx.render->draw(
                info, _reduce_shader, _vi, state.ubo_count, state.ubos, state.texture_count,
                state.textures);

            swap_fbos();

            size /= 2;
        }

        ctx.binder->pop_state();

        auto iteration_index = _iteration % 2;
        _download_ids[iteration_index] = hrz::render::acquire_texture_download_id();
        ctx.render->color_texture_download_async(
            _download_ids[iteration_index].value(), _fbos[read_fbo], my::Attachment::Color0,
            {0, 0, MinBufferSize, MinBufferSize}, my::TextureDownloadFormat::RGBA32F,
            _readback_buffers[iteration_index]);

        _iteration += 1;
    }

    void process_data(gsl::span<const lm::vec4> data)
    {
        double near = 1.0;
        double far = 0.0;

        for (const lm::vec4& v : data)
        {
            if (v.x < 1.0)
            {
                near = std::min(near, (double)v.x);
            }

            if (v.y < 1.0)
            {
                far = std::max(far, (double)v.y);
            }
        }

        if (near > far) std::swap(near, far);

        _last_near = (float)hrz::render::undo_log_depth(near);
        _last_far = (float)hrz::render::undo_log_depth(far);
    }

    void update(hrz::Render* ctx)
    {
        if (_download_ids[_iteration % 2].has_value())
        {
            HRZ_SCOPED_SAMPLE("depth reduction update");

            if (ctx->my->is_texture_download_ready(_download_ids[_iteration % 2].value()))
            {
                my::TextureDownloadData data =
                    ctx->my->retrieve_texture_download(_download_ids[_iteration % 2].value());
                hrz::render::release_texture_download_id(_download_ids[_iteration % 2].value());

                gsl::span<lm::vec4> data_typed(
                    (lm::vec4*)data.data.get(), MinBufferSize * MinBufferSize);

                process_data(data_typed);
            }
            else
            {
                HRZ_LOG_ERROR("Texture download should have been ready...");
            }
            _download_ids[_iteration % 2].reset();
        }
    }

    lm::vec2 get_near_far() { return lm::vec2(_last_near, _last_far); }
};

class HighlightGatherPass : public hrz::render::TimedRenderPass
{
    const char* _input_overlays[HRZ_S_MAX_OVERLAY_CASCADES];
    const char* _input_camera_height;
    const char* _output_color;
    const char* _output_depth;
    my::ResourceHandle _target_color;
    my::ResourceHandle _target_depth;
    my::ResourceHandle _fbo;

    my::ResourceHandle _overlay_textures[HRZ_S_MAX_OVERLAY_CASCADES];
    my::ResourceHandle _overlay_texture_sampler;
    my::ResourceHandle _camera_height_texture;
    my::ResourceHandle _camera_height_sampler;

    bool _should_execute = false;

public:
    HighlightGatherPass(
        const hrz::VectorFlatOverlaySystem* flat_overlay,
        const char* input_camera_height) :
        TimedRenderPass("highlight gather"),
        _input_camera_height(input_camera_height),
        _output_color("highlight gather color"),
        _output_depth("highlight gather depth")
    {
        hrz::vector_flat_overlay::get_selection_target_names(flat_overlay, _input_overlays);
    }

    const char* output_color() const { return _output_color; }

    const char* output_depth() const { return _output_depth; }

    void set_should_execute(bool should_execute) { _should_execute = should_execute; }

    void setup_pass(my::RenderGraph::SetupContext& ctx) override
    {
        for (const char* name : _input_overlays)
        {
            ctx.read(name, Sampled);
        }

        my::RenderGraph::ResourceInfo res;
        res.width = 1.0f;
        res.height = 1.0f;
        res.size_class = my::RenderGraph::ResourceInfo::BackbufferRelative;
        res.format = my::TextureFormat::R8;

        // We need this to be the exact same format used for the main
        // framebuffer that it's going to be compared to, so that comparison is
        // nice and stable.
        my::RenderGraph::ResourceInfo depth;
        depth.format = my::TextureFormat::Depth32F;
        depth.size_class = my::RenderGraph::ResourceInfo::BackbufferRelative;
        depth.width = 1;
        depth.height = 1;

        ctx.create(_output_color, Target, res);
        ctx.create(_output_depth, Target, depth);
        ctx.read(_input_camera_height, Sampled);
    }

    void retrieve_resources(
        my::Instance* my,
        my::ResourceContext* rc,
        const my::RenderGraph::ResourceContext& ctx) override
    {
        _target_color = ctx.retrieve(_output_color);
        _target_depth = ctx.retrieve(_output_depth);
        _camera_height_texture = ctx.retrieve(_input_camera_height);

        for (uint32_t i = 0; i < HRZ_S_MAX_OVERLAY_CASCADES; i++)
        {
            _overlay_textures[i] = ctx.retrieve(_input_overlays[i]);
        }

        {
            my::SamplerResource res;
            res.sampler.wrap_x = my::SamplerParams::Wrap::Clamp;
            res.sampler.wrap_y = my::SamplerParams::Wrap::Clamp;
            res.sampler.wrap_z = my::SamplerParams::Wrap::Clamp;
            res.sampler.mag_filter = my::SamplerParams::Filter::Linear;
            res.sampler.min_filter = my::SamplerParams::Filter::Linear;
            res.use_mipmaps = false;
            _overlay_texture_sampler = ((hrz::GpuResourceContext*)rc)
                                           ->alloc(
                                               &res, hrz::monitoring::systems::SceneView,
                                               {{"contents"_ss, "highlight overlay"_ss}});
        }

        {
            my::SamplerResource res;
            res.sampler.wrap_x = my::SamplerParams::Wrap::Clamp;
            res.sampler.wrap_y = my::SamplerParams::Wrap::Clamp;
            res.sampler.wrap_z = my::SamplerParams::Wrap::Clamp;
            res.sampler.mag_filter = my::SamplerParams::Filter::Nearest;
            res.sampler.min_filter = my::SamplerParams::Filter::Nearest;
            res.sampler.is_shadow = false;
            res.use_mipmaps = false;
            _camera_height_sampler =
                ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::SceneView);
        }

        {
            my::FramebufferAttachment attachments[] = {
                {my::Attachment::Depth, _target_depth},
                {my::Attachment::Color0, _target_color},
            };

            my::FramebufferResource res;
            res.attachment_count = HRZ_ARRAY_COUNT(attachments);
            res.attachments = attachments;
            _fbo = ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::SceneView);
        }
    }

    void destroy(hrz::Render* render)
    {
        render->rc->dealloc(_fbo);
        render->rc->dealloc(_overlay_texture_sampler);
        render->rc->dealloc(_camera_height_sampler);
    }

    void execute_timed(const my::RenderGraph::ExecutionContext& ctx) override
    {
        HRZ_SCOPED_SAMPLE("highlight gather pass");

        if (!_should_execute)
        {
            return;
        }

        const hrz::SceneViewRenderGraphUserData* user_data =
            (const hrz::SceneViewRenderGraphUserData*)ctx.user_data;

        const my::ViewportState viewport_state = {
            {0, 0, ctx.backbuffer_width, ctx.backbuffer_height},
            {0, 0, ctx.backbuffer_width, ctx.backbuffer_height},
        };

        ctx.binder->push_state();

        hrz::StaticVector<my::TextureBinding, HRZ_S_MAX_OVERLAY_CASCADES + 1> bindings;

        for (int i = 0; i < HRZ_S_MAX_OVERLAY_CASCADES; i++)
        {
            bindings.push_back(
                {hrz::vector_flat_overlay::SamplerOverlayStart + i, _overlay_textures[i],
                 _overlay_texture_sampler});
        }

        bindings.push_back(
            {hrz::SamplerCameraHeight, _camera_height_texture, _camera_height_sampler});

        ctx.binder->bind(bindings.size(), bindings.data());

        ctx.render->set_framebuffer(_fbo, viewport_state);

        my::ClearTarget clears[] = {
            {my::Attachment::Color0, my::ClearValue::make_color_float(0, 0, 0, 0)},
            {my::Attachment::Depth, my::ClearValue::make_depth(1.0)}};
        ctx.render->clear(HRZ_ARRAY_COUNT(clears), clears);

        my::Renderer::BinMask pass_masks[] = {
            hrz::RenderWorldOpaqueBin | hrz::RenderWorldTransparentBin | hrz::RenderSymbolicBin
                | hrz::RenderSymbolicOverlayBin,
        };

        ctx.renderer->draw(
            hrz::RenderSelection, user_data->main_view, HRZ_ARRAY_COUNT(pass_masks), pass_masks,
            ctx.render, ctx.binder, ctx.user_data);

        ctx.binder->pop_state();
    }
};

class HighlightApplyPass : public hrz::render::TimedRenderPass
{
    const char* _input_highlight_name;
    const char* _input_highlight_depth_name;
    const char* _input_color_name;
    const char* _input_depth_name;
    const char* _output_name;
    my::ResourceHandle _highlight_texture;
    my::ResourceHandle _color_target;
    my::ResourceHandle _scene_depth_target;
    my::ResourceHandle _highlight_depth_target;
    my::ResourceHandle _fbo;
    my::ResourceHandle _highlight_sampler;
    my::ResourceHandle _depth_sampler;
    my::ResourceHandle _vb;
    my::ResourceHandle _vi;
    my::ResourceHandle _shader;
    my::ResourceHandle _ubo;
    bool _should_execute = false;

    struct UboData
    {
        lm::vec3 color;
        float fill_alpha;
        float outline_alpha;
        float outline_size;
        float occlusion_alpha;
        uint32_t _padding[1];
    } _ubo_data;

    static_assert(sizeof(UboData) == 32, "Highlight UBO size");

    bool _ubo_updated = false;

    enum
    {
        HighlightUboLocation = hrz::UboCustomStart,
    };

public:
    HighlightApplyPass(
        const char* input_scene_color_name,
        const char* input_scene_depth_name,
        const char* input_highlight_name,
        const char* input_highlight_depth_name) :
        TimedRenderPass("highlight apply"),
        _input_highlight_name(input_highlight_name),
        _input_highlight_depth_name(input_highlight_depth_name),
        _input_color_name(input_scene_color_name),
        _input_depth_name(input_scene_depth_name),
        _output_name("highlight output color")
    {
    }

    void set_should_execute(bool should_execute) { _should_execute = should_execute; }

    void set_settings(const hrz_proto::HighlightSettings& settings)
    {
        lm::vec4 selection_color = hrz::to_lm(settings.selection_color());
        _ubo_data.color = selection_color.rgb;
        _ubo_data.fill_alpha = selection_color.a;
        _ubo_data.outline_alpha = settings.selection_outline_alpha();
        _ubo_data.outline_size = settings.selection_outline_size();
        _ubo_data.occlusion_alpha = settings.selection_occlusion_alpha();
        _ubo_updated = true;
    }

    const char* output_color() const { return _output_name; }

    void setup_pass(my::RenderGraph::SetupContext& ctx) override
    {
        ctx.read(_input_highlight_name, Sampled);
        ctx.read(_input_depth_name, Sampled);
        ctx.read(_input_highlight_depth_name, Sampled);
        ctx.read_write(_input_color_name, Target, _output_name);
    }

    void retrieve_resources(
        my::Instance* my,
        my::ResourceContext* rc,
        const my::RenderGraph::ResourceContext& ctx) override
    {
        _highlight_texture = ctx.retrieve(_input_highlight_name);
        _color_target = ctx.retrieve(_input_color_name);
        _scene_depth_target = ctx.retrieve(_input_depth_name);
        _highlight_depth_target = ctx.retrieve(_input_highlight_depth_name);

        {
            my::FramebufferAttachment attachments[] = {
                {my::Attachment::Color0, _color_target},
            };

            my::FramebufferResource res;
            res.attachment_count = HRZ_ARRAY_COUNT(attachments);
            res.attachments = attachments;
            _fbo = ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::SceneView);
        }

        {
            my::SamplerResource res;
            res.use_mipmaps = false;
            res.sampler.mag_filter = my::SamplerParams::Filter::Linear;
            res.sampler.min_filter = my::SamplerParams::Filter::Linear;
            res.sampler.wrap_x = my::SamplerParams::Wrap::Clamp;
            res.sampler.wrap_y = my::SamplerParams::Wrap::Clamp;
            res.sampler.is_shadow = false;

            _highlight_sampler =
                ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::SceneView);
        }

        {
            my::SamplerResource res;
            res.use_mipmaps = false;
            res.sampler.mag_filter = my::SamplerParams::Filter::Nearest;
            res.sampler.min_filter = my::SamplerParams::Filter::Nearest;
            res.sampler.wrap_x = my::SamplerParams::Wrap::Clamp;
            res.sampler.wrap_y = my::SamplerParams::Wrap::Clamp;
            res.sampler.is_shadow = false;

            _depth_sampler =
                ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::SceneView);
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

            _vb = ((hrz::GpuResourceContext*)rc)
                      ->alloc(
                          &res, hrz::monitoring::systems::SceneView,
                          {{"contents"_ss, "highlight full-screen triangle vertices"_ss}});
        }

        {
            const my::VertexInputStream streams[] = {
                {0, _vb, my::VertexFormat::Float32_2, 0, 0, my::VertexRate::PerVertex},
            };

            my::VertexInputResource res;
            res.attrib_count = HRZ_ARRAY_COUNT(streams);
            res.attribs = streams;

            _vi = ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::SceneView);
        }

        _shader = rc->retrieve_shader(hrz_shaders::HighlightApply_name);

        {
            my::BufferResource res(my::BufferResource::BufferType::Uniform);
            res.usage = my::UsageHint::Updatable;
            res.size = sizeof(UboData);
            res.data = nullptr;

            _ubo = ((hrz::GpuResourceContext*)rc)
                       ->alloc(
                           &res, hrz::monitoring::systems::SceneView,
                           {{"contents"_ss, "highlight parameters"_ss}});
        }
    }

    static void collect_shaders(hrz::GpuResourceContext* rc)
    {
        my::IndexName attribs[] = {{0, "i_pos"}};

        my::IndexName ubos[] = {
            {hrz::UboFrame, "Frame"},
            {HighlightUboLocation, "Highlight"},
        };

        my::IndexName samplers[] = {
            {0, "u_selection"},
            {1, "u_scene_depth"},
            {2, "u_selection_depth"}};

        const char* outputs[] = {"o_color"};

        my::ShaderResource res{};
        res.name = hrz_shaders::HighlightApply_name;
        res.link_hint = my::ShaderLinkHint::Initial;
        res.vertex_source_len = hrz_shaders::HighlightApply_vert_len;
        res.vertex_source = hrz_shaders::HighlightApply_vert;
        res.fragment_source_len = hrz_shaders::HighlightApply_frag_len;
        res.fragment_source = hrz_shaders::HighlightApply_frag;
        res.attrib_count = HRZ_ARRAY_COUNT(attribs);
        res.attribs = attribs;
        res.uniform_block_count = HRZ_ARRAY_COUNT(ubos);
        res.uniform_blocks = ubos;
        res.sampler_count = HRZ_ARRAY_COUNT(samplers);
        res.samplers = samplers;
        res.output_count = HRZ_ARRAY_COUNT(outputs);
        res.outputs = outputs;
        res.initial_state.color_blend.enable = true;
        res.initial_state.color_blend.mask = my::ColorBlendState::RGBA;
        res.initial_state.color_blend.color.op = my::ColorBlendState::Op::Add;
        res.initial_state.color_blend.color.src = my::ColorBlendState::Factor::One;
        res.initial_state.color_blend.color.dst = my::ColorBlendState::Factor::OneMinusSrcAlpha;
        res.initial_state.depth.test = false;
        res.initial_state.rasterization.cull_mode = my::RasterizationState::None;
        res.initial_state.stencil.enable = false;

        rc->alloc(&res, hrz::monitoring::systems::SceneView);
    }

    void destroy(hrz::Render* render)
    {
        render->rc->dealloc(_fbo);
        render->rc->dealloc(_highlight_sampler);
        render->rc->dealloc(_depth_sampler);
        render->rc->dealloc(_vi);
        render->rc->dealloc(_vb);
        render->rc->dealloc(_ubo);
    }

    void execute_timed(const my::RenderGraph::ExecutionContext& ctx) override
    {
        HRZ_SCOPED_SAMPLE("highlight apply pass");

        if (!_should_execute)
        {
            return;
        }

        if (_ubo_updated)
        {
            ctx.render->update_buffer(_ubo, 0, sizeof(UboData), &_ubo_data);
            _ubo_updated = false;
        }

        const my::ViewportState viewport_state = {
            {0, 0, ctx.backbuffer_width, ctx.backbuffer_height},
            {0, 0, ctx.backbuffer_width, ctx.backbuffer_height},
        };

        ctx.render->set_framebuffer(_fbo, viewport_state);

        ctx.binder->push_state();

        my::TextureBinding binding[] = {
            {0, _highlight_texture, _highlight_sampler},
            {1, _scene_depth_target, _depth_sampler},
            {2, _highlight_depth_target, _depth_sampler}};
        ctx.binder->bind(HRZ_ARRAY_COUNT(binding), binding);

        my::UboBinding ubo_binding = {HighlightUboLocation, _ubo, 0, sizeof(UboData)};
        ctx.binder->bind(1, &ubo_binding);

        auto state = ctx.binder->get_current_state();

        static const auto info = my::DrawBatchInfo(my::PrimitiveType::TriangleList, 3);

        ctx.render->draw(
            info, _shader, _vi, state.ubo_count, state.ubos, state.texture_count, state.textures);

        ctx.binder->pop_state();
    }
};

} // namespace

namespace hrz
{
struct SceneView
{
    VectorFlatOverlaySystem* flat_overlay;
    HeatmapSystem* heatmaps;
    PlanetGeometry* planet_geometry;
    CameraHeightSystem* camera_height_system;
    SkySystem* sky;
    std::optional<ShadowsSystem*> shadows;
    PickingSystem* picking_system;
    ViewshedsSystem* viewsheds;

    my::RenderGraph* rg;
    my::RenderGraphSubsetId rg_picking_subset;
    my::RenderGraphSubsetId rg_planet_feedback_subset;
    render::DoubleBufferedUniformBuffer<FrameUniformData> frame_uniforms;

    std::unique_ptr<InitPass> init_pass;
    std::unique_ptr<WorldForwardPass> world_forward_pass;
    std::unique_ptr<DecalPass> decal_pass;
    std::unique_ptr<SymbolicForwardPass> symbolic_forward_pass;
    std::unique_ptr<MergeDepthsPass> merge_perceived_depth_pass;
    std::unique_ptr<InWorldUiPass> in_world_ui_pass;
    std::unique_ptr<UiPass> ui_pass;
    std::unique_ptr<DepthReductionPass> depth_reduction_pass;
    std::unique_ptr<HighlightGatherPass> highlight_gather_pass;
    std::unique_ptr<HighlightApplyPass> highlight_apply_pass;
    std::unique_ptr<FinalPass> final_pass;

    hrz_proto::CameraIndex camera_index;
    hrz_proto::SceneViewIndex view_index;
    my::Renderer::ViewId main_view_id;

    // We cache these info so that retrieving them is very quick.
    // It's cheap to compute every frame anyway.
    hrz_proto::ViewScaleAltitude view_scale_altitude;

    bool highlight_settings_updated = false;
    bool viewport_settings_updated = false;
    bool terrain_settings_updated = false;
    bool ambient_settings_updated = false;
    bool associated_camera_updated = false;
    bool lighting_settings_updated = false;

    lm::vec4 terrain_color_opacity;
    int32_t terrain_clip_id = -1;
    render::LightingSettings global_lighting;
    render::LightingSettings terrain_lighting;

    lm::vec4 quick_highlight_color;

    // This is the actual view viewport on the canvas, taking into account both
    // viewport and subfrustum. Origin is bottom-left (like OpenGL).
    lm::ibbox2 viewport_on_canvas;

    // Same as above but origin is top-left. It's used to filter events.
    // Computed from the info above.
    lm::ibbox2 events_viewport_on_canvas;

    uint32_t flat_overlay_texture_size;
};

namespace scene
{
template<typename T>
lm::Bbox<T, 2> invert_y_axis(lm::Bbox<T, 2> bbox, const lm::Vector<T, 2>& size)
{
    bbox.min.y = size.y - bbox.min.y;
    bbox.max.y = size.y - bbox.max.y;
    std::swap(bbox.min.y, bbox.max.y);
    return bbox;
}

ViewportInfo compute_viewport_info(
    hrz_proto::SceneViewIndex view_index,
    SceneModel* model,
    float device_pixel_ratio,
    lm::uvec2 canvas_size)
{
    SceneModelAccessor accessor(model);
    hrz_proto::SceneViewSettingsPathBuilder<SceneModelAccessor> builder(accessor, view_index);

    auto viewport_settings = builder.clone().viewport().get();

    lm::bbox2 viewport = invert_y_axis(to_lm(viewport_settings.viewport()), {1.0f, 1.0f});
    lm::bbox2 subfrustum = invert_y_axis(to_lm(viewport_settings.scissor()), {1.0f, 1.0f});

    ViewportInfo viewport_info{};
    viewport_info.device_pixel_ratio = device_pixel_ratio;
    viewport_info.near = HRZ_S_NEAR;
    viewport_info.far = HRZ_S_FAR;
    viewport_info.size = canvas_size * lm::size(viewport);
    viewport_info.subfrustum = subfrustum;

    return viewport_info;
}

void update_viewport(
    SceneView* view,
    SceneModel* model,
    lm::uvec2 canvas_size,
    float device_pixel_ratio)
{
    SceneModelAccessor accessor(model);
    hrz_proto::SceneViewSettingsPathBuilder<SceneModelAccessor> builder(accessor, view->view_index);

    auto viewport_settings = builder.clone().viewport().get();

    lm::bbox2 viewport = invert_y_axis(to_lm(viewport_settings.viewport()), {1.0f, 1.0f});
    lm::bbox2 subfrustum = invert_y_axis(to_lm(viewport_settings.scissor()), {1.0f, 1.0f});

    lm::vec2 viewport_size = lm::size(viewport);

    view->viewport_on_canvas = {
        lm::ivec2{lm::round((viewport.min + viewport_size * subfrustum.min) * canvas_size)},
        lm::ivec2{lm::round((viewport.min + viewport_size * subfrustum.max) * canvas_size)}};

    view->events_viewport_on_canvas =
        invert_y_axis(view->viewport_on_canvas, lm::ivec2(canvas_size));
    view->events_viewport_on_canvas.max -= {1, 1};
}

RenderRequest refresh_camera_and_viewport(
    SceneView* view,
    SceneModel* model,
    lm::uvec2 canvas_size,
    float device_pixel_ratio)
{
    RenderRequest render_request;

    SceneModelAccessor accessor(model);
    hrz_proto::SceneViewSettingsPathBuilder<SceneModelAccessor> builder(accessor, view->view_index);

    if (view->associated_camera_updated)
    {
        view->camera_index = builder.clone().camera().get();
        view->associated_camera_updated = false;
        render_request.request_visual_render();
        render_request.schedule_planet_feedback();
        render_request.schedule_flat_overlay_render();
    }

    if (view->viewport_settings_updated)
    {
        update_viewport(view, model, canvas_size, device_pixel_ratio);
        view->viewport_settings_updated = false;
        render_request.request_visual_render();
        render_request.schedule_planet_feedback();
        render_request.schedule_flat_overlay_render();
    }

    return render_request;
}

SceneView* create_scene_view(
    SceneModel* scene_model,
    hrz_proto::SceneViewIndex view_index,
    lm::uvec2 canvas_size,
    float device_pixel_ratio,
    uint32_t flat_overlay_cascade_count,
    uint32_t flat_overlay_texture_size,
    uint32_t shadow_map_cascade_count)
{
    SceneView* view = new SceneView();

    view->picking_system = hrz::picking::create_system();
    view->planet_geometry = planet::create_geometry();
    view->camera_height_system = camera_height::create_system();
    view->sky = sky::create();
    if (hrz::get_flag(hrz::Flag::EnableShadows))
    {
        view->shadows = shadows::create(shadow_map_cascade_count);
    }
    view->viewsheds = viewsheds::create();
    view->flat_overlay = vector_flat_overlay::create_system(flat_overlay_cascade_count);
    view->heatmaps = heatmaps::create_system(view_index);
    view->view_index = view_index;
    view->rg = my::RenderGraph::create();

    view->flat_overlay_texture_size = flat_overlay_texture_size;

    {
        auto path_builder = hrz_proto::SceneViewSettingsPathBuilder<int>(0, view_index);
        auto path = scene_model::SceneViewSettingsPath(path_builder._path);
        notify_model_update(view, scene_model::UpdateType::Set, path);
    }

    refresh_camera_and_viewport(view, scene_model, canvas_size, device_pixel_ratio);

    return view;
}

void destroy_scene_view(SceneView* view, AssetsLoader* al, JobScheduler* js, Render* render_global)
{
    RenderView render(*render_global, view->rg);

    vector_flat_overlay::destroy_system(view->flat_overlay, &render);
    heatmaps::destroy_system(view->heatmaps, &render);
    planet::destroy(view->planet_geometry, js, &render);
    camera_height::destroy_system(view->camera_height_system, &render);
    sky::destroy(view->sky, &render);
    if (view->shadows.has_value())
    {
        shadows::destroy(view->shadows.value(), &render);
    }
    viewsheds::destroy(view->viewsheds, &render);
    picking::destroy_system(view->picking_system, &render);

    view->init_pass->destroy(&render);
    view->world_forward_pass->destroy(&render);
    view->decal_pass->destroy(&render);
    view->symbolic_forward_pass->destroy(&render);
    view->in_world_ui_pass->destroy(&render);
    view->ui_pass->destroy(&render);
    view->depth_reduction_pass->destroy(&render);
    view->highlight_gather_pass->destroy(&render);
    view->highlight_apply_pass->destroy(&render);
    view->frame_uniforms.destroy(&render);

    view->rg->free(render.my);

    delete view->rg;
    delete view;
}

void initialize_rendering(SceneView* view, Render* render_global)
{
    HRZ_SCOPED_SAMPLE("scene view initialize rendering");

    RenderView render(*render_global, view->rg);

    std::vector<my::RenderPassId> final_passes;

    view->frame_uniforms.initialize(
        &render, hrz::monitoring::systems::SceneView, {{"contents"_ss, "frame parameters"_ss}});
    FrameUniformData frame_uniforms;
    // These values are checked in work(), so they need to be initialised.
    frame_uniforms.quick_highlight_feature_reference = {0, 0, 0};
    frame_uniforms.debug_flags = 0;
    view->frame_uniforms.set(0, frame_uniforms);

    camera_height::init_render(view->camera_height_system, &render);

    heatmaps::initialize_rendering(
        view->heatmaps, view->flat_overlay_texture_size, view->camera_height_system, &render);
    heatmaps::add_passes_to_render_graph(view->heatmaps, &render);

    vector_flat_overlay::initialize_rendering(
        view->flat_overlay, view->flat_overlay_texture_size, &render, view->camera_height_system,
        view->heatmaps);
    vector_flat_overlay::add_passes_to_render_graph(view->flat_overlay, &render);

    auto picking_pass_id = picking::initialize_rendering(
        view->picking_system, view->camera_height_system, view->flat_overlay,
        view->flat_overlay_texture_size, &render);
    final_passes.push_back(picking_pass_id);

    const char* camera_height_name = camera_height::get_target_name(view->camera_height_system);

    if (view->shadows.has_value())
    {
        shadows::init_render(view->shadows.value(), &render, camera_height_name);
    }

    viewsheds::init_render(view->viewsheds, &render, camera_height_name);

    view->init_pass.reset(new InitPass());
    view->rg->add_pass("init", view->init_pass.get());

    sky::init_render_precompute(view->sky, &render);
    sky::init_render_background(view->sky, &render, view->init_pass->output_color());

    view->world_forward_pass.reset(new WorldForwardPass(
        sky::get_background_color_output(view->sky), view->init_pass->output_depth(),
        camera_height_name, view->flat_overlay, view->shadows, view->viewsheds, view->sky));

    view->rg->add_pass("world forward", view->world_forward_pass.get());

    view->decal_pass.reset(new DecalPass(
        view->world_forward_pass->output_color(), view->world_forward_pass->output_depth(),
        camera_height_name));

    view->rg->add_pass("decal", view->decal_pass.get());

    sky::init_render_world(
        view->sky, &render, view->decal_pass->output_color(),
        view->decal_pass->output_depth_stencil());

    view->symbolic_forward_pass.reset(new SymbolicForwardPass(
        sky::get_world_color_output(view->sky), sky::get_world_depth_output(view->sky),
        camera_height_name));

    view->rg->add_pass("symbolic forward", view->symbolic_forward_pass.get());

    view->merge_perceived_depth_pass.reset(new MergeDepthsPass(
        view->symbolic_forward_pass->output_depth(),
        view->symbolic_forward_pass->output_overlay_depth()));

    view->rg->add_pass("merge depths", view->merge_perceived_depth_pass.get());

    view->in_world_ui_pass.reset(new InWorldUiPass(
        view->symbolic_forward_pass->output_color(),
        view->merge_perceived_depth_pass->output_perceived_depth()));

    view->rg->add_pass("in-world ui", view->in_world_ui_pass.get());

    view->depth_reduction_pass.reset(
        new DepthReductionPass(view->in_world_ui_pass->output_depth()));
    my::RenderPassId depth_reduction_pass_id =
        view->rg->add_pass("scene depth reduction", view->depth_reduction_pass.get());
    final_passes.push_back(depth_reduction_pass_id);

    my::RenderPassId planet_feedback_pass_id =
        planet::initialize_rendering(view->planet_geometry, &render);
    final_passes.push_back(planet_feedback_pass_id);

    view->highlight_gather_pass.reset(
        new HighlightGatherPass(view->flat_overlay, camera_height_name));
    view->rg->add_pass("highlight gather", view->highlight_gather_pass.get());

    view->highlight_apply_pass.reset(new HighlightApplyPass(
        view->in_world_ui_pass->output_color(), view->in_world_ui_pass->output_depth(),
        view->highlight_gather_pass->output_color(), view->highlight_gather_pass->output_depth()));
    view->rg->add_pass("highlight apply", view->highlight_apply_pass.get());

    if (hrz::get_flag(hrz::Flag::EnabledDepthPeelingForUiElements))
    {
        view->ui_pass.reset(new UiDepthPeelingPass(
            view->highlight_apply_pass->output_color(), view->in_world_ui_pass->output_depth()));
    }
    else
    {
        view->ui_pass.reset(new UiForwardPass(
            view->highlight_apply_pass->output_color(), view->in_world_ui_pass->output_depth()));
    }
    view->rg->add_pass("ui", view->ui_pass.get());

    view->final_pass.reset(new FinalPass(view->ui_pass->output_color()));
    my::RenderPassId final_pass_id = view->rg->add_pass("final scene view", view->final_pass.get());
    final_passes.push_back(final_pass_id);

    HRZ_LOG_INFO("Render graph initialized");

    hrz::GpuResourceContext gpu_rc = *render.rc;
    gpu_rc.default_system_for_allocs = monitoring::systems::SceneView;

    if (render.rg->build(render.my, &gpu_rc, final_passes.size(), final_passes.data()))
    {
        HRZ_LOG_INFO("Render graph built");

        view->rg_picking_subset = render.rg->make_subset(render.my, 1, &picking_pass_id);
        view->rg_planet_feedback_subset =
            render.rg->make_subset(render.my, 1, &planet_feedback_pass_id);
    }
    else
    {
        HRZ_LOG_ERROR("Error when building render graph");
    }
}

my::ResourceHandle get_color_output(SceneView* view)
{
    return view->final_pass->output_color();
}

lm::ibbox2 get_color_output_viewport_on_canvas(SceneView* view)
{
    return view->viewport_on_canvas;
}

lm::ibbox2 get_event_viewport_on_canvas(SceneView* view)
{
    return view->events_viewport_on_canvas;
}

void set_canvas_size(
    SceneView* view,
    SceneModel* model,
    lm::uvec2 canvas_size,
    float device_pixel_ratio)
{
    update_viewport(view, model, canvas_size, device_pixel_ratio);
}

ViewportEvent make_viewport_event(SceneView* view, const Event& event)
{
    return ViewportEvent(event, view->events_viewport_on_canvas);
}

void work_start_frame(SceneView* view, ShapeEditor* shape_editor, Render* render)
{
    picking::retrieve_results(view->picking_system, view->heatmaps, render);
    editor::work_picking(shape_editor, view->picking_system, view->view_index);
}

RenderRequest work(
    SceneView* view,
    SceneModel* model,
    const CameraViewInfo& cam_view_info,
    const lm::uvec2& canvas_size,
    float device_pixel_ratio,
    ClippingPlaneInfo cpi[HRZ_S_MAX_CLIP_PLANES],
    PlanetSurface* planet_surface,
    const HeatmapReprRegistry* heatmap_repr_registry,
    picking::FeatureReference quick_highlight_feature_id)
{
    RenderRequest render_request;

    SceneModelAccessor accessor(model);
    hrz_proto::SceneViewSettingsPathBuilder<SceneModelAccessor> builder(accessor, view->view_index);

    FrameUniformData& frame_uniforms_data = view->frame_uniforms.get_mutable();

    refresh_camera_and_viewport(view, model, canvas_size, device_pixel_ratio);

    if (view->highlight_settings_updated)
    {
        auto highlight_settings = builder.clone().highlight().get();
        view->highlight_apply_pass->set_settings(highlight_settings);
        view->highlight_settings_updated = false;
        view->quick_highlight_color = hrz::to_lm(highlight_settings.mouse_hover_highlight_color());
        render_request.request_visual_render();
    }

    if (view->terrain_settings_updated)
    {
        auto terrain_settings = builder.clone().terrain().get();
        view->terrain_color_opacity.rgb = to_lm(terrain_settings.terrain_color()).rgb;
        view->terrain_color_opacity.a = terrain_settings.terrain_opacity();
        view->terrain_clip_id = terrain_settings.clip_id();
        view->terrain_lighting = render::from_proto(terrain_settings.lighting());
        view->terrain_settings_updated = false;
        render_request.request_visual_render();
    }

    if (view->ambient_settings_updated)
    {
        auto ambient_settings = builder.clone().ambient().get();
        view->ambient_settings_updated = false;
        render_request.request_visual_render();
    }

    if (view->lighting_settings_updated)
    {
        auto ambient_settings = builder.clone().ambient().get();
        view->global_lighting = render::from_proto(builder.clone().ambient().lighting().get());
        view->lighting_settings_updated = false;
        render_request.request_visual_render();
    }

    lm::vec2 near_far = view->depth_reduction_pass->get_near_far();

    render_request |= vector_flat_overlay::work(view->flat_overlay, cam_view_info, near_far);
    render_request |= heatmaps::work(view->heatmaps, heatmap_repr_registry, view->flat_overlay);

    picking::set_matrices_for_this_frame(
        view->picking_system, cam_view_info.proj, cam_view_info.cam.view,
        vector_flat_overlay::get_latest_overlay_cameras_info(view->flat_overlay));

    frame_uniforms_data.proj = lm::mat4(cam_view_info.proj);
    frame_uniforms_data.view = lm::mat4(cam_view_info.cam.view);
    frame_uniforms_data.view_cc = lm::mat4(cam_view_info.cam.view_cc);
    frame_uniforms_data.pv = lm::mat4(cam_view_info.pv);
    frame_uniforms_data.pv_cc = lm::mat4(cam_view_info.pv_cc);
    frame_uniforms_data.view_cc_inv = lm::mat4(lm::inverse(cam_view_info.cam.view_cc));
    frame_uniforms_data.proj_inv = lm::mat4(lm::inverse(cam_view_info.proj));

    lm::dvec3 camera_pos = cam_view_info.cam.pos;
    GeoPosition3 camera_pos_geo = hrz::ecef_to_geo3(camera_pos);
    hrz::split_double(
        camera_pos.x, frame_uniforms_data.view_pos_low.x, frame_uniforms_data.view_pos_high.x);
    hrz::split_double(
        camera_pos.y, frame_uniforms_data.view_pos_low.y, frame_uniforms_data.view_pos_high.y);
    hrz::split_double(
        camera_pos.z, frame_uniforms_data.view_pos_low.z, frame_uniforms_data.view_pos_high.z);
    frame_uniforms_data.view_pos_low.w = 0;
    frame_uniforms_data.view_pos_high.w = 0;
    frame_uniforms_data.view_elevation = camera_pos_geo.alt;
    frame_uniforms_data.view_latitude = camera_pos_geo.lat;
    frame_uniforms_data.viewport_size = lm::uvec2(cam_view_info.viewport.subview_size());
    frame_uniforms_data.device_pixel_ratio = cam_view_info.viewport.device_pixel_ratio;
    frame_uniforms_data.pixel_size_in_meters =
        render::compute_logical_pixel_size_in_meters(cam_view_info);
    frame_uniforms_data.pixel_size_in_clip =
        lm::vec2(2.0) / cam_view_info.viewport.size * cam_view_info.viewport.device_pixel_ratio;
    frame_uniforms_data.camera_height_to_perceived_distance =
        cam_view_info.cam.perceived_distance_factor();

    camera_height::update(view->camera_height_system, cam_view_info);

    double latest_camera_height =
        camera_height::get_last_downloaded_height(view->camera_height_system);
    double pixel_size_at_perceived_distance =
        render::compute_logical_pixel_size_in_meters(cam_view_info)
        * frame_uniforms_data.camera_height_to_perceived_distance * latest_camera_height;

    view->view_scale_altitude.set_altitude_absolute(camera_pos_geo.alt);
    view->view_scale_altitude.set_altitude_relative_to_terrain(latest_camera_height);
    if (!std::isnan(pixel_size_at_perceived_distance)
        && pixel_size_at_perceived_distance > std::numeric_limits<double>::epsilon())
    {
        view->view_scale_altitude.set_one_meter_size_in_pixels(
            1.0 / pixel_size_at_perceived_distance);
    }
    else
    {
        view->view_scale_altitude.set_one_meter_size_in_pixels(0.0);
    }

    render_request |= sky::update(view->sky, cam_view_info, model);

    lm::dvec3 ecef_sun_direction = sky::get_ecef_sun_direction(view->sky);
    frame_uniforms_data.view_sun_direction =
        lm::vec3((cam_view_info.cam.view * lm::dvec4(ecef_sun_direction, 0)).xyz);

    if (view->shadows.has_value())
    {
        shadows::update(view->shadows.value(), cam_view_info, ecef_sun_direction, near_far, model);

        for (size_t i = 0; i < shadows::get_cascade_count(view->shadows.value()); ++i)
        {
            frame_uniforms_data.sun_matrix[i] =
                lm::mat4(shadows::get_sun_matrix(view->shadows.value(), i));
        }
    }

    render_request |= viewsheds::update(view->viewsheds, cam_view_info, model);

    for (size_t i = 0; i < HRZ_S_VIEWSHED_CNT; ++i)
    {
        frame_uniforms_data.vs_pv_matrix[i] =
            lm::mat4(viewsheds::get_viewshed_matrix(view->viewsheds, i));
        frame_uniforms_data.vs_position_from_main_view[i].xyz =
            lm::vec3(viewsheds::get_viewshed_position_from_main_view(view->viewsheds, i));
        lm::vec4 colors[2];
        viewsheds::get_viewshed_colors(view->viewsheds, i, colors);
        frame_uniforms_data.vs_seen_color[i] = colors[0];
        frame_uniforms_data.vs_hidden_color[i] = colors[1];
    }

    for (size_t i = 0; i < HRZ_S_MAX_CLIP_PLANES; ++i)
    {
        frame_uniforms_data.clip_planes[i].matrix =
            lm::mat4(cpi[i].view * cam_view_info.cam.inv_view);
        frame_uniforms_data.clip_planes[i].normal = lm::normalize(lm::vec3(cpi[i].normal));
        frame_uniforms_data.clip_planes[i].outline_color = cpi[i].outline_color;
        frame_uniforms_data.clip_planes[i].outline_distance = cpi[i].outline_distance;
    }

    if (view->shadows.has_value())
    {
        frame_uniforms_data.receive_shadows = view->global_lighting.receive_shadows
            && shadows::are_shadows_enabled(view->shadows.value());
        frame_uniforms_data.shadow_map_cascade_count =
            shadows::get_cascade_count(view->shadows.value());
        frame_uniforms_data.shadow_map_far_lin =
            (float)render::lin_depth(shadows::get_shadow_map_far(view->shadows.value()));
    }
    else
    {
        frame_uniforms_data.receive_shadows = false;
        frame_uniforms_data.shadow_map_cascade_count = 0;
        frame_uniforms_data.shadow_map_far_lin = 0.0;
    }

    uint32_t new_atmosphere_flags = 0;
    if (sky::is_dynamic_sun_lighting_enabled(view->sky))
    {
        new_atmosphere_flags |= FrameUniformData::DynamicSunLighting;
    }
    if (sky::is_dynamic_ambient_lighting_enabled(view->sky))
    {
        new_atmosphere_flags |= FrameUniformData::DynamicAmbientLighting;
    }

    if (frame_uniforms_data.atmosphere_flags != new_atmosphere_flags)
    {
        frame_uniforms_data.atmosphere_flags = new_atmosphere_flags;
        render_request.request_visual_render();
    }

    frame_uniforms_data.lighting_enabled = view->global_lighting.lighting_enabled;
    frame_uniforms_data.viewsheds_enabled = viewsheds::is_viewshed_enabled(view->viewsheds);

    frame_uniforms_data.terrain_color_opacity = view->terrain_color_opacity;
    frame_uniforms_data.terrain_clip_id = view->terrain_clip_id;
    frame_uniforms_data.terrain_lighting_enabled = view->terrain_lighting.lighting_enabled;
    frame_uniforms_data.terrain_receive_shadows = view->terrain_lighting.receive_shadows;
    frame_uniforms_data.merge_groups_bitset =
        planet::get_imagery_raster_groups_bitset(planet_surface, view->view_index);

    frame_uniforms_data.quick_highlight_color = view->quick_highlight_color;

    frame_uniforms_data.time = hrz::now_frame_s();

    auto previous_quick_highlight = frame_uniforms_data.quick_highlight_feature_reference;
    frame_uniforms_data.quick_highlight_feature_reference = quick_highlight_feature_id.to_uvec3();

    if (frame_uniforms_data.quick_highlight_feature_reference != previous_quick_highlight)
    {
        render_request.request_visual_render();
        render_request.schedule_flat_overlay_render();
    }

    render_request |= picking::work(view->picking_system);

    uint32_t new_frame_uniform_debug_flags = 0;
    if (get_flag(Flag::DebugDrawFlatOverlayCascades))
    {
        new_frame_uniform_debug_flags |= FrameUniformData::DrawFlatOverlayCascades;
    }
    if (get_flag(Flag::DebugDrawHeatmapOobSampling))
    {
        new_frame_uniform_debug_flags |= FrameUniformData::DrawHeatmapOobSampling;
    }

    if (frame_uniforms_data.debug_flags != new_frame_uniform_debug_flags)
    {
        frame_uniforms_data.debug_flags = new_frame_uniform_debug_flags;
        render_request.request_all();
    }

    return render_request;
}

double get_camera_height(SceneView* view)
{
    return camera_height::get_last_downloaded_height(view->camera_height_system);
}

const hrz_proto::ViewScaleAltitude& get_view_scale_altitude(SceneView* view)
{
    return view->view_scale_altitude;
}

void set_highlight_enabled(SceneView* view, bool enabled)
{
    view->highlight_gather_pass->set_should_execute(enabled);
    view->highlight_apply_pass->set_should_execute(enabled);
}

void register_aux_views(
    SceneView* view,
    Render* render_global,
    my::Renderer::ViewId main_view_id,
    std::vector<my::Renderer::ViewId>& created_views)
{
    RenderView render(*render_global, view->rg);

    view->main_view_id = main_view_id;

    vector_flat_overlay::register_views(view->flat_overlay, &render, created_views);
    heatmaps::register_views(view->heatmaps, view->flat_overlay, &render, created_views);
    if (view->shadows.has_value())
    {
        shadows::register_views(view->shadows.value(), &render, created_views);
    }
    viewsheds::register_views(view->viewsheds, &render, created_views);
}

RenderRequest work_gpu(SceneView* view, Render* render_global)
{
    RenderView render(*render_global, view->rg);

    double height_above_terrain = get_camera_height(view);

    RenderRequest rr = sky::work_gpu(view->sky, &render);
    rr |=
        planet::work_gpu(view->planet_geometry, &render, view->main_view_id, height_above_terrain);

    heatmaps::work_gpu(view->heatmaps, &render);

    return rr;
}

void draw(
    SceneView* view,
    const RenderRequest& render_request,
    Render* render_global,
    const planet::GeometryResources& planet_geometry_resources)
{
    HRZ_SCOPED_SAMPLE("scene view draw");

    RenderView render(*render_global, view->rg);

    if (render_request.is_any_render_requested())
    {
        {
            FrameUniformData& frame_uniforms_data = view->frame_uniforms.get_mutable();
            sky::fill_frame_uniform_data(view->sky, &frame_uniforms_data);
        }

        view->frame_uniforms.update(render.my);

        {
            my::UboBinding binding = {
                hrz::UboFrame, view->frame_uniforms.get_for_gpu(), 0,
                sizeof(hrz::FrameUniformData)};

            render.rb->bind(1, &binding);
        }

        if (view->shadows.has_value())
        {
            shadows::draw(view->shadows.value(), &render);
        }

        viewsheds::draw(view->viewsheds, &render);
        camera_height::draw(view->camera_height_system, &render, planet_geometry_resources);
        planet::draw(
            view->planet_geometry, &render, planet_geometry_resources,
            view->terrain_lighting.cast_shadows);

        heatmaps::draw(view->heatmaps, &render);
        vector_flat_overlay::draw(view->flat_overlay, &render);
    }

    view->depth_reduction_pass->update(&render);

    lm::ivec2 canvas_size = lm::size(view->viewport_on_canvas);

    SceneViewRenderGraphUserData user_data;
    user_data.main_view = view->main_view_id;
    user_data.scene_view = view->view_index;

    my::RenderGraph::ExecutionContext ctx;
    ctx.backbuffer_width = canvas_size.x;
    ctx.backbuffer_height = canvas_size.y;
    ctx.instance = render.my;
    ctx.resource = render.rc;
    ctx.render = render.my;
    ctx.binder = render.rb;
    ctx.renderer = render.rd;
    ctx.user_data = &user_data;

    bool render_visual = render_request.is_render_requested(RenderRequest::Type::Visual);
    bool render_picking = render_request.is_render_requested(RenderRequest::Type::Picking);
    bool render_feedback = render_request.is_render_requested(RenderRequest::Type::PlanetFeedback);
    if (render_visual || (render_picking && render_feedback))
    {
        HRZ_SCOPED_SAMPLE("render all");
        render.rg->execute(ctx);
    }
    else if (render_picking)
    {
        HRZ_SCOPED_SAMPLE("render picking only");
        render.rg->execute_subset(ctx, view->rg_picking_subset);
    }
    else if (render_feedback)
    {
        HRZ_SCOPED_SAMPLE("render feedback only");
        render.rg->execute_subset(ctx, view->rg_planet_feedback_subset);
    }

    picking::end_frame(view->picking_system);
}

void notify_model_update(
    SceneView* view,
    scene_model::UpdateType update_type,
    const scene_model::SceneViewSettingsPath& settings_path)
{
    sky::notify_model_update(view->sky, update_type, settings_path);

    if (view->shadows.has_value())
    {
        shadows::notify_model_update(view->shadows.value(), update_type, settings_path);
    }

    viewsheds::notify_model_update(view->viewsheds, update_type, settings_path);
    planet::notify_model_update(view->planet_geometry, update_type, settings_path);

    if (settings_path.leaf() || settings_path.is_ambient())
    {
        view->lighting_settings_updated = true;
    }

    if (settings_path.leaf() || settings_path.is_highlight())
    {
        view->highlight_settings_updated = true;
    }

    if (settings_path.leaf() || settings_path.is_viewport())
    {
        view->viewport_settings_updated = true;
    }

    if (settings_path.leaf() || settings_path.is_terrain())
    {
        view->terrain_settings_updated = true;
    }

    if (settings_path.leaf() || settings_path.is_ambient())
    {
        view->ambient_settings_updated = true;
    }

    if (settings_path.leaf() || settings_path.is_camera())
    {
        view->associated_camera_updated = true;
    }
}

bool retrieve_picking_result(
    SceneView* view,
    picking::PositionTicket ticket,
    picking::PositionResult* pick_result)
{
    return picking::retrieve_result(view->picking_system, ticket, pick_result);
}

bool retrieve_picking_result(
    SceneView* view,
    picking::AreaTicket ticket,
    std::vector<picking::AreaResult>& pick_result)
{
    return picking::retrieve_result(view->picking_system, ticket, pick_result);
}

std::optional<picking::PositionTicket> schedule_pick(
    SceneView* view,
    lm::ivec2 mouse_position,
    gsl::span<const hrz_proto::LayerHandle> included_rasters)
{
    if (lm::contains(view->events_viewport_on_canvas, mouse_position))
    {
        hrz::vector_flat_overlay::schedule_picking_render(view->flat_overlay);
        return picking::schedule_pick(
            view->picking_system, mouse_position - view->events_viewport_on_canvas.min,
            included_rasters);
    }
    else
    {
        return std::nullopt;
    }
}

std::optional<picking::AreaTicket> schedule_pick(SceneView* view, lm::ibbox2 rect)
{
    if (lm::contains(view->events_viewport_on_canvas, lm::center(rect)))
    {
        rect = lm::intersection(rect, view->events_viewport_on_canvas);
        rect.min -= view->events_viewport_on_canvas.min;
        rect.max -= view->events_viewport_on_canvas.min;
        hrz::vector_flat_overlay::schedule_picking_render(view->flat_overlay);
        return picking::schedule_pick(view->picking_system, rect);
    }
    else
    {
        return std::nullopt;
    }
}

PlanetGeometry* get_planet_geometry(SceneView* view)
{
    return view->planet_geometry;
}

VectorFlatOverlaySystem* get_vector_flat_overlay(SceneView* view)
{
    return view->flat_overlay;
}

hrz_proto::CameraIndex get_camera_index(const SceneView* view)
{
    return view->camera_index;
}

PickingSystem* get_picking_system(SceneView* view)
{
    return view->picking_system;
}

void collect_shaders(hrz::GpuResourceContext* rc)
{
    DepthReductionPass::collect_shaders(rc);
    HighlightApplyPass::collect_shaders(rc);
    if (hrz::get_flag(hrz::Flag::EnabledDepthPeelingForUiElements))
    {
        UiDepthPeelingPass::collect_shaders(rc);
    }
    MergeDepthsPass::collect_shaders(rc);
}
} // namespace scene
} // namespace hrz
