// SPDX-FileCopyrightText: Copyright 2024 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/core/pmtiles.h"

#include "hrz/common/compression.h"
#include "hrz/common/fmt.h" // IWYU pragma: keep
#include "hrz/common/geo.h"
#include "hrz/common/proj.h"
#include "hrz/core/clock.h"
#include "hrz/core/jobs/decompress_blob.h"
#include "hrz/core/jobs/jobs_tickets.h"
#include "hrz/fnd/flat_hash_map.h"
#include "hrz/fnd/flat_hash_set.h"
#include "hrz/fnd/gen_index_pool.h"
#include "hrz/fnd/gen_object_pool.h"
#include "hrz/fnd/json_utils.h"
#include "hrz/fnd/log.h"
#include "hrz/fnd/lru.h"
#include "hrz/fnd/meta.h"
#include "hrz/fnd/varint.h"

#include <rapidjson/document.h>

#include <bit>
#include <string>

namespace hrz
{
namespace
{

// "the size of the header plus the compressed size of the root directory MUST
// NOT exceed 16384 bytes to allow latency-optimized clients to retrieve the
// root directory in its entirety."
// https://github.com/protomaps/PMTiles/blob/main/spec/v3/spec.md#4-directories
static constexpr size_t kInitialDownloadSize = 16384;

enum class Compression : uint8_t
{
    kUnknown = 0,
    kNone = 1,
    kGzip = 2,
    kBrotli = 3,
    kZstd = 4,
    kCompressionInvalid,
};

std::optional<hrz_jobs::DecompressBlobParams::CompressionType> convert_compression_type(
    Compression c)
{
    switch (c)
    {
        case Compression::kGzip: return hrz_jobs::DecompressBlobParams::CompressionType::kGzip;
        case Compression::kBrotli: return hrz_jobs::DecompressBlobParams::CompressionType::kBrotli;
        case Compression::kZstd: return hrz_jobs::DecompressBlobParams::CompressionType::kZstd;
        default: return std::nullopt;
    }
}

#pragma pack(push, 1)

static_assert(std::endian::native == std::endian::little, "PMTiles file format is little-endian");

struct RawHeader
{
    char magic[7];
    uint8_t version;
    uint64_t root_dir_offset;
    uint64_t root_dir_length;
    uint64_t metadata_offset;
    uint64_t metadata_length;
    uint64_t leaf_dirs_offset;
    uint64_t leaf_dirs_length;
    uint64_t tile_data_offset;
    uint64_t tile_data_length;
    uint64_t num_addressed_tiles;
    uint64_t num_tile_entries;
    uint64_t num_tile_contents;
    uint8_t clustered;
    Compression internal_compression;
    Compression tile_compression;
    uint8_t tile_type;
    uint8_t min_zoom;
    uint8_t max_zoom;
    uint64_t min_position_raw;
    uint64_t max_position_raw;
    uint8_t center_zoom;
    uint64_t center_position_raw;
};

#pragma pack(pop)
static_assert(sizeof(RawHeader) == 127, "Header size mismatch");

constexpr void hilbert_rotate(int64_t n, int64_t* x, int64_t* y, int64_t rx, int64_t ry)
{
    if (ry == 0)
    {
        if (rx == 1)
        {
            *x = n - 1 - *x;
            *y = n - 1 - *y;
        }

        int64_t t = *x;
        *x = *y;
        *y = t;
    }
}

constexpr uint64_t tile_coords_to_tile_id(const TileCoords& tc)
{
    assert(tc.lod <= 31);
    assert(tc.x <= (1U << tc.lod) - 1U);
    assert(tc.y <= (1U << tc.lod) - 1U);

    // Original version did a loop. We use the fact that adding the squares of
    // powers of two just places bits at even locations:
    // 1^2 + 2^2 + 4^2 + 16^2 + ... = 1 + 4 + 16 + 256 + ... =
    // 0b0001 + 0b0100 + 0b10000 + 0b100000000 + ...
    // So we mask a bunch of even bits by the length given by the LOD.
    uint64_t tile_count_above = 0x5555'5555'5555'5555ULL & ((1ULL << tc.lod * 2) - 1);

    int64_t d = 0;
    int64_t tx = tc.x;
    int64_t ty = tc.y;
    for (int64_t s = (1LL << tc.lod) / 2; s > 0; s /= 2)
    {
        int64_t rx = (tx & s) > 0;
        int64_t ry = (ty & s) > 0;
        d += s * s * ((3LL * rx) ^ ry);
        hilbert_rotate(s, &tx, &ty, rx, ry);
    }
    return tile_count_above + d;
}

static_assert(tile_coords_to_tile_id({0, 0, 0}) == 0, "Tile ID mismatch");
static_assert(tile_coords_to_tile_id({0, 0, 1}) == 1, "Tile ID mismatch");
static_assert(tile_coords_to_tile_id({0, 1, 1}) == 2, "Tile ID mismatch");
static_assert(tile_coords_to_tile_id({1, 1, 1}) == 3, "Tile ID mismatch");
static_assert(tile_coords_to_tile_id({1, 0, 1}) == 4, "Tile ID mismatch");
static_assert(tile_coords_to_tile_id({0, 0, 2}) == 5, "Tile ID mismatch");
static_assert(tile_coords_to_tile_id({1, 0, 2}) == 6, "Tile ID mismatch");
static_assert(tile_coords_to_tile_id({1, 1, 2}) == 7, "Tile ID mismatch");
static_assert(tile_coords_to_tile_id({0, 1, 2}) == 8, "Tile ID mismatch");
static_assert(tile_coords_to_tile_id({3423, 1763, 12}) == 19078479, "Tile ID mismatch");

struct Directory;

struct DirectoryIdentity
{
    uint64_t offset;
    uint64_t length;

