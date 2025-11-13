#include "hrz/core/jobs/jobs_executor.h"

#include "hrz/common/profiling.h"
#include "hrz/core/jobs/jobs_declarations.h"
#include "hrz/core/jobs/jobs_enum_names.h"
#include "hrz/fnd/log.h"

namespace hrz_jobs::executor
{
hrz::JobResult run_job(
    uint32_t job_id,
    hrz_jobs::JobType job_type,
    const std::any& params,
    std::any& response,
    const JobContext& context)
{
    HRZ_SCOPED_SAMPLE("job executor run job");

    auto job_function = hrz_jobs::get_job_function(job_type);
    if (job_function != nullptr)
    {
        HRZ_SCOPED_SAMPLE("job executor execute job function");

        return job_function(params, response, context);
    }
    else
    {
        HRZ_LOG_ERROR("Unknown job type {}: {}", job_id, hrz_jobs::job_type_name(job_type));

        return hrz::JobResult::FAILURE;
    }
}

} // namespace hrz_jobs::executor
