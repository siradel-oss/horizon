#include "vector/hrz_core_vector_flat_overlay.h"

#include "camera/hrz_core_camera_system.h"
#include "hrz_core_camera_height.h"
#include "hrz_core_global_flags.h"
#include "hrz_core_render.h"
#include "vector/hrz_core_vector_heatmaps.h"

#include <hrz_common_geo.h>
#include <hrz_common_monitoring_defs.h>
#include <hrz_common_profiling.h>
#include <hrz_fnd_format.h>
#include <hrz_fnd_log.h>
#include <hrz_fnd_mem.h>
#include <hrz_fnd_time.h>

#include <optional>

extern "C"
{
#include <microui/microui.h>
}

#include <array>
#include <limits>
#include <sstream>

namespace hrz
{
constexpr uint32_t VISUAL_RENDER_RATE_MS = 150;
constexpr uint32_t ANIMATION_RENDER_RATE_MS = 16;

constexpr float ORTHO_ALT = 10000.0;

struct OverlayPassUniform
{
    uint32_t pass_index;
    float world_size;
    uint32_t texture_size;
    uint32_t _padding[1];
};

struct OverlayCamerasUniform
{
    uint32_t cascade_count;
    uint32_t _padding[3];

    lm::vec4 overlay_cams_pos_low;
    lm::vec4 overlay_cams_pos_high;
    lm::mat4 overlay_cams_proj[HRZ_S_MAX_OVERLAY_CASCADES];
    lm::mat4 overlay_cams_view_cc;
    lm::mat4 overlay_cams_pv_cc[HRZ_S_MAX_OVERLAY_CASCADES];

    lm::mat4 overlay_cams_mvp_inv_main_view_visual[HRZ_S_MAX_OVERLAY_CASCADES];
    lm::mat4 overlay_cams_mvp_inv_main_view_picking[HRZ_S_MAX_OVERLAY_CASCADES];
};

class VectorFlatOverlayRenderPass : public hrz::render::TimedRenderPass
{
    struct Cascade
    {
        std::string output_target_name;
        my::ResourceHandle output_target;
        my::ResourceHandle fbo;
        my::Renderer::ViewId view_id;
        float world_size;
    };

    size_t _cascade_count;
    size_t _cascade_count_to_render;
    hrz::RenderType _render_type;
    Cascade _cascades[HRZ_S_MAX_OVERLAY_CASCADES];
    render::DoubleBufferedUniformBuffer<OverlayPassUniform> _pass_ubos;
    uint32_t _texture_size;
    my::ClearTarget _clear_values[1];

    const char* _input_camera_height;
    my::ResourceHandle _camera_height_texture;
    my::ResourceHandle _camera_height_sampler;

    const char* _input_heatmaps;

    bool _render_requested;

public:
    VectorFlatOverlayRenderPass(
        const char* name,
        const char* input_camera_height,
        const char* input_heatmaps,
        size_t texture_size,
        size_t cascade_count,
        hrz::RenderType render_type,
        bool is_dummy) :
        TimedRenderPass(name),
        _cascade_count(cascade_count),
        _render_type(render_type),
        _pass_ubos(HRZ_S_MAX_OVERLAY_CASCADES),
        _input_camera_height(input_camera_height),
        _input_heatmaps(input_heatmaps),
        _render_requested(false)
    {
        for (size_t i = 0; i < HRZ_S_MAX_OVERLAY_CASCADES; ++i)
        {
            Cascade& cascade = _cascades[i];
            cascade.output_target_name = "flat overlay ";

            switch (_render_type)
            {
                case hrz::RenderType::RenderVisual: break;
                case hrz::RenderType::RenderPicking:
                    cascade.output_target_name += "picking ";
                    break;
                case hrz::RenderType::RenderSelection:
                    cascade.output_target_name += "selection ";
                    break;
                default: assert(false && "Unhandled case"); break;
            }

            cascade.output_target_name += "target " + std::to_string(i);
        }

        _texture_size = (is_dummy)
            ? 1
            : std::max(
                texture_size / (_render_type == hrz::RenderType::RenderVisual ? 1 : 2), (size_t)1);

        _clear_values[0] = {
            my::Attachment::Color0,
            _render_type == hrz::RenderType::RenderPicking
                ? my::ClearValue::make_color_uint(0, 0, 0, 0)
                : my::ClearValue::make_color_float(0, 0, 0, 0)};
    }