    template<typename H>
    friend H AbslHashValue(H h, const DirectoryIdentity& id)
    {
        return H::combine(std::move(h), id.offset, id.length);
    }

    constexpr bool operator ==(const DirectoryIdentity& other) const = default;
};

struct DirectoryEntry
{
    uint64_t tile_id{};
    uint64_t offset{};
    uint32_t length{};
    uint32_t run_length{};
};

struct Directory
{
    // Used for the LRU.
    Directory* prev{};
    Directory* next{};

    enum Status
    {
        kError = -1,
        kLoading,
        kReady,
    } status = kLoading;

    uint64_t asset_request_id{};
    std::vector<DirectoryEntry> entries;

    DirectoryIdentity id{};
    int ref_count = 0;

    Directory(const DirectoryIdentity& id, uint64_t asset_request_id) :
        asset_request_id(asset_request_id), id{id}
    {
    }

    Directory(const DirectoryIdentity& id, std::vector<DirectoryEntry>&& entries) :
        status(kReady), entries(std::move(entries)), id{id}
    {
    }

    std::optional<DirectoryEntry> find(uint64_t tile_id) const
    {
        if (entries.size() == 0)
        {
            return std::nullopt;
        }

        auto it = std::ranges::upper_bound(
            entries, tile_id, std::less<uint64_t>{},
            [](const DirectoryEntry& entry) { return entry.tile_id; });

        if (it == std::begin(entries))
        {
            return std::nullopt;
        }

        it -= 1;

        if (it->run_length > 0 && tile_id >= it->tile_id + it->run_length)
        {
            return std::nullopt;
        }

        return *it;
    }

    std::optional<DirectoryEntry> find(const TileCoords& tile) const
    {
        return find(tile_coords_to_tile_id(tile));
    }
};

class DirectoriesManager
{
    LruList<Directory> _cache;
    hrz::flat_hash_map<DirectoryIdentity, Directory*> _index;

public:
    template<typename... Args>
    Directory* acquire_ref(const DirectoryIdentity& id, Args&&... args)
    {
        Directory* dir = _cache.emplace_front(id, std::forward<Args>(args)...);
        _index[id] = dir;
        dir->ref_count = 1;
        return dir;
    }

    Directory* get_ref(const DirectoryIdentity& id)
    {
        auto it = _index.find(id);
        if (it == _index.end())
        {
            return nullptr;
        }

        it->second->ref_count += 1;
        _cache.touch(it->second);
        return it->second;
    }

    void unref(Directory* dir)
    {
        dir->ref_count -= 1;
        assert(dir->ref_count >= 0);
        if (dir->ref_count == 0)
        {
            _cache.move_to_back(dir);
        }
    }

    void ref(Directory* dir)
    {
        dir->ref_count += 1;
        _cache.touch(dir);
    }

    void clean_unused_directories(assets_loader::Channel& asset_loader_channel)
    {
        while (!_cache.empty())
        {
            auto* it = _cache.back();
            if (it->ref_count == 0)
            {
                if (it->status == Directory::kLoading)
                {
                    asset_loader_channel.send(
                        assets_loader::messages::CancelRequest{it->asset_request_id});
                }
                _cache.pop_back();
                _index.erase(it->id);
            }
            else
            {
                break;
            }
        }
    }

