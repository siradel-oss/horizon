#include "hrz_core.h"
#include "hrz_core_job_scheduler.h"
#include "hrz_jobs_tickets.h"

#include <hrz_common_blob_allocator.h>
#include <hrz_common_font_rasterizer.h>
#include <hrz_common_job_params.h>

#include <gtest/gtest.h>

namespace
{
using namespace hrz;
using namespace hrz::test;

TEST(JobScheduler, add)
{
    auto blob_allocator = blobs::create_allocator(1024, true);
    auto job_scheduler = job_scheduler::create(1, blob_allocator, nullptr);

    TestJob1Params params1;
    params1.a = 7;
    params1.b = 11;
    auto job1 = hrz_jobs::add_job_test_job_1(job_scheduler, params1, {});

    while (!hrz_jobs::is_job_finished(job_scheduler, job1))
    {
    }

    EXPECT_EQ(
        hrz_jobs::get_job_status(job_scheduler, job1), job_scheduler::JobStatus::Finished_Success);

    TestJob1Response response;
    hrz_jobs::get_job_response(job_scheduler, job1, response);
    EXPECT_EQ(response.v, 77);

    job_scheduler::destroy(job_scheduler, false);
    blobs::destroy_allocator(blob_allocator);
}

TEST(JobScheduler, add_multiple_jobs_one_worker)
{
    auto blob_allocator = blobs::create_allocator(1024, true);
    auto job_scheduler = job_scheduler::create(1, blob_allocator, nullptr);

    TestJob1Params params1;
    params1.a = 6;
    params1.b = 4;
    auto job1 = hrz_jobs::add_job_test_job_1(job_scheduler, params1, {});

    TestJob2Params params2;
    params2.s = "Test1";
    params2.count = 4;
    auto job2 = hrz_jobs::add_job_test_job_2(job_scheduler, params2, {});

    while (!hrz_jobs::is_job_finished(job_scheduler, job1)
           || !hrz_jobs::is_job_finished(job_scheduler, job2))
    {
    }

    EXPECT_EQ(
        hrz_jobs::get_job_status(job_scheduler, job1), job_scheduler::JobStatus::Finished_Success);
    EXPECT_EQ(
        hrz_jobs::get_job_status(job_scheduler, job2), job_scheduler::JobStatus::Finished_Success);

    TestJob1Response response1;
    hrz_jobs::get_job_response(job_scheduler, job1, response1);
    EXPECT_EQ(response1.v, 24);

    TestJob2Response response2;
    hrz_jobs::get_job_response(job_scheduler, job2, response2);
    EXPECT_EQ(response2.res, "Test1Test1Test1Test1");

    job_scheduler::destroy(job_scheduler, false);
    blobs::destroy_allocator(blob_allocator);
}

TEST(JobScheduler, add_multiple_jobs_multiple_workers)
{
    auto blob_allocator = blobs::create_allocator(1024, true);
    auto job_scheduler = job_scheduler::create(1, blob_allocator, nullptr);

    TestJob1Params params1;
    params1.a = 10;
    params1.b = 6;
    auto job1 = hrz_jobs::add_job_test_job_1(job_scheduler, params1, {});

    TestJob2Params params2;
    params2.s = "Test2";
    params2.count = 4;
    auto job2 = hrz_jobs::add_job_test_job_2(job_scheduler, params2, {});

    while (!hrz_jobs::is_job_finished(job_scheduler, job1)
           || !hrz_jobs::is_job_finished(job_scheduler, job2))
    {
    }

    EXPECT_EQ(
        hrz_jobs::get_job_status(job_scheduler, job1), job_scheduler::JobStatus::Finished_Success);
    EXPECT_EQ(
        hrz_jobs::get_job_status(job_scheduler, job2), job_scheduler::JobStatus::Finished_Success);

    TestJob1Response response1;
    hrz_jobs::get_job_response(job_scheduler, job1, response1);
    EXPECT_EQ(response1.v, 60);

    TestJob2Response response2;
    hrz_jobs::get_job_response(job_scheduler, job2, response2);
    EXPECT_EQ(response2.res, "Test2Test2Test2Test2");

    job_scheduler::destroy(job_scheduler, false);
    blobs::destroy_allocator(blob_allocator);
}

TEST(JobScheduler, get_job_type)
{
    auto blob_allocator = blobs::create_allocator(1024, true);
    auto job_scheduler = job_scheduler::create(1, blob_allocator, nullptr);

    TestJob1Params params1;
    params1.a = 7;
    params1.b = 11;
    auto job1 = hrz_jobs::add_job_test_job_1(job_scheduler, params1, {});

    EXPECT_EQ(hrz::job_scheduler::get_job_type(job_scheduler, job1.ticket), hrz_jobs::TEST_JOB_1);

    while (!hrz_jobs::is_job_finished(job_scheduler, job1))
    {
    }

    EXPECT_EQ(
        hrz_jobs::get_job_status(job_scheduler, job1), job_scheduler::JobStatus::Finished_Success);

    EXPECT_EQ(hrz::job_scheduler::get_job_type(job_scheduler, job1.ticket), hrz_jobs::TEST_JOB_1);

    TestJob1Response response;
    hrz_jobs::get_job_response(job_scheduler, job1, response);

    job_scheduler::destroy(job_scheduler, false);
    blobs::destroy_allocator(blob_allocator);
}

TEST(JobScheduler, get_job_status)
{
    auto blob_allocator = blobs::create_allocator(1024, true);
    auto job_scheduler = job_scheduler::create(1, blob_allocator, nullptr);

    TestJob1Params params1;
    params1.a = 7;
    params1.b = 11;
    auto job1 = hrz_jobs::add_job_test_job_1(job_scheduler, params1, {});

    while (!hrz_jobs::is_job_finished(job_scheduler, job1))
    {
    }

    EXPECT_EQ(
        hrz_jobs::get_job_status(job_scheduler, job1), job_scheduler::JobStatus::Finished_Success);

    EXPECT_EQ(hrz::job_scheduler::get_job_type(job_scheduler, job1.ticket), hrz_jobs::TEST_JOB_1);

    TestJob1Response response;
    hrz_jobs::get_job_response(job_scheduler, job1, response);

    EXPECT_EQ(hrz_jobs::get_job_status(job_scheduler, job1), job_scheduler::JobStatus::Invalid);

    job_scheduler::destroy(job_scheduler, false);
    blobs::destroy_allocator(blob_allocator);
}

} // namespace
