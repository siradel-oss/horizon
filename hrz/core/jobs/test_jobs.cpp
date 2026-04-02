#include "hrz/core/jobs/test_jobs.h"

#include "hrz/core/jobs/jobs_declarations.h"

namespace hrz_jobs
{
namespace test_job_1
{

hrz_jobs::JobResult run(
    const hrz_jobs::TestJob1Params& params,
    hrz_jobs::TestJob1Response& response,
    const JobContext&)
{
    auto a = params.a;
    auto b = params.b;
    auto v = a * b;

    response.v = v;

    // std::this_thread::sleep_for(std::chrono::seconds(5));

    return hrz_jobs::JobResult::SUCCESS;
}

} // namespace test_job_1

namespace test_job_2
{

hrz_jobs::JobResult run(
    const hrz_jobs::TestJob2Params& params,
    hrz_jobs::TestJob2Response& response,
    const JobContext&)
{
    auto s = params.s;
    auto count = params.count;

    auto res = std::string("");
    for (int i = 0; i < count; i++)
    {
        res += s;
    }

    response.res = res.c_str();

    // std::this_thread::sleep_for(std::chrono::seconds(2));

    return hrz_jobs::JobResult::SUCCESS;
}

} // namespace test_job_2

} // namespace hrz_jobs