    void destroy(assets_loader::Channel& asset_loader_channel)
    {
        while (!_cache.empty())
        {
            auto* it = _cache.back();
            if (it->status == Directory::kLoading)
            {
                asset_loader_channel.send(
                    assets_loader::messages::CancelRequest{it->asset_request_id});
            }
            _cache.pop_back();
        }
        _index.clear();
    }
};

std::optional<std::vector<std::byte>> decompress(
    std::span<const std::byte> compressed,
    Compression compression)
{
    std::vector<std::byte> decompressed;
    auto callback = [&decompressed](std::span<const std::byte> chunk)
    { decompressed.insert(decompressed.end(), chunk.begin(), chunk.end()); };

    switch (compression)
    {
        case Compression::kGzip:
        {
            if (!decompress_gzip(compressed, callback))
            {
                return std::nullopt;
            }
            break;
        }
        case Compression::kBrotli:
        {
            if (!decompress_brotli(compressed, callback))
            {
                return std::nullopt;
            }
            break;
        }
        case Compression::kZstd:
        {
            if (!decompress_zstd(compressed, callback))
            {
                return std::nullopt;
            }
            break;
        }
        default: assert(false && "We shouldn't be here"); break;
    }
    return decompressed;
}

} // namespace

class PmTilesImpl : public PmTiles
{
    std::string _url;
    HttpHeaders _headers;
    assets_loader::Queue _queue;
    assets_loader::Channel _asset_loader_channel;

    uint64_t _last_frame_work = 0;
    Status _status = kInitial;

    Compression _tile_compression = Compression::kUnknown;
    Compression _internal_compression = Compression::kUnknown;
    uint64_t _leaf_directories_offset = 0;
    uint64_t _tile_data_offset = 0;

    uint8_t _lod_min = 0;
    uint8_t _lod_max = 0;
    hrz::GeoBounds _bounds;
    std::optional<std::string> _attribution;

    Directory* _root_directory{};
    DirectoriesManager _directories_manager;

    static constexpr uint64_t kHeaderAssetRequestId = 0;
    uint64_t _next_asset_request_id = 1;

#if HRZ_DEBUG
    size_t _tile_queries_count = 0;
    bool _has_been_destroyed = false;
#endif

    static constexpr double kDirectoriesCleanupInterval = 10.0;
    double _last_directories_cleanup_s = 0.0;

    struct TileQuery
    {
        TileQuery(
            uint64_t asset_request_id,
            const TileCoords& tile_coords,
            Directory* root_directory,
            assets_loader::Queue queue,
            uint32_t priority,
            const monitoring::ResourceOwner& owner) :
            asset_request_id(asset_request_id),
            tile_coords(tile_coords),
            queue(queue),
            priority(priority),
            owner(owner),
            directory(root_directory)
        {
        }

        enum Status
        {
            kError = -1,
            kLoadingDirectory = 0,
            kLoadingTileData,
            kDecompressTileData,
            kDone,
        } status = kLoadingDirectory;

        uint64_t asset_request_id;
        TileCoords tile_coords;
        assets_loader::Queue queue;
        uint32_t priority;
        monitoring::ResourceOwner owner;
        Directory* directory{};

        std::variant<std::monostate, hrz_jobs::DecompressBlobTicket, blobs::BlobHandle> tile_data;
    };

    using QueryHandle = uint64_t;
    using QueryIndexPool = hrz::GenIndexPool<QueryHandle, 32, 32>;
    using QueryPool = hrz::GenObjectPool<TileQuery, QueryIndexPool>;

    QueryPool _query_pool;
    hrz::flat_hash_set<QueryHandle> _loading_queries;

    hrz::flat_hash_map<uint64_t, std::variant<QueryHandle, Directory*>>
        _asset_request_ids_to_loading_objects;

public:
    PmTilesImpl(
        std::string_view url,
        const HttpHeaders& headers,
        assets_loader::Queue queue,
        assets_loader::Channel asset_loader_channel) :
        _url(url),
        _headers(headers),
        _queue(queue),
        _asset_loader_channel(std::move(asset_loader_channel))
    {
    }

    Status get_status() const override { return _status; }

