#pragma once

#include "hrz/common/blob_allocator.h"

#include <cstddef>
#include <span>

namespace hrz
{

bool is_woff_or_woff2(std::span<const std::byte> data);

std::optional<blobs::BlobHandle> woff_or_woff2_to_ttf(
    BlobAllocator* ba,
    std::span<const std::byte> woff_data);

} // namespace hrz
