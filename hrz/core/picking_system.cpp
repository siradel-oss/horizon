#include "hrz/core/picking_system.h"

#include "hrz/common/monitoring_defs.h"
#include "hrz/common/profiling.h"
#include "hrz/core/camera_height.h"
#include "hrz/core/download_buffer_pool.h"
#include "hrz/core/render/context.h"
#include "hrz/core/render/defs.h"
#include "hrz/core/render/resource_context.h"
#include "hrz/core/render/timed_render_pass.h"
#include "hrz/core/vector/flat_overlay.h"
#include "hrz/core/vector/heatmaps.h"
#include "hrz/fnd/defines.h"
#include "hrz/fnd/flat_hash_set.h"
#include "hrz/fnd/gen_object_pool.h"
#include "hrz/fnd/maths.h"
#include "hrz/fnd/mem.h"

#include <algorithm>
#include <deque>
#include <optional>
#include <vector>

namespace
{
struct PositionPickingRequest
{
    uint32_t layer_id;
    uint32_t object_id;
    lm::ivec2 pos;
    lm::ivec2 heatmap_pos;
    std::optional<lm::dvec3> world_pos;
    std::vector<hrz_proto::LayerHandle> included_rasters;
    float data_texture_value;
    std::vector<std::pair<hrz::heatmaps::ReprId, float>> heatmap_values;
    bool ready;
};

struct AreaPickingRequest
{
    lm::ibbox2 rect;
    hrz::flat_hash_set<uint64_t> result;
    bool ready;
};

class PickingPass : public hrz::render::TimedRenderPass
{
    const char* _input_camera_height;
    const char* _input_overlays[HRZ_S_MAX_OVERLAY_CASCADES];
    const char* _color_name;
    const char* _depth_stencil_name;
    const char* _depth_read_value_name;
    my::ResourceHandle _color_target;
    my::ResourceHandle _depth_stencil_target;
    my::ResourceHandle _depth_read_value_target;
    my::ResourceHandle _fbo;
    lm::ibbox2 _rect;
    my::Rect _viewport;

    my::ResourceHandle _camera_height_texture;
    my::ResourceHandle _camera_height_sampler;
    my::ResourceHandle _overlay_textures[HRZ_S_MAX_OVERLAY_CASCADES];
    my::ResourceHandle _overlay_texture_sampler;

    bool _scheduled = false;
    uint64_t _next_download_id_id;
    uint64_t _next_download_id_depth_value;

    hrz::DownloadBufferPool _buffers_pool;
    hrz::DownloadBuffer _depth_value_buffer;
    hrz::DownloadBuffer _id_buffer;

public:
    PickingPass(
        const hrz::CameraHeightSystem* camera_height_system,
        const hrz::VectorFlatOverlaySystem* flat_overlay) :
        TimedRenderPass("picking"),
        _color_name("picking_color"),
        _depth_stencil_name("picking_depth"),
        _depth_read_value_name("picking_depth_read")
    {
        _input_camera_height = hrz::camera_height::get_target_name(camera_height_system);
        hrz::vector_flat_overlay::get_picking_target_names(flat_overlay, _input_overlays);
    }

    void schedule_pick(lm::ibbox2 rect, uint64_t id_id, uint64_t depth_id)
    {
        if (!_scheduled)
        {
            _rect = rect;
            _scheduled = true;
            _next_download_id_id = id_id;
            _next_download_id_depth_value = depth_id;
        }
        else
        {
            assert(id_id == _next_download_id_id);
            assert(depth_id == _next_download_id_depth_value);
            _rect = lm::expand(lm::expand(_rect, rect.min), rect.max);
        }
    }

    void free_download_buffer(const hrz::DownloadBuffer& buffer) { _buffers_pool.release(buffer); }

    void destroy(my::ResourceContext* rc)
    {
        rc->dealloc(_fbo);
        rc->dealloc(_camera_height_sampler);
        rc->dealloc(_overlay_texture_sampler);
        _buffers_pool.free_all(rc);
    }

