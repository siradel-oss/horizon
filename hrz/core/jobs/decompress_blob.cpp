#include "hrz/core/jobs/decompress_blob.h"

#include "hrz/common/blob_vector.h"
#include "hrz/common/compression.h"
#include "hrz/common/profiling.h"
#include "hrz/core/jobs/jobs_declarations.h"
#include "hrz/fnd/log.h"

namespace hrz_jobs::decompress_blob
{

hrz_jobs::JobResult run(
    const hrz_jobs::DecompressBlobParams& params,
    hrz::blobs::BlobHandle& output_handle,
    const JobContext& context)
{
    HRZ_SCOPED_SAMPLE("decompress blob job");

    auto input_data = params.compressed.get_data();
    if (!input_data.is_valid())
    {
        HRZ_LOG_ERROR("Invalid blob to decompress");
        return hrz_jobs::JobResult::FAILURE;
    }

    hrz::BlobVector<std::byte> output(
        context.get_blob_allocator(), input_data.as_bytes().size_bytes());

    bool had_error = false;
    auto append = [&output, &had_error](std::span<const std::byte> data)
    {
        if (had_error || data.empty()) return;

        const size_t old_size = output.size().value_or(0);
        const size_t new_size = old_size + data.size();
        output.resize(new_size);

        if (output.size() != new_size)
        {
            HRZ_LOG_ERROR("Failed to resize output buffer during decompression");
            had_error = true;
            return;
        }

        assert(output.data().has_value()); // Should be true if resize was successful
        std::memcpy(output.data().value().data() + old_size, data.data(), data.size_bytes());
    };

    bool decompression_success = false;
    switch (params.type)
    {
        using enum hrz_jobs::DecompressBlobParams::CompressionType;
        case kGzip:
            decompression_success = hrz::decompress_gzip(input_data.as_bytes(), append);
            break;
        case kBrotli:
            decompression_success = hrz::decompress_brotli(input_data.as_bytes(), append);
            break;
        case kZstd:
            decompression_success = hrz::decompress_zstd(input_data.as_bytes(), append);
            break;
        default: assert(false && "Unhandled case"); break;
    }

    if (!had_error && decompression_success)
    {
        if (auto array = output.to_blob_array(); array.has_value())
        {
            output_handle = array.value().blob();
            return hrz_jobs::JobResult::SUCCESS;
        }
    }

    return hrz_jobs::JobResult::FAILURE;
}

} // namespace hrz_jobs::decompress_blob