    static hrz::GeoPosition2 decode_position(uint64_t encoded)
    {
        auto lon = (int32_t)(encoded & 0xFFFFFFFF);
        auto lat = (int32_t)(encoded >> 32);
        return {lm::radians(lat / 10'000'000.0), lm::radians(lon / 10'000'000.0)};
    }

    void handle_metadata(std::span<const std::byte> metadata)
    {
        rapidjson::Document doc;
        doc.Parse((const char*)metadata.data(), metadata.size_bytes());

        if (doc.HasParseError())
        {
            HRZ_LOG_ERROR("Couldn't parse PMTiles metadata at {}", _url);
            _status = kError;
            return;
        }

        if (auto attribution = json::get_str(doc, "attribution"); attribution.has_value())
        {
            _attribution = std::string(attribution.value());
        }

        _status = kReady;
    }

    void handle_raw_metadata(std::span<const std::byte> metadata_data)
    {
        if (_internal_compression != Compression::kNone)
        {
            auto decompressed = decompress(metadata_data, _internal_compression);
            if (!decompressed)
            {
                HRZ_LOG_ERROR("Couldn't decompress PMTiles metadata at {}", _url);
                _status = kError;
                return;
            }

            handle_metadata(decompressed.value());
        }
        else
        {
            handle_metadata(metadata_data);
        }
    }

    std::optional<std::vector<DirectoryEntry>> decode_dir_entries(
        std::span<const std::byte> dir_data)
    {
        const auto* it = dir_data.data();
        const auto* end = dir_data.data() + dir_data.size_bytes();
        auto remaining_size = [&]() -> uint64_t { return end - it; };

        uint64_t entry_count = decode_varint_u64(&it, end);

        // Each directory entry takes at least 4 bytes, to check that we have enough data for the
        // minimal case.
        if (remaining_size() < entry_count * 4)
        {
            HRZ_LOG_ERROR("PMTiles directory at {} is too small", _url);
            return std::nullopt;
        }

        std::vector<DirectoryEntry> entries(entry_count);

        uint64_t last_tile_id = 0;
        for (uint64_t i = 0; i < entry_count && it < end; ++i)
        {
            last_tile_id += decode_varint_u64(&it, end);
            entries[i].tile_id = last_tile_id;
        }

        if (it == end)
        {
            HRZ_LOG_ERROR("PMTiles directory at {} is too small", _url);
            return std::nullopt;
        }

        for (uint64_t i = 0; i < entry_count && it < end; ++i)
        {
            entries[i].run_length = (uint32_t)decode_varint_u64(&it, end);
        }

        if (it == end)
        {
            HRZ_LOG_ERROR("PMTiles directory at {} is too small", _url);
            return std::nullopt;
        }

        for (uint64_t i = 0; i < entry_count && it < end; ++i)
        {
            entries[i].length = (uint32_t)decode_varint_u64(&it, end);
        }

        if (it == end)
        {
            HRZ_LOG_ERROR("PMTiles directory at {} is too small", _url);
            return std::nullopt;
        }

        for (uint64_t i = 0; i < entry_count && it < end; ++i)
        {
            entries[i].offset = decode_varint_u64(&it, end);
            if (entries[i].offset == 0 && i > 0)
            {
                entries[i].offset = entries[i - 1].offset + entries[i - 1].length;
            }
            else if (entries[i].offset > 0)
            {
                entries[i].offset -= 1;
            }
            else
            {
                HRZ_LOG_ERROR("PMTiles directory at {} has invalid offset", _url);
                return std::nullopt;
            }
        }

        if (it != end)
        {
            HRZ_LOG_ERROR("PMTiles directory at {} is too large", _url);
            return std::nullopt;
        }

        return entries;
    }

    std::optional<std::vector<DirectoryEntry>> decode_raw_dir_entries(
        std::span<const std::byte> raw_dir_data)
    {
        if (_internal_compression != Compression::kNone)
        {
            auto decompressed = decompress(raw_dir_data, _internal_compression);
            if (!decompressed)
            {
                HRZ_LOG_ERROR("Couldn't decompress PMTiles directory at {}", _url);
                return std::nullopt;
            }

            return decode_dir_entries(decompressed.value());
        }
        else
        {
            return decode_dir_entries(raw_dir_data);
        }
    }

