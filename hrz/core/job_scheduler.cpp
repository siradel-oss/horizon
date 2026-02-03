#include "hrz/core/job_scheduler.h"

#include "hrz/common/metrics.h"
#include "hrz/common/monitoring_defs.h"
#include "hrz/common/profiling.h"
#include "hrz/core/jobs/job_result.h"
#include "hrz/fnd/defines.h"
#include "hrz/fnd/mem.h"
#include "hrz/fnd/thread.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

extern "C"
{
#include <microui/microui.h>
}

#include "hrz/common/blob_allocator.h"
#include "hrz/core/jobs/jobs_enum_names.h"
#include "hrz/core/jobs/jobs_executor.h"
#include "hrz/fnd/gen_object_pool.h"
#include "hrz/fnd/thread.h"

namespace
{
using namespace hrz;

struct Worker
{
    enum class Status
    {
        NEW,
        IDLE,
        BUSY,
        DEAD
    };

    uint32_t id;
    Status status;
    job_scheduler::Ticket ticket;

    JobScheduler* job_scheduler;

    std::thread thread;
    hrz::ThreadProfiler* profiler;
    hrz::ThreadMetricsRegistry* metrics;
};

struct Job
{
    uint32_t id;
    std::atomic<job_scheduler::JobStatus> status;
    hrz_jobs::JobType type;
    std::any parameters;
    hrz::monitoring::ResourceOwner resource_owner;
    hrz_jobs::JobResult result;
    std::any response;
};

} // namespace

namespace hrz
{
struct JobScheduler
{
    using IndexPool = GenIndexPool<job_scheduler::Ticket, 12, 20>;
    using JobPool = GenObjectPool<Job, IndexPool, 128>;

    uint32_t worker_count;
    std::vector<Worker*> workers;

    JobPool job_pool;
    std::deque<job_scheduler::Ticket> queued_jobs;

