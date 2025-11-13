#include "hrz/common/woff.h"

#include "hrz/common/compression.h"
#include "hrz/fnd/log.h"
#include "hrz/fnd/maths.h"
#include "hrz/fnd/mem.h"

#include <woff2/decode.h>
#include <woff2/output.h>

static bool is_woff(const std::byte* data, size_t size)
{
    static constexpr std::byte woff_magic[] = {0x77_b, 0x4F_b, 0x46_b, 0x46_b};
    return size >= sizeof(woff_magic) && std::memcmp(data, woff_magic, sizeof(woff_magic)) == 0;
}

static bool is_woff2(const std::byte* data, size_t size)
{
    static constexpr std::byte woff2_magic[] = {0x77_b, 0x4F_b, 0x46_b, 0x32_b};
    return size >= sizeof(woff2_magic) && std::memcmp(data, woff2_magic, sizeof(woff2_magic)) == 0;
}

bool hrz::is_woff_or_woff2(std::span<const std::byte> data)
{
    return is_woff(data.data(), data.size()) || is_woff2(data.data(), data.size());
}

static std::optional<hrz::blobs::BlobHandle> woff2_to_ttf(
    hrz::BlobAllocator* ba,
    std::span<const std::byte> woff_data)
{
    auto size = woff2::ComputeWOFF2FinalSize((const uint8_t*)woff_data.data(), woff_data.size());
    hrz::blobs::BlobHandle blob{};

    if (auto blob_opt = hrz::blobs::allocate_blob_sync(ba, size);
        blob_opt.has_value() && blob_opt->is_valid())
    {
        blob = blob_opt.value();
    }
    else
    {
        HRZ_LOG_ERROR("Couldn't allocate memory to decode WOFF2 font");
        return std::nullopt;
    }

    hrz::blobs::register_metadata(ba, blob, "content"_ss, "TTF data decoded from WOFF2"_ss);

    // Mutable access only in this block
    {
        auto decompressed_data = blob.get_mutable_data();
        if (!decompressed_data.data())
        {
            HRZ_LOG_ERROR("Couldn't allocate memory to decode WOFF2 font");
            return std::nullopt;
        }

        woff2::WOFF2MemoryOut woff2_out((uint8_t*)decompressed_data.data(), size);
        if (!woff2::ConvertWOFF2ToTTF(
                (const uint8_t*)woff_data.data(), woff_data.size(), &woff2_out))
        {
            HRZ_LOG_ERROR("Couldn't decode WOFF2 font");
            return std::nullopt;
        }
    }

    return blob;
}

static uint32_t read_be_uint32(std::span<const std::byte> data, size_t offset)
{
    return (static_cast<uint32_t>(data[offset]) << 24)
        | (static_cast<uint32_t>(data[offset + 1]) << 16)
        | (static_cast<uint32_t>(data[offset + 2]) << 8) | static_cast<uint32_t>(data[offset + 3]);
}

static uint16_t read_be_uint16(std::span<const std::byte> data, size_t offset)
{
    return (static_cast<uint16_t>(data[offset]) << 8) | static_cast<uint16_t>(data[offset + 1]);
}

static void write_be_uint32(std::span<std::byte> data, size_t offset, uint32_t value)
{
    data[offset] = static_cast<std::byte>((value >> 24) & 0xFF);
    data[offset + 1] = static_cast<std::byte>((value >> 16) & 0xFF);
    data[offset + 2] = static_cast<std::byte>((value >> 8) & 0xFF);
    data[offset + 3] = static_cast<std::byte>(value & 0xFF);
}

static void write_be_uint16(std::span<std::byte> data, size_t offset, uint16_t value)
{
    data[offset] = static_cast<std::byte>((value >> 8) & 0xFF);
    data[offset + 1] = static_cast<std::byte>(value & 0xFF);
}

static uint32_t round_up_4(uint32_t value)
{
    return (value + 3) & ~3;
}

