#pragma once

#include "hrz/common/monitoring_defs.h"
#include "hrz/core/jobs/jobs_type.h"

#include <any>
#include <cassert>
#include <cstdint>

namespace hrz_monitoring
{
struct MessageBuffer;
}

namespace hrz
{
struct JobScheduler;
struct BlobAllocator;
struct FontRasterizer;

namespace job_scheduler
{
enum class JobStatus
{
    Queued,
    InProgress,
    Finished_Success,
    Finished_Failure,
    Canceled,
    Invalid
};

enum class ProfilingDataStatus
{
    NotQueried,
    InProgress,
    Ready
};

using Ticket = uint32_t;

/**
 * Create a job scheduler instance and its workers.
 */
JobScheduler* create(uint32_t max_worker_count, BlobAllocator*, FontRasterizer*);

/**
 * Destroy the job scheduler and optionally its workers.
 * If some jobs haven't been picked up yet by the runners,
 * they are dropped and will never run.
 *
 * Leaking workers avoids join their threads and blocking
 * the calling thread. However the worker theads will be
 * unrecoverable. Only use this option if you intend to
 * close the application immediately after.
 */
void destroy(JobScheduler*, bool leak_workers);

/**
 * Add a job with the given parameters, which include
 * its type.
 * It will be run as soon as a runner is available.
 */
Ticket add_job(
    JobScheduler*,
    hrz_jobs::JobType,
    std::any& parameters,
    const hrz::monitoring::ResourceOwner& owner);

/**
 * Cancel a job and discard the result.
 */
void cancel_job(JobScheduler*, Ticket);

/**
 * Return true if the job exists in the system.
 */
bool is_job_valid(JobScheduler*, Ticket);

/**
 * Return true if the job is finished, whether is was
 * successful or not.
 */
bool is_job_finished(JobScheduler*, Ticket);

/**
 * Return the status of the job.
 */
JobStatus get_job_status(JobScheduler*, Ticket);

/**
 * Return the type of the job.
 */
hrz_jobs::JobType get_job_type(JobScheduler*, Ticket);

/**
 * Get the response of the job.
 * The job must be finished before calling this function.
 * This call removes the job from the scheduler.
 */
std::any get_job_response(JobScheduler*, Ticket);

void work(JobScheduler*);

} // namespace job_scheduler

} // namespace hrz