    void handle_initial_data(std::span<const std::byte> raw_data)
    {
        if (raw_data.size_bytes() < sizeof(RawHeader))
        {
            HRZ_LOG_ERROR("PMTiles resource at {} is too small", _url);
            _status = kError;
            return;
        }

        RawHeader header{};
        memcpy(&header, raw_data.data(), sizeof(RawHeader));

        if (memcmp(&header.magic, "PMTiles", 7) != 0)
        {
            HRZ_LOG_ERROR("PMTiles resource at {} has invalid magic", _url);
            _status = kError;
            return;
        }

        if (header.version != 3)
        {
            HRZ_LOG_ERROR("PMTiles resource at {} has invalid version {}", _url, header.version);
            _status = kError;
            return;
        }

        if (header.tile_compression == Compression::kUnknown
            || header.tile_compression >= Compression::kCompressionInvalid)
        {
            HRZ_LOG_ERROR(
                "PMTiles resource at {} has invalid tile compression {}", _url,
                static_cast<int>(header.tile_compression));
            _status = kError;
            return;
        }
        _tile_compression = header.tile_compression;

        if (header.internal_compression == Compression::kUnknown
            || header.internal_compression >= Compression::kCompressionInvalid)
        {
            HRZ_LOG_ERROR(
                "PMTiles resource at {} has invalid internal compression {}", _url,
                static_cast<int>(header.internal_compression));
            _status = kError;
            return;
        }
        _internal_compression = header.internal_compression;

        _tile_data_offset = header.tile_data_offset;
        _leaf_directories_offset = header.leaf_dirs_offset;

        _lod_min = header.min_zoom;
        _lod_max = header.max_zoom;

        _bounds = hrz::GeoBounds{
            decode_position(header.min_position_raw), decode_position(header.max_position_raw)
        };

        if (header.root_dir_offset + header.root_dir_length > raw_data.size_bytes())
        {
            HRZ_LOG_ERROR("PMTiles resource at {} has invalid root directory location", _url);
            _status = kError;
            return;
        }

        auto root_dir_data = raw_data.subspan(header.root_dir_offset, header.root_dir_length);
        if (auto dir = decode_raw_dir_entries(root_dir_data); dir.has_value())
        {
            _root_directory = _directories_manager.acquire_ref(
                DirectoryIdentity{header.root_dir_offset, header.root_dir_length},
                std::move(dir).value());
        }
        else
        {
            HRZ_LOG_ERROR("Couldn't decode PMTiles root directory at {}", _url);
            _status = kError;
            return;
        }

        // The metadata might already be in the downloaded data!
        if (header.metadata_offset + header.metadata_length <= raw_data.size_bytes())
        {
            auto metadata_data = raw_data.subspan(header.metadata_offset, header.metadata_length);
            handle_raw_metadata(metadata_data);
        }
        else
        {
            _asset_loader_channel.send(
                assets_loader::messages::LoadRequest{
                    kHeaderAssetRequestId,
                    _url,
                    header.metadata_offset,
                    header.metadata_length,
                    _headers,
                    _queue,
                    0,
                    {}
                });
            _status = kLoadingMetadata;
        }
    }

    void on_directory_data(Directory* dir, const blobs::BlobHandle& data)
    {
        if (dir->status != Directory::kLoading)
        {
            HRZ_LOG_ERROR("Unexpected data for directory {}:{}", dir->id.offset, dir->id.length);
            return;
        }

        auto raw_dir_data = data.get_data();
        if (auto entries = decode_raw_dir_entries(raw_dir_data); entries.has_value())
        {
            dir->entries = std::move(entries.value());
            dir->status = Directory::kReady;
        }
        else
        {
            HRZ_LOG_ERROR("Couldn't decode PMTiles directory at {}", _url);
            dir->status = Directory::kError;
        }
    }

    void on_query_data(
        QueryHandle handle,
        TileQuery* query,
        const blobs::BlobHandle& data,
        JobScheduler* js)
    {
        if (query->status != TileQuery::kLoadingTileData)
        {
            HRZ_LOG_ERROR("Unexpected data for query {}", handle);
            return;
        }

        if (auto compression = convert_compression_type(_tile_compression); compression.has_value())
        {
            hrz_jobs::DecompressBlobParams params;
            params.compressed = data;
            params.type = compression.value();

            query->tile_data = hrz_jobs::add_job_decompress_blob(js, std::move(params), {});
            query->status = TileQuery::kDecompressTileData;
        }
        else
        {
            query->tile_data = data;
            query->status = TileQuery::kDone;

            _loading_queries.erase(handle);
        }
    }

