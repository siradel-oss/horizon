#pragma once

#include "hrz/common/blob_allocator.h"

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
