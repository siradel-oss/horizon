// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/core/download_buffer_pool.h"

#include "hrz/core/render/resource_context.h"
#include "hrz/fnd/maths.h"

namespace hrz
{

DownloadBuffer DownloadBufferPool::acquire(
    GpuResourceContext* rc,
    size_t size,
    const monitoring::ResourceOwner& resource_owner)
{
    size = hrz::next_power_of_two(size);

    auto it = _buffers.find(size);
    if (it != _buffers.end())
    {
        assert(it->first >= size);

        DownloadBuffer buffer{it->second, it->first};
        _buffers.erase(it);
        return buffer;
    }
    else
    {
        my::BufferResource res(my::BufferResource::TextureDownload);
        res.size = size;
        res.data = nullptr;
        res.usage = my::UsageHint::Download;

        my::ResourceHandle buffer = rc->alloc(&res, resource_owner);

        return DownloadBuffer{buffer, size};
    }
}

void DownloadBufferPool::release(const DownloadBuffer& buffer)
{
    _buffers.insert(std::make_pair(buffer.size, buffer.buffer));
}

void DownloadBufferPool::free_all(my::ResourceContext* rc)
{
    for (auto it : _buffers)
    {
        rc->dealloc(it.second);
    }
    _buffers.clear();
}

} // namespace hrz