    void on_query_data_error(QueryHandle handle, TileQuery* query)
    {
        if (query->status != TileQuery::kLoadingTileData)
        {
            HRZ_LOG_ERROR("Unexpected data for query {}", handle);
            return;
        }

        HRZ_LOG_ERROR("Couldn't load PMTiles tile {} at {}", query->tile_coords, _url);
        query->tile_data = std::monostate{};
        query->status = TileQuery::kError;
    }

    void work_query(QueryHandle handle, TileQuery* query, JobScheduler* js, BlobAllocator* ba)
    {
        assert(
            query->status == TileQuery::kLoadingTileData
            || query->status == TileQuery::kLoadingDirectory
            || query->status == TileQuery::kDecompressTileData);

        while (query->status == TileQuery::kLoadingDirectory && query->directory)
        {
            switch (query->directory->status)
            {
                case Directory::kError:
                {
                    query->status = TileQuery::kError;
                    return;
                }
                case Directory::kLoading: return;
                default: break;
            }

            DirectoryEntry entry;
            if (auto entry_opt = query->directory->find(query->tile_coords); entry_opt)
            {
                entry = *entry_opt;
            }
            else
            {
                HRZ_LOG_ERROR("Couldn't find tile {} in PMTiles {}", query->tile_coords, _url);
                query->status = TileQuery::kError;
                return;
            }

            if (entry.run_length > 0)
            {
                _asset_loader_channel.send(
                    assets_loader::messages::LoadRequest{
                        query->asset_request_id, _url, _tile_data_offset + entry.offset,
                        entry.length, _headers, query->queue, query->priority, query->owner
                    });
                _asset_request_ids_to_loading_objects.insert({query->asset_request_id, {handle}});

                _directories_manager.unref(query->directory);
                query->directory = nullptr;

                query->status = TileQuery::kLoadingTileData;
            }
            else
            {
                _directories_manager.unref(query->directory);

                DirectoryIdentity id{_leaf_directories_offset + entry.offset, entry.length};
                query->directory = _directories_manager.get_ref(id);

                if (!query->directory)
                {
                    uint64_t directory_request_id = _next_asset_request_id++;
                    _asset_loader_channel.send(
                        assets_loader::messages::LoadRequest{
                            directory_request_id,
                            _url,
                            id.offset,
                            id.length,
                            _headers,
                            _queue,
                            0,
                            {}
                        });
                    query->directory = _directories_manager.acquire_ref(id, directory_request_id);
                    _asset_request_ids_to_loading_objects.insert(
                        {directory_request_id, {query->directory}});
                }

                assert(query->directory != nullptr);
            }
        }

        if (query->status == TileQuery::kDecompressTileData)
        {
            assert(std::holds_alternative<hrz_jobs::DecompressBlobTicket>(query->tile_data));

            auto ticket = std::get<hrz_jobs::DecompressBlobTicket>(query->tile_data);
            if (hrz_jobs::is_job_finished(js, ticket))
            {
                if (hrz_jobs::get_job_status(js, ticket)
                    == hrz::job_scheduler::JobStatus::Finished_Success)
                {
                    auto response_blob = hrz_jobs::get_job_response(js, ticket);
                    query->tile_data = response_blob;
                    query->status = TileQuery::kDone;
                }
                else
                {
                    HRZ_LOG_ERROR(
                        "Couldn't decompress PMTiles tile {} at {}", query->tile_coords, _url);
                    query->tile_data = std::monostate{};
                    query->status = TileQuery::kError;
                }
            }
        }
    }

