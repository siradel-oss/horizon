#pragma once

#include "hrz/common/blob_image.h"
#include "hrz/common/metadata.h"
#include "hrz/common/monitoring_defs.h"

#include <utility>

namespace hrz
{
struct GpuResourceContext;

my::ResourceHandle to_gpu(
    const BlobImage&,
    GpuResourceContext*,
    bool generate_mipmaps,
    bool allow_allocation_failure,
    const monitoring::ResourceOwner& resource_owner,
    std::initializer_list<std::pair<MetadataString, MetadataString>> metadata = {});
} // namespace hrz
