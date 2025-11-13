#include "hrz/core/actor_runner.h"

#include "hrz/common/blob_allocator.h"
#include "hrz/common/metrics.h"
#include "hrz/common/monitoring_defs.h"
#include "hrz/common/profiling.h"
#include "hrz/core/actor.h"
#include "hrz/core/vector/data_loader/data_loader.h"
#include "hrz/fnd/thread.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace hrz
{
static constexpr std::chrono::milliseconds RUN_INTERVAL = std::chrono::milliseconds(16);

struct ActorRunner
{
    bool run_on_main_thread;

    std::thread thread;
    std::mutex run_mutex;
    std::condition_variable condition;
    std::atomic<bool> main_thread_has_run;
    std::atomic<bool> stop;

    std::vector<std::unique_ptr<Actor>> actors;

    std::mutex new_actors_mutex;
    std::vector<std::unique_ptr<Actor>> new_actors;

    std::atomic<bool> is_working;

    hrz::ThreadProfiler* profiler;
    hrz::ThreadMetricsRegistry* metrics;
};

namespace actor_runner
{
namespace
{
// Returns true if is working.
bool run_actors(
    ActorRunner* runner,
    AttributionRegistry* attributions,
    BlobAllocator* blob_allocator,
    ClientMessageQueue* client_message_queue,
    JobScheduler* job_scheduler,
    SceneModel* scene_model,
    VectorDataLoader* vector_data_loader)
{
    {
        HRZ_SCOPED_LOCK(runner->new_actors_mutex);
        runner->actors.insert(
            runner->actors.end(), std::make_move_iterator(runner->new_actors.begin()),
            std::make_move_iterator(runner->new_actors.end()));
        runner->new_actors.clear();
    }

    vector_data::work(
        vector_data_loader, scene_model, job_scheduler, blob_allocator, client_message_queue,
        attributions);

    hrz::blobs::work(blob_allocator);

    bool is_working = false;

    for (auto it = runner->actors.begin(); it != runner->actors.end();)
    {
        auto status = (*it)->work_async(blob_allocator, job_scheduler);

        if (status == ActorStatus::RELEASED)
        {
            it = runner->actors.erase(it);
        }
        else
        {
            if (status == ActorStatus::WORKING)
            {
                is_working = true;
            }

            ++it;
        }
    }

    return is_working;
}

void run_func(
    ActorRunner* runner,
    AttributionRegistry* attributions,
    BlobAllocator* blob_allocator,
    ClientMessageQueue* client_message_queue,
    JobScheduler* job_scheduler,
    SceneModel* scene_model,
    VectorDataLoader* vector_data_loader)
{
    runner->profiler = hrz::profiling::create_thread_profiler("actor runner");
    runner->metrics = hrz::metrics::create_thread_registry(false);

    for (;;)
    {
        {
            // We only run when the main thread also runs. Otherwise some actors
            // might do too much work and queues might fill up to the point of
            // running out of memory.
            std::unique_lock<std::mutex> lock(runner->run_mutex);
            runner->condition.wait(
                lock,
                [runner]
                {
                    bool should_run = true;
                    runner->main_thread_has_run.compare_exchange_strong(should_run, false);
                    return should_run || runner->stop;
                });
        }

        if (runner->stop.load(std::memory_order_relaxed))
        {
            std::atomic_thread_fence(std::memory_order_acquire);
            break;
        }

        auto run_start_time = std::chrono::steady_clock::now();

        runner->is_working = run_actors(
            runner, attributions, blob_allocator, client_message_queue, job_scheduler, scene_model,
            vector_data_loader);

        hrz::metrics::finish_thread_registry_frame();
        hrz::metrics::synchronize_thread_registry();
        hrz::profiling::synchronize_thread_profiler();

        auto next_run_start_time = run_start_time + RUN_INTERVAL;
        std::this_thread::sleep_until(next_run_start_time);
    }

    hrz::profiling::destroy_thread_profiler(runner->profiler);
    hrz::metrics::destroy_thread_registry(runner->metrics);
}
} // namespace

ActorRunner* create(
    bool run_on_main_thread,
    AttributionRegistry* attributions,
    BlobAllocator* blob_allocator,
    ClientMessageQueue* client_message_queue,
    JobScheduler* job_scheduler,
    SceneModel* scene_model,
    VectorDataLoader* vector_data_loader)
{
    assert(
        attributions && blob_allocator && client_message_queue && job_scheduler && scene_model
        && vector_data_loader);

    auto runner = new ActorRunner();
    runner->run_on_main_thread = run_on_main_thread;

    if (!run_on_main_thread)
    {
        runner->main_thread_has_run = false;
        runner->stop = false;
        runner->thread = std::thread(
            [runner, attributions, blob_allocator, client_message_queue, job_scheduler, scene_model,
             vector_data_loader]
            {
                run_func(
                    runner, attributions, blob_allocator, client_message_queue, job_scheduler,
                    scene_model, vector_data_loader);
            });

#if HRZ_DESKTOP
        hrz::set_thread_priority(runner->thread, hrz::ThreadPriority::Low);
#endif
    }

    return runner;
}

void work(
    ActorRunner* runner,
    AttributionRegistry* attributions,
    BlobAllocator* blob_allocator,
    ClientMessageQueue* client_message_queue,
    JobScheduler* job_scheduler,
    SceneModel* scene_model,
    VectorDataLoader* vector_data_loader)
{
    assert(runner);

    if (runner->run_on_main_thread)
    {
        runner->is_working = run_actors(
            runner, attributions, blob_allocator, client_message_queue, job_scheduler, scene_model,
            vector_data_loader);
    }
    else
    {
        HRZ_SCOPED_LOCK(runner->run_mutex);
        runner->main_thread_has_run = true;
        runner->condition.notify_one();
    }
}

void destroy(ActorRunner* runner, bool leak)
{
    assert(runner);

    if (!runner->run_on_main_thread)
    {
        runner->stop = true;
        runner->condition.notify_one();

        if (!leak)
        {
            runner->thread.join();
            delete runner;
        }
    }
    else
    {
        delete runner;
    }
}

void add_actor(ActorRunner* runner, std::unique_ptr<Actor> actor)
{
    assert(runner && actor);

    HRZ_SCOPED_LOCK(runner->new_actors_mutex);
    runner->new_actors.push_back(std::move(actor));
}

bool is_working(ActorRunner* runner)
{
    assert(runner);

    return runner->is_working.load();
}
} // namespace actor_runner
} // namespace hrz
