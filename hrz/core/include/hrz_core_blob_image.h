#pragma once

#include <hrz_common_blob_image.h>
#include <hrz_common_metadata.h>
#include <hrz_common_monitoring_defs.h>

#include <string_view>
#include <utility>

namespace hrz
{
class GpuResourceContext;

my::ResourceHandle to_gpu(
    const BlobImage&,
    GpuResourceContext*,
    bool generate_mipmaps,
    bool allow_allocation_failure,
    const monitoring::ResourceOwner& resource_owner,
    std::initializer_list<std::pair<MetadataString, MetadataString>> metadata = {});
} // namespace hrz
