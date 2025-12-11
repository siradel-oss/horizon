#pragma once

#include "hrz/fnd/function_ref.h"

#include <span>

namespace hrz
{

// When decompressing, the callback will be called repeatedly with chunks of decompressed data.
// The functions returns true or false depending on whether decompression was successful.

bool decompress_gzip(
    std::span<const std::byte> compressed,
    hrz::function_ref<void(std::span<const std::byte>)> callback);

bool decompress_zlib_uncompress(
    std::span<const std::byte> compressed,
    std::span<std::byte>* decompressed);

bool decompress_zstd(
    std::span<const std::byte> compressed,
    hrz::function_ref<void(std::span<const std::byte>)> callback);

bool decompress_brotli(
    std::span<const std::byte> compressed,
    hrz::function_ref<void(std::span<const std::byte>)> callback);

} // namespace hrz
