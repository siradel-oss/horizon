#pragma once

#include "hrz/common/metadata.h"
#include "hrz/common/monitoring_defs.h"

#include <mycelium/mycelium.h>

#include <cstdint>
#include <string_view>

namespace hrz::monitoring
{
class GpuResourceMonitoring
{
public:
    virtual void register_gpu_resource(
        my::ResourceHandle,
        my::Resource::Type,
        systems::Name,
        uint64_t layer_id) = 0;
    virtual void register_gpu_resource_size(my::ResourceHandle, size_t size) = 0;
    virtual void register_gpu_resource_metadata(
        my::ResourceHandle,
        MetadataString key,
        MetadataString value) = 0;
    virtual void update_gpu_resource_owner(
        my::ResourceHandle,
        systems::Name,
        uint64_t layer_id) = 0;
    virtual void unregister_gpu_resource(my::ResourceHandle) = 0;
};
} // namespace hrz::monitoring
