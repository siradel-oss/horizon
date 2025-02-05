#pragma once

#include <hrz_common_monitoring_defs.h>
#include <hrz_fnd_flat_hash_map.h>

#include <mycelium_backend.h>

namespace hrz
{
struct GpuResourceContext;

struct DownloadBuffer
{
    my::ResourceHandle buffer;
    size_t size;
};

// This pool is used to recycle buffers for downloading data from the GPU
// asynchronously. The size of the buffer is always rounded up to its next
// power of two so as to recycle more often each buffer.
class DownloadBufferPool
{
private:
    std::unordered_multimap<size_t, my::ResourceHandle> _buffers;

public:
    DownloadBuffer acquire(
        GpuResourceContext*,
        size_t size,
        const monitoring::ResourceOwner& resource_owner);
    void release(const DownloadBuffer& buffer);
    void free_all(my::ResourceContext* rc);
};

} // namespace hrz
