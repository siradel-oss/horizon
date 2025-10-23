#pragma once

#include <hrz_fnd_class.h>
#include <hrz_fnd_defines.h>

#include <functional>
#include <source_location>
#include <stdint.h>
#include <string_view>

namespace hrz_monitoring
{
struct MessageBuffer;
}

namespace hrz
{
struct ThreadProfiler;

namespace profiling
{
ThreadProfiler* create_thread_profiler(const char* thread_name);
void destroy_thread_profiler(ThreadProfiler*);

// Moves the thread profiler's data to the main thread.
// It should be called regularly, otherwise that data will be discarded when it becomes too large.
void synchronize_thread_profiler();

// The main thread has exclusive access to some of the following functions.
void init_main_thread();
void cleanup_main_thread();

// Can only be called by the main thread.
void set_profiling_enabled(bool enabled);
// Can only be called by the main thread.
bool is_profiling_enabled();
// Can only be called by the main thread.
void flush_messages(const std::function<void(const hrz_monitoring::MessageBuffer*)>& callback);

void dump_thread_names(
    const std::function<void(uint32_t thread_id, std::string_view thread_name)>& callback);

enum SampleFlags
{
    None = 0,
    Recursive = 1,
    Aggregate = 2,
    Root = 4,
};

void begin_sample(
    const char* name,
    int flags,
    uint64_t* hash_cache,
    const std::source_location& source_loc = std::source_location::current());

void end_sample();

enum FrameRenderTypes
{
    VisualRender = 1,
    PickingRender = 2,
    PlanetFeedbackRender = 4
};

void begin_frame();
void end_frame(int render_types);

struct EndSampleOnScopeExit
{
    EndSampleOnScopeExit() = default;

    HRZ_DELETE_COPY_MOVE(EndSampleOnScopeExit);

    ~EndSampleOnScopeExit() { end_sample(); }
};

} // namespace profiling

} // namespace hrz

// _R suffix means recursive: recursive samples with the same name will be
// collapsed together.
// _A suffix means aggregate: sibling samples with the same name will be
// collapsed togather.
// _RA means recursive and aggregate.
// _ROOT means that this samples marks the root of a samples tree. Its use is
// encouraged to check that there is no sample tree imbalance.

#define HRZ_BEGIN_SAMPLE_EX2(NAME, FLAGS, ID)                       \
    static uint64_t HRZ_CONCAT(hrz_profiling_sample_hash_, ID) = 0; \
    hrz::profiling::begin_sample(NAME, FLAGS, &HRZ_CONCAT(hrz_profiling_sample_hash_, ID))

#define HRZ_BEGIN_SAMPLE_EX(NAME, FLAGS) HRZ_BEGIN_SAMPLE_EX2(NAME, FLAGS, __COUNTER__)

#define HRZ_END_SAMPLE() hrz::profiling::end_sample()

#define HRZ_BEGIN_SAMPLE(NAME) HRZ_BEGIN_SAMPLE_EX(NAME, hrz::profiling::None)
#define HRZ_BEGIN_SAMPLE_ROOT(NAME) HRZ_BEGIN_SAMPLE_EX(NAME, hrz::profiling::Root)
#define HRZ_BEGIN_SAMPLE_R(NAME) HRZ_BEGIN_SAMPLE_EX(NAME, hrz::profiling::Recursive)
#define HRZ_BEGIN_SAMPLE_A(NAME) HRZ_BEGIN_SAMPLE_EX(NAME, hrz::profiling::Aggregate)
#define HRZ_BEGIN_SAMPLE_RA(NAME) \
    HRZ_BEGIN_SAMPLE(NAME, hrz::profiling::Recursive | hrz::profiling::Aggregate)

#define HRZ_SCOPED_SAMPLE_EX(NAME, FLAGS)                  \
    HRZ_BEGIN_SAMPLE_EX(NAME, FLAGS);                      \
    const hrz::profiling::EndSampleOnScopeExit HRZ_CONCAT( \
        hrz_profiling_end_sample_on_scope_exit_, __COUNTER__)

#define HRZ_SCOPED_SAMPLE(NAME) HRZ_SCOPED_SAMPLE_EX(NAME, hrz::profiling::None)
#define HRZ_SCOPED_SAMPLE_ROOT(NAME) HRZ_SCOPED_SAMPLE_EX(NAME, hrz::profiling::Root)
#define HRZ_SCOPED_SAMPLE_R(NAME) HRZ_SCOPED_SAMPLE_EX(NAME, hrz::profiling::Recursive)
#define HRZ_SCOPED_SAMPLE_A(NAME) HRZ_SCOPED_SAMPLE_EX(NAME, hrz::profiling::Aggregate)
#define HRZ_SCOPED_SAMPLE_RA(NAME) \
    HRZ_SCOPED_SAMPLE_EX(NAME, hrz::profiling::Recursive | hrz::profiling::Aggregate)
