#include "hrz/core/monitoring/monitoring.h"

#include "hrz/common/layers.h"
#include "hrz/common/monitoring_resource_sorter.h"
#include "hrz/common/profiling.h"
#include "hrz/common/ui_utils.h"
#include "hrz/core/global_flags.h"
#include "hrz/core/job_scheduler.h"
#include "hrz/core/monitoring/remote.h"
#include "hrz/core/render.h"
#include "hrz/fnd/format.h"
#include "hrz/fnd/log.h"
#include "hrz/fnd/mem.h"
#include "hrz/fnd/string_utils.h"
#include "hrz/fnd/time.h"
#include "hrz/monitoring/monitoring.h"

#include <mycelium/mycelium.h>

#if HRZ_LINUX || HRZ_EMSCRIPTEN
#    include <malloc.h>
#endif
#if HRZ_EMSCRIPTEN
#    include <emscripten/emscripten.h>
#endif

#include <algorithm>
#include <array>
#include <cassert>
#include <functional>

namespace
{
static constexpr size_t LOW_MEMORY = 50 * 1024 * 1024; // bytes

static constexpr mu_Color blue{122, 128, 239, 255};
static constexpr mu_Color cyan{83, 178, 181, 255};
static constexpr mu_Color dark_green{29, 71, 7, 255};
static constexpr mu_Color dark_red{80, 4, 4, 255};
static constexpr mu_Color dark_yellow{80, 67, 4, 255};
static constexpr mu_Color gray{80, 80, 80, 255};
static constexpr mu_Color green{97, 204, 38, 255};
static constexpr mu_Color light_green{97, 181, 83, 255};
static constexpr mu_Color light_red{241, 59, 59, 255};
static constexpr mu_Color pink{236, 123, 180, 255};
static constexpr mu_Color purple{172, 117, 239, 255};
static constexpr mu_Color red{229, 26, 26, 255};
static constexpr mu_Color white{255, 255, 255, 255};
static constexpr mu_Color yellow{229, 196, 26, 255};

using PbArena = google::protobuf::Arena;
} // namespace

