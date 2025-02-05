#pragma once

#include "hrz_core_render.h"
#include "hrz_core_render_request.h"
#include "hrz_core_scene.h"
#include "monitoring/hrz_core_monitoring_gpu.h"

#include <hrz_common_layers.h>
#include <hrz_common_metrics.h>
#include <hrz_common_monitoring_defs.h>
#include <hrz_common_monitoring_resource_sorter.h>
#include <hrz_common_ui_utils.h>
#include <hrz_fnd_defines.h>
#include <hrz_fnd_flat_hash_map.h>
#include <hrz_fnd_flat_hash_set.h>
#include <hrz_fnd_hash.h>
#include <hrz_fnd_inlined_vector.h>
#include <hrz_protocol_all.h>

#include <fmt/format.h>
#include <mycelium.h>

#include <optional>
#include <utility>
#include <vector>

extern "C"
{
#include <microui/microui.h>
}

namespace hrz_monitoring
{
struct MessageBuffer;
}

namespace hrz
{
struct RemoteMonitoring;

class Monitoring : public monitoring::GpuResourceMonitoring
{
private:
    static const int CpuEventCount = 64;

    struct CpuTime
    {
        float events_ms;
        float update_ms;
        float update_gpu_ms;
        float draw_ms;
        float swap_ms;
        float loop_ms;

        float total_ms() const
        {
            return events_ms + update_ms + update_gpu_ms + draw_ms + swap_ms + loop_ms;
        }
    };

    int _cpu_cursor = 0;
    CpuTime _cpu_times[CpuEventCount];
    int _cpu_average = 0;

    std::optional<hrz::render::profiling::GpuProfilingData> _gpu_profiling_data;
    hrz::flat_hash_map<std::string, hrz::metrics::MetricDesc> _gpu_pass_metrics;
    bool _freeze_gpu_profiling = false;

    static const int FrameCount = 300;

    struct Frame
    {
        RenderRequest render_request;
    };

    int _frame_cursor = 0;
    Frame _frames[FrameCount];

    struct GpuResourceInfo
    {
        my::Resource::Type type;
        size_t size = 0;
        monitoring::ResourceOwner owner;
        hrz::InlinedVector<std::pair<MetadataString, MetadataString>, 3> metadata;
    };

    uint64_t _max_video_ram_size = 0;
    std::optional<my::Instance::GpuMemoryInfo> _gpu_memory_info = std::nullopt;

    hrz::flat_hash_map<uint64_t, GpuResourceInfo> _gpu_resources;
    hrz::monitoring::ResourceSorter<uint64_t, GpuResourceInfo> _gpu_resource_sorter;
    uint64_t _total_video_ram_usage = 0;
    hrz::metrics::MetricDesc _total_video_ram_usage_metric;

    struct MemoryUsage
    {
#if HRZ_LINUX || HRZ_EMSCRIPTEN
        size_t heap_size = 0;
        size_t allocated_heap_space = 0;
        size_t free_heap_space = 0;
        size_t releasable_heap_space = 0;

        hrz::metrics::MetricDesc heap_size_metric;
        hrz::metrics::MetricDesc allocated_heap_space_metric;
        hrz::metrics::MetricDesc free_heap_space_metric;
        hrz::metrics::MetricDesc releasable_heap_space_metric;

#    if HRZ_EMSCRIPTEN
        size_t emscripten_heap_size = 0;
        hrz::metrics::MetricDesc emscripten_heap_size_metric;
#    endif
#endif
    };

    int _compute_memory_usage = 0;
    MemoryUsage _memory_usage;

#if HRZ_EMSCRIPTEN
    uint64_t _max_wasm_memory_size = 0;
    bool _has_warned_about_low_memory = false;
#endif

    hrz::flat_hash_set<mu_Id> _expanded_nodes;

    std::vector<std::pair<std::string, std::string>> _info;

    hrz::ui::TooltipContext _tooltip_ctx;

public:
    explicit Monitoring(const hrz_proto::ViewerOptions& viewer_options);

    void register_cpu_time(
        int64_t events_us,
        int64_t update_us,
        int64_t update_gpu_us,
        int64_t draw_us,
        int64_t swap_us,
        int64_t loop_us);
    void register_gpu_times(hrz::render::profiling::GpuProfilingData&&);
    void register_frame(const RenderRequest&);
    void register_gpu_memory_info(const my::Instance::GpuMemoryInfo&);
    void register_gpu_resource(
        my::ResourceHandle resource_handle,
        my::Resource::Type resource_type,
        monitoring::systems::Name system,
        uint64_t layer_id = 0) override;
    void register_gpu_resource_size(my::ResourceHandle resource_handle, size_t size) override;
    void register_gpu_resource_metadata(
        my::ResourceHandle,
        MetadataString key,
        MetadataString value) override;
    void update_gpu_resource_owner(my::ResourceHandle, monitoring::systems::Name, uint64_t layer_id)
        override;
    void unregister_gpu_resource(my::ResourceHandle resource_handle) override;

    void work();

    void draw_ui(
        my::Instance*,
        RemoteMonitoring*,
        JobScheduler*,
        const LayersInfo*,
        mu_Context* ctx,
        const char* window_name);

    void dump_gpu_resources(const LayersInfo*, hrz_monitoring::MessageBuffer*) const;

    void add_info(std::string_view name, std::string_view value);

private:
    CpuTime get_cpu_time() const;

    void draw_frametime_fps(mu_Context* ctx, float frametime);
    void draw_frametime_categories(mu_Context* ctx, const CpuTime& t);
    void draw_frames(mu_Context* ctx);
    void draw_gpu_performance(mu_Context* ctx);
    void draw_main_memory_usage(mu_Context* ctx);
    void draw_gpu_memory_usage(mu_Context* ctx, const LayersInfo*);
    void draw_resource_tree(
        mu_Context* ctx,
        uint64_t tree_id,
        gsl::span<const uint64_t> gpu_resources,
        const std::function<uint64_t(const GpuResourceInfo&)>& get_first_value,
        const std::function<const char*(uint64_t)>& print_first_value,
        const mu_Color& first_bar_color,
        const std::function<uint64_t(const GpuResourceInfo&)>& get_second_value,
        const std::function<const char*(uint64_t)>& print_second_value,
        const mu_Color& second_bar_color);
};

} // namespace hrz