    void setup_pass(my::RenderGraph::SetupContext& ctx) override
    {
        for (const char* name : _input_overlays)
        {
            ctx.read(name, my::RenderGraph::Sampled);
        }

        my::RenderGraph::ResourceInfo depth_stencil;
        depth_stencil.format = my::TextureFormat::Depth32FStencil8;
        depth_stencil.size_class = my::RenderGraph::ResourceInfo::BackbufferRelative;
        depth_stencil.width = 1.0f;
        depth_stencil.height = 1.0f;

        my::RenderGraph::ResourceInfo depth_read_value;
        depth_read_value.format = my::TextureFormat::RG32F;
        depth_read_value.size_class = my::RenderGraph::ResourceInfo::BackbufferRelative;
        depth_read_value.width = 1.0f;
        depth_read_value.height = 1.0f;

        my::RenderGraph::ResourceInfo color;
#if HRZ_DESKTOP
        // This makes ReadPixels faster on desktop. No effect on WebGL. (On Windows at least...)
        color.format = my::TextureFormat::RGBA32UI;
#else
        color.format = my::TextureFormat::RG32UI;
#endif
        color.size_class = my::RenderGraph::ResourceInfo::BackbufferRelative;
        color.width = 1.0f;
        color.height = 1.0f;

        ctx.create(_color_name, my::RenderGraph::Target, color);
        ctx.create(_depth_stencil_name, my::RenderGraph::Target, depth_stencil);
        ctx.create(_depth_read_value_name, my::RenderGraph::Target, depth_read_value);
    }

    void retrieve_resources(
        my::Instance* my,
        my::ResourceContext* rc,
        const my::RenderGraph::ResourceContext& ctx) override
    {
        _color_target = ctx.retrieve(_color_name);
        _depth_stencil_target = ctx.retrieve(_depth_stencil_name);
        _depth_read_value_target = ctx.retrieve(_depth_read_value_name);

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
                ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::Picking);
        }

        for (uint32_t i = 0; i < HRZ_S_MAX_OVERLAY_CASCADES; i++)
        {
            _overlay_textures[i] = ctx.retrieve(_input_overlays[i]);
        }

