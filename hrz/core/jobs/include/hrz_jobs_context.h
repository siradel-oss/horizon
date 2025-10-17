#pragma once

#include <hrz_common_monitoring_defs.h>

#include <cstdint>
#include <span>

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
    virtual int get_worker_id() const = 0;
    virtual hrz::BlobAllocator* get_blob_allocator() const = 0;
    virtual hrz::FontRasterizer* get_font_rasterizer() const = 0;
    virtual hrz::monitoring::ResourceOwner get_resource_owner() const = 0;
};
} // namespace hrz_jobs