    std::mutex mutex;
    std::condition_variable condition;
    bool stop;
};

namespace job_scheduler
{
namespace
{
struct JobStatusSetResult
{
    bool status_changed;
    JobStatus observed_status;
};

JobStatusSetResult try_set_job_status(Job* job, JobStatus expected_status, JobStatus desired_status)
{
    JobStatus desired_or_actual_status = desired_status;
    bool success = job->status.compare_exchange_strong(
        expected_status, desired_or_actual_status, std::memory_order_seq_cst,
        std::memory_order_seq_cst);
    return {success, success ? desired_status : desired_or_actual_status};
}

void set_job_status(Job* job, JobStatus status)
{
    job->status.store(status, std::memory_order_relaxed);
}

JobStatus get_job_status(Job* job)
{
    return job->status.load(std::memory_order_relaxed);
}

void worker_func(Worker* worker, BlobAllocator* blob_allocator, FontRasterizer* font_rasterizer)
{
    std::string worker_name = fmt::format("Worker #{}", worker->id);
    auto scheduler = worker->job_scheduler;

    worker->profiler = hrz::profiling::create_thread_profiler(worker_name.c_str());
    worker->metrics = hrz::metrics::create_thread_registry(false);

    // Loop forever. When an enqueued job is found, execute it.
    for (;;)
    {
        Ticket ticket;
        Job* job = nullptr;

        {
            // Wait until at least one job is queued, or the scheduler is being stopped.
            std::unique_lock<std::mutex> lock(scheduler->mutex);
            scheduler->condition.wait(
                lock, [scheduler] { return scheduler->stop || !scheduler->queued_jobs.empty(); });

            // Exit the loop (and the function) if the scheduler is being stopped.
            if (scheduler->stop) break;

            // Try to get a queued job to execute.
            ticket = scheduler->queued_jobs.front();
            scheduler->queued_jobs.pop_front();

            job = scheduler->job_pool.get_object(ticket);
            if (job == nullptr)
            {
                HRZ_LOG_ERROR("Worker {}: No job for ticket {}!", worker->id, ticket);
                continue;
            }

            // Set the job status to in-progress.
            // @Note We don't release the mutex before setting the status to
            // InProgress because from the exterior, removing the job from the
            // queue and setting its status should appear atomic.
            auto result = try_set_job_status(job, JobStatus::Queued, JobStatus::InProgress);

            // If the job's status wasn't queued, it means it has been taken
            // by another worker.
            if (!result.status_changed) continue;
        }

        worker->status = Worker::Status::BUSY;
        worker->ticket = ticket;
        set_job_status(job, JobStatus::InProgress);

        // Prepare the job context.
        hrz_jobs::ExecutorContext runner_context;
        runner_context.worker_id = worker->id;
        runner_context.blob_allocator = blob_allocator;
        runner_context.font_rasterizer = font_rasterizer;
        runner_context.resource_owner = job->resource_owner;

        // Run the job.
        auto job_result = hrz_jobs::executor::run_job(
            job->id, job->type, job->parameters, job->response, runner_context);
        job->parameters = {};

        std::atomic_thread_fence(std::memory_order_release);

        // Update the job's status to finished, if it's still in-progress.
        auto status_set_result = try_set_job_status(
            job, JobStatus::InProgress,
            job_result == hrz_jobs::JobResult::SUCCESS ? JobStatus::Finished_Success
                                                       : JobStatus::Finished_Failure);

        if (!status_set_result.status_changed)
        {
            // The job's status wasn't in-progress. Because we know we have
            // started the job, it means that is has been cancelled in the
            // meantime.

            HRZ_SCOPED_LOCK(scheduler->mutex);
            scheduler->job_pool.release(ticket);
        }

        worker->status = Worker::Status::IDLE;

        HRZ_INCREMENT_COUNTER("Jobs processed", {});
        hrz::metrics::finish_thread_registry_frame();
        hrz::metrics::synchronize_thread_registry();
        hrz::profiling::synchronize_thread_profiler();
    }

    hrz::profiling::destroy_thread_profiler(worker->profiler);
    hrz::metrics::destroy_thread_registry(worker->metrics);
}
} // namespace

JobScheduler* create(
    uint32_t max_worker_count,
    BlobAllocator* blob_allocator,
    FontRasterizer* font_rasterizer)
{
    assert(blob_allocator);

    if (max_worker_count == 0)
    {
        max_worker_count = std::numeric_limits<uint32_t>::max();
    }

    uint32_t cpu_core_count = std::thread::hardware_concurrency();
    if (cpu_core_count == 0) cpu_core_count = 1;
    uint32_t worker_count =
        std::min(max_worker_count, (uint32_t)std::max(1, (int32_t)cpu_core_count - 2));

    auto scheduler = new JobScheduler();
    scheduler->worker_count = worker_count;

    scheduler->stop = false;

    for (uint32_t i = 0; i < worker_count; i++)
    {
        auto worker = new Worker();
        worker->id = i;
        worker->job_scheduler = scheduler;
        worker->status = Worker::Status::IDLE;

        worker->thread = std::thread([worker, blob_allocator, font_rasterizer]
                                     { worker_func(worker, blob_allocator, font_rasterizer); });

#if HRZ_DESKTOP
        hrz::set_thread_priority(worker->thread, hrz::ThreadPriority::Low);
#endif

        scheduler->workers.push_back(worker);
    }

#if HRZ_DESKTOP
    hrz::set_current_thread_priority(hrz::ThreadPriority::High);
#endif

    return scheduler;
}

void destroy(JobScheduler* scheduler, bool leak_workers)
{
    assert(scheduler);

    {
        HRZ_SCOPED_LOCK(scheduler->mutex);
        scheduler->stop = true;
        scheduler->queued_jobs.clear();
    }

    scheduler->condition.notify_all();

    if (!leak_workers)
    {
        for (auto worker : scheduler->workers)
        {
            worker->thread.join();
            delete worker;
        }
    }

    scheduler->worker_count = 0;
    scheduler->workers.clear();

    delete scheduler;
}

Ticket add_job(
    JobScheduler* scheduler,
    hrz_jobs::JobType job_type,
    std::any&& parameters,
    const hrz::monitoring::ResourceOwner& resource_owner)
{
    assert(scheduler);

#if HRZ_DEBUG
    {
        HRZ_SCOPED_LOCK(scheduler->mutex);
        assert(!scheduler->stop);
    }
#endif

    Ticket ticket;
    {
        HRZ_SCOPED_LOCK(scheduler->mutex);

        ticket = scheduler->job_pool.alloc();

        // Create the job and add it to the queue.
        // No need to take the job mutex, as workers do not know this job yet.
        auto job = scheduler->job_pool.get_object(ticket);
        job->id = ticket;
        job->type = job_type;
        job->parameters = std::move(parameters);
        job->resource_owner = resource_owner;
        set_job_status(job, JobStatus::Queued);

        std::atomic_thread_fence(std::memory_order_release);

        scheduler->queued_jobs.push_back(ticket);
    }

    // HRZ_LOG_DEBUG("Enqueued job {}", ticket);

    scheduler->condition.notify_one();

    return ticket;
}

void cancel_job(JobScheduler* scheduler, Ticket ticket)
{
    assert(scheduler);

    HRZ_SCOPED_LOCK(scheduler->mutex);
    Job* job = scheduler->job_pool.get_object(ticket);

    if (job == nullptr) return;

    if (get_job_status(job) == JobStatus::Canceled) return;

    // Try to set the job's status to cancelled.
    auto result = try_set_job_status(job, JobStatus::InProgress, JobStatus::Canceled);

    if (!result.status_changed)
    {
        // The job's status wasn't in-progress. This means
        // that it was still queued, or already complete.

        auto it = std::ranges::find_if(
            scheduler->queued_jobs, [=](const Ticket& t) { return t == ticket; });

        if (it != scheduler->queued_jobs.end())
        {
            // The job was still queued.
            scheduler->queued_jobs.erase(it);
        }

        scheduler->job_pool.release(ticket);
    }
}

bool is_job_valid(JobScheduler* scheduler, Ticket ticket)
{
    auto status = get_job_status(scheduler, ticket);

    return !(status == JobStatus::Invalid || status == JobStatus::Canceled);
}

bool is_job_finished(JobScheduler* scheduler, Ticket ticket)
{
    auto status = get_job_status(scheduler, ticket);

    return status == JobStatus::Finished_Success || status == JobStatus::Finished_Failure;
}

JobStatus get_job_status(JobScheduler* scheduler, Ticket ticket)
{
    assert(scheduler);

    HRZ_SCOPED_LOCK(scheduler->mutex);
    auto job = scheduler->job_pool.get_object(ticket);

    if (job == nullptr) return JobStatus::Invalid;

    return get_job_status(job);
}

hrz_jobs::JobType get_job_type(JobScheduler* scheduler, Ticket ticket)
{
    assert(scheduler);

    HRZ_SCOPED_LOCK(scheduler->mutex);
    auto job = scheduler->job_pool.get_object(ticket);

    assert(job);

    return job->type;
}

std::any get_job_response(JobScheduler* scheduler, Ticket ticket)
{
    assert(scheduler);

    Job* job;
    {
        HRZ_SCOPED_LOCK(scheduler->mutex);
        job = scheduler->job_pool.get_object(ticket);
    }

    assert(job);
    if (get_job_status(job) != JobStatus::Finished_Success
        && get_job_status(job) != JobStatus::Finished_Failure)
    {
        assert(!"Job not finished when calling get_job_response");
        return {};
    }

    std::atomic_thread_fence(std::memory_order_acquire);
    std::any response = std::move(job->response);

    {
        HRZ_SCOPED_LOCK(scheduler->mutex);
        scheduler->job_pool.release(ticket);
    }

    return response;
}

void work(JobScheduler* scheduler)
{
    assert(scheduler);

    HRZ_SET_GAUGE("Jobs queued", scheduler->queued_jobs.size(), {});
}

const char* worker_status_str(const Worker* worker)
{
    switch (worker->status)
    {
        case Worker::Status::NEW: return "Initializing";
        case Worker::Status::IDLE: return "Idle";
        case Worker::Status::BUSY: return "Working";
        case Worker::Status::DEAD: return "Dead";
        default: return "Unknown";
    }
}

void dev_ui(JobScheduler* scheduler, mu_Context* ctx, const char* window_name)
{
    HRZ_SCOPED_LOCK(scheduler->mutex);

    char buffer[1024];

    if (mu_begin_window_ex(ctx, window_name, mu_rect(300, 100, 400, 300), MU_OPT_CLOSED))
    {
        const int window_width = mu_get_current_container(ctx)->body.w - 16;

        mu_layout_row(ctx, 1, &window_width, 0);
        auto fmtres = fmt::format_to_n(
            buffer, HRZ_ARRAY_COUNT(buffer) - 1, "{} requests queued",
            scheduler->queued_jobs.size());
        *fmtres.out = '\0';
        mu_text(ctx, buffer);

        mu_layout_row(ctx, 1, &window_width, 0);
        fmtres = fmt::format_to_n(
            buffer, HRZ_ARRAY_COUNT(buffer) - 1, "{} workers", scheduler->worker_count);
        *fmtres.out = '\0';
        mu_text(ctx, buffer);

        if (mu_header(ctx, "Workers"))
        {
            static int layout[] = {20, 90, -1};
            mu_layout_row(ctx, 3, layout, 0);

            mu_text(ctx, "#");
            mu_text(ctx, "Status");
            mu_text(ctx, "Type");

            for (auto worker : scheduler->workers)
            {
                fmtres = fmt::format_to_n(buffer, HRZ_ARRAY_COUNT(buffer) - 1, "{}", worker->id);
                *fmtres.out = '\0';
                mu_text(ctx, buffer);
                mu_text(ctx, worker_status_str(worker));

                if (worker->status == Worker::Status::BUSY)
                {
                    auto ticket = worker->ticket;
                    auto job = scheduler->job_pool.get_object(ticket);
                    if (job)
                    {
                        mu_text(ctx, hrz_jobs::job_type_name(job->type));
                    }
                    else
                    {
                        mu_text(ctx, "<Invalid ticket>");
                    }
                }
                else
                {
                    mu_text(ctx, "<Sleeping>");
                }
            }
        }

        mu_end_window(ctx);
    }
}

} // namespace job_scheduler

} // namespace hrz