        {
            my::SamplerResource res;
            res.sampler.wrap_x = my::SamplerParams::Wrap::Clamp;
            res.sampler.wrap_y = my::SamplerParams::Wrap::Clamp;
            res.sampler.wrap_z = my::SamplerParams::Wrap::Clamp;
            res.sampler.mag_filter = my::SamplerParams::Filter::Nearest;
            res.sampler.min_filter = my::SamplerParams::Filter::Nearest;
            res.use_mipmaps = false;
            _overlay_texture_sampler =
                ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::Picking);
        }

        {
            my::FramebufferAttachment attachments[] = {
                {my::Attachment::DepthStencil, _depth_stencil_target},
                {my::Attachment::Color0, _color_target},
                {my::Attachment::Color1, _depth_read_value_target}};

            my::FramebufferResource res;
            res.attachment_count = HRZ_ARRAY_COUNT(attachments);
            res.attachments = attachments;

            _fbo = ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::Picking);
        }
    }

    void execute_timed(const my::RenderGraph::ExecutionContext& ctx) override
    {
        if (!_scheduled) return;

        HRZ_SCOPED_SAMPLE("picking pass draw");

        const hrz::SceneViewRenderGraphUserData* user_data =
            (const hrz::SceneViewRenderGraphUserData*)ctx.user_data;

        my::Rect scissor = rect({ctx.backbuffer_width, ctx.backbuffer_height});
        _viewport = my::Rect{0, 0, ctx.backbuffer_width, ctx.backbuffer_height};

        const my::ViewportState viewport_state = {
            _viewport,
            scissor,
        };

        ctx.binder->push_state();

        my::TextureBinding bindings[HRZ_S_MAX_OVERLAY_CASCADES + 1];

        bindings[0] = {hrz::SamplerCameraHeight, _camera_height_texture, _camera_height_sampler};

        for (int i = 0; i < HRZ_S_MAX_OVERLAY_CASCADES; i++)
        {
            bindings[i + 1] = {
                hrz::vector_flat_overlay::SamplerOverlayStart + i, _overlay_textures[i],
                _overlay_texture_sampler};
        }

        ctx.binder->bind(HRZ_ARRAY_COUNT(bindings), bindings);

        ctx.render->set_framebuffer(_fbo, viewport_state);

        static const my::ClearTarget depth_clear_target = {
            my::Attachment::DepthStencil,
            my::ClearValue::make_depth_stencil(1.0, 128),
        };

        static const my::ClearTarget all_clear_targets[] = {
            // @Note: Some devices (e.g. Huawei MediaPad M5) complain when clearing color attachment
            // 0 in Horizon WASM with the following error: "GL ERROR :GL_INVALID_OPERATION :
            // glClearBufferuiv: can only be called on unsigned integer buffers".
            // When a shape from the shape editor is present, then picking also yields the following
            // error which may be correlated: "GL ERROR :GL_INVALID_OPERATION : glDrawElements:
            // buffer format and fragment output variable type incompatible".
            {
                my::Attachment::Color0,
                my::ClearValue::make_color_uint(0, 0, 0, 0),
            },
            {
                my::Attachment::Color1,
                my::ClearValue::make_color_float(0.0, 0.0, 0.0, 0.0),
            },
            depth_clear_target};

        ctx.render->clear(3, all_clear_targets);

        {
            my::Renderer::BinMask pass_masks[] = {
                hrz::RenderWorldOpaqueBin | hrz::RenderWorldTransparentBin
                | hrz::RenderSymbolicBin};

            ctx.renderer->draw(
                hrz::RenderPicking, user_data->main_view, HRZ_ARRAY_COUNT(pass_masks), pass_masks,
                ctx.render, ctx.binder, ctx.user_data);
        }

        {
            my::Renderer::BinMask pass_masks[] = {hrz::RenderDecalBin, hrz::RenderInWorldBin};

            ctx.renderer->draw(
                hrz::RenderPicking, user_data->main_view, HRZ_ARRAY_COUNT(pass_masks), pass_masks,
                ctx.render, ctx.binder, ctx.user_data);
        }

        // Everything in the SymbolicOverlay bin appears above other world elements (except for UI
        // elements in the world and such, like the shape editor controls). So everything in that
        // bin should not be obstructed during the picking render either.
        // The depth buffer used for the picking renders is not used for anything else either, so
        // clearing it is OK.
        ctx.render->clear(1, &depth_clear_target);

        {
            my::Renderer::BinMask pass_masks[] = {hrz::RenderSymbolicOverlayBin};

            ctx.renderer->draw(
                hrz::RenderPicking, user_data->main_view, HRZ_ARRAY_COUNT(pass_masks), pass_masks,
                ctx.render, ctx.binder, ctx.user_data);
        }

        size_t id_buffer_size = scissor.w * scissor.h * sizeof(uint32_t) * 4;
        size_t depth_value_buffer_size = scissor.w * scissor.h * sizeof(float) * 4;

        _id_buffer = _buffers_pool.acquire(
            (hrz::GpuResourceContext*)ctx.resource, id_buffer_size,
            {hrz::monitoring::systems::Picking});
        _depth_value_buffer = _buffers_pool.acquire(
            (hrz::GpuResourceContext*)ctx.resource, depth_value_buffer_size,
            {hrz::monitoring::systems::Picking});

        ctx.render->color_texture_download_async(
            _next_download_id_id, _fbo, my::Attachment::Color0, scissor,
            my::TextureDownloadFormat::RGBA32UI, _id_buffer.buffer);

        ctx.render->color_texture_download_async(
            _next_download_id_depth_value, _fbo, my::Attachment::Color1, scissor,
            my::TextureDownloadFormat::RGBA32F, _depth_value_buffer.buffer);

        ctx.binder->pop_state();

        _scheduled = false;
    }

    hrz::DownloadBuffer begin_heatmap_texture_download(
        uint64_t download_id,
        my::Rect rect,
        my::ResourceHandle fbo,
        hrz::Render* render)
    {
        size_t heatmap_buffer_size = rect.w * rect.h * sizeof(float) * 4;

        hrz::DownloadBuffer download_buffer = _buffers_pool.acquire(
            render->rc, heatmap_buffer_size, {hrz::monitoring::systems::Picking});

        render->my->color_texture_download_async(
            download_id, fbo, my::Attachment::Color0, rect, my::TextureDownloadFormat::RGBA32F,
            download_buffer.buffer);

        return download_buffer;
    }

    my::Rect rect(lm::uvec2 viewport_size) const
    {
        uint32_t min_x = (uint32_t)hrz::clamp(_rect.min.x, 0, (int)viewport_size.x - 1);
        uint32_t max_x = (uint32_t)hrz::clamp(_rect.max.x, 0, (int)viewport_size.x - 1);

        uint32_t min_y = (uint32_t)hrz::clamp(
            ((int)viewport_size.y - _rect.max.y - 1), 0, (int)viewport_size.y - 1);
        uint32_t max_y = (uint32_t)hrz::clamp(
            ((int)viewport_size.y - _rect.min.y - 1), 0, (int)viewport_size.y - 1);

        return my::Rect{
            min_x,
            min_y,
            max_x - min_x + 1,
            max_y - min_y + 1,
        };
    }

    void recycle_buffer(hrz::DownloadBuffer buffer) { _buffers_pool.release(buffer); }

    my::Rect viewport() const { return _viewport; }

    hrz::DownloadBuffer id_buffer() const { return _id_buffer; }

    hrz::DownloadBuffer depth_value_buffer() const { return _depth_value_buffer; }
};

} // namespace

