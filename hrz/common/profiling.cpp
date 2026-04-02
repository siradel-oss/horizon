#include "hrz/common/profiling.h"

#include "hrz/fnd/arena.h"
#include "hrz/fnd/hash.h"
#include "hrz/fnd/log.h"
#include "hrz/fnd/time.h"
#include "hrz/monitoring/monitoring.h"

#include <assert.h>
#include <google/protobuf/arena.h>

#include <mutex>
#include <string>

using PbArena = google::protobuf::Arena;

static constexpr size_t MaxBufferedDataSize = 4 * 1024 * 1024;

namespace hrz
{
namespace profiling
{

struct Sample;
using SamplePtr = Arena::Ptr<Sample>;

struct Sample
{
    hrz_monitoring::Sample* pb_sample; // In the profiler's protobuf arena

    SamplePtr parent = nullptr;
    SamplePtr first_child = nullptr;
    SamplePtr last_child = nullptr;
    SamplePtr next_sibling = nullptr;
    uint64_t id_hash;
    int64_t last_begin_time_us; // Last time we entered this sample
    uint32_t current_recursion = 0;

    inline void increment_aggregation_count()
    {
        pb_sample->set_aggregation_count(pb_sample->aggregation_count() + 1);
    }

    inline void increment_current_recursion()
    {
        current_recursion += 1;
        pb_sample->set_recursion_max(std::max(pb_sample->recursion_max(), current_recursion));
    }

    inline void update_exit_time(int64_t time_us)
    {
        pb_sample->set_exit(time_us);
        pb_sample->set_aggregation_time_total(
            pb_sample->aggregation_time_total() + (time_us - last_begin_time_us));
    }
};

} // namespace profiling

struct SharedData
{
    std::mutex mutex;
    hrz_monitoring::MessageBuffer* buffer;

    bool profiling_enabled = false;

    bool main_thread_set = false;

    struct ThreadInfo
    {
        std::string name;
        bool active;
    };

    std::vector<ThreadInfo> threads;

    SharedData() : buffer(hrz_monitoring::create_buffer()) {}

    ~SharedData() { hrz_monitoring::destroy_buffer(buffer); }
};

struct MainThreadData
{
    bool profiling_enabled = false;
};

struct ThreadProfiler
{
    bool _enabled = false;

    Arena _sample_arena;
    profiling::SamplePtr _root = nullptr;
    profiling::SamplePtr _current_parent = nullptr;
    bool _frame_started = false;
    hrz_monitoring_proto::MonitoringMessage _frame_message;

    hrz_monitoring::MessageBuffer* _monitoring_buffer;
    PbArena _pb_arena;

    static constexpr int64_t SYNCHRONIZATION_INTERVAL_MS = 250;
    int64_t _last_synchronization_timestamp_ms = 0;
    uint32_t _thread_id = 0;

    explicit ThreadProfiler(bool enabled) :
        _enabled(enabled),
        _monitoring_buffer(hrz_monitoring::create_buffer()),
        _last_synchronization_timestamp_ms(hrz::now_ms_s64())
    {
    }

    ~ThreadProfiler() { hrz_monitoring::destroy_buffer(_monitoring_buffer); }

    void begin_sample(
        const char* name,
        const std::source_location& source_loc,
        int flags,
        uint64_t hash)
    {
        if (!_enabled) return;

        if ((flags & profiling::Root) != 0 && _root)
        {
            HRZ_LOG_ERROR(
                "Expected sample \"{}\" to be a root, but it's not. Is there a sample tree "
                "imbalance? Disabling thread #{} profiler now until next synchronization to avoid "
                "memory blowing up.",
                name, _thread_id);
            return;
        }

        if (!_root)
        {
            _root = init_sample(name, _thread_id, source_loc, hash);
            _current_parent = _root;
        }
        else
        {
            assert(_current_parent);

            if ((flags & profiling::Aggregate) != 0)
            {
                for (profiling::SamplePtr it = _current_parent->first_child; it;
                     it = it->next_sibling)
                {
                    if (it->id_hash == hash)
                    {
                        it->increment_aggregation_count();
                        it->last_begin_time_us = now_us_s64();
                        _current_parent = it;
                        return;
                    }
                }
            }

            if ((flags & profiling::Recursive) != 0 && _current_parent->id_hash == hash)
            {
                _current_parent->increment_current_recursion();
            }
            else
            {
                profiling::SamplePtr sample = init_sample(name, _thread_id, source_loc, hash);
                if (!_current_parent->first_child)
                {
                    _current_parent->first_child = sample;
                    _current_parent->last_child = sample;
                }
                else
                {
                    _current_parent->last_child->next_sibling = sample;
                    _current_parent->last_child = sample;
                }
                _current_parent = sample;
            }
        }
    }