    inline const char* output_target_name(size_t cascade)
    {
        return _cascades[cascade].output_target_name.c_str();
    }

    void request_render() { _render_requested = true; };

    bool is_render_requested() const { return _render_requested; }

    void update(size_t cascade, float world_size) { _cascades[cascade].world_size = world_size; }

    void setup_pass(my::RenderGraph::SetupContext& ctx) override
    {
        ctx.read(_input_camera_height, my::RenderGraph::Sampled);

        // We don't use the heatmaps input. It is an empty texture used to schedule the heatmap pass
        // before this pass.
        ctx.read(_input_heatmaps, my::RenderGraph::Sampled);

        ctx.set_invocation_count(HRZ_S_MAX_OVERLAY_CASCADES);

        my::RenderGraph::ResourceInfo res;

        switch (_render_type)
        {
            case hrz::RenderType::RenderVisual: res.format = my::TextureFormat::RGBA8; break;
            case hrz::RenderType::RenderPicking: res.format = my::TextureFormat::RG32UI; break;
            case hrz::RenderType::RenderSelection: res.format = my::TextureFormat::R8; break;
            default: assert(false && "Unhandled case"); break;
        }

        res.size_class = my::RenderGraph::ResourceInfo::Absolute;
        res.width = (float)_texture_size;
        res.height = (float)_texture_size;

        auto dummy_res = res;
        dummy_res.width = 1.0f;
        dummy_res.height = 1.0f;

        for (size_t i = 0; i < HRZ_S_MAX_OVERLAY_CASCADES; ++i)
        {
            ctx.create(
                _cascades[i].output_target_name.c_str(), my::RenderGraph::Target,
                i < _cascade_count ? res : dummy_res);
        }
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
        _camera_height_sampler =
            render.rc->alloc(&sampler_res, hrz::monitoring::systems::FlatOverlays);

        sampler_res.sampler.mag_filter = my::SamplerParams::Filter::Linear;
        sampler_res.sampler.min_filter = my::SamplerParams::Filter::Linear;

        _pass_ubos.initialize(
            &render, monitoring::systems::FlatOverlays, {{"contents"_ss, "pass ubos"_ss}});

        for (size_t i = 0; i < HRZ_S_MAX_OVERLAY_CASCADES; ++i)
        {
            Cascade& cascade = _cascades[i];
            cascade.output_target = ctx.retrieve(cascade.output_target_name.c_str());

            {
                my::FramebufferAttachment attachment{my::Attachment::Color0, cascade.output_target};

                my::FramebufferResource res;
                res.attachment_count = 1;
                res.attachments = &attachment;

                cascade.fbo = render.rc->alloc(&res, hrz::monitoring::systems::FlatOverlays);
            }
        }
    }

    void set_cascade_count_to_render(size_t count) { _cascade_count_to_render = count; }

    void set_view(size_t cascade, my::Renderer::ViewId view_id)
    {
        _cascades[cascade].view_id = view_id;
    }

    void destroy(Render* render)
    {
        render->my->dealloc(_camera_height_sampler);
        _pass_ubos.destroy(render->my);
        for (size_t i = 0; i < HRZ_S_MAX_OVERLAY_CASCADES; ++i)
        {
            render->rc->dealloc(_cascades[i].fbo);
        }
    }