    void work(JobScheduler* js, BlobAllocator* ba) override
    {
        if (_last_frame_work >= hrz::clock::CurrentFrameNumber) return;
        _last_frame_work = hrz::clock::CurrentFrameNumber;

        if (_status == kError)
        {
            return;
        }

        for (auto& generic_message : _asset_loader_channel.receive())
        {
            std::visit(
                hrz::overload{
                    [this, js](const assets_loader::messages::LoadedData& message)
                    {
                        if (message.request_id == kHeaderAssetRequestId)
                        {
                            if (_status == kLoadingInitial)
                            {
                                handle_initial_data(message.data.get_data());
                            }
                            else if (_status == kLoadingMetadata)
                            {
                                handle_raw_metadata(message.data.get_data());
                            }
                            else
                            {
                                assert(false && "Unexpected data");
                            }
                        }
                        else
                        {
                            auto it =
                                _asset_request_ids_to_loading_objects.find(message.request_id);
                            if (it != _asset_request_ids_to_loading_objects.end())
                            {
                                if (std::holds_alternative<QueryHandle>(it->second))
                                {
                                    auto handle = std::get<QueryHandle>(it->second);
                                    auto query = _query_pool.get_object(handle);
                                    if (query != nullptr)
                                    {
                                        on_query_data(handle, query, message.data, js);
                                    }
                                    else
                                    {
                                        HRZ_LOG_ERROR(
                                            "No query for asset query ID: {}", message.request_id);
                                    }
                                }
                                else if (std::holds_alternative<Directory*>(it->second))
                                {
                                    auto dir = std::get<Directory*>(it->second);
                                    on_directory_data(dir, message.data);
                                }
                                else
                                {
                                    assert(false && "Unhandled case");
                                }

                                _asset_request_ids_to_loading_objects.erase(it);
                            }
                            else
                            {
                                HRZ_LOG_ERROR("Unknown request ID: {}", message.request_id);
                            }
                        }
                    },
                    [this](const assets_loader::messages::LoadFailure& message)
                    {
                        if (message.request_id == kHeaderAssetRequestId)
                        {
                            HRZ_LOG_ERROR("Couldn't load PMTiles resource at {}", _url);
                            _status = kError;
                        }
                        else
                        {
                            auto it =
                                _asset_request_ids_to_loading_objects.find(message.request_id);
                            if (it != _asset_request_ids_to_loading_objects.end())
                            {
                                if (std::holds_alternative<QueryHandle>(it->second))
                                {
                                    auto handle = std::get<QueryHandle>(it->second);
                                    auto query = _query_pool.get_object(handle);
                                    if (query != nullptr)
                                    {
                                        on_query_data_error(handle, query);
                                    }
                                    else
                                    {
                                        HRZ_LOG_ERROR(
                                            "No query for asset query ID: {}", message.request_id);
                                    }
                                }
                                else if (std::holds_alternative<Directory*>(it->second))
                                {
                                    auto dir = std::get<Directory*>(it->second);
                                    dir->status = Directory::kError;
                                }
                                else
                                {
                                    assert(false && "Unhandled case");
                                }

                                _asset_request_ids_to_loading_objects.erase(it);
                            }
                            else
                            {
                                HRZ_LOG_ERROR("Unknown request ID: {}", message.request_id);
                            }
                        }
                    },
                    [](const assets_loader::messages::NewChannel&)
                    {
                        // No-op
                    }
                },
                generic_message);
        }

        if (_status == kInitial)
        {
            _asset_loader_channel.send(
                assets_loader::messages::LoadRequest{
                    kHeaderAssetRequestId,
                    _url,
                    0,
                    kInitialDownloadSize,
                    _headers,
                    _queue,
                    0,
                    {}
                });
            _status = kLoadingInitial;
        }

        if (_status != kReady) return;

        for (auto it = _loading_queries.begin(); it != _loading_queries.end();)
        {
            auto handle = *it;
            auto* query = _query_pool.get_object(handle);
            assert(
                query
                && (query->status == TileQuery::kLoadingTileData
                    || query->status == TileQuery::kLoadingDirectory
                    || query->status == TileQuery::kDecompressTileData));

            work_query(handle, query, js, ba);

            if (query->status != TileQuery::kLoadingDirectory
                && query->status != TileQuery::kLoadingTileData
                && query->status != TileQuery::kDecompressTileData)
            {
                _loading_queries.erase(it++);
            }
            else
            {
                ++it;
            }
        }

        const double now_s = hrz::clock::CurrentFrameWallTime.s;
        if (now_s - _last_directories_cleanup_s > kDirectoriesCleanupInterval)
        {
            _directories_manager.clean_unused_directories(_asset_loader_channel);
            _last_directories_cleanup_s = now_s;
        }
    }

    QueryHandle request_tile(
        TileCoords tile_coords,
        assets_loader::Queue queue,
        uint32_t priority,
        const monitoring::ResourceOwner& owner) override
    {
        auto handle = _query_pool.alloc(
            _next_asset_request_id++, tile_coords, _root_directory, queue, priority, owner);
        _directories_manager.ref(_root_directory);
        _loading_queries.insert(handle);
#if HRZ_DEBUG
        _tile_queries_count += 1;
#endif
        return handle;
    }

