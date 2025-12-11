#include "hrz/common/compression.h"

#include "hrz/fnd/defer.h"
#include "hrz/fnd/log.h"

#include <brotli/decode.h>
#include <zlib.h>
#include <zstd.h>

#include <array>
#include <memory>

bool hrz::decompress_gzip(
    std::span<const std::byte> compressed,
    hrz::function_ref<void(std::span<const std::byte>)> callback)
{
    z_stream stream{};
    stream.next_in = (Bytef*)compressed.data();
    stream.avail_in = compressed.size_bytes();
    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;

    // MAX_WBITS | 16 is specific to gzip streams.
    if (inflateInit2(&stream, MAX_WBITS | 16) != Z_OK)
    {
        HRZ_LOG_ERROR("Couldn't initialize zlib for decompression");
        return false;
    }

    HRZ_DEFER[&stream]
    {
        inflateEnd(&stream);
    };

    std::array<std::byte, 4096> buffer{};
    do
    {
        stream.next_out = (Bytef*)buffer.data();
        stream.avail_out = buffer.size();

        auto status = inflate(&stream, Z_NO_FLUSH);
        callback(std::span<const std::byte>(buffer.data(), buffer.size() - stream.avail_out));

        if (status == Z_STREAM_END)
        {
            break;
        }
        else if (status < 0)
        {
            HRZ_LOG_ERROR("Couldn't decompress gzip stream: {}", stream.msg);
            return false;
        }

    } while (stream.avail_out == 0);

    return true;
}

bool hrz::decompress_zlib_uncompress(
    std::span<const std::byte> compressed,
    std::span<std::byte>* decompressed)
{
    uLongf decompressed_len = decompressed->size_bytes();
    int status = uncompress(
        (Bytef*)decompressed->data(), &decompressed_len, (const Bytef*)compressed.data(),
        compressed.size_bytes());
    if (status != Z_OK)
    {
        HRZ_LOG_ERROR("Couldn't decompress zlib stream: {}", zError(status));
        return false;
    }
    *decompressed = std::span<std::byte>(decompressed->data(), decompressed_len);
    return true;
}

bool hrz::decompress_brotli(
    std::span<const std::byte> compressed,
    hrz::function_ref<void(std::span<const std::byte>)> callback)
{
    std::unique_ptr<BrotliDecoderState, decltype(BrotliDecoderDestroyInstance)*> decoder_state(
        BrotliDecoderCreateInstance(nullptr, nullptr, nullptr), &BrotliDecoderDestroyInstance);

    if (!decoder_state)
    {
        HRZ_LOG_ERROR("Couldn't initialize brotli for decompression");
        return false;
    }

    std::array<std::byte, 4096> buffer{};
    size_t available_in = compressed.size_bytes();
    auto next_in = (const uint8_t*)compressed.data();

    while (true)
    {
        size_t available_out = buffer.size();
        auto next_out = (uint8_t*)buffer.data();

        auto status = BrotliDecoderDecompressStream(
            decoder_state.get(), &available_in, &next_in, &available_out, &next_out, nullptr);

        callback(std::span<const std::byte>(buffer.data(), buffer.size() - available_out));

        if (status == BROTLI_DECODER_RESULT_SUCCESS)
        {
            break;
        }
        else if (
            status == BROTLI_DECODER_RESULT_ERROR
            || status == BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT)
        {
            HRZ_LOG_ERROR("Couldn't decompress brotli stream");
            return false;
        }
    }

    return true;
}

bool hrz::decompress_zstd(
    std::span<const std::byte> compressed,
    hrz::function_ref<void(std::span<const std::byte>)> callback)
{
    // Decompress zstd stream using streaming.
    std::unique_ptr<ZSTD_DStream, decltype(ZSTD_freeDStream)*> stream(
        ZSTD_createDStream(), &ZSTD_freeDStream);
    if (!stream)
    {
        HRZ_LOG_ERROR("Couldn't initialize zstd for decompression");
        return false;
    }

    size_t ret = ZSTD_initDStream(stream.get());
    if (ZSTD_isError(ret))
    {
        HRZ_LOG_ERROR("Couldn't initialize zstd for decompression: {}", ZSTD_getErrorName(ret));
        return false;
    }

    std::array<std::byte, 4096> buffer{};
    ZSTD_inBuffer input = {compressed.data(), compressed.size_bytes(), 0};

    while (true)
    {
        ZSTD_outBuffer output = {buffer.data(), buffer.size(), 0};

        ret = ZSTD_decompressStream(stream.get(), &output, &input);
        callback(std::span<const std::byte>(buffer.data(), output.pos));

        if (ZSTD_isError(ret))
        {
            HRZ_LOG_ERROR("Couldn't decompress zstd stream: {}", ZSTD_getErrorName(ret));
            return false;
        }

        if (ret == 0)
        {
            break;
        }
    }

    return true;
}
