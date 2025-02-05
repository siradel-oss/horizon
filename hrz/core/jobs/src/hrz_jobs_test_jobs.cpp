#include "hrz_jobs_declarations.h"

#include <hrz_common_test_jobs.h>

namespace hrz_jobs
{
namespace test_job_1
{
hrz::JobResult run(
    const hrz::test::TestJob1Params& params,
    hrz::test::TestJob1Response& response,
    const JobContext&)
{
    auto a = params.a;
    auto b = params.b;
    auto v = a * b;

    response.v = v;

    // std::this_thread::sleep_for(std::chrono::seconds(5));

    return hrz::JobResult::SUCCESS;
}

} // namespace test_job_1

namespace test_job_2
{
hrz::JobResult run(
    const hrz::test::TestJob2Params& params,
    hrz::test::TestJob2Response& response,
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

    return hrz::JobResult::SUCCESS;
}

} // namespace test_job_2

} // namespace hrz_jobs
