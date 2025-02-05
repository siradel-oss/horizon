#pragma once

#include "assets_loader/hrz_core_assets_loader.h"

#include <hrz_common_tile_coords.h>
#include <hrz_fnd_class.h>

namespace hrz
{
struct AttributionRegistry;
struct JobScheduler;

class PmTiles
{
public:
    using QueryHandle = uint64_t;

    enum Status
    {
        kError = -1,
        kInitial = 0,
        kLoadingInitial,
        kLoadingMetadata,
        kReady,
    };

    PmTiles() = default;
    HRZ_DELETE_COPY_MOVE(PmTiles);
    virtual ~PmTiles() = default;

    // This can be called any number of tiles during the same frame.
    // Those calls are deduplicated.
    // This is done to make integration easier with systems that don't have
    // one central place to do work, like the vector data loader.
    virtual void work(JobScheduler*, BlobAllocator*) = 0;

    virtual void destroy() = 0;

    // Returns whether this header change also changes content negotiation.
    virtual bool set_http_headers(const HttpHeaders& http_headers) = 0;

    virtual QueryHandle request_tile(
        TileCoords,
        assets_loader::Queue,
        uint32_t priority,
        const monitoring::ResourceOwner&) = 0;

    virtual void cancel(QueryHandle, JobScheduler*) = 0;

    virtual blobs::BlobHandle retrieve_blob(QueryHandle) const = 0;

    virtual Status get_status() const = 0;

    virtual bool is_finished(QueryHandle) const = 0;
    virtual bool is_success(QueryHandle) const = 0;

    virtual std::string_view get_attribution() const = 0;
    virtual hrz_proto::RasterGeometry get_geometry() const = 0;

    static std::unique_ptr<PmTiles> create(
        std::string_view url,
        const HttpHeaders& headers,
        assets_loader::Queue queue,
        assets_loader::Channel&& asset_loader_channel);
};

} // namespace hrz