    void execute_timed(const my::RenderGraph::ExecutionContext& ctx) override
    {
        if (!_render_requested) return;
        if (ctx.invocation >= _cascade_count_to_render) return;

        ctx.binder->push_state();

        if (ctx.invocation == 0)
        {
            for (size_t i = 0; i < HRZ_S_MAX_OVERLAY_CASCADES; ++i)
            {
                OverlayPassUniform uniform;
                uniform.pass_index = i;
                uniform.world_size = _cascades[i].world_size;
                uniform.texture_size = _texture_size;
                _pass_ubos.set(i, uniform);
            }
            _pass_ubos.update(ctx.render);
        }

        uint32_t pass_index = (uint32_t)ctx.invocation;
        const Cascade& cascade = _cascades[pass_index];

        my::UboBinding binding = {
            hrz::vector_flat_overlay::UboVectorOverlayPass, _pass_ubos.get_for_gpu(),
            (uint32_t)_pass_ubos.offset(pass_index), sizeof(OverlayPassUniform)};
        ctx.binder->bind(1, &binding);

        my::TextureBinding texture_bindings[] = {
            {hrz::SamplerCameraHeight, _camera_height_texture, _camera_height_sampler}};
        ctx.binder->bind(HRZ_ARRAY_COUNT(texture_bindings), texture_bindings);

        const my::ViewportState viewport_state = {
            {0, 0, _texture_size, _texture_size},
            {0, 0, _texture_size, _texture_size},
        };

        ctx.render->set_framebuffer(cascade.fbo, viewport_state);
        ctx.render->clear(1, _clear_values);

        my::Renderer::BinMask to_render = RenderFlatOverlayBin;

        ctx.renderer->draw(
            _render_type, cascade.view_id, 1, &to_render, ctx.render, ctx.binder, ctx.user_data);

        ctx.binder->pop_state();

        if (pass_index == _cascade_count_to_render - 1)
        {
            _render_requested = false;
        }
    }
};

struct VectorFlatOverlaySystem
{
    std::unique_ptr<VectorFlatOverlayRenderPass> _visual_pass;
    std::unique_ptr<VectorFlatOverlayRenderPass> _picking_pass;
    std::unique_ptr<VectorFlatOverlayRenderPass> _selection_pass;

    double _last_visual_render_ms;
    bool _visual_render_requested;
    bool _animation_render_requested;

    lm::vec2 _last_near_far;

    OverlayCamerasInfo _overlay_cameras_info_visual;
    OverlayCamerasInfo _overlay_cameras_info_picking;
    OverlayCamerasInfo* _latest_overlay_camera_info;

    my::Renderer::ViewId _latest_cascade_view_ids[HRZ_S_MAX_OVERLAY_CASCADES];

    render::DoubleBufferedUniformBuffer<OverlayCamerasUniform> _overlay_cameras_ubo;

    uint32_t _cascade_count;
    bool _disabled;

    VectorFlatOverlaySystem(uint32_t cascade_count, bool disabled) :
        _cascade_count(
            hrz::clamp(cascade_count, (uint32_t)1, (uint32_t)HRZ_S_MAX_OVERLAY_CASCADES)),
        _disabled(disabled)
    {
        _last_visual_render_ms = std::numeric_limits<double>::lowest();
        _visual_render_requested = false;
        _animation_render_requested = false;
        _last_near_far = {0, 0};

        _latest_overlay_camera_info = &_overlay_cameras_info_visual;
    }

    void initialize_rendering(
        size_t texture_size,
        Render* render,
        CameraHeightSystem* camera_height_system,
        HeatmapSystem* heatmap_system)
    {
        _visual_pass.reset(new VectorFlatOverlayRenderPass(
            "flat overlay ortho", hrz::camera_height::get_target_name(camera_height_system),
            hrz::heatmaps::get_output_target_name(heatmap_system), texture_size, _cascade_count,
            hrz::RenderType::RenderVisual, _disabled));
        _picking_pass.reset(new VectorFlatOverlayRenderPass(
            "flat overlay picking", hrz::camera_height::get_target_name(camera_height_system),
            hrz::heatmaps::get_output_target_name(heatmap_system), texture_size, _cascade_count,
            hrz::RenderType::RenderPicking, _disabled));
        _selection_pass.reset(new VectorFlatOverlayRenderPass(
            "flat overlay selection", hrz::camera_height::get_target_name(camera_height_system),
            hrz::heatmaps::get_output_target_name(heatmap_system), texture_size, _cascade_count,
            hrz::RenderType::RenderSelection, _disabled));

        _overlay_cameras_ubo.initialize(
            render, monitoring::systems::FlatOverlays, {{"contents"_ss, "overlay camera ubos"_ss}});
    }

    void destroy(Render* render) { _overlay_cameras_ubo.destroy(render->my); }

    bool is_visual_render_needed() const
    {
        if (_disabled) return false;

        if (hrz::get_flag(hrz::Flag::ForceFlatOverlayRender)) return true;

        const double elapsed_time_ms = hrz::now_frame_ms() - _last_visual_render_ms;
        if (_animation_render_requested)
        {
            return elapsed_time_ms > ANIMATION_RENDER_RATE_MS;
        }
        else if (_visual_render_requested)
        {
            return elapsed_time_ms > VISUAL_RENDER_RATE_MS;
        }

        return false;
    }