namespace hrz
{
struct Frame
{
    std::vector<picking::PositionTicket> pos_queued;
    std::vector<picking::AreaTicket> area_queued;
    lm::dmat4 proj;
    lm::dmat4 view;
    hrz::OverlayCamerasInfo heatmap_overlay_cameras_info;
    my::Rect viewport;
    my::Rect rect;
    uint64_t download_id_id;
    uint64_t download_id_depth_value;
    DownloadBuffer id_buffer;
    DownloadBuffer depth_value_buffer;
};

struct HeatmapFrame
{
    std::vector<picking::PositionTicket> pos_queued;
    hrz::OverlayCamerasInfo overlay_cameras_info;
    my::Rect rect;

    struct DownloadData
    {
        uint64_t id;
        heatmaps::ReprId heatmap_repr_id;
        DownloadBuffer buffer;
    };

    std::vector<DownloadData> downloads;
};

struct PickingSystem
{
    static_assert(sizeof(picking::PositionTicket) == 4, "Size of picking position ticket");
    using PositionIndexPool = GenIndexPool<uint32_t, 12, 20>;
    using PositionRequestsPool = GenObjectPool<PositionPickingRequest, PositionIndexPool, 128>;

    static_assert(sizeof(picking::AreaTicket) == 4, "Size of picking area ticket");
    using AreaIndexPool = GenIndexPool<uint32_t, 12, 20>;
    using AreaRequestsPool = GenObjectPool<AreaPickingRequest, AreaIndexPool, 128>;

    std::optional<Frame> next_frame;
    std::deque<Frame> previous_frames;
    std::deque<HeatmapFrame> previous_heatmap_frames;

    PositionRequestsPool position_requests_pool;
    AreaRequestsPool area_requests_pool;
    std::unique_ptr<PickingPass> pass;

    lm::dmat4 proj_matrix;
    lm::dmat4 view_matrix;
    hrz::OverlayCamerasInfo overlay_cameras_info;
    uint32_t flat_overlay_texture_size;
};

} // namespace hrz