    bool is_finished(QueryHandle handle) const override
    {
        auto query = _query_pool.get_object(handle);
        return !query
            || (query->status != TileQuery::kLoadingDirectory
                && query->status != TileQuery::kLoadingTileData
                && query->status != TileQuery::kDecompressTileData);
    }

    bool is_success(QueryHandle handle) const override
    {
        auto query = _query_pool.get_object(handle);
        return query && query->status == TileQuery::kDone;
    }

    blobs::BlobHandle retrieve_blob(QueryHandle handle) const override
    {
        auto query = _query_pool.get_object(handle);
        if (!query || query->status != TileQuery::kDone)
        {
            return blobs::BlobHandle{};
        }
        return std::get<blobs::BlobHandle>(query->tile_data);
    }

    void cancel(QueryHandle handle, JobScheduler* js) override
    {
        auto query = _query_pool.get_object(handle);
        if (!query) return;

        if (query->status == TileQuery::kLoadingDirectory)
        {
            _loading_queries.erase(handle);
        }
        else if (query->status == TileQuery::kLoadingTileData)
        {
            _asset_loader_channel.send(
                assets_loader::messages::CancelRequest{query->asset_request_id});
            _loading_queries.erase(handle);
        }
        else if (query->status == TileQuery::kDecompressTileData)
        {
            auto ticket = std::get<hrz_jobs::DecompressBlobTicket>(query->tile_data);
            hrz_jobs::cancel_job(js, ticket);
            _loading_queries.erase(handle);
        }

        _query_pool.release(handle);

#if HRZ_DEBUG
        assert(_tile_queries_count > 0);
        _tile_queries_count -= 1;
#endif
    }

    std::string_view get_attribution() const override
    {
        return _attribution.has_value() ? std::string_view(_attribution.value())
                                        : std::string_view{};
    }

    hrz::planet::TiledRasterGeometry get_geometry() const override
    {
        hrz::planet::TiledRasterGeometry geometry;

        auto& projection = geometry.projection;
        projection.set_descriptor_type(hrz_proto::SrsDescriptorType::PROJ4_STRING_DESCRIPTOR);
        projection.set_descriptor_(hrz_proj::wmerc_proj_str);

        auto tiling = geometry.tiling_scheme.mutable_global_tiling();
        tiling->set_tile_size(256); // @Todo Not sure how to handle this
        tiling->set_level_zero_tile_count_x(1);
        tiling->set_level_zero_tile_count_y(1);
        tiling->set_min_level(_lod_min);
        tiling->set_max_level(_lod_max);

        tiling->set_border_tile_aspect(hrz_proto::BorderTileAspect::FULL_SIZED);

        auto web_mercator_bounds = lm::dbbox2{
            hrz::geo_to_web_mercator(GeoPosition2{_bounds.south, _bounds.west}),
            hrz::geo_to_web_mercator(GeoPosition2{_bounds.north, _bounds.east})
        };

        web_mercator_bounds.min.y =
            std::max(web_mercator_bounds.min.y, -hrz::MERCATOR_MAX_LAT_METERS);
        web_mercator_bounds.max.y =
            std::min(web_mercator_bounds.max.y, hrz::MERCATOR_MAX_LAT_METERS);

        geometry.bounds = web_mercator_bounds;

        return geometry;
    }

    bool set_http_headers(const HttpHeaders& http_headers) override
    {
        auto old = std::exchange(_headers, http_headers);
        return old.hash_content() != _headers.hash_content();
    }

    void destroy() override
    {
#if HRZ_DEBUG
        assert(!_has_been_destroyed);
        assert(_tile_queries_count == 0);
        _has_been_destroyed = true;
#endif
        // All queries should have been cancelled externally already.

        if (_status == kLoadingInitial || _status == kLoadingMetadata)
        {
            _asset_loader_channel.send(
                assets_loader::messages::CancelRequest{kHeaderAssetRequestId});
        }

        if (_root_directory)
        {
            _directories_manager.unref(_root_directory);
            _root_directory = nullptr;
        }

        _directories_manager.destroy(_asset_loader_channel);
    }

#if HRZ_DEBUG
    ~PmTilesImpl() override { assert(_has_been_destroyed); }
#endif
};

std::unique_ptr<PmTiles> PmTiles::create(
    std::string_view url,
    const HttpHeaders& headers,
    assets_loader::Queue queue,
    assets_loader::Channel&& asset_loader_channel)
{
    return std::make_unique<PmTilesImpl>(url, headers, queue, std::move(asset_loader_channel));
}

} // namespace hrz
