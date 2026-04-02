#pragma once

#include "hrz/core/render/common_ubos.h"
#include "hrz/core/render/defs.h"
#include "hrz/core/render/double_buffered_uniform_buffer.h"
#include "hrz/core/render/timed_render_pass.h"

namespace hrz::shadow_map
{

class ShadowMapPass : public hrz::render::TimedRenderPass
{
    struct Cascade
    {
        const char* output_name;
        my::Renderer::ViewId view;
        my::ResourceHandle shadow_map;
        my::ResourceHandle fbo;
        bool render_scheduled;
    };

    const char* _input_camera_height;
    my::ResourceHandle _camera_height_texture;
    my::ResourceHandle _camera_height_sampler;

    // There can be more cascades created than cascades that are
    // actually rendered to, and usable. Cascades that are not
    // rendered have a 1x1 texture that is never written to.
    // This is useful to set up a variable number of cascades at
    // initialisation time, without having to modify shaders.
    size_t _cascade_count;
    size_t _rendered_cascade_count;
    uint32_t _size;
    std::unique_ptr<Cascade[]> _cascades;

    render::DoubleBufferedUniformBuffer<AuxViewUniformData> _view_ubos;
    bool _enabled = false;
    bool _initialized = false;
    RenderType _render_type;
    size_t _ubo_aligned_size;

public:
    ShadowMapPass(
        const char* pass_name,
        const char* input_camera_height,
        std::span<const char* const> output_names,
        size_t cascade_count,
        size_t rendered_cascade_count,
        uint32_t size,
        RenderType render_type) :
        TimedRenderPass(pass_name),
        _input_camera_height(input_camera_height),
        _cascade_count(cascade_count),
        _rendered_cascade_count(rendered_cascade_count),
        _size(size),
        _cascades(new Cascade[cascade_count]),
        _view_ubos(cascade_count),
        _render_type(render_type)
    {
        assert(output_names.size() == cascade_count);
        assert(rendered_cascade_count <= cascade_count);

        for (size_t i = 0; i < cascade_count; ++i)
        {
            _cascades[i].output_name = output_names[i];
        }
    }

    void set_enabled(bool enabled);

    void schedule_render_all();
    void schedule_render(size_t cascade);

    void set_view(size_t cascade, my::Renderer::ViewId view);

    const char* depth_output(size_t cascade) const;

    void setup_pass(my::RenderGraph::SetupContext& ctx) override;

    void retrieve_resources(
        my::Instance* my,
        my::ResourceContext* rc,
        const my::RenderGraph::ResourceContext& ctx) override;

    void destroy(my::ResourceContext* rc);

    void update_ubo(
        size_t cascade,
        hrz::Render* render,
        const lm::dmat4& pv_from_main_view,
        const lm::dmat4& pv_cc);

    void execute_timed(const my::RenderGraph::ExecutionContext& ctx) override;
};

} // namespace hrz::shadow_map