namespace hrz
{
Monitoring::Monitoring(const hrz_proto::ViewerOptions& viewer_options) :
    _max_video_ram_size(viewer_options.max_video_memory_size())
{
    memset(_cpu_times, 0, sizeof(_cpu_times));

    _total_video_ram_usage_metric = metrics::MetricDesc("Allocated video memory (B)", false, {});

#if HRZ_LINUX || HRZ_EMSCRIPTEN
    _memory_usage.heap_size_metric = metrics::MetricDesc("Heap size (B)", false, {});
    _memory_usage.allocated_heap_space_metric =
        metrics::MetricDesc("Allocated heap space (B)", false, {});
    _memory_usage.free_heap_space_metric = metrics::MetricDesc("Free heap space (B)", false, {});
    _memory_usage.releasable_heap_space_metric =
        metrics::MetricDesc("Releasable heap space (B)", false, {});
#    if HRZ_EMSCRIPTEN
    _memory_usage.emscripten_heap_size_metric =
        metrics::MetricDesc("Emscripten heap size (B)", false, {});

    _max_wasm_memory_size = viewer_options.max_wasm_memory_size();
#    endif
#endif
}

void Monitoring::register_cpu_time(
    int64_t events_us,
    int64_t update_us,
    int64_t update_gpu_us,
    int64_t draw_us,
    int64_t swap_us,
    int64_t loop_us)
{
    CpuTime t{
        (float)events_us / 1000, (float)update_us / 1000, (float)update_gpu_us / 1000,
        (float)draw_us / 1000,   (float)swap_us / 1000,   (float)loop_us / 1000,
    };

    _cpu_times[_cpu_cursor] = t;
    _cpu_cursor = (_cpu_cursor + 1) % CpuEventCount;

    HRZ_SET_GAUGE("Frame total time (us)", t.total_ms() * 1000, {});
    HRZ_SET_GAUGE("Frame CPU time (us)", events_us + update_us + update_gpu_us + draw_us, {});

    HRZ_SET_GAUGE("Events time (us)", events_us, {});
    HRZ_SET_GAUGE("Update time (us)", update_us, {});
    HRZ_SET_GAUGE("Update GPU time (us)", update_gpu_us, {});
    HRZ_SET_GAUGE("Draw time (us)", draw_us, {});
    HRZ_SET_GAUGE("Swap time (us)", swap_us, {});
    HRZ_SET_GAUGE("Loop time (us)", loop_us, {});

    HRZ_OBSERVE_HISTOGRAM("Update time us", 0.0, 10'000.0, 20, update_us, {});
}

Monitoring::CpuTime Monitoring::get_cpu_time() const
{
    if (_cpu_average)
    {
        CpuTime avg{0, 0, 0, 0};

        for (const auto& t : _cpu_times)
        {
            avg.events_ms += t.events_ms;
            avg.update_ms += t.update_ms;
            avg.update_gpu_ms += t.update_gpu_ms;
            avg.draw_ms += t.draw_ms;
            avg.swap_ms += t.swap_ms;
            avg.loop_ms += t.loop_ms;
        }

        avg.events_ms /= CpuEventCount;
        avg.update_ms /= CpuEventCount;
        avg.update_gpu_ms /= CpuEventCount;
        avg.draw_ms /= CpuEventCount;
        avg.swap_ms /= CpuEventCount;
        avg.loop_ms /= CpuEventCount;

        return avg;
    }
    else
    {
        return _cpu_times[(_cpu_cursor + CpuEventCount - 1) % CpuEventCount];
    }
}

void Monitoring::register_gpu_times(hrz::render::profiling::GpuProfilingData&& gpu_profiling_data)
{
    if (gpu_profiling_data.render_request.is_render_requested(RenderRequest::Type::Visual))
    {
        if (!_freeze_gpu_profiling || !_gpu_profiling_data.has_value())
        {
            _gpu_profiling_data = {std::move(gpu_profiling_data)};

            auto& frame_times = _gpu_profiling_data.value().pass_durations;
            for (uint32_t view_index = 0; view_index < frame_times.size(); ++view_index)
            {
                auto& view_times = frame_times[view_index];
                for (auto& time_queries : view_times)
                {
                    if (!_gpu_pass_metrics.contains(time_queries.first))
                    {
                        metrics::MetricDesc metric(
                            "GPU pass duration (us)", false,
                            {{"pass", time_queries.first},
                             {"view", fmt::format("{}", view_index)}});
                        _gpu_pass_metrics[time_queries.first] = metric;
                    }
                    metrics::set_gauge(
                        &_gpu_pass_metrics.at(time_queries.first), time_queries.second);
                }
            }
        }
    }
}

void Monitoring::register_frame(const RenderRequest& render_request)
{
    _frames[_frame_cursor] = {render_request};
    _frame_cursor = (_frame_cursor + 1) % FrameCount;
}

void Monitoring::register_gpu_memory_info(const my::Instance::GpuMemoryInfo& info)
{
    _gpu_memory_info = {info};
}

void Monitoring::register_gpu_resource(
    my::ResourceHandle resource_handle,
    my::Resource::Type resource_type,
    monitoring::systems::Name system,
    uint64_t layer_id)
{
    auto handle = resource_handle.handle;

    auto it = _gpu_resources.find(handle);
    if (it == _gpu_resources.end())
    {
        _gpu_resources.insert({handle, {resource_type, 0, {system, layer_id}}});
        _gpu_resource_sorter.register_handle(handle);
    }
    else
    {
        HRZ_LOG_ERROR("GPU resource {} already registered", handle);
    }
}

void Monitoring::register_gpu_resource_size(my::ResourceHandle resource_handle, size_t size)
{
    auto handle = resource_handle.handle;

    auto it = _gpu_resources.find(handle);
    if (it != _gpu_resources.end())
    {
        _total_video_ram_usage -= it->second.size;
        it->second.size = size;
        _total_video_ram_usage += size;
        _gpu_resource_sorter.schedule_sort();

        HRZ_SET_GAUGE("VRAM usage (B)", _total_video_ram_usage, {});
    }
    else
    {
        HRZ_LOG_ERROR("GPU resource {} not registered", handle);
    }
}

void Monitoring::register_gpu_resource_metadata(
    my::ResourceHandle resource_handle,
    MetadataString key,
    MetadataString value)
{
    auto handle = resource_handle.handle;

    auto it = _gpu_resources.find(handle);
    if (it != _gpu_resources.end())
    {
        it->second.metadata.push_back({std::move(key), std::move(value)});
    }
    else
    {
        HRZ_LOG_ERROR("GPU resource {} not registered", handle);
    }
}

void Monitoring::update_gpu_resource_owner(
    my::ResourceHandle resource_handle,
    monitoring::systems::Name system,
    uint64_t layer_id)
{
    auto handle = resource_handle.handle;

    auto it = _gpu_resources.find(handle);
    if (it != _gpu_resources.end())
    {
        it->second.owner = {system, layer_id};
        _gpu_resource_sorter.schedule_sort();
    }
    else
    {
        HRZ_LOG_ERROR("GPU resource {} not registered", handle);
    }
}

void Monitoring::unregister_gpu_resource(my::ResourceHandle resource_handle)
{
    auto handle = resource_handle.handle;

    auto it = _gpu_resources.find(handle);
    if (it != _gpu_resources.end())
    {
        _total_video_ram_usage -= it->second.size;
        _gpu_resource_sorter.unregister_handle(it->first);
        _gpu_resources.erase(it);

        HRZ_SET_GAUGE("VRAM usage (B)", _total_video_ram_usage, {});
    }
    else
    {
        HRZ_LOG_ERROR("GPU resource {} not registered", handle);
    }
}

void Monitoring::draw_frametime_fps(mu_Context* ctx, float frametime)
{
    static const float fps_60 = 16.66667f;
    static const float fps_30 = 33.33333f;
    static const float fps_20 = 50.00000f;

    mu_Rect rect = mu_layout_next(ctx);

    mu_Rect fps60_rect{rect.x, rect.y, (int)(rect.w * fps_60 / fps_20), rect.h};

    mu_Rect fps30_rect{
        fps60_rect.x + fps60_rect.w, rect.y, (int)(rect.w * (fps_30 - fps_60) / fps_20), rect.h};

    mu_Rect fps20_rect{
        fps30_rect.x + fps30_rect.w, rect.y, rect.w - fps60_rect.w - fps30_rect.w, rect.h};

    mu_draw_rect(ctx, fps60_rect, dark_green);
    mu_draw_rect(ctx, fps30_rect, dark_yellow);
    mu_draw_rect(ctx, fps20_rect, dark_red);

    if (frametime > fps_20) frametime = fps_20;

    mu_Rect frametime_rect{rect.x, rect.y, (int)(rect.w * frametime / fps_20), rect.h};

    mu_Color frametime_color;
    if (frametime <= fps_60)
    {
        frametime_color = green;
    }
    else if (frametime <= fps_30)
    {
        frametime_color = yellow;
    }
    else
    {
        frametime_color = red;
    }

    mu_draw_rect(ctx, frametime_rect, frametime_color);
    mu_draw_box(ctx, rect, mu_Color{0, 0, 0, 255});

    rect = mu_layout_next(ctx);

    mu_draw_text(
        ctx, ctx->style->font, "60fps", 5, mu_Vec2{rect.x + rect.w / 3 - 15, rect.y}, white);

    mu_draw_text(
        ctx, ctx->style->font, "30fps", 5, mu_Vec2{rect.x + rect.w * 2 / 3 - 15, rect.y}, white);
}

void Monitoring::draw_frametime_categories(mu_Context* ctx, const CpuTime& t)
{
    static const mu_Color events_color = blue;
    static const mu_Color update_color = purple;
    static const mu_Color update_gpu_color = yellow;
    static const mu_Color draw_color = light_green;
    static const mu_Color swap_color = cyan;
    static const mu_Color loop_color = pink;

    mu_Rect rect = mu_layout_next(ctx);
    float total = t.total_ms();

    if (total == 0.0f)
    {
        // This happens on browsers because their clock are not precise
        // enough.
        return;
    }

    mu_Rect events_rect{rect.x, rect.y, (int)(rect.w * t.events_ms / total), rect.h};
    mu_Rect update_rect{
        events_rect.x + events_rect.w, rect.y, (int)(rect.w * t.update_ms / total), rect.h};
    mu_Rect update_gpu_rect{
        update_rect.x + update_rect.w, rect.y, (int)(rect.w * t.update_gpu_ms / total), rect.h};
    mu_Rect draw_rect{
        update_gpu_rect.x + update_gpu_rect.w, rect.y, (int)(rect.w * t.draw_ms / total), rect.h};
    mu_Rect swap_rect{draw_rect.x + draw_rect.w, rect.y, (int)(rect.w * t.swap_ms / total), rect.h};
    mu_Rect loop_rect{
        swap_rect.x + swap_rect.w, rect.y,
        rect.w - events_rect.w - update_rect.w - update_gpu_rect.w - draw_rect.w - swap_rect.w,
        rect.h};

    mu_draw_rect(ctx, events_rect, events_color);
    mu_draw_rect(ctx, update_rect, update_color);
    mu_draw_rect(ctx, update_gpu_rect, update_gpu_color);
    mu_draw_rect(ctx, draw_rect, draw_color);
    mu_draw_rect(ctx, swap_rect, swap_color);
    mu_draw_rect(ctx, loop_rect, loop_color);
    mu_draw_box(ctx, rect, mu_Color{0, 0, 0, 255});

    char buffer[64];

    auto display_time = [&](const char* category, float time)
    {
        auto res = fmt::format_to(buffer, "{}: {:.3f} ms", category, time);
        hrz::ui::add_tooltip(ctx, &_tooltip_ctx, {buffer, (size_t)std::distance(buffer, res.out)});
    };

    if (mu_mouse_over(ctx, events_rect))
    {
        display_time("Events", t.events_ms);
    }
    else if (mu_mouse_over(ctx, update_rect))
    {
        display_time("Update", t.update_ms);
    }
    else if (mu_mouse_over(ctx, update_gpu_rect))
    {
        display_time("Update (GPU)", t.update_gpu_ms);
    }
    else if (mu_mouse_over(ctx, draw_rect))
    {
        display_time("Draw", t.draw_ms);
    }
    else if (mu_mouse_over(ctx, swap_rect))
    {
        display_time("Swap", t.swap_ms);
    }
    else if (mu_mouse_over(ctx, loop_rect))
    {
        display_time("Loop", t.loop_ms);
    }

    if (mu_begin_treenode(ctx, "Details"))
    {
        static int layout[] = {140, 70, -1};
        mu_layout_row(ctx, 3, layout, 0);

        fmt::memory_buffer buffer;

        auto print_time = [&](const char* name, float time, const mu_Color& color)
        {
            mu_text(ctx, name);
            mu_text(ctx, hrz::format_to_buffer(buffer, "{:.3f} ms", time));
            hrz::ui::draw_progress_bar(ctx, time / t.total_ms(), color, gray, &_tooltip_ctx);
        };

        print_time("Events", t.events_ms, events_color);
        print_time("Update", t.update_ms, update_color);
        print_time("Update (GPU)", t.update_gpu_ms, update_gpu_color);
        print_time("Draw", t.draw_ms, draw_color);
        print_time("Swap", t.swap_ms, swap_color);
        print_time("Loop", t.loop_ms, loop_color);

        mu_end_treenode(ctx);
    }
}

namespace monitoring
{
void draw_remote_connection(RemoteMonitoring*, JobScheduler*, mu_Context* ctx);
}

void Monitoring::draw_gpu_performance(mu_Context* ctx)
{
    if (mu_header(ctx, "GPU performance"))
    {
        static int layout[] = {150, -1};
        mu_layout_row(ctx, 2, layout, 0);

        int is_profiling_enabled = hrz::render::profiling::is_enabled();
        mu_checkbox(ctx, "Enable GPU profiling", &is_profiling_enabled);
        hrz::render::profiling::enable_profiling((bool)is_profiling_enabled);

        int freeze = _freeze_gpu_profiling ? 1 : 0;
        mu_checkbox(ctx, "Freeze", &freeze);
        _freeze_gpu_profiling = freeze == 0 ? false : true;

        if (_gpu_profiling_data.has_value())
        {
            const auto& frame_times = _gpu_profiling_data.value().pass_durations;

            static int layout[] = {120, -1};
            mu_layout_row(ctx, 2, layout, 0);

            for (uint32_t view_index = 0; view_index < frame_times.size(); ++view_index)
            {
                auto& view_times = frame_times[view_index];
                double total = 0;
                for (auto& time_queries : view_times)
                {
                    total += time_queries.second;
                }

                if (total < std::numeric_limits<double>::epsilon()) continue;

                std::string label = fmt::format("View {} : {:.3f} us", view_index, total);
                std::string unique_label = fmt::format("View {}", view_index);

                if (mu_begin_treenode_ex(ctx, unique_label.c_str(), label.c_str(), 0))
                {
                    static int layout[] = {140, 70, -1};
                    mu_layout_row(ctx, 3, layout, 0);

                    fmt::memory_buffer buffer;

                    for (auto& time_queries : view_times)
                    {
                        mu_text(ctx, hrz::format_to_buffer(buffer, "{}", time_queries.first));
                        mu_text(
                            ctx, hrz::format_to_buffer(buffer, "{:.3f} us", time_queries.second));

                        hrz::ui::draw_progress_bar(
                            ctx, time_queries.second / total, purple, gray, &_tooltip_ctx);
                    }
                    mu_end_treenode(ctx);
                }
            }
        }
        else
        {
            mu_text(ctx, ""); // Fill the row
        }

        mu_text(ctx, ""); // A bit of vertical spacing
    }
}

void Monitoring::draw_frames(mu_Context* ctx)
{
    if (mu_header(ctx, "Frames"))
    {
        auto draw_section_text = [&](const char* text)
        {
            static int layout[] = {-1};
            mu_layout_row(ctx, 1, layout, 0);

            mu_text(ctx, text);
        };

        auto draw_right_aligned_text = [&](const char* text)
        {
            mu_Rect rect = mu_layout_next(ctx);
            mu_draw_control_text(ctx, text, rect, MU_COLOR_TEXT, MU_OPT_ALIGNRIGHT);
        };

        draw_section_text("Render types:");

        static int layout[] = {80, -1};
        mu_layout_row(ctx, 2, layout, 0);

        // Make sure this has the same size as the amount of values in RenderRequest::Type
        mu_Rect type_rects[3];

        draw_right_aligned_text("Visual");
        type_rects[0] = mu_layout_next(ctx);

        draw_right_aligned_text("Picking");
        type_rects[1] = mu_layout_next(ctx);

        draw_right_aligned_text("Feedback");
        type_rects[2] = mu_layout_next(ctx);

        draw_section_text("Render causes:");

        mu_layout_row(ctx, 2, layout, 0);

        // Make sure this has the same size as the amount of values in RenderRequest::VisualCause
        mu_Rect cause_rects[5];

        draw_right_aligned_text("Scene");
        cause_rects[0] = mu_layout_next(ctx);

        draw_right_aligned_text("Animation");
        cause_rects[1] = mu_layout_next(ctx);

        draw_right_aligned_text("Flat overlay");
        cause_rects[2] = mu_layout_next(ctx);

        draw_right_aligned_text("Symbol culling");
        cause_rects[3] = mu_layout_next(ctx);

        draw_right_aligned_text("Vector tiles");
        cause_rects[4] = mu_layout_next(ctx);

        auto get_color = [&](int flags)
        {
            static constexpr mu_Color colors[] = {
                gray,        // 000
                light_red,   // 001
                blue,        // 010
                purple,      // 011
                light_green, // 100
                yellow,      // 101
                cyan,        // 110
                white        // 111
            };
            return colors[flags % HRZ_ARRAY_COUNT(colors)];
        };

        int frame_count = FrameCount;
        int col_count = std::min(type_rects[0].w - 2, frame_count);
        int offset = FrameCount - col_count;

        // Frames gauges
        for (int col = 0; col < col_count; ++col)
        {
            int index = (_frame_cursor + offset + col) % FrameCount;
            const auto& frame = _frames[index];

            auto types = frame.render_request.get_requested_render_types();
            auto color = get_color(types);

            for (unsigned int i = 0; i < HRZ_ARRAY_COUNT(type_rects); ++i)
            {
                if (types & (1 << i))
                {
                    mu_Rect frame_rect{
                        type_rects[i].x + col + 1, type_rects[i].y, 1, type_rects[i].h};
                    mu_draw_rect(ctx, frame_rect, color);
                }
            }

            auto causes = frame.render_request.get_visual_render_causes();

            for (unsigned int i = 0; i < HRZ_ARRAY_COUNT(cause_rects); ++i)
            {
                if (causes & (1 << i))
                {
                    mu_Rect frame_rect{
                        cause_rects[i].x + col + 1, cause_rects[i].y, 1, cause_rects[i].h};
                    mu_draw_rect(ctx, frame_rect, color);
                }
            }
        }

        // Gauge outlines
        for (auto rect : type_rects)
        {
            rect.w = col_count + 2;
            mu_draw_box(ctx, rect, mu_Color{0, 0, 0, 255});
        }
        for (auto rect : cause_rects)
        {
            rect.w = col_count + 2;
            mu_draw_box(ctx, rect, mu_Color{0, 0, 0, 255});
        }

        mu_text(ctx, ""); // A bit of vertical spacing
    }
}

void Monitoring::draw_main_memory_usage(mu_Context* ctx)
{
#if HRZ_LINUX || HRZ_EMSCRIPTEN
    // This function relies on `mallinfo()`, which is an old
    // function that has severe limitations. (Among others,
    // it does not handle heaps sizes greater than 4 GB.)
    // It does not exist on Windows.
    //
    // @Todo Implement a better heap analysis, using functions
    // such as `malloc_info()` on Linux, and `HeapWalk()` on
    // Windows.

    if (mu_header(ctx, "Main memory usage"))
    {
        fmt::memory_buffer buffer;

#    if HRZ_EMSCRIPTEN
        {
            static int layout[] = {130, -1};
            mu_layout_row(ctx, 2, layout, 0);

            {
                mu_text(ctx, "WASM max memory size");
                mu_text(ctx, bytes_to_string(_max_wasm_memory_size, buffer));
            }
            {
                mu_text(ctx, "Emscripten heap size");
                mu_text(ctx, bytes_to_string(_memory_usage.emscripten_heap_size, buffer));
            }
        }
#    endif

        static int layout[] = {-1};
        mu_layout_row(ctx, 1, layout, 0);

        mu_checkbox(ctx, "Compute memory usage", &_compute_memory_usage);

        if (_compute_memory_usage != 0)
        {
            static int layout[] = {130, -1};
            mu_layout_row(ctx, 2, layout, 0);

            {
                mu_text(ctx, "Heap size");
                mu_text(ctx, bytes_to_string(_memory_usage.heap_size, buffer));
            }
            {
                mu_text(ctx, "Allocated");
                mu_text(ctx, bytes_to_string(_memory_usage.allocated_heap_space, buffer));
            }
            {
                mu_text(ctx, "Free");
                mu_text(ctx, bytes_to_string(_memory_usage.free_heap_space, buffer));
            }
            {
                mu_text(ctx, "Releasable");
                mu_text(ctx, bytes_to_string(_memory_usage.releasable_heap_space, buffer));
            }
        }
    }
#endif
}

namespace
{
const char* to_string(my::Resource::Type type)
{
    switch (type)
    {
        case my::Resource::VertexBuffer: return "Vertex buffer";
        case my::Resource::IndexBuffer: return "Index buffer";
        case my::Resource::UniformBuffer: return "Uniform buffer";
        case my::Resource::TextureDownloadBuffer: return "Texture download buffer";
        case my::Resource::Shader: return "Shader";
        case my::Resource::ShaderDerivative: return "ShaderDerivative";
        case my::Resource::VertexInput: return "Vertex input";
        case my::Resource::Texture: return "Texture";
        case my::Resource::Renderbuffer: return "Renderbuffer";
        case my::Resource::Sampler: return "Sampler";
        case my::Resource::Framebuffer: return "Framebuffer";
        default: assert(false && "Unhandled case"); return "";
    }
}

struct ResourceTypeCounter
{
    void add_resource(my::Resource::Type type)
    {
        auto index = (size_t)type;
        if (index == 0 || index >= counts.size())
        {
            assert(false && "Unhandled resource type");
            return;
        }

        counts[index] += 1;
    }

    void write_string_to_buffer(fmt::memory_buffer& buffer)
    {
        buffer.clear();
        for (size_t i = 1; i < counts.size(); ++i)
        {
            size_t count = counts[i];
            if (count == 0) continue;

            fmt::format_to(
                std::back_inserter(buffer), "{}: {}\n", to_string((my::Resource::Type)i), count);
        }
    }

    std::array<size_t, (size_t)my::Resource::Framebuffer + 1> counts = {0};
};

using TreenodeId = std::array<uint64_t, 3>;

} // namespace

// First: first sort criterion
// Second: second sort criterion
void Monitoring::draw_resource_tree(
    mu_Context* ctx,
    uint64_t tree_id,
    std::span<const uint64_t> gpu_resources,
    const std::function<uint64_t(const GpuResourceInfo&)>& get_first_value,
    const std::function<const char*(uint64_t)>& print_first_value,
    const mu_Color& first_bar_color,
    const std::function<uint64_t(const GpuResourceInfo&)>& get_second_value,
    const std::function<const char*(uint64_t)>& print_second_value,
    const mu_Color& second_bar_color)
{
    fmt::memory_buffer buffer;
    size_t next_first_start = 0;

    auto draw_resource_metadata_tooltip = [this, &buffer, ctx](const GpuResourceInfo& resource)
    {
        buffer.clear();
        for (const auto& it : resource.metadata)
        {
            if (!it.second.empty())
            {
                fmt::format_to(
                    std::back_inserter(buffer), "{}: {}\n", it.first.data(), it.second.data());
            }
            else
            {
                fmt::format_to(std::back_inserter(buffer), "{}\n", it.first.data());
            }
        }
        hrz::ui::add_tooltip(ctx, &_tooltip_ctx, {buffer.data(), buffer.size()});
    };

    while (next_first_start < gpu_resources.size())
    {
        auto current_first_start = next_first_start;
        auto current_first = get_first_value(_gpu_resources.at(gpu_resources[current_first_start]));
        size_t current_first_res_size = 0;
        ResourceTypeCounter current_first_resource_counter;

        size_t i = current_first_start;
        while (i < gpu_resources.size())
        {
            const auto& resource = _gpu_resources.at(gpu_resources[i]);

            if (get_first_value(resource) != current_first) break;

            current_first_res_size += resource.size;
            current_first_resource_counter.add_resource(resource.type);

            i += 1;
        }

        next_first_start = i;

        TreenodeId system_row_id = {tree_id << 1, current_first, 0};
        bool first_expanded = hrz::ui::begin_layout_treenode(
            ctx, &system_row_id, sizeof(system_row_id), _expanded_nodes);
        static int first_layout[] = {100, 60, -1};
        mu_layout_row(ctx, 3, first_layout, 0);
        mu_text(ctx, print_first_value(current_first));
        mu_Rect tooltip_rect = mu_layout_next(ctx);
        mu_layout_set_next(ctx, tooltip_rect, 0);
        if (mu_mouse_over(ctx, tooltip_rect))
        {
            current_first_resource_counter.write_string_to_buffer(buffer);
            hrz::ui::add_tooltip(ctx, &_tooltip_ctx, {buffer.data(), buffer.size()});
        }
        mu_text(ctx, bytes_to_string(current_first_res_size, buffer));
        hrz::ui::draw_progress_bar(
            ctx, (float)current_first_res_size / (float)_total_video_ram_usage, first_bar_color,
            gray, &_tooltip_ctx);
        hrz::ui::end_layout_treenode_header(ctx, first_expanded);
        if (first_expanded)
        {
            size_t next_second_start = current_first_start;

            while (next_second_start < next_first_start)
            {
                auto current_second_start = next_second_start;
                auto current_second =
                    get_second_value(_gpu_resources.at(gpu_resources[current_second_start]));
                size_t current_second_res_size = 0;
                ResourceTypeCounter current_second_resource_counter;

                size_t j = current_second_start;
                while (j < next_first_start)
                {
                    const auto& resource = _gpu_resources.at(gpu_resources[j]);

                    if (get_second_value(resource) != current_second) break;

                    current_second_res_size += resource.size;
                    current_second_resource_counter.add_resource(resource.type);

                    j += 1;
                }

                next_second_start = j;

                TreenodeId layer_row_id = {(tree_id << 1) + 1, current_first, current_second};
                bool second_expanded = hrz::ui::begin_layout_treenode(
                    ctx, &layer_row_id, sizeof(layer_row_id), _expanded_nodes);
                static int second_layout[] = {100, 60, -1};
                mu_layout_row(ctx, 3, second_layout, 0);
                mu_text(ctx, print_second_value(current_second));
                mu_Rect tooltip_rect = mu_layout_next(ctx);
                mu_layout_set_next(ctx, tooltip_rect, 0);
                if (mu_mouse_over(ctx, tooltip_rect))
                {
                    current_second_resource_counter.write_string_to_buffer(buffer);
                    hrz::ui::add_tooltip(ctx, &_tooltip_ctx, {buffer.data(), buffer.size()});
                }
                mu_text(ctx, bytes_to_string(current_second_res_size, buffer));
                hrz::ui::draw_progress_bar(
                    ctx, (float)current_second_res_size / (float)current_first_res_size,
                    second_bar_color, gray, &_tooltip_ctx);
                hrz::ui::end_layout_treenode_header(ctx, second_expanded);
                if (second_expanded)
                {
                    static int layout[] = {120, 140, -1};
                    mu_layout_row(ctx, 3, layout, 0);

                    for (size_t k = current_second_start; k < next_second_start; ++k)
                    {
                        auto resource_handle = gpu_resources[k];
                        const auto& resource = _gpu_resources.at(resource_handle);

                        if (resource.size == 0) continue;

                        if (!resource.metadata.empty())
                        {
                            mu_Rect tooltip_rect = mu_layout_next(ctx);
                            mu_layout_set_next(ctx, tooltip_rect, 0);
                            if (mu_mouse_over(ctx, tooltip_rect))
                            {
                                draw_resource_metadata_tooltip(resource);
                            }
                        }

                        mu_text(ctx, hrz::format_to_buffer(buffer, "{}", resource_handle));
                        mu_text(ctx, to_string(resource.type));
                        mu_text(ctx, bytes_to_string(resource.size, buffer));
                    }
                }
                hrz::ui::end_layout_treenode(ctx, second_expanded);
            }
        }
        hrz::ui::end_layout_treenode(ctx, first_expanded);
    }
}

void Monitoring::draw_gpu_memory_usage(mu_Context* ctx, const LayersInfo* layers_info)
{
    HRZ_SCOPED_SAMPLE("monitoring draw gpu memory usage");

    if (mu_header(ctx, "Video memory usage"))
    {
        if (_gpu_memory_info.has_value())
        {
            mu_text(ctx, "Driver info");

            static int layout[] = {20, 106, -1};
            mu_layout_row(ctx, 3, layout, 0);

            fmt::memory_buffer buffer;

            {
                mu_layout_next(ctx);
                mu_text(ctx, "Total size");
                mu_text(ctx, bytes_to_string(_gpu_memory_info->total_device_memory, buffer));
            }
            {
                mu_layout_next(ctx);
                mu_text(ctx, "Usable");
                mu_text(ctx, bytes_to_string(_gpu_memory_info->usable_memory, buffer));
            }
            {
                mu_layout_next(ctx);
                mu_text(ctx, "Free");
                mu_text(ctx, bytes_to_string(_gpu_memory_info->free_memory, buffer));
            }
        }

        _gpu_resource_sorter.work(
            [this](const uint64_t& handle) -> const GpuResourceInfo&
            { return _gpu_resources.at(handle); },
            [](const GpuResourceInfo& info) -> const monitoring::ResourceOwner&
            { return info.owner; },
            [](const GpuResourceInfo& a, const GpuResourceInfo& b) { return a.size > b.size; });

        fmt::memory_buffer buffer;

        {
            static int layout[] = {130, -1};
            mu_layout_row(ctx, 2, layout, 0);

            if (_max_video_ram_size > 0)
            {
                mu_text(ctx, "Maximum size");
                mu_text(ctx, bytes_to_string(_max_video_ram_size, buffer));
            }

            mu_text(ctx, "Allocated");
            if (_max_video_ram_size > 0)
            {
                buffer.clear();
                fmt::format_to(
                    std::back_inserter(buffer), "{} ({:.2f}%)",
                    bytes_to_string(_total_video_ram_usage).data(),
                    100.0f * (float)_total_video_ram_usage / _max_video_ram_size);
                buffer.push_back(0);
                mu_text(ctx, buffer.data());
            }
            else
            {
                mu_text(ctx, bytes_to_string(_total_video_ram_usage, buffer));
            }
        }

        auto get_system = [&](const GpuResourceInfo& info) { return (uint64_t)info.owner.system; };
        auto system_to_string = [&](uint64_t system_as_uint)
        { return monitoring::systems::to_string((monitoring::systems::Name)system_as_uint); };
        auto get_layer_id = [&](const GpuResourceInfo& info) { return info.owner.layer_id; };
        auto layer_id_to_string = [&](uint64_t layer_id)
        {
            if (layer_id == monitoring::NoLayer) return "No layer";
            auto it = layers_info->layers.find(layer_id);
            return (it != layers_info->layers.end())
                ? it->second.name.c_str()
                : hrz::format_to_buffer(buffer, "Layer {}", layer_id);
        };

        if (mu_begin_treenode(ctx, "By system"))
        {
            draw_resource_tree(
                ctx, 0, _gpu_resource_sorter.get_handles_sorted_by_system(), get_system,
                system_to_string, purple, get_layer_id, layer_id_to_string, cyan);
            mu_end_treenode(ctx);
        }

        if (mu_begin_treenode(ctx, "By layer"))
        {
            draw_resource_tree(
                ctx, 1, _gpu_resource_sorter.get_handles_sorted_by_layer(), get_layer_id,
                layer_id_to_string, cyan, get_system, system_to_string, purple);
            mu_end_treenode(ctx);
        }
    }
}

struct GpuResourceBucketKey
{
    my::Resource::Type type;
    monitoring::ResourceOwner owner;

    bool operator==(const GpuResourceBucketKey& other) const
    {
        return type == other.type && owner.system == other.owner.system
            && owner.layer_id == other.owner.layer_id;
    }
};

template<typename H>
static H AbslHashValue(H h, const GpuResourceBucketKey& res)
{
    return H::combine(std::move(h), res.type, res.owner.system, res.owner.layer_id);
}

void Monitoring::dump_gpu_resources(
    const LayersInfo* layers_info,
    hrz_monitoring::MessageBuffer* mb) const
{
    HRZ_SCOPED_SAMPLE("monitoring dump gpu resources");

    PbArena arena;
    auto* msgs = PbArena::Create<hrz_monitoring::MonitoringMessages>(&arena);
    auto* msg = msgs->add_messages();
    auto* snapshot = msg->mutable_gpu_resources();
    snapshot->set_timestamp(hrz::now_frame_us_s64());

    hrz::flat_hash_map<GpuResourceBucketKey, hrz_monitoring::GpuResourcesBucket*> buckets_index;

    for (const auto& res_it : _gpu_resources)
    {
        const auto& res = res_it.second;
        GpuResourceBucketKey key{res.type, res.owner};

        hrz_monitoring::GpuResourcesBucket* bucket = nullptr;

        auto it = buckets_index.find(key);
        if (it == buckets_index.end())
        {
            bucket = snapshot->add_buckets();
            buckets_index.insert(std::make_pair(key, bucket));

            auto it = layers_info->layers.find(res.owner.layer_id);
            if (it == layers_info->layers.end())
            {
                bucket->set_layer("(no layer)");
            }
            else
            {
                bucket->set_layer(it->second.name);
            }

            bucket->set_system(monitoring::systems::to_string(res.owner.system));
            bucket->set_type(to_string(res.type));
        }
        else
        {
            bucket = it->second;
        }

        auto* pb_res = bucket->add_resources();
        pb_res->set_size(res.size);

        for (const auto& data : res.metadata)
        {
            auto* pb_meta = pb_res->add_metadata();
            pb_meta->set_name(data.first.data());
            pb_meta->set_value(data.second.data());
        }
    }

    hrz_monitoring::push_messages(mb, *msgs);
}

void Monitoring::add_info(std::string_view name, std::string_view value)
{
    _info.push_back(std::make_pair(std::string(name), std::string(value)));
}

void Monitoring::work()
{
    HRZ_SCOPED_SAMPLE("Monitoring work");
    metrics::set_gauge(&_total_video_ram_usage_metric, _total_video_ram_usage);

#if HRZ_LINUX || HRZ_EMSCRIPTEN
    // This function relies on `mallinfo()`/`mallinfo2()`,
    // which is an old function that has severe limitations.
    // The `mallinfo2()` variant can handle heap sizes
    // greater than 2 GiB, but it is the only improvement.
    // `mallinfo()` does not exist on Windows.
    //
    // @Todo Implement a better heap analysis, using functions
    // such as `malloc_info()` on Linux, and `HeapWalk()` on
    // Windows.

    if (_compute_memory_usage != 0)
    {
        HRZ_SCOPED_SAMPLE("mallinfo");
#    if defined(__GLIBC__) && defined(__GLIBC_MINOR__) \
        && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33))
        auto info = mallinfo2();
#    else
        auto info = mallinfo();
#    endif
        _memory_usage.heap_size = info.arena;
        _memory_usage.allocated_heap_space = info.uordblks;
        _memory_usage.free_heap_space = info.fordblks;
        _memory_usage.releasable_heap_space = info.keepcost;

        metrics::set_gauge(&_memory_usage.heap_size_metric, _memory_usage.heap_size);
        metrics::set_gauge(
            &_memory_usage.allocated_heap_space_metric, _memory_usage.allocated_heap_space);
        metrics::set_gauge(&_memory_usage.free_heap_space_metric, _memory_usage.free_heap_space);
        metrics::set_gauge(
            &_memory_usage.releasable_heap_space_metric, _memory_usage.releasable_heap_space);
    }

#    if HRZ_EMSCRIPTEN
    _memory_usage.emscripten_heap_size = (size_t)EM_ASM_DOUBLE({ return HEAP8.length; });
    metrics::set_gauge(
        &_memory_usage.emscripten_heap_size_metric, _memory_usage.emscripten_heap_size);