    bool is_picking_render_needed() const
    {
        return !_disabled && _picking_pass->is_render_requested();
    }

    void prepare_visual_render()
    {
        _visual_pass->request_render();
        _selection_pass->request_render();
        _last_visual_render_ms = hrz::now_frame_ms();
        _visual_render_requested = false;
        _animation_render_requested = false;
    }

    void schedule_visual_render(bool animation)
    {
        _visual_render_requested = true;
        _animation_render_requested |= animation;
    }

    void schedule_picking_render() { _picking_pass->request_render(); }

    bool is_working() const
    {
        if (_disabled) return false;

        return _visual_render_requested || _picking_pass->is_render_requested();
    }
};

namespace vector_flat_overlay
{
const char* const sampler_names[HRZ_S_MAX_OVERLAY_CASCADES] = {
    "hrz_flat_overlay_image[0]", "hrz_flat_overlay_image[1]", "hrz_flat_overlay_image[2]",
    "hrz_flat_overlay_image[3]"};
const char* const picking_sampler_names[HRZ_S_MAX_OVERLAY_CASCADES] = {
    "hrz_flat_overlay_picking_image[0]", "hrz_flat_overlay_picking_image[1]",
    "hrz_flat_overlay_picking_image[2]", "hrz_flat_overlay_picking_image[3]"};
const char* const selection_sampler_names[HRZ_S_MAX_OVERLAY_CASCADES] = {
    "hrz_flat_overlay_selection_image[0]", "hrz_flat_overlay_selection_image[1]",
    "hrz_flat_overlay_selection_image[2]", "hrz_flat_overlay_selection_image[3]"};

RenderRequest work(
    VectorFlatOverlaySystem* system,
    const CameraViewInfo& main_view_info,
    lm::vec2 near_far)
{
    RenderRequest render_request;

    if (near_far != system->_last_near_far)
    {
        // Flat overlay camera parameters depend on the near and far distances,
        // so the textures must be re-rendered whenever these distances change.
        system->schedule_visual_render(false);
        system->_last_near_far = near_far;
    }

    bool visual_render_needed = system->is_visual_render_needed();
    bool picking_render_needed = system->is_picking_render_needed();

    if (visual_render_needed)
    {
        system->prepare_visual_render();
    }

    OverlayCamerasInfo new_info;

    if (visual_render_needed || picking_render_needed)
    {
        // Origin of the overlays is the main camera at the surface
        GeoPosition3 center_geo = hrz::ecef_to_geo3(main_view_info.cam.pos);
        center_geo.alt = ORTHO_ALT;

        // Compute the camera's bearing. This is used to orientate the
        // textures along the same axes as the camera, which in turn
        // makes the texels a bit less noticeable, and enables better
        // positioning of the view bbox.
        lm::dmat4 ecef_to_enu = hrz::ecef_to_enu_rotation_matrix_for_geo(center_geo.latlon());
        lm::dvec3 forward_enu = (ecef_to_enu * lm::dvec4(main_view_info.cam.forward(), 0)).xyz;

        double bearing = 0;
        if (std::abs(forward_enu.x) < 0.1 && std::abs(forward_enu.y) < 0.1)
        {
            // The camera points down too much. Use the up vector.
            lm::dvec3 up_enu = (ecef_to_enu * lm::dvec4(main_view_info.cam.up(), 0)).xyz;
            bearing = std::atan2(up_enu.y, up_enu.x);
        }
        else
        {
            bearing = std::atan2(forward_enu.y, forward_enu.x);
        }
        // Offset the angle so that 0 means the camera points to the North.
        // (The angle grows anticlockwise.)
        bearing -= lm::PI * 0.5;

        new_info.pos = hrz::geo_to_ecef(center_geo);
        new_info.view = lm::rotation_normalized(lm::axis_angle(lm::dvec3(0, 0, 1), -bearing))
            * hrz::ecef_to_enu_transform_for_geo(center_geo);
        new_info.view_cc = new_info.view * lm::translation(new_info.pos);

        double near = near_far.x * 0.9;
        double far = near_far.y;

        // The far / near ratio tells us more or less how tilted the camera is.
        // When this ratio is low, the camera is perpendicular to the ground
        // (even when very far from the planet). This ratio grows when the
        // camera is tilted. When this ratio is low, we don't need a lot of
        // cascades to have a decent result, thus we establish a few threshold
        // that change the number of cascades used depending on this ratio.
        // I've determined by just looking at scenes and observing this ratio
        // that some logarithm of this ratio is a pretty good heuristic. Which
        // makes sense because the cascades have an exponential distribution.
        // For example with a log 2 distribution, ratio <= 2, use 1 cascade.
        // ratio <= 4, use 2 cascades, etc.
        //      -slerouzic, 2023-01-06

        int overlay_count = std::min(
            std::max((int)std::ceil(std::log2(far / near) / std::log2(2.5)), 1),
            (int)system->_cascade_count);
        new_info.cascade_count = overlay_count;

        // When the camera orientation is close to horizontal, and its position
        // is close to the surface, the ratio between near and far plane dis-
        // tances is large. If nothing is done, this wastes a lot of texture
        // space for far away and beraly visible features, which degrades con-
        // siderably the quality of the closest cascades. This is a considerable
        // problem when the maximum number of cascades is low.
        // To counter this, if there are few cascades and the far plane is too
        // far away, it is brought closer by limiting its distance to n times
        // that of the near plane.
        // It means that the screen may not be entirely covered, but it's usually
        // not too bad.
        //      -tpetillon, 2023-03-30

        if (system->_cascade_count <= 2)
        {
            far = std::min(far, near * 20.0);
        }
        else if (system->_cascade_count <= 3)
        {
            far = std::min(far, near * 80.0);
        }

        double plane_step = std::pow(far / near, 1.0 / (float)(overlay_count));
        new_info.plane_distances[0] = near;
        for (int i = 0; i < overlay_count; ++i)
        {
            new_info.plane_distances[i + 1] = near * std::pow(plane_step, (double)i + 1);
        }

        auto compute_proj = [&](double near, double far, double margin) -> lm::dmat4
        {
            lm::dmat4 main_proj = lm::perspective_subfrustum_opengl<double>(
                main_view_info.cam.fovy, main_view_info.viewport.aspect_ratio(),
                main_view_info.viewport.subfrustum.min.x * 2 - 1,
                main_view_info.viewport.subfrustum.max.x * 2 - 1,
                main_view_info.viewport.subfrustum.min.y * 2 - 1,
                main_view_info.viewport.subfrustum.max.y * 2 - 1, near, far);

            lm::dmat4 main_proj_view_inv = lm::inverse(main_proj * main_view_info.cam.view);

            static const lm::dbbox3 NDC(lm::dvec3(-1), lm::dvec3(1));
            lm::dbbox3 main_frustum_cascade_bbox = lm::dbbox3::invalid();

            for (unsigned int j = 0; j < 8; ++j)
            {
                // Find the coordinates of the corners of the frustum in world space.
                lm::dvec3 pt_ndc = lm::corner(NDC, j);
                lm::dvec4 pt_homogeneous = main_proj_view_inv * lm::dvec4(pt_ndc, 1.0);
                lm::dvec3 pt = pt_homogeneous.xyz / pt_homogeneous.w;

                // And then in view space
                lm::dvec3 pt_view = (new_info.view * lm::dvec4(pt, 1.0)).xyz;
                main_frustum_cascade_bbox = lm::expand(main_frustum_cascade_bbox, pt_view);
            }

            // "Squarify" the bbox so the polylines don't look squished, and position it
            // so that almost all of it is in front of the camera, and only a tiny sliver
            // "behind" the camera. (In practice, on the ground in front of the camera, but
            // below the view frustum.)
            lm::dvec2 bbox_center = lm::center(main_frustum_cascade_bbox).xy;
            lm::dvec2 bbox_bottom_center = {bbox_center.x, main_frustum_cascade_bbox.min.y};
            double max_side_size_half = lm::maxelem(lm::size(main_frustum_cascade_bbox).xy) / 2.0;

            lm::dmat4 proj = lm::orthographic_opengl(
                bbox_bottom_center.x - margin * max_side_size_half,
                bbox_bottom_center.x + margin * max_side_size_half,
                bbox_bottom_center.y + (1.0 - margin) * max_side_size_half,
                bbox_bottom_center.y + (1.0 + margin) * max_side_size_half, -(double)ORTHO_ALT,
                ORTHO_ALT + hrz::EARTH_RADIUS);

            return proj;
        };

        const double margin = 1.1;

        for (int i = 0; i < overlay_count; ++i)
        {
            double this_near = new_info.plane_distances[i];
            double this_far = new_info.plane_distances[i + 1];

            lm::dmat4 proj = compute_proj(this_near, this_far, margin);

            new_info.proj[i] = proj;
            new_info.pv_cc[i] = proj * new_info.view_cc;

            lm::dmat4 pv = proj * new_info.view;
            new_info.mvp_inv_main_view[i] = pv * lm::inverse(main_view_info.cam.view);

            const double max_side_size_half = std::max(1.0 / proj.x.x, 1.0 / proj.y.y);
            new_info.world_sizes[i] = max_side_size_half * 2.0;
        }

        const double heatmap_far = std::min(far, near * 20.0);
        new_info.heatmap_proj = compute_proj(near, heatmap_far, margin);
    }

    auto refresh_mvp = [&](OverlayCamerasInfo& info)
    {
        for (int i = 0; i < info.cascade_count; i++)
        {
            info.mvp_inv_main_view[i] =
                info.proj[i] * info.view * lm::inverse(main_view_info.cam.view);
        }
    };

    auto update_view = [&](VectorFlatOverlayRenderPass* pass, OverlayCamerasInfo& info)
    {
        for (int i = 0; i < info.cascade_count; i++)
        {
            pass->update(i, info.world_sizes[i]);
        }
    };

    if (visual_render_needed)
    {
        system->_overlay_cameras_info_visual = new_info;
        system->_latest_overlay_camera_info = &system->_overlay_cameras_info_visual;

        update_view(system->_visual_pass.get(), new_info);
        update_view(system->_selection_pass.get(), new_info);

        render_request.request_visual_render(RenderRequest::VisualCause::FlatOverlayRefresh);
    }
    else
    {
        refresh_mvp(system->_overlay_cameras_info_visual);
    }

    if (picking_render_needed)
    {
        system->_overlay_cameras_info_picking = new_info;
        system->_latest_overlay_camera_info = &system->_overlay_cameras_info_picking;

        update_view(system->_picking_pass.get(), new_info);

        render_request.request_picking_render();
    }
    else
    {
        refresh_mvp(system->_overlay_cameras_info_visual);
    }

    return render_request;
}

void register_views(
    VectorFlatOverlaySystem* system,
    Render* render,
    std::vector<my::Renderer::ViewId>& created_views)
{
    system->_visual_pass->set_cascade_count_to_render(
        system->_latest_overlay_camera_info->cascade_count);
    system->_picking_pass->set_cascade_count_to_render(
        system->_latest_overlay_camera_info->cascade_count);
    system->_selection_pass->set_cascade_count_to_render(
        system->_latest_overlay_camera_info->cascade_count);

    for (int i = 0; i < system->_latest_overlay_camera_info->cascade_count; i++)
    {
        my::View view;
        view.view = system->_latest_overlay_camera_info->view;
        view.projection = system->_latest_overlay_camera_info->proj[i];

        my::Renderer::ViewId view_id =
            render->rd->add_auxiliary_view(view, hrz::RenderFlatOverlayBin);

        system->_visual_pass->set_view(i, view_id);
        system->_picking_pass->set_view(i, view_id);
        system->_selection_pass->set_view(i, view_id);
        created_views.push_back(view_id);

        system->_latest_cascade_view_ids[i] = view_id;
    }
}

void draw(VectorFlatOverlaySystem* system, Render* render)
{
    HRZ_SCOPED_SAMPLE("vector flat overlay draw");

    OverlayCamerasUniform uniform_data;

    if (!system->_disabled)
    {
        // Apart from `overlay_cams_mvp_inv_main_view_*`, the rest of the
        // data is only used when drawing the vector features in the flat
        // overlay textures.
        // If both visual and picking renders are scheduled, they will have
        // the same values. So it doesn't matter which `overlay_camera_info`
        // is used, as long as it's the latest one.

        const OverlayCamerasInfo& info = *system->_latest_overlay_camera_info;
        lm::dvec3 overlay_cams_cam_pos = info.pos;
        hrz::split_double(
            overlay_cams_cam_pos.x, uniform_data.overlay_cams_pos_low.x,
            uniform_data.overlay_cams_pos_high.x);
        hrz::split_double(
            overlay_cams_cam_pos.y, uniform_data.overlay_cams_pos_low.y,
            uniform_data.overlay_cams_pos_high.y);
        hrz::split_double(
            overlay_cams_cam_pos.z, uniform_data.overlay_cams_pos_low.z,
            uniform_data.overlay_cams_pos_high.z);
        uniform_data.overlay_cams_pos_low.w = 0;
        uniform_data.overlay_cams_pos_high.w = 0;

        uniform_data.overlay_cams_view_cc = lm::mat4(info.view_cc);

        uniform_data.cascade_count = info.cascade_count;

        for (int i = 0; i < info.cascade_count; i++)
        {
            uniform_data.overlay_cams_proj[i] = lm::mat4(info.proj[i]);
            uniform_data.overlay_cams_pv_cc[i] = lm::mat4(info.pv_cc[i]);

            // On the other hand, `overlay_cams_mvp_inv_main_view_*` is
            // specific to when the flat overlay textures have been
            // rendered, so they can be different between visual and
            // picking.

            uniform_data.overlay_cams_mvp_inv_main_view_visual[i] =
                lm::mat4(system->_overlay_cameras_info_visual.mvp_inv_main_view[i]);
            uniform_data.overlay_cams_mvp_inv_main_view_picking[i] =
                lm::mat4(system->_overlay_cameras_info_picking.mvp_inv_main_view[i]);
        }
    }

    system->_overlay_cameras_ubo.set(0, uniform_data);
    system->_overlay_cameras_ubo.update(render->my);

    my::UboBinding binding = {
        hrz::vector_flat_overlay::UboVectorOverlayCameras,
        system->_overlay_cameras_ubo.get_for_gpu(), 0, sizeof(OverlayCamerasUniform)};

    render->rb->bind(1, &binding);
}

VectorFlatOverlaySystem* create_system(uint32_t cascade_count)
{
    return new VectorFlatOverlaySystem(cascade_count, !get_flag(Flag::EnableFlatOverlays));
}

void initialize_rendering(
    VectorFlatOverlaySystem* system,
    size_t texture_size,
    Render* render,
    CameraHeightSystem* camera_height_system,
    HeatmapSystem* heatmap_system)
{
    assert(system);
    system->initialize_rendering(texture_size, render, camera_height_system, heatmap_system);
}

void destroy_system(VectorFlatOverlaySystem* system, hrz::Render* render)
{
    assert(system);
    system->_visual_pass->destroy(render);
    system->_picking_pass->destroy(render);
    system->_selection_pass->destroy(render);
    delete system;
}

void get_visual_target_names(
    const VectorFlatOverlaySystem* system,
    const char* target_names[HRZ_S_MAX_OVERLAY_CASCADES])
{
    for (uint32_t i = 0; i < HRZ_S_MAX_OVERLAY_CASCADES; i++)
    {
        target_names[i] = system->_visual_pass->output_target_name(i);
    }
}

void get_picking_target_names(
    const VectorFlatOverlaySystem* system,
    const char* target_names[HRZ_S_MAX_OVERLAY_CASCADES])
{
    for (uint32_t i = 0; i < HRZ_S_MAX_OVERLAY_CASCADES; i++)
    {
        target_names[i] = system->_picking_pass->output_target_name(i);
    }
}

void get_selection_target_names(
    const VectorFlatOverlaySystem* system,
    const char* target_names[HRZ_S_MAX_OVERLAY_CASCADES])
{
    for (uint32_t i = 0; i < HRZ_S_MAX_OVERLAY_CASCADES; i++)
    {
        target_names[i] = system->_selection_pass->output_target_name(i);
    }
}

const OverlayCamerasInfo& get_latest_overlay_cameras_info(const VectorFlatOverlaySystem* system)
{
    assert(system && system->_latest_overlay_camera_info);
    return *system->_latest_overlay_camera_info;
}

void add_passes_to_render_graph(VectorFlatOverlaySystem* system, hrz::RenderView* render)
{
    render->rg->add_pass("flat overlay ortho", system->_visual_pass.get());
    render->rg->add_pass("flat overlay picking", system->_picking_pass.get());
    render->rg->add_pass("flat overlay selection", system->_selection_pass.get());
}

void schedule_visual_render(VectorFlatOverlaySystem* system, bool animation)
{
    assert(system);

    system->schedule_visual_render(animation);
}

void schedule_picking_render(VectorFlatOverlaySystem* system)
{
    assert(system);

    system->schedule_picking_render();
}

bool is_working(const VectorFlatOverlaySystem* system)
{
    assert(system);

    return system->is_working();
}

bool is_about_to_render(const VectorFlatOverlaySystem* system)
{
    assert(system);

    return system->_visual_pass->is_render_requested();
}

void dev_ui(const VectorFlatOverlaySystem* system, mu_Context* ctx)
{
    assert(system && ctx);

    if (system->_disabled)
    {
        mu_text(ctx, "Disabled");
        return;
    }

    fmt::memory_buffer buffer;

    static int layout[] = {150, -1};
    mu_layout_row(ctx, 2, layout, 0);

    const auto& visual_info = system->_overlay_cameras_info_visual;
    double scene_depth_min = system->_last_near_far.x;
    double scene_depth_max = system->_last_near_far.y;

    mu_text(ctx, "Cascade count");
    mu_text(ctx, hrz::format_to_buffer(buffer, "{}", visual_info.cascade_count));

    mu_text(ctx, "Scene depth range");
    mu_layout_next(ctx);

    {
        static int layout[] = {20, 126, -1};
        mu_layout_row(ctx, 3, layout, 0);

        mu_layout_next(ctx);
        mu_text(ctx, "Min");
        mu_text(ctx, hrz::format_to_buffer(buffer, "{:.2f} m", scene_depth_min));

        mu_layout_next(ctx);
        mu_text(ctx, "Max");
        mu_text(ctx, hrz::format_to_buffer(buffer, "{:.2f} m", scene_depth_max));
    }

    mu_layout_row(ctx, 2, layout, 0);

    const auto& pos = visual_info.pos;
    auto pos_geo = hrz::ecef_to_geo3(pos);

    mu_text(ctx, "Camera position (ECEF)");
    mu_text(ctx, hrz::format_to_buffer(buffer, "({:.2f}, {:.2f}, {:.2f})", pos.x, pos.y, pos.z));

    mu_text(ctx, "Camera position (geo)");
    mu_text(
        ctx,
        hrz::format_to_buffer(
            buffer, "(lat. {:.3f} deg, lon. {:.3f} deg, alt. {:.2f} m)", lm::degrees(pos_geo.lat),
            lm::degrees(pos_geo.lon), pos_geo.alt));

    if (mu_begin_treenode(ctx, "Cascades"))
    {
        for (int i = 0; i < visual_info.cascade_count; ++i)
        {
            {
                static const int layout[] = {-1};
                mu_layout_row(ctx, 1, layout, 0);

                mu_text(ctx, hrz::format_to_buffer(buffer, "Cascade {}", i));
            }

            {
                static int layout[] = {20, 102, -1};
                mu_layout_row(ctx, 3, layout, 0);

                auto near = visual_info.plane_distances[i];
                auto far = visual_info.plane_distances[i + 1];

                auto equals_eps = [](double x, double y) { return std::abs(x - y) < 0.01; };

                mu_layout_next(ctx);
                mu_text(ctx, "Near plane");
                mu_text(
                    ctx,
                    hrz::format_to_buffer(
                        buffer, "{:.2f} m{}", near,
                        equals_eps(near, scene_depth_min) ? " (scene depth range min)" : ""));

                mu_layout_next(ctx);
                mu_text(ctx, "Far plane");
                mu_text(
                    ctx,
                    hrz::format_to_buffer(
                        buffer, "{:.2f} m{}", far,
                        equals_eps(far, scene_depth_max) ? " (scene depth range max)" : ""));

                mu_layout_next(ctx);
                mu_text(ctx, "Size");
                mu_text(ctx, hrz::format_to_buffer(buffer, "{:.2f} m", visual_info.world_sizes[i]));
            }
        }

        mu_end_treenode(ctx);
    }
}
} // namespace vector_flat_overlay

} // namespace hrz
