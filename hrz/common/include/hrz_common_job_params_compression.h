#pragma once

#include "hrz_common_blob_allocator.h"

namespace hrz
{

struct DecompressBlobParams
{
    enum class CompressionType
    {
        kGzip,
        kBrotli,
        kZstd,
    };

    CompressionType type;
    blobs::BlobHandle compressed;
};

} // namespace hrz
