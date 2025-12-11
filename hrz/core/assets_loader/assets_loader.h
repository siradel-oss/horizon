#pragma once

#include "hrz/common/blob_allocator.h"
#include "hrz/core/channel.h"
#include "hrz/fnd/defines.h"
#include "hrz/fnd/http.h"

#include <cstdint>
#include <string_view>
#include <variant>

namespace HrzProtocol
{
class HttpHeaderList;
class AssetRequestResponse;
}; // namespace HrzProtocol

namespace hrz
{
struct AssetsLoader;
struct ClientMessageQueue;

namespace assets_loader
{
HttpHeaders from_proto(const HrzProtocol::HttpHeaderList&);

enum class RequestStatus
{
    Queued,
    Loading,
    Loaded,
    Canceled,
    Error,
};

enum Queue
{
    Early = 0,
    Late,
    Dtm,
    ImageryTop,
    ImageryMiddle,
    ImageryBottom,
    VectorData,
    ThreeDTiles,
    MeshModels,
    Default,

    Queue_Count,
};

using Ticket = uint64_t;

static constexpr uint32_t MAX_PRIORITY = (1 << 24) - 1;

#if HRZ_DESKTOP
AssetsLoader* create(const char* user_agent, const char* http_referrer, size_t http_cache_size);
#else
AssetsLoader* create();
#endif

void destroy(AssetsLoader*);

/**
 * Starts a request for the asset at the given URL.
 *
 * A `queue` can be assigned see `assets_loader::Queue`.
 * A `priority` can be provided to internally sort requests in the same queue. The greater
 * the priority the sooner the request will happen.
 *
 * The priority value must be less than or equal to `MAX_PRIORITY`.
 */
Ticket begin(
    AssetsLoader*,
    std::string_view url,
    const HttpHeaders&,
    Queue queue = Queue::Default,
    uint32_t priority = 0,
    monitoring::ResourceOwner resource_owner = {});

/**
 * Starts a request for the asset at the given URL and byte range.
 *
 * A `range_size` of `0` means that the range reaches the end of the file.
 * A `queue` can be assigned see `assets_loader::Queue`.
 * A `priority` can be provided to internally sort requests in the same queue. The greater
 * the priority the sooner the request will happen.
 *
 * The priority value must be less than or equal to `MAX_PRIORITY`.
 */
Ticket begin(
    AssetsLoader*,
    std::string_view url,
    uint64_t range_start,
    uint64_t range_size,
    const HttpHeaders&,
    Queue queue = Queue::Default,
    uint32_t priority = 0,
    monitoring::ResourceOwner resource_owner = {});

/**
 * Creates a new ticket for loading the same data, with a new priority.
 * The ticket passed as parameter becomes invalid.
 *
 * The priority value must be less than or equal to `MAX_PRIORITY`.
 */
Ticket reset_priority(AssetsLoader*, Ticket, uint32_t priority);

/**
 * Ends the request for the given ticket. If the data has already been
 * loaded, free it.
 * Note that it is necessary to call this method to free memory.
 */
void end(AssetsLoader*, Ticket);

/**
 * Returns true if the ticket exists in the loader.
 */
bool is_valid(const AssetsLoader*, Ticket);

/**
 * Returns whether the data of the request designated by the given
 * ticket is ready to be retrieved.
 * This does not mean that the transfer was OK.
 * Use `get_status` to get the actual status of the request.
 */
bool is_finished(const AssetsLoader*, Ticket);

/**
 * Returns the URL of the request
 */
std::string_view get_url(const AssetsLoader*, Ticket);

/**
 * Returns the current status of the request.
 */
RequestStatus get_status(const AssetsLoader*, Ticket);

/**
 * Returns the MIME type of the request's data.
 * This is the value returned by the server, the data is not analysed.
 */
std::string_view get_content_type(const AssetsLoader*, Ticket);

/**
 * Returns the blob for a given ticket. `is_loaded` should be called
 * first to ensure the blob is ready.
 * The blob can only be retrieved once.
 */
blobs::BlobHandle get_blob(AssetsLoader*, BlobAllocator*, Ticket);

/**
 * Updates the internal states of the assets loader. This allows it to actually
 * do work. It should be called regularly.
 */
void work(AssetsLoader*, BlobAllocator*, ClientMessageQueue*);

void clear_http_cache(AssetsLoader*);

void provide_client_asset_data(
    AssetsLoader*,
    BlobAllocator*,
    const HrzProtocol::AssetRequestResponse&);

namespace messages
{
struct LoadRequest
{
    uint64_t request_id;
    std::string url;
    uint64_t range_start = 0;
    uint64_t range_size = 0;
    HttpHeaders headers;
    Queue queue = Queue::Default;
    uint32_t priority = 0;
    monitoring::ResourceOwner resource_owner = {};
};

struct ResetRequestPriority
{
    uint64_t request_id;
    uint32_t priority;
};

struct CancelRequest
{
    uint64_t request_id;
};

struct CreateChannel
{
    uint64_t request_id;
};
} // namespace messages

using ToLoaderMessages = std::variant<
    messages::LoadRequest,
    messages::ResetRequestPriority,
    messages::CancelRequest,
    messages::CreateChannel>;

namespace messages
{
struct LoadedData
{
    uint64_t request_id;
    blobs::BlobHandle data;
    std::string content_type;
};

struct LoadFailure
{
    uint64_t request_id;
};

struct NewChannel;
} // namespace messages

using FromLoaderMessages =
    std::variant<messages::LoadedData, messages::LoadFailure, messages::NewChannel>;

using Channel = hrz::Channel<ToLoaderMessages, FromLoaderMessages>;

namespace messages
{
struct NewChannel
{
    uint64_t request_id;
    Channel channel;
};
} // namespace messages

Channel create_channel(AssetsLoader*);
} // namespace assets_loader
} // namespace hrz