namespace
{
void _read_depth_and_id_pixels(
    hrz::PickingSystem* system,
    const hrz::Frame& frame,
    const uint32_t* id_buffer,
    const float* depth_value_buffer)
{
    HRZ_SCOPED_SAMPLE("picking system read pixels");

    lm::dmat4 inverse_view = lm::inverse(frame.view);
    ptrdiff_t max_index = frame.rect.w * frame.rect.h - 1;
    lm::ibbox2 viewport_rect = lm::ibbox2(
        lm::ivec2(frame.viewport.x, frame.viewport.y),
        lm::ivec2(frame.viewport.x + frame.viewport.w, frame.viewport.y + frame.viewport.h));

    for (hrz::picking::PositionTicket ticket : frame.pos_queued)
    {
        PositionPickingRequest* req = system->position_requests_pool.get_object(ticket.o);
        if (!req) continue;

        lm::ivec2 pos = req->pos;
        lm::ivec2 pixel = lm::ivec2(pos.x, frame.viewport.h - pos.y - 1);
        ptrdiff_t index = pixel.x - frame.rect.x + (pixel.y - frame.rect.y) * frame.rect.w;

        if (index > max_index || !lm::contains(viewport_rect, pos))
        {
            HRZ_LOG_ERROR("Out-of-bounds picking pixel read: {}, {}", pos.x, pos.y);
            req->ready = true;
            continue;
        }

        ptrdiff_t id_offset = index * 4;
        uint32_t combined_id = id_buffer[id_offset + 0];
        uint32_t object_id = id_buffer[id_offset + 1];

        req->layer_id = combined_id;
        req->object_id = object_id;

        ptrdiff_t depth_value_offset = index * 4;
        double depth = (double)depth_value_buffer[depth_value_offset];
        float value = depth_value_buffer[depth_value_offset + 1];

        // If the depth texture has a value of 0.0, we consider that nothing was picked, so there
        // is no world position to return.
        if (depth == 0.0)
        {
            req->world_pos = std::nullopt;
        }
        else
        {
            double ndc_x = (2 * ((double)pixel.x + 0.5f)) / (double)frame.viewport.w - 1;
            double ndc_y = (2 * ((double)pixel.y + 0.5f)) / (double)frame.viewport.h - 1;

            lm::dvec3 ndc_coords(
                (ndc_x + frame.proj.m[2][0]) * depth / frame.proj.m[0][0],
                (ndc_y + frame.proj.m[2][1]) * depth / frame.proj.m[1][1], -depth);

            lm::dvec4 world_coords = inverse_view * lm::dvec4(ndc_coords, 1.0);
            req->world_pos = world_coords.xyz;
        }

        req->data_texture_value = value;

        // There is no heatmap value to fetch if nowhere in the world was picked, so the request
        // is ready to be returned.
        req->ready = !req->world_pos.has_value();
    }

    for (hrz::picking::AreaTicket ticket : frame.area_queued)
    {
        AreaPickingRequest* req = system->area_requests_pool.get_object(ticket.o);
        if (!req) continue;

        uint64_t last_id = 0;

        int rect_width = lm::size(req->rect).x;

        for (int y = req->rect.min.y; y < req->rect.max.y; ++y)
        {
            lm::ivec2 pos(req->rect.min.x, y);
            lm::ivec2 pixel = lm::ivec2(pos.x, frame.viewport.h - pos.y - 1);
            ptrdiff_t index = pixel.x - frame.rect.x + (pixel.y - frame.rect.y) * frame.rect.w;

            const uint32_t* id_ptr = (const uint32_t*)(id_buffer + index * 4);

            ptrdiff_t index_end = index + rect_width - 1;
            lm::ivec2 pos_end = pos + lm::ivec2(rect_width - 1, 0);
            if (index > max_index || index_end > max_index || !lm::contains(viewport_rect, pos_end))
            {
                HRZ_LOG_ERROR(
                    "Out-of-bounds picking pixel read: ({}, {}) to ({}, {})", pos.x, pos.y,
                    pos_end.x, pos_end.y);
                continue;
            }

            for (int x = 0; x < rect_width; ++x)
            {
                const uint32_t layer_id = id_ptr[0];
                const uint32_t object_id = id_ptr[1];
                id_ptr += 4;

                const uint64_t id = hrz::picking::make_packed_object_reference(layer_id, object_id);
                if (id != last_id && id != 0) // Optimization: skip insertion most of the time
                {
                    last_id = id;
                    req->result.insert(id);
                }
            }
        }

        // Heatmap values are not returned for area requests, so the request can already be marked
        // as ready.
        req->ready = true;
    }
}

void _read_heatmap_pixels(
    hrz::PickingSystem* system,
    const hrz::HeatmapFrame& frame,
    hrz::heatmaps::ReprId heatmap_repr_id,
    const float* heatmap_value_buffer)
{
    for (hrz::picking::PositionTicket ticket : frame.pos_queued)
    {
        PositionPickingRequest* req = system->position_requests_pool.get_object(ticket.o);
        if (!req) continue;

        ptrdiff_t index =
            req->heatmap_pos.x - frame.rect.x + (req->heatmap_pos.y - frame.rect.y) * frame.rect.w;

        req->heatmap_values.push_back({heatmap_repr_id, heatmap_value_buffer[index * 4]});
    }
}

} // namespace

