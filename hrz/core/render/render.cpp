#include "hrz/core/global_flags.h"
#include "hrz/core/render/context.h"
#include "hrz/core/render/profiling.h"
#include "hrz/core/render/resource_context.h"
#include "hrz/core/render/screen_space.h"
#include "hrz/core/render/timed_render_pass.h"
#include "hrz/core/render/vertex_input_builder.h"
#include "hrz/core/render_request.h"
#include "hrz/fnd/gen_index_pool.h"

#include <mycelium/properties.h>

#include <deque>

namespace hrz
{
my::ResourceHandle GpuResourceContext::alloc(const my::Resource* res)
{
    auto handle = rc->alloc(res);
    monitoring->register_gpu_resource(
        handle, res->type, default_system_for_allocs, monitoring::NoLayer);
    register_texture_metadata(res, handle);
    return handle;
}

my::ResourceHandle GpuResourceContext::alloc(
    const my::Resource* res,
    monitoring::systems::Name system,
    uint64_t layer_id,
    std::initializer_list<std::pair<MetadataString, MetadataString>> metadata)
{
    auto handle = rc->alloc(res);
    if (handle.is_null()) return handle;

    monitoring->register_gpu_resource(handle, res->type, system, layer_id);
    for (auto& it : metadata)
    {
        monitoring->register_gpu_resource_metadata(
            handle, std::move(it.first), std::move(it.second));
    }
    register_texture_metadata(res, handle);
    return handle;
}

my::ResourceHandle GpuResourceContext::alloc(
    const my::Resource* res,
    const monitoring::ResourceOwner& resource_owner,
    std::initializer_list<std::pair<MetadataString, MetadataString>> metadata)
{
    auto handle = rc->alloc(res);
    if (handle.is_null()) return handle;

    monitoring->register_gpu_resource(
        handle, res->type, resource_owner.system, resource_owner.layer_id);
    for (auto& it : metadata)
    {
        monitoring->register_gpu_resource_metadata(
            handle, std::move(it.first), std::move(it.second));
    }
    register_texture_metadata(res, handle);
    return handle;
}

my::ResourceHandle GpuResourceContext::alloc(
    const my::Resource* res,
    const monitoring::ResourceOwner& resource_owner,
    std::span<std::pair<MetadataString, MetadataString>> metadata)
{
    auto handle = rc->alloc(res);
    if (handle.is_null()) return handle;

    monitoring->register_gpu_resource(
        handle, res->type, resource_owner.system, resource_owner.layer_id);
    for (const auto& it : metadata)
    {
        monitoring->register_gpu_resource_metadata(handle, it.first, it.second);
    }
    register_texture_metadata(res, handle);
    return handle;
}

void GpuResourceContext::register_texture_metadata(
    const my::Resource* res,
    my::ResourceHandle handle)
{
    if (res->type == my::Resource::Texture)
    {
        auto texture_res = (const my::TextureResource*)res;

        if (texture_res->is_render_graph_texture)
        {
            monitoring->register_gpu_resource_metadata(handle, "render graph texture", "");
            monitoring->register_gpu_resource_metadata(handle, "contents", texture_res->name);
        }

        auto& layout = texture_res->layout;

        const char* type = "";
        switch (layout.type)
        {
            case my::TextureLayout::Type::Type2D: type = "2D"; break;
            case my::TextureLayout::Type::Type3D: type = "3D"; break;
            case my::TextureLayout::Type::Array: type = "array"; break;
            default: assert(false && "Unhandled case");
        }
        monitoring->register_gpu_resource_metadata(handle, "texture type", type);

        monitoring->register_gpu_resource_metadata(handle, "format", my::format_str(layout.format));

        monitoring->register_gpu_resource_metadata(handle, "width", std::to_string(layout.width));
        monitoring->register_gpu_resource_metadata(handle, "height", std::to_string(layout.height));
        if (layout.type == my::TextureLayout::Type::Type3D
            || layout.type == my::TextureLayout::Type::Array)
        {
            monitoring->register_gpu_resource_metadata(
                handle, "depth", std::to_string(layout.depth));
        }

        uint32_t levels = layout.levels;
        if (levels == 1 && texture_res->generate_mipmaps)
        {
            auto max_size = std::max(std::max(layout.width, layout.height), layout.depth);
            levels = (uint32_t)std::log2((float)hrz::next_power_of_two(max_size)) + 1;
        }
        monitoring->register_gpu_resource_metadata(handle, "mipmap levels", std::to_string(levels));
    }
    else if (res->type == my::Resource::Renderbuffer)
    {
        auto rb_res = (const my::RenderbufferResource*)res;

        if (rb_res->is_render_graph_render_buffer)
        {
            monitoring->register_gpu_resource_metadata(handle, "render graph texture", "");
            monitoring->register_gpu_resource_metadata(handle, "contents", rb_res->name);
        }

        monitoring->register_gpu_resource_metadata(
            handle, "format", my::format_str(rb_res->format));
        monitoring->register_gpu_resource_metadata(handle, "width", std::to_string(rb_res->width));
        monitoring->register_gpu_resource_metadata(
            handle, "height", std::to_string(rb_res->height));
    }
}

namespace render
{
static GenIndexPool<uint64_t, 32, 32> texture_index_pool;

uint64_t acquire_texture_download_id()
{
    return texture_index_pool.alloc();
}

void release_texture_download_id(uint64_t id)
{
    texture_index_pool.release(id);
}

namespace profiling
{
static GenIndexPool<uint64_t, 32, 32> query_index_pool;
static bool _enabled = false;

struct FrameTimeQueries
{
    std::vector<std::vector<uint64_t>> time_queries;
    RenderRequest render_request;
};

static std::deque<FrameTimeQueries> time_queries;

uint64_t acquire_time_query_id()
{
    return query_index_pool.alloc();
}

void release_time_query_id(uint64_t id)
{
    query_index_pool.release(id);
}

void enable_profiling(bool enabled)
{
    _enabled = enabled;
}

bool is_enabled()
{
    return _enabled;
}

void new_frame_start_point()
{
    time_queries.push_back({});
}

void new_view_start_point()
{
    time_queries.back().time_queries.push_back({});
}

void register_frame_time_query(uint64_t id)
{
    time_queries.back().time_queries.back().push_back(id);
}

void set_render_requests(const RenderRequest& render_request)
{
    time_queries.back().render_request = render_request;
}

std::optional<GpuProfilingData> get_frame_profile(my::Instance* my)
{
    assert(my);

    if (!_enabled) return std::nullopt;

    GpuProfilingData res;

    auto oldest_profile = time_queries.front();

    for (auto& view : oldest_profile.time_queries)
    {
        std::vector<std::pair<std::string, float>> view_res;
        for (auto& query : view)
        {
            my::QueryTimeStatus query_status;
            auto query_res = my->retrieve_elapsed_time(query, query_status);
            switch (query_status)
            {
                case my::QueryTimeStatus::OK:
                {
                    // Merge with last sample if it has the same name (useful
                    // for render passes with multiple invocations)
                    if (!view_res.empty() && view_res.back().first == query_res.name)
                    {
                        view_res.back().second += query_res.elapsed_time;
                    }
                    else
                    {
                        view_res.push_back(std::make_pair(query_res.name, query_res.elapsed_time));
                    }
                    break;
                }
                case my::QueryTimeStatus::INVALID:
                {
                    time_queries.clear();
                    return std::nullopt;
                }
                case my::QueryTimeStatus::NOT_FOUND:
                {
                    time_queries.pop_front();
                    return std::nullopt;
                }
                case my::QueryTimeStatus::NOT_AVAILABLE:
                {
                    return std::nullopt;
                }
            }
        }

        res.pass_durations.push_back(std::move(view_res));
    }

    res.render_request = oldest_profile.render_request;

    for (auto& view : oldest_profile.time_queries)
    {
        for (auto& query : view)
        {
            my->delete_query(query);
            release_time_query_id(query);
        }
    }

    time_queries.pop_front();

    return {std::move(res)};
}

void clear(my::Instance* my)
{
    assert(my);

    while (!time_queries.empty())
    {
        auto oldest_profile = time_queries.front();

        for (auto& view : oldest_profile.time_queries)
        {
            for (auto& query : view)
            {
                my->delete_query(query);
                release_time_query_id(query);
            }
        }

        time_queries.pop_front();
    }
}
} // namespace profiling

void TimedRenderPass::execute(const my::RenderGraph::ExecutionContext& ctx)
{
    const auto& instance_info = ctx.instance->get_info();

    uint64_t query_id = 0;
    if (profiling::is_enabled() && instance_info.has_disjoint_time_query)
    {
        query_id = profiling::acquire_time_query_id();
        ctx.render->begin_time_query(query_id, _name.c_str());
    }

    execute_timed(ctx);

    if (profiling::is_enabled() && instance_info.has_disjoint_time_query)
    {
        ctx.render->end_time_query(query_id);
        profiling::register_frame_time_query(query_id);
    }
}

void initialize_ui_blending_params(my::ColorBlendState* blend)
{
    if (hrz::get_flag(hrz::Flag::EnabledDepthPeelingForUiElements))
    {
        // Each peel is opaque
        blend->enable = false;
    }
    else
    {
        // Premultiplied alpha over
        blend->enable = true;
        blend->color.op = my::ColorBlendState::Add;
        blend->color.src = my::ColorBlendState::One;
        blend->color.dst = my::ColorBlendState::OneMinusSrcAlpha;
        blend->alpha.op = my::ColorBlendState::Add;
        blend->alpha.src = my::ColorBlendState::Zero;
        blend->alpha.dst = my::ColorBlendState::One;
    }
}

double compute_device_pixel_size_in_meters(const hrz::CameraViewInfo& view_info)
{
    return 2.0 * std::tan(view_info.cam.fovy * 0.5) / view_info.viewport.size.y;
}

double compute_logical_pixels_to_meters(
    const lm::dvec3& ecef_pos,
    const hrz::CameraViewInfo& view_info)
{
    return compute_device_pixels_to_meters(ecef_pos, view_info)
        * view_info.viewport.device_pixel_ratio;
}

double compute_device_pixels_to_meters(
    const lm::dvec3& ecef_pos,
    const hrz::CameraViewInfo& view_info)
{
    double dist = lm::dot(view_info.cam.forward(), ecef_pos - view_info.cam.pos);
    return compute_device_pixel_size_in_meters(view_info) * dist;
}

double compute_logical_pixel_size_in_meters(const hrz::CameraViewInfo& view_info)
{
    return compute_device_pixel_size_in_meters(view_info) * view_info.viewport.device_pixel_ratio;
}

void VertexInputBuilder::add_input_stream_raw(
    int index,
    std::span<const std::byte> data,
    my::VertexFormat format,
    my::VertexRate rate)
{
    size_t type_size = my::vertex_size(format);

    // Align up to type size
    auto offset = hrz::align_up_any<size_t>(full_size, type_size);
    full_size = offset + data.size_bytes();

    to_upload.emplace_back(ToUpload{data.data(), offset, data.size_bytes()});
    vertex_input_streams.emplace_back(
        my::VertexInputStream{index, my::ResourceHandle::null(), format, offset, 0, rate});
}

std::pair<my::ResourceHandle, my::ResourceHandle> VertexInputBuilder::build(
    hrz::Render* render,
    monitoring::systems::Name system,
    uint64_t layer_id,
    std::initializer_list<std::pair<MetadataString, MetadataString>> metadata)
{
    my::BufferResource buffer_res(my::BufferResource::BufferType::Vertex);
    buffer_res.size = full_size;
    buffer_res.data = nullptr;
    buffer_res.usage = my::UsageHint::Static;

    my::ResourceHandle vbo = render->rc->alloc(&buffer_res, system, layer_id, metadata);
    for (const auto& upload : to_upload)
    {
        render->my->update_buffer(vbo, upload.offset, upload.size, upload.data);
    }

    for (auto& input : vertex_input_streams)
    {
        input.buffer = vbo;
    }

    my::VertexInputResource vi_res;
    vi_res.attribs = vertex_input_streams;

    my::ResourceHandle vertex_input = render->rc->alloc(&vi_res, system, layer_id, metadata);

    return {vbo, vertex_input};
}

void VertexInputBuilder::add_input_stream(
    int index,
    blobs::BlobHandle blob,
    my::VertexFormat format,
    my::VertexRate rate)
{
    auto data = blob.get_data();

    std::span<const std::byte> raw_data = data.as_bytes();
    add_input_stream_raw(index, raw_data, format, rate);

    pinned_blobs.push_back(std::move(data));
}

} // namespace render
} // namespace hrz
