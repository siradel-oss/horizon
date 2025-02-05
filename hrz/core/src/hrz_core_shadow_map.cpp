#include "hrz_core_shadow_map.h"

#include "hrz_core_render.h"

#include <hrz_common_monitoring_defs.h>
#include <hrz_common_profiling.h>
#include <hrz_fnd_mem.h>

#include <cstring>

namespace hrz::shadow_map
{
void ShadowMapPass::set_enabled(bool enabled)
{
    _enabled = enabled;
}

void ShadowMapPass::schedule_render_all()
{
    for (size_t i = 0; i < _rendered_cascade_count; ++i)
    {
        _cascades[i].render_scheduled = true;
    }
}

void ShadowMapPass::schedule_render(size_t cascade)
{
    assert(cascade < _cascade_count);

    if (cascade >= _rendered_cascade_count) return;

    _cascades[cascade].render_scheduled = true;
}

void ShadowMapPass::set_view(size_t cascade, my::Renderer::ViewId view)
{
    assert(cascade < _cascade_count);
    _cascades[cascade].view = view;
}

const char* ShadowMapPass::depth_output(size_t cascade) const
{
    assert(cascade < _cascade_count);
    return _cascades[cascade].output_name;
}

void ShadowMapPass::setup_pass(my::RenderGraph::SetupContext& ctx)
{
    ctx.set_invocation_count(_rendered_cascade_count);

    my::RenderGraph::ResourceInfo res;
    res.format = my::TextureFormat::Depth32F;
    res.size_class = my::RenderGraph::ResourceInfo::Absolute;
    res.width = _size;
    res.height = _size;

    auto dummy_res = res;
    dummy_res.width = 1;
    dummy_res.height = 1;

    for (size_t i = 0; i < _cascade_count; ++i)
    {
        ctx.create(
            _cascades[i].output_name, my::RenderGraph::Target,
            i < _rendered_cascade_count ? res : dummy_res);
    }

    ctx.read(_input_camera_height, my::RenderGraph::Sampled);
}

void ShadowMapPass::retrieve_resources(
    my::Instance* my,
    my::ResourceContext* rc,
    const my::RenderGraph::ResourceContext& ctx)
{
    size_t alignment = my->get_uniform_buffer_offset_alignment();
    _ubo_aligned_size = render::compute_ubo_stride<AuxViewUniformData>(alignment);

    for (size_t i = 0; i < _cascade_count; ++i)
    {
        _cascades[i].shadow_map = ctx.retrieve(_cascades[i].output_name);

        my::FramebufferAttachment attachments[] = {
            {my::Attachment::Depth, _cascades[i].shadow_map},
        };

        my::FramebufferResource res;
        res.attachment_count = HRZ_ARRAY_COUNT(attachments);
        res.attachments = attachments;

        _cascades[i].fbo =
            ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::Shadows);
    }

    Render render;
    render.my = my;
    render.rc = (hrz::GpuResourceContext*)rc;
    _view_ubos.initialize(
        &render, hrz::monitoring::systems::Shadows, {{"contents"_ss, "shadow map ubo"_ss}});

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

void ShadowMapPass::destroy(my::ResourceContext* rc)
{
    for (size_t i = 0; i < _cascade_count; ++i)
    {
        rc->dealloc(_cascades[i].fbo);
    }
    rc->dealloc(_camera_height_sampler);
    _view_ubos.destroy(rc);
}

void ShadowMapPass::update_ubo(
    size_t cascade,
    hrz::Render* render,
    const lm::dmat4& pv_from_main_view,
    const lm::dmat4& pv_cc)
{
    assert(cascade < _cascade_count);

    HRZ_SCOPED_SAMPLE("shadow map pass update ubo");

    AuxViewUniformData view;
    view.pv_from_main_view = lm::mat4(pv_from_main_view);
    view.pv_cc = lm::mat4(pv_cc);

    _view_ubos.set(cascade, view);
}

void ShadowMapPass::execute_timed(const my::RenderGraph::ExecutionContext& ctx)
{
    if (!_enabled)
    {
        return;
    }

    if (ctx.invocation == 0)
    {
        _view_ubos.update(ctx.render);
    }

    if (ctx.invocation == 0 && !_initialized)
    {
        static const my::ClearTarget clear_targets[] = {{
            my::Attachment::Depth,
            my::ClearValue::make_depth(1.0),
        }};

        for (size_t i = 0; i < _cascade_count; ++i)
        {
            ctx.render->set_framebuffer(
                _cascades[i].fbo, my::ViewportState{{0, 0, _size, _size}, {0, 0, _size, _size}});
            ctx.render->clear(HRZ_ARRAY_COUNT(clear_targets), clear_targets);
        }

        _initialized = true;
    }

    HRZ_SCOPED_SAMPLE("depth map draw");

    auto& cascade = _cascades[ctx.invocation];
    if (!cascade.render_scheduled)
    {
        return;
    }

    cascade.render_scheduled = false;

    ctx.render->set_framebuffer(
        cascade.fbo, my::ViewportState{{0, 0, _size, _size}, {0, 0, _size, _size}});

    ctx.binder->push_state();

    static const my::ClearTarget clear_targets[] = {{
        my::Attachment::Depth,
        my::ClearValue::make_depth(1.0),
    }};

    ctx.render->clear(HRZ_ARRAY_COUNT(clear_targets), clear_targets);

    my::UboBinding ubo_binding{
        hrz::UboView, _view_ubos.get_for_gpu(), (uint32_t)(_ubo_aligned_size * ctx.invocation),
        sizeof(AuxViewUniformData)};
    ctx.binder->bind(1, &ubo_binding);

    my::TextureBinding texture_binding = {
        hrz::SamplerCameraHeight, _camera_height_texture, _camera_height_sampler};
    ctx.binder->bind(1, &texture_binding);

    static const my::Renderer::BinMask pass_masks[] = {
        hrz::RenderWorldOpaqueBin | hrz::RenderWorldTransparentBin,
    };

    ctx.renderer->draw(
        _render_type, cascade.view, HRZ_ARRAY_COUNT(pass_masks), pass_masks, ctx.render, ctx.binder,
        ctx.user_data);

    ctx.binder->pop_state();
}

} // namespace hrz::shadow_map