    void end_sample()
    {
        if (!_enabled || !_root) return;
        assert(_current_parent);

        if (_current_parent->current_recursion > 0)
        {
            _current_parent->current_recursion -= 1;
        }
        else
        {
            _current_parent->update_exit_time(now_us_s64());

            if (_current_parent == _root)
            {
                end_root();
            }
            else
            {
                assert(_current_parent->parent);
                _current_parent = _current_parent->parent;
            }
        }
    }

    void begin_frame()
    {
        if (!_enabled) return;

        _frame_started = true;
        _frame_message.mutable_frame()->set_timestamp_begin(now_us_s64());
    }

    void end_frame(int render_types)
    {
        if (!_enabled || !_frame_started) return;

        _frame_started = false;
        auto* frame = _frame_message.mutable_frame();
        frame->set_timestamp_end(now_us_s64());
        frame->set_visual_render(render_types & profiling::VisualRender);
        frame->set_picking_render(render_types & profiling::PickingRender);
        frame->set_planet_feedback_render(render_types & profiling::PlanetFeedbackRender);

        hrz_monitoring::push_message(_monitoring_buffer, _frame_message);
    }

    void synchronize(SharedData* data)
    {
        const int64_t now = hrz::now_ms_s64();
        if (now - _last_synchronization_timestamp_ms > SYNCHRONIZATION_INTERVAL_MS)
        {
            const bool was_enabled = _enabled;

            {
                std::unique_lock<std::mutex> lock(data->mutex);

                hrz_monitoring::append_messages(data->buffer, _monitoring_buffer);
                _enabled = data->profiling_enabled;

                if (hrz_monitoring::get_written_data(data->buffer).size() > MaxBufferedDataSize)
                {
                    HRZ_LOG_WARNING("Too much data in message buffer, dropping all of it.");
                    hrz_monitoring::reset_buffer(data->buffer);
                }
            }

            if (was_enabled && !_enabled) reset();

            hrz_monitoring::reset_buffer(_monitoring_buffer);
            _last_synchronization_timestamp_ms = now;
        }
    }

    void reset()
    {
        _sample_arena.reset();
        _pb_arena.Reset();
        _root = nullptr;
        _current_parent = nullptr;
    }

    profiling::SamplePtr init_sample(
        const char* name,
        uint32_t thread_id,
        const std::source_location& source_loc,
        uint64_t hash)
    {
        int64_t now = now_us_s64();

        profiling::SamplePtr sample = _sample_arena.alloc<profiling::Sample>();
        sample->parent = _current_parent;
        sample->id_hash = hash;
        sample->last_begin_time_us = now;

        if (!_current_parent)
        {
            sample->pb_sample = PbArena::Create<hrz_monitoring::Sample>(&_pb_arena);
        }
        else
        {
            sample->pb_sample = _current_parent->pb_sample->add_children();
        }

        sample->pb_sample->set_name(name);
        sample->pb_sample->set_thread_id(thread_id);
        sample->pb_sample->set_source_file(source_loc.file_name());
        sample->pb_sample->set_source_line(source_loc.line());
        sample->pb_sample->set_entry(now);

        return sample;
    }

    void end_root()
    {
        if (_root)
        {
            auto* msgs = PbArena::Create<hrz_monitoring::MonitoringMessages>(&_pb_arena);
            msgs->add_messages()->unsafe_arena_set_allocated_sample(_root->pb_sample);
            hrz_monitoring::push_messages(_monitoring_buffer, *msgs);
        }
        reset();
    }
};

} // namespace hrz

static hrz::SharedData g_shared_data;

static thread_local hrz::MainThreadData* g_main_thread_data;
static thread_local hrz::ThreadProfiler* g_thread_profiler;