static std::optional<hrz::blobs::BlobHandle> woff_to_ttf(
    hrz::BlobAllocator* ba,
    std::span<const std::byte> woff_data)
{
    static constexpr size_t kWoffHeaderSize = 44;
    static constexpr size_t kWoffTableEntrySize = 20;

    static constexpr size_t kSfntHeaderSize = 12;
    static constexpr size_t kSfntTableEntrySize = 16;

    if (woff_data.size_bytes() < kWoffHeaderSize)
    {
        HRZ_LOG_ERROR("Invalid WOFF file: too small to contain header");
        return std::nullopt;
    }

    uint32_t sfnt_version = read_be_uint32(woff_data, 4);
    uint16_t num_tables = read_be_uint16(woff_data, 12);

    if (woff_data.size_bytes()
        < kWoffHeaderSize + static_cast<size_t>(num_tables) * kWoffTableEntrySize)
    {
        HRZ_LOG_ERROR("Invalid WOFF file: too small to contain table entries");
        return std::nullopt;
    }

    size_t total_sfnt_size =
        kWoffHeaderSize + static_cast<size_t>(num_tables) * kWoffTableEntrySize;
    for (size_t i = 0; i < static_cast<size_t>(num_tables); ++i)
    {
        uint32_t orig_length =
            read_be_uint32(woff_data, kWoffHeaderSize + i * kWoffTableEntrySize + 12);
        total_sfnt_size += round_up_4(orig_length);
    }

    auto blob_opt = hrz::blobs::allocate_blob_sync(ba, total_sfnt_size);
    if (!blob_opt.has_value() || !blob_opt->is_valid())
    {
        HRZ_LOG_ERROR("Couldn't allocate memory to decode WOFF font");
        return std::nullopt;
    }

    hrz::blobs::BlobHandle blob = blob_opt.value();
    hrz::blobs::register_metadata(ba, blob, "content"_ss, "TTF data decoded from WOFF"_ss);

    auto sfnt_data_blob = blob.get_mutable_data();
    if (!sfnt_data_blob.data())
    {
        HRZ_LOG_ERROR("Couldn't allocate memory to decode WOFF font");
        return std::nullopt;
    }

    std::span<std::byte> sfnt_data{sfnt_data_blob.data(), sfnt_data_blob.size()};
    memset(sfnt_data.data(), 0, sfnt_data.size_bytes());

    // Compute some stuff for sfnt header
    // Doing stuff on 32 bits because I don't want to rewrite those functions for 16 bits.
    uint16_t search_range = (uint16_t)hrz::previous_power_of_two((uint32_t)num_tables) * 16;
    uint16_t entry_selector = (uint16_t)hrz::log2((uint32_t)search_range / 16);
    uint16_t range_shift = num_tables * 16 - search_range;

    // Write SFNT header
    write_be_uint32(sfnt_data, 0, sfnt_version);
    write_be_uint16(sfnt_data, 4, num_tables);
    write_be_uint16(sfnt_data, 6, search_range);
    write_be_uint16(sfnt_data, 8, entry_selector);
    write_be_uint16(sfnt_data, 10, range_shift);

    size_t sfnt_data_offset = kSfntHeaderSize + num_tables * kSfntTableEntrySize;

    for (size_t i = 0; i < static_cast<size_t>(num_tables); ++i)
    {
        uint32_t tag = read_be_uint32(woff_data, kWoffHeaderSize + i * kWoffTableEntrySize);
        uint32_t offset = read_be_uint32(woff_data, kWoffHeaderSize + i * kWoffTableEntrySize + 4);
        uint32_t comp_length =
            read_be_uint32(woff_data, kWoffHeaderSize + i * kWoffTableEntrySize + 8);
        uint32_t orig_length =
            read_be_uint32(woff_data, kWoffHeaderSize + i * kWoffTableEntrySize + 12);
        uint32_t orig_checksum =
            read_be_uint32(woff_data, kWoffHeaderSize + i * kWoffTableEntrySize + 16);

        if (offset + comp_length > woff_data.size_bytes())
        {
            HRZ_LOG_ERROR("Invalid WOFF file: table data out of bounds");
            return std::nullopt;
        }

        auto compressed_slice = woff_data.subspan(offset, comp_length);
        auto decompressed_slice = sfnt_data.subspan(sfnt_data_offset, orig_length);

        if (compressed_slice.size_bytes() == decompressed_slice.size_bytes())
        {
            std::memcpy(
                decompressed_slice.data(), compressed_slice.data(), compressed_slice.size_bytes());
        }
        else if (!hrz::decompress_zlib_uncompress(compressed_slice, &decompressed_slice))
        {
            HRZ_LOG_ERROR("Couldn't decompress WOFF font table");
            return std::nullopt;
        }

        std::span<std::byte> table_entry =
            sfnt_data.subspan(kSfntHeaderSize + i * kSfntTableEntrySize, kSfntTableEntrySize);
        write_be_uint32(table_entry, 0, tag);
        write_be_uint32(table_entry, 4, orig_checksum);
        write_be_uint32(table_entry, 8, sfnt_data_offset);
        write_be_uint32(table_entry, 12, decompressed_slice.size_bytes());

        sfnt_data_offset += round_up_4(decompressed_slice.size());
    }

    return blob;
}

std::optional<hrz::blobs::BlobHandle> hrz::woff_or_woff2_to_ttf(
    BlobAllocator* ba,
    std::span<const std::byte> woff_data)
{
    if (is_woff(woff_data.data(), woff_data.size()))
    {
        return woff_to_ttf(ba, woff_data);
    }

    if (is_woff2(woff_data.data(), woff_data.size()))
    {
        return woff2_to_ttf(ba, woff_data);
    }

    return std::nullopt;
}
