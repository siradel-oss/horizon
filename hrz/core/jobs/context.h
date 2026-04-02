#pragma once

#include "hrz/common/monitoring_defs.h"
#include "hrz/fnd/class.h"

#include <cstdint>

namespace hrz
{

struct BlobAllocator;
struct FontRasterizer;

namespace blobs
{

using BlobId = uint64_t;

}
} // namespace hrz

namespace hrz_jobs
{

class JobContext
{
public:
    JobContext() = default;
    HRZ_DEFAULT_COPY_MOVE(JobContext);
    virtual ~JobContext() = default;

    virtual int get_worker_id() const = 0;
    virtual hrz::BlobAllocator* get_blob_allocator() const = 0;
    virtual hrz::FontRasterizer* get_font_rasterizer() const = 0;
    virtual hrz::monitoring::ResourceOwner get_resource_owner() const = 0;
};

} // namespace hrz_jobs
