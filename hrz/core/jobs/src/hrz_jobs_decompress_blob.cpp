#include "hrz_common_job_params_compression.h"
#include "hrz_jobs_declarations.h"

#include <hrz_common_blob_vector.h>
#include <hrz_common_compression.h>
#include <hrz_common_profiling.h>
#include <hrz_fnd_log.h>

namespace hrz_jobs::decompress_blob
{

hrz::JobResult run(
    const hrz::DecompressBlobParams& params,
    hrz::blobs::BlobHandle& output_handle,
    const JobContext& context)
{
    HRZ_SCOPED_SAMPLE("decompress blob job");

    auto input_data = params.compressed.get_data();
    if (!input_data.is_valid())
    {
        HRZ_LOG_ERROR("Invalid blob to decompress");
        return hrz::JobResult::FAILURE;
    }

    hrz::BlobVector<std::byte> output(
        context.get_blob_allocator(), input_data.as_bytes().size_bytes());

    bool had_error = false;
    auto append = [&output, &had_error](gsl::span<const std::byte> data)
    {
        if (had_error || data.empty()) return;

        size_t old_size = output.size().value_or(0);
        size_t new_size = old_size + data.size();
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
        case hrz::DecompressBlobParams::CompressionType::kGzip:
            decompression_success = hrz::decompress_gzip(input_data.as_bytes(), append);
            break;
        case hrz::DecompressBlobParams::CompressionType::kBrotli:
            decompression_success = hrz::decompress_brotli(input_data.as_bytes(), append);
            break;
        case hrz::DecompressBlobParams::CompressionType::kZstd:
            decompression_success = hrz::decompress_zstd(input_data.as_bytes(), append);
            break;
        default: assert(false && "Unhandled case"); break;
    }

    if (!had_error && decompression_success)
    {
        if (auto array = output.to_blob_array(); array.has_value())
        {
            output_handle = array.value().blob();
            return hrz::JobResult::SUCCESS;
        }
    }

    return hrz::JobResult::FAILURE;
}

} // namespace hrz_jobs::decompress_blob
