#pragma once

#include <hrz_common_blob_allocator.h>

#include <gsl/gsl-lite.hpp>

#include <cstddef>

namespace hrz
{

bool is_woff_or_woff2(gsl::span<const std::byte> data);

std::optional<blobs::BlobHandle> woff_or_woff2_to_ttf(
    BlobAllocator* ba,
    gsl::span<const std::byte> woff_data);

} // namespace hrz