namespace hrz::picking
{
void _prepare_next_frame_if_needed(PickingSystem* system)
{
    if (!system->next_frame.has_value())
    {
        Frame next_frame;
        next_frame.viewport = {0, 0, 0, 0};
        next_frame.rect = {0, 0, 0, 0};
        next_frame.download_id_id = hrz::render::acquire_texture_download_id();
        next_frame.download_id_depth_value = hrz::render::acquire_texture_download_id();
        system->next_frame = std::move(next_frame);
    }
}

PositionTicket schedule_pick(
    PickingSystem* system,
    lm::ivec2 pos,
    std::span<const hrz_proto::LayerHandle> included_rasters)
{
    assert(system);

    PositionTicket ticket{system->position_requests_pool.alloc()};
    PositionPickingRequest* req = system->position_requests_pool.get_object(ticket.o);
    req->ready = false;
    req->pos = pos;
    for (const auto& layer : included_rasters)
    {
        req->included_rasters.push_back(layer);
    }

    _prepare_next_frame_if_needed(system);

    Frame& next_frame = system->next_frame.value();
    next_frame.pos_queued.push_back(ticket);
    system->pass->schedule_pick(
        lm::ibbox2(pos, pos), next_frame.download_id_id, next_frame.download_id_depth_value);

    return ticket;
}

AreaTicket schedule_pick(PickingSystem* system, lm::ibbox2 rect)
{
    assert(system);

    AreaTicket ticket{system->area_requests_pool.alloc()};
    AreaPickingRequest* req = system->area_requests_pool.get_object(ticket.o);
    req->ready = false;
    req->rect = rect;

    _prepare_next_frame_if_needed(system);

    Frame& next_frame = system->next_frame.value();
    next_frame.area_queued.push_back(ticket);
    system->pass->schedule_pick(
        rect, next_frame.download_id_id, next_frame.download_id_depth_value);

    return ticket;
}

RenderRequest work(PickingSystem* system)
{
    assert(system);

    RenderRequest render_request;
    if (system->next_frame.has_value())
    {
        render_request.request_picking_render();
    }
    return render_request;
}

PickingSystem* create_system()
{
    auto system = new PickingSystem();
    return system;
}

my::RenderPassId initialize_rendering(
    PickingSystem* sys,
    const CameraHeightSystem* camera_height_system,
    const VectorFlatOverlaySystem* flat_overlays,
    uint32_t flat_overlay_texture_size,
    RenderView* render)
{
    assert(sys && camera_height_system && flat_overlays && render);

    sys->flat_overlay_texture_size = flat_overlay_texture_size;

    sys->pass.reset(new PickingPass(camera_height_system, flat_overlays));
    return render->rg->add_pass("picking", sys->pass.get());
}

void destroy_system(PickingSystem* system, Render* render)
{
    assert(system);
    assert(render);

    for (auto& frame : system->previous_frames)
    {
        render->my->cancel_texture_download(frame.download_id_depth_value);
        render->my->cancel_texture_download(frame.download_id_id);
        system->pass->free_download_buffer(frame.depth_value_buffer);
        system->pass->free_download_buffer(frame.id_buffer);
        hrz::render::release_texture_download_id(frame.download_id_depth_value);
        hrz::render::release_texture_download_id(frame.download_id_id);
    }
    system->previous_frames.clear();

    for (auto& frame : system->previous_heatmap_frames)
    {
        for (auto& download : frame.downloads)
        {
            render->my->cancel_texture_download(download.id);
            system->pass->free_download_buffer(download.buffer);
            hrz::render::release_texture_download_id(download.id);
        }
    }
    system->previous_heatmap_frames.clear();

    system->pass->destroy(render->my);
    delete system;
}

void set_matrices_for_this_frame(
    PickingSystem* sys,
    const lm::dmat4& proj,
    const lm::dmat4& view,
    const hrz::OverlayCamerasInfo& overlay_cameras_info)
{
    assert(sys);

    // We don't put them directly in next_frame because we may not know
    // that we need picking this frame yet.
    // Instead we'll copy the matrices in end_frame, when we know for sure
    // that some picking stuff happened during the frame.
    sys->proj_matrix = proj;
    sys->view_matrix = view;
    sys->overlay_cameras_info = overlay_cameras_info;
}

void end_frame(PickingSystem* sys)
{
    assert(sys);

    if (!sys->next_frame.has_value()) return;

    Frame this_frame = std::move(*sys->next_frame);
    sys->next_frame.reset();

    this_frame.proj = sys->proj_matrix;
    this_frame.view = sys->view_matrix;
    this_frame.viewport = sys->pass->viewport();
    this_frame.rect = sys->pass->rect({this_frame.viewport.w, this_frame.viewport.h});
    this_frame.id_buffer = sys->pass->id_buffer();
    this_frame.depth_value_buffer = sys->pass->depth_value_buffer();
    this_frame.heatmap_overlay_cameras_info = sys->overlay_cameras_info;

    sys->previous_frames.push_back(std::move(this_frame));
}

void retrieve_results(PickingSystem* sys, HeatmapSystem* heatmaps, Render* render)
{
    assert(sys && render);

    while (!sys->previous_frames.empty())
    {
        const Frame& frame = sys->previous_frames.front();

        bool id_ready = render->my->is_texture_download_ready(frame.download_id_id);
        bool depth_ready = render->my->is_texture_download_ready(frame.download_id_depth_value);

        // Since the results are expected to be made available in the same order they
        // were submitted, we don't need to continue looking at the queue if one
        // frame was not ready, so we break.
        if (!id_ready || !depth_ready)
        {
            break;
        }

        auto result_id = render->my->retrieve_texture_download(frame.download_id_id);
        auto result_depth_value =
            render->my->retrieve_texture_download(frame.download_id_depth_value);

        _read_depth_and_id_pixels(
            sys, frame, (const uint32_t*)result_id.data.get(),
            (const float*)result_depth_value.data.get());

        sys->pass->recycle_buffer(frame.id_buffer);
        sys->pass->recycle_buffer(frame.depth_value_buffer);

        hrz::render::release_texture_download_id(frame.download_id_depth_value);
        hrz::render::release_texture_download_id(frame.download_id_id);

        // Position requests require downloading the heatmap texture used for the flat overlays,
        // meaning they are not ready to be returned yet and need further work.
        if (!frame.pos_queued.empty())
        {
            std::optional<lm::ubbox2> required_heatmap_rect = std::nullopt;

            for (auto ticket : frame.pos_queued)
            {
                PositionPickingRequest* req = sys->position_requests_pool.get_object(ticket.o);
                if (!req || req->ready) continue;

                auto heatmap_pixel_opt =
                    hrz::heatmaps::world_position_to_heatmap_texture_coordinates(
                        req->world_pos.value(), frame.heatmap_overlay_cameras_info,
                        sys->flat_overlay_texture_size);
                if (heatmap_pixel_opt.has_value())
                {
                    req->heatmap_pos = (lm::ivec2)heatmap_pixel_opt.value();
                    required_heatmap_rect = (required_heatmap_rect.has_value())
                        ? lm::expand(required_heatmap_rect.value(), heatmap_pixel_opt.value())
                        : lm::ubbox2(heatmap_pixel_opt.value(), heatmap_pixel_opt.value());
                }
            }

            if (!required_heatmap_rect.has_value())
            {
                for (auto ticket : frame.pos_queued)
                {
                    PositionPickingRequest* req = sys->position_requests_pool.get_object(ticket.o);
                    req->ready = true;
                }
            }
            else
            {
                HeatmapFrame heatmap_frame;
                heatmap_frame.pos_queued = std::move(frame.pos_queued);
                heatmap_frame.overlay_cameras_info = frame.heatmap_overlay_cameras_info;

                heatmap_frame.rect.x = required_heatmap_rect.value().min.x;
                heatmap_frame.rect.y = required_heatmap_rect.value().min.y;
                heatmap_frame.rect.w =
                    required_heatmap_rect.value().max.x - heatmap_frame.rect.x + 1;
                heatmap_frame.rect.h =
                    required_heatmap_rect.value().max.y - heatmap_frame.rect.y + 1;

                auto heatmap_fbos = heatmaps::get_fbos(heatmaps);
                for (auto fbo : heatmap_fbos)
                {
                    HeatmapFrame::DownloadData download = {};
                    download.id = render::acquire_texture_download_id();
                    download.heatmap_repr_id = fbo.first;
                    download.buffer = sys->pass->begin_heatmap_texture_download(
                        download.id, heatmap_frame.rect, fbo.second, render);

                    heatmap_frame.downloads.push_back(std::move(download));
                }

                sys->previous_heatmap_frames.push_back(std::move(heatmap_frame));
            }
        }

        sys->previous_frames.pop_front();
    }

    while (!sys->previous_heatmap_frames.empty())
    {
        HeatmapFrame& frame = sys->previous_heatmap_frames.front();

        for (auto it = frame.downloads.begin(); it != frame.downloads.end();)
        {
            auto& download = *it;
            if (!render->my->is_texture_download_ready(download.id))
            {
                it++;
                continue;
            }

            auto result_heatmap = render->my->retrieve_texture_download(download.id);
            _read_heatmap_pixels(
                sys, frame, download.heatmap_repr_id, (const float*)result_heatmap.data.get());

            sys->pass->recycle_buffer(download.buffer);
            hrz::render::release_texture_download_id(download.id);

            it = frame.downloads.erase(it);
        }

        if (!frame.downloads.empty())
        {
            break;
        }

        for (auto ticket : frame.pos_queued)
        {
            auto* req = sys->position_requests_pool.get_object(ticket.o);
            req->ready = true;
        }
        sys->previous_heatmap_frames.pop_front();
    }
}

void cancel(PickingSystem* system, PositionTicket ticket)
{
    assert(system);
    assert(system->position_requests_pool.is_valid(ticket.o));
    system->position_requests_pool.release(ticket.o);
}

void cancel(PickingSystem* system, AreaTicket ticket)
{
    assert(system);
    assert(system->area_requests_pool.is_valid(ticket.o));
    system->area_requests_pool.release(ticket.o);
}

bool retrieve_result(PickingSystem* system, PositionTicket ticket, PositionResult* result)
{
    assert(system);

    PositionPickingRequest* req = system->position_requests_pool.get_object(ticket.o);
    if (!req || !req->ready) return false;

    result->position = req->world_pos;
    result->ref = ObjectReference::from_packed(req->layer_id, req->object_id);
    result->data_texture_value = req->data_texture_value;
    result->heatmap_values = std::move(req->heatmap_values);
    result->included_rasters = req->included_rasters;
    system->position_requests_pool.release(ticket.o);

    return true;
}

bool retrieve_result(PickingSystem* sys, AreaTicket ticket, std::vector<AreaResult>& result)
{
    assert(sys);

    AreaPickingRequest* req = sys->area_requests_pool.get_object(ticket.o);
    if (!req || !req->ready) return false;

    const size_t start_index = result.size();

    for (const uint64_t id : req->result)
    {
        AreaResult res;
        res.ref = ObjectReference::from_packed(id);

        result.push_back(res);
    }

    sys->area_requests_pool.release(ticket.o);

    // Sort the results so that translation to layer handle/object id pairs can
    // be faster than naively translating each one one by one later on.

    std::sort(
        result.begin() + start_index, result.end(),
        [](const AreaResult& a, const AreaResult& b) -> bool
        {
            if (a.ref.system_id != b.ref.system_id)
            {
                return a.ref.system_id < b.ref.system_id;
            }
            else if (a.ref.complementary_id != b.ref.complementary_id)
            {
                return a.ref.complementary_id < b.ref.complementary_id;
            }
            else
            {
                return a.ref.object_id < b.ref.object_id;
            }
        });

    return true;
}

} // namespace hrz::picking
