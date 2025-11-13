#pragma once

#include "hrz/common/job_result.h"
#include "hrz/common/monitoring_defs.h"
#include "hrz/core/jobs/context.h"
#include "hrz/core/jobs/jobs_declarations.h"
#include "hrz/fnd/defines.h"

#include <any>

namespace hrz
{
struct BlobAllocator;
struct FontRasterizer;
} // namespace hrz

namespace hrz_jobs
{
struct ExecutorContext : public JobContext
{
    ExecutorContext() : worker_id(-1) {}

    virtual ~ExecutorContext() = default;

    int get_worker_id() const override { return worker_id; }

    hrz::BlobAllocator* get_blob_allocator() const override { return blob_allocator; }

    hrz::FontRasterizer* get_font_rasterizer() const override { return font_rasterizer; }

    hrz::monitoring::ResourceOwner get_resource_owner() const override { return resource_owner; }

    int worker_id;
    hrz::BlobAllocator* blob_allocator{};
    hrz::FontRasterizer* font_rasterizer{};
    hrz::monitoring::ResourceOwner resource_owner;
};

namespace executor
{
hrz::JobResult run_job(
    uint32_t job_id,
    hrz_jobs::JobType job_type,
    const std::any& params,
    std::any& response,
    const JobContext& context);
}

} // namespace hrz_jobs