    if (!_has_warned_about_low_memory
        && _max_wasm_memory_size - _memory_usage.emscripten_heap_size < LOW_MEMORY)
    {
        fmt::memory_buffer buffer;
        bytes_to_string(LOW_MEMORY, buffer);
        HRZ_LOG_WARNING(
            "Less than {} is available for heap memory growth. The application will crash if heap "
            "memory tries to grow beyond maximum WASM memory size!",
            buffer.data());
        _has_warned_about_low_memory = true;
    }
#    endif
#endif
}

void Monitoring::draw_ui(
    my::Instance* my,
    RemoteMonitoring* remote,
    JobScheduler* job_scheduler,
    const LayersInfo* layers_info,
    mu_Context* ctx,
    const char* window_name)
{
    if (mu_begin_window_ex(ctx, window_name, mu_rect(300, 200, 420, 370), MU_OPT_CLOSED))
    {
        // These variables are declared here so that they have different addresses.
        // When declared only in the block they are used, multiple blocks can reuse
        // the same adresses. But MicroUI relies on addresses to be different in
        // order to work, as it compares them.
        int force_render_enabled;
        int force_overlay_render;
        int profiling_enabled;
        int metrics_enabled;

        static int layout = -1;
        mu_layout_row(ctx, 1, &layout, 0);

        if (mu_header(ctx, "Information"))
        {
            static int layout[] = {120, -1};
            mu_layout_row(ctx, 2, layout, 0);

            for (const auto& info : _info)
            {
                mu_text(ctx, info.first.c_str());
                mu_text(ctx, info.second.c_str());
            }

            mu_text(ctx, ""); // A bit of vertical spacing
        }

        if (mu_header(ctx, "Force render"))
        {
            static int layout[] = {-1};
            mu_layout_row(ctx, 1, layout, 0);

            force_render_enabled = hrz::get_flag(hrz::Flag::ForceRender);
            if (mu_checkbox(ctx, "Force render", &force_render_enabled))
            {
                hrz::set_flag(hrz::Flag::ForceRender, force_render_enabled);
            }

            force_overlay_render = hrz::get_flag(hrz::Flag::ForceFlatOverlayRender);
            if (mu_checkbox(ctx, "Force flat overlay render", &force_overlay_render))
            {
                hrz::set_flag(hrz::Flag::ForceFlatOverlayRender, force_overlay_render);
            }

            mu_text(ctx, ""); // A bit of vertical spacing
        }

        if (mu_header(ctx, "Flags"))
        {
            static int layout[] = {34, -1};
            mu_layout_row(ctx, 2, layout, 0);

            static mu_Color colors[2] = {{255, 127, 127, 255}, {127, 255, 127, 255}};

            hrz::iterate_flags(
                [&](const char* name, bool value)
                {
                    mu_text_color(ctx, value ? "ON" : "OFF", colors[(int)value]);
                    mu_text_color(ctx, name, colors[(int)value]);
                });

            mu_text(ctx, ""); // A bit of vertical spacing
        }

        if (mu_header(ctx, "Texture formats"))
        {
            static int layout[] = {60, 80, -1};
            mu_layout_row(ctx, 3, layout, 0);

            mu_text(ctx, "");
            mu_text(ctx, "Linear");
            mu_text(ctx, "sRGB");

            static mu_Color colors[2] = {{255, 127, 127, 255}, {127, 255, 127, 255}};

            auto draw_availability = [&](bool available) {
                mu_text_color(
                    ctx, available ? "Available" : "Unavailable", colors[available ? 1 : 0]);
            };

            auto draw_format = [&](const char* name, bool rgb_available, bool srgb_available)
            {
                mu_text(ctx, name);
                draw_availability(rgb_available);
                draw_availability(srgb_available);
            };

            draw_format(
                "BC1~3", my->get_info().has_bc1_bc2_bc3_texture_compression,
                my->get_info().has_bc1_bc2_bc3_srgb_texture_compression);
            draw_format(
                "BC7", my->get_info().has_bc7_texture_compression,
                my->get_info().has_bc7_srgb_texture_compression);
            draw_format(
                "ETC1", my->get_info().has_etc1_texture_compression,
                my->get_info().has_etc1_srgb_texture_compression);
            draw_format(
                "ETC2", my->get_info().has_etc2_texture_compression,
                my->get_info().has_etc2_srgb_texture_compression);
            draw_format(
                "ASTC", my->get_info().has_astc_texture_compression,
                my->get_info().has_astc_srgb_texture_compression);
            draw_format(
                "PVRTC", my->get_info().has_pvrtc_texture_compression,
                my->get_info().has_pvrtc_srgb_texture_compression);
            draw_format(
                "PVRTC2", my->get_info().has_pvrtc2_texture_compression,
                my->get_info().has_pvrtc2_srgb_texture_compression);

            mu_text(ctx, ""); // A bit of vertical spacing
        }

        if (mu_header(ctx, "Remote monitoring"))
        {
            if (remote)
            {
                monitoring::draw_remote_connection(remote, job_scheduler, ctx);
            }

            profiling_enabled = (int)hrz::profiling::is_profiling_enabled();
            if (mu_checkbox(ctx, "Profiling", &profiling_enabled))
            {
                hrz::profiling::set_profiling_enabled((bool)profiling_enabled);
            }

            metrics_enabled = (int)hrz::metrics::are_metrics_registries_enabled();
            if (mu_checkbox(ctx, "Metrics", &metrics_enabled))
            {
                hrz::metrics::set_metrics_registries_enabled((bool)metrics_enabled);
            }

            mu_text(ctx, "");
        }

        if (mu_header(ctx, "Performance"))
        {
            mu_checkbox(ctx, "Enable rolling average (64 frames)", &_cpu_average);

            CpuTime t = get_cpu_time();
            float total = t.total_ms();
            fmt::memory_buffer buffer;
            mu_text(
                ctx,
                hrz::format_to_buffer(
                    buffer, "Total frametime: {:.3f} ms ({:.1f} fps)", total, 1000.0f / total));

            draw_frametime_fps(ctx, total);
            draw_frametime_categories(ctx, t);

            mu_text(ctx, ""); // A bit of vertical spacing
        }

        if (my->get_info().has_disjoint_time_query)
        {
            draw_gpu_performance(ctx);
        }

        if (mu_header(ctx, "Shaders"))
        {
            fmt::memory_buffer buffer;
            auto shaders_info = my->get_shaders_info();

#if HRZ_EMSCRIPTEN
            static mu_Color colors[2] = {{255, 127, 127, 255}, {127, 255, 127, 255}};
            bool has_ext = my->get_info().has_parallel_shader_compile;
            mu_text_color(
                ctx,
                hrz::format_to_buffer(
                    buffer, "KHR_parallel_shader_compile {}", has_ext ? "in use" : "unavailable"),
                colors[(int)has_ext]);
#endif
            static int layout[] = {130, -1};
            mu_layout_row(ctx, 2, layout, 0);

            mu_text(ctx, "Initial shaders");
            mu_text(
                ctx,
                hrz::format_to_buffer(
                    buffer, "{}/{} linked", shaders_info.to_link_initial_done,
                    shaders_info.to_link_initial));
            mu_text(ctx, "Root shaders");
            mu_text(
                ctx,
                hrz::format_to_buffer(
                    buffer, "{}/{} linked", shaders_info.root_shaders_linked,
                    shaders_info.root_shaders));
            mu_text(ctx, "Derivatives");
            mu_text(ctx, hrz::format_to_buffer(buffer, "{}", shaders_info.shader_derivatives));
        }

        draw_frames(ctx);
        draw_main_memory_usage(ctx);
        draw_gpu_memory_usage(ctx, layers_info);

        hrz::ui::draw_tooltips(ctx, &_tooltip_ctx);
        _tooltip_ctx.tooltips.clear();

        mu_end_window(ctx);
    }
}

void monitoring_dev_ui(
    Monitoring* m,
    RemoteMonitoring* rm,
    JobScheduler* js,
    const LayersInfo* layers_info,
    my::Instance* my,
    mu_Context* ctx,
    const char* window_name)
{
    m->draw_ui(my, rm, js, layers_info, ctx, window_name);
}

} // namespace hrz
