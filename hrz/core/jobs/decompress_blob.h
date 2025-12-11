#pragma once

#include "hrz/common/blob_allocator.h"

namespace hrz_jobs
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
    hrz::blobs::BlobHandle compressed;
};

} // namespace hrz_jobs