hrz::ThreadProfiler* hrz::profiling::create_thread_profiler(const char* thread_name)
{
    assert(!g_thread_profiler && "This thread's profiler is already owned.");

    std::unique_lock<std::mutex> lock(g_shared_data.mutex);

    g_thread_profiler = new ThreadProfiler(g_shared_data.profiling_enabled);

    uint32_t thread_id = g_shared_data.threads.size();
    g_shared_data.threads.push_back({thread_name, true});
    g_thread_profiler->_thread_id = thread_id;

    return g_thread_profiler;
}

void hrz::profiling::destroy_thread_profiler(hrz::ThreadProfiler* profiler)
{
    if (!g_thread_profiler || g_thread_profiler != profiler) return;

    g_shared_data.threads[g_thread_profiler->_thread_id].active = false;
    delete g_thread_profiler;
    g_thread_profiler = nullptr;
}

void hrz::profiling::synchronize_thread_profiler()
{
    if (!g_thread_profiler) return;

    g_thread_profiler->synchronize(&g_shared_data);
}

void hrz::profiling::init_main_thread()
{
    std::unique_lock<std::mutex> lock(g_shared_data.mutex);
    assert(!g_shared_data.main_thread_set);

    g_main_thread_data = new MainThreadData();
    g_shared_data.main_thread_set = true;
}

void hrz::profiling::cleanup_main_thread()
{
    assert(g_main_thread_data);

    delete g_main_thread_data;

    std::unique_lock<std::mutex> lock(g_shared_data.mutex);
    g_shared_data.main_thread_set = false;
}

void hrz::profiling::set_profiling_enabled(bool enabled)
{
    assert(g_main_thread_data && "This function should only be called from the main thread.");

    g_main_thread_data->profiling_enabled = enabled;

    std::unique_lock<std::mutex> lock(g_shared_data.mutex);
    g_shared_data.profiling_enabled = enabled;
}

bool hrz::profiling::is_profiling_enabled()
{
    assert(g_main_thread_data && "This function should only be called from the main thread.");
    return g_main_thread_data->profiling_enabled;
}

void hrz::profiling::flush_messages(
    const std::function<void(const hrz_monitoring::MessageBuffer*)>& callback)
{
    assert(g_main_thread_data && "This function should only be called from the main thread.");
    if (!g_main_thread_data->profiling_enabled) return;

    std::unique_lock<std::mutex> lock(g_shared_data.mutex);

    if (hrz_monitoring::get_written_data(g_shared_data.buffer).empty()) return;

    callback(g_shared_data.buffer);
    hrz_monitoring::reset_buffer(g_shared_data.buffer);
}

void hrz::profiling::dump_thread_names(
    const std::function<void(uint32_t thread_id, std::string_view thread_name)>& callback)
{
    std::unique_lock<std::mutex> lock(g_shared_data.mutex);
    for (uint32_t i = 0; i < g_shared_data.threads.size(); i++)
    {
        if (!g_shared_data.threads[i].active) continue;

        callback(i, g_shared_data.threads[i].name);
    }
}

void hrz::profiling::begin_sample(
    const char* name,
    int flags,
    uint64_t* hash_cache,
    const std::source_location& source_loc)
{
    if (!g_thread_profiler || !g_thread_profiler->_enabled) return;

    if (*hash_cache == 0)
    {
        *hash_cache = murmur3_x64_64(name);
        *hash_cache = hash_mix(*hash_cache, murmur3_x64_64(source_loc.file_name()));
        *hash_cache = hash_mix(*hash_cache, (uint64_t)source_loc.line());

        if (*hash_cache == 0) *hash_cache = 1;
    }

    g_thread_profiler->begin_sample(name, source_loc, flags, *hash_cache);
}

void hrz::profiling::end_sample()
{
    if (!g_thread_profiler || !g_thread_profiler->_enabled) return;

    g_thread_profiler->end_sample();
}

void hrz::profiling::begin_frame()
{
    if (!g_thread_profiler || !g_thread_profiler->_enabled) return;

    g_thread_profiler->begin_frame();
}

void hrz::profiling::end_frame(int render_types)
{
    if (!g_thread_profiler || !g_thread_profiler->_enabled) return;

    g_thread_profiler->end_frame(render_types);
}
