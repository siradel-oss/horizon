#pragma once

#include <gsl/gsl-lite.hpp>

#include <functional>

namespace hrz
{

// When decompressing, the callback will be called repeatedly with chunks of decompressed data.
// The functions returns true or false depending on whether decompression was successful.

bool decompress_gzip(
    gsl::span<const std::byte> compressed,
    const std::function<void(gsl::span<const std::byte>)>& callback);

bool decompress_zstd(
    gsl::span<const std::byte> compressed,
    const std::function<void(gsl::span<const std::byte>)>& callback);

bool decompress_brotli(
    gsl::span<const std::byte> compressed,
    const std::function<void(gsl::span<const std::byte>)>& callback);

} // namespace hrz
