#include "assets_loader/hrz_core_assets_loader.h"

#include "assets_loader/hrz_core_assets_loader_http_platform.h"
#include "hrz_core_channel_group.h"
#include "hrz_core_client_message_queue.h"
#include "hrz_core_client_messages.h"
#include "hrz_core_platform.h"

#include <hrz_common_ui_utils.h>
#include <hrz_fnd_flat_hash_map.h>
#include <hrz_fnd_flat_hash_set.h>
#include <hrz_fnd_format.h>
#include <hrz_fnd_inlined_vector.h>
#include <hrz_fnd_meta.h>
#include <hrz_fnd_string_utils.h>

#include <array>
#include <deque>
#include <mutex>
#include <queue>

extern "C"
{
#include <microui/microui.h>
}

#include <hrz_common_metrics.h>
#include <hrz_common_profiling.h>
#include <hrz_fnd_gen_object_pool.h>
#include <hrz_fnd_log.h>
#include <hrz_fnd_thread.h>

namespace
{
using namespace hrz;

union InnerTicket
{
    assets_loader::Ticket ticket;
    HttpTicket http_ticket;

    struct
    {
        uint32_t handle : 32;
        uint32_t queue : 8;
        uint32_t priority : 24;
    };

    bool operator==(const InnerTicket& other) const { return ticket == other.ticket; }
};

static_assert(
    sizeof(InnerTicket) == sizeof(assets_loader::Ticket)
        && sizeof(InnerTicket) == sizeof(HttpTicket),
    "Too many bits");

struct ChannelRequestId
{
    uint64_t channel_id;
    uint64_t request_id;

    bool operator==(const ChannelRequestId& other) const
    {
        return other.channel_id == channel_id && other.request_id == request_id;
    }

    template<typename H>
    friend H AbslHashValue(H h, const ChannelRequestId& request)
    {
        return H::combine(std::move(h), request.channel_id, request.request_id);
    }
};

struct Request
{
    assets_loader::RequestStatus status;
    std::string url;
    uint64_t range_start;
    uint64_t range_size;
    blobs::AllocationTicket blob_ticket;
    blobs::BlobHandle blob;
    std::optional<blobs::MutableBlobData> blob_data;
    size_t history_index;
    monitoring::ResourceOwner resource_owner;
    std::optional<ChannelRequestId> channel_request_id;

    struct HttpData
    {
        HttpHeaders headers;
    };

    struct ClientData
    {
        std::string provided_bytes;
    };

    using ProtocolData = std::variant<HttpData, ClientData>;
    ProtocolData protocol_data;
    std::string content_type;

    const HttpData& http_request() const { return std::get<Request::HttpData>(protocol_data); }

    HttpData& http_request() { return std::get<Request::HttpData>(protocol_data); }

    bool is_http_request() const
    {
        return protocol_data.index() == hrz::index_of_variant<ProtocolData, HttpData>();
    }

    const ClientData& client_request() const
    {
        return std::get<Request::ClientData>(protocol_data);
    }

    ClientData& client_request() { return std::get<Request::ClientData>(protocol_data); }

    bool is_client_request() const
    {
        return protocol_data.index() == hrz::index_of_variant<ProtocolData, ClientData>();
    }
};

} // namespace

/** AssetsLoader.
 *
 * When loading assets the user can prioritize requests in two ways.
 * First, a high level category (queue) can be provided, such categories are listed in the enum
 * `assets_loader::Queue`. Then, a priority can be provided to allow fine-grained control over the
 * ordering within a queue. The higher the priority value, the sooner the data will be downloaded.
 *
 * The assets loader works on a multi-level queues scheme, one queue represents a priority level.
 * Then, scheduling works in a Round-Robin fashion with a number of requests pre-allocated for each
 * queue. Higher level queues will be able to make more consecutive requests than lower level
 * queues. Internally, this mechanism works by using a 'slot table'. In this table are all the slots
 * (pre- allocated requests) for every queue. Every slot is iterated over and filled with a request
 * from the queue attributed to the slot. In the case of two queue level, the slot table can look
 * like the following:
 *
 *      +----------+----------+
 *      |   Queue  |  Request |
 *      |   type   |  ticket  |
 *      +----------+----------+
 *      |   Dtm    |   100    |
 *      | Imagery  |    0     |
 *      |   Dtm    |  empty   |
 *      | Imagery  |    1     |
 *      |   Dtm    |   101    |
 *      |   Dtm    |   102    |
 *      +----------+----------+
 *
 * In this example the 'Dtm' queue has 4 requests to be made, and the 'Imagery' level only 2.
 *
 * During the work, this table is iterated over and requests are performed if any worker available.
 * Requests are are interleaved to allow a bit more variety and don't emit all the requests of one
 * queue in a row.
 *
 * Adopting this scheduling strategy also avoid the starvation problem (at queue level) where lower
 * level queues wouldn't get a chance to make any request.
 *
 * On top of the 'standard' queues, two additional queues are available: the 'Early' and 'Late'
 * queues. All the requests from the 'Early' queue are emitted before the other queues are
 * considered. (So beware of starvation.) Conversely, requests in the 'Late' are only emitted
 * when the other queues are empty. Inside these two queues, the requests are ordered by their
 * priority value.
 *
 * From a queue perspective, requests are also sorted. This is done through the `priority`
 * parameter. It can be provided by the user upon request creation to allow fine-grained control
 * inside a queue. The greater the priority is the sooner the request will be executed. This can be
 * used, for instance, to request imagery tile closer to the camera before the one further back.
 *
 * Finally, all this information is stored in the ticket returned by `assets_loader::begin()` which
 * looks like this:
 *
 *      +----------------------------------------------------------------------------+
 *      |                           assets_loader::Ticket                            |
 *      +------------------------------+------------------------+--------------------+
 *      |  32-bits object pool ticket  |   8-bits queue level   |  24-bits priority  |
 *      +------------------------------+------------------------+--------------------+
 *
 * @Note @Todo: starvation could still happen inside queues. Right this doesn't seem to much of
 * an issue, will see at usage if this becomes a problem.
 * A solution would be to add dynamic priorities based on the request's waiting time, before
 * emission.
 */

namespace
{
enum
{
    FirstRegularQueue = hrz::assets_loader::Queue::Late + 1,
    LastRegularQueue = hrz::assets_loader::Queue::Queue_Count - 1,
};

// Number of pre-allocated slots per queue.
// @Note: these are empiric numbers that can/should be tweaked.
// Early and Last queues have no slots, as their requests are handled
// differently. Instead of being inserted in slots, they are started
// directly from the queues.
enum QueueSlots
{
    EarlySlots = 0,
    LateSlots = 0,
    DtmSlots = 12,
    FeatureSlots = 8,
    ImageryTopSlots = 8,
    ImageryMiddleSlots = 8,
    ImageryBottomSlots = 8,
    VectorDataSlots = 8,
    ThreeDTilesSlots = 8,
    MeshModelslots = 8,
    DefaultSlots = 2,

    QueueSlots_Count = EarlySlots + LateSlots + DtmSlots + ImageryBottomSlots + ImageryMiddleSlots
        + ImageryTopSlots + VectorDataSlots + ThreeDTilesSlots + MeshModelslots + DefaultSlots,
};

// Generate a lookup array to map to which queue a slot is associated with.
static inline void generate_slot_to_queue_lookup(
    assets_loader::Queue slot_to_queue[QueueSlots_Count])
{
    uint32_t available_slots_per_queue[assets_loader::Queue_Count] = {
        EarlySlots,         LateSlots,       DtmSlots,         ImageryTopSlots, ImageryMiddleSlots,
        ImageryBottomSlots, VectorDataSlots, ThreeDTilesSlots, MeshModelslots,  DefaultSlots};

    uint32_t queue = 0;
    for (unsigned int i = 0; i < QueueSlots_Count;)
    {
        if (available_slots_per_queue[queue] > 0)
        {
            slot_to_queue[i] = (assets_loader::Queue)queue;
            available_slots_per_queue[queue]--;
            i++;
        }
        if (++queue == assets_loader::Queue_Count) queue = 0;
    }
}
} // anonymous namespace

namespace hrz
{
struct AssetsLoader
{
    using PoolTicket = uint32_t;
    using IndexPool = GenIndexPool<PoolTicket, 16, 16>;
    using RequestsPool = GenObjectPool<Request, IndexPool, 128>;

    RequestsPool requests_pool;
    std::unique_ptr<IHttpLoader> platform_http_loader;

    // @Todo @Performance :Memory Use something better than deque.
    // Perhaps a growable ring buffer?
    std::deque<InnerTicket> queues[assets_loader::Queue::Queue_Count];
    bool should_sort_queues = false;
    assets_loader::Ticket slots_table[QueueSlots_Count] = {0};
    assets_loader::Queue slot_to_queue[QueueSlots_Count];
    uint32_t current_slot = 0;
    std::vector<InnerTicket> ended_requests;
    hrz::flat_hash_set<assets_loader::Ticket> running_requests;
    hrz::flat_hash_set<assets_loader::Ticket> loading_requests;
    hrz::flat_hash_set<assets_loader::Ticket> requests_to_blob;
    uint32_t queued_request_count = 0;
    hrz::flat_hash_map<ChannelRequestId, assets_loader::Ticket> channel_tickets;

    uint32_t memory_used_bytes = 0;

    struct RequestHistory
    {
        static constexpr size_t CAPACITY = 512;
        static constexpr size_t MAX_URL_SIZE = 2048;

        struct Entry
        {
            PoolTicket ticket;
            std::string url;
            uint64_t range_start;
            uint64_t range_size;
            assets_loader::RequestStatus status;
            size_t data_size;
        };

        Entry entries[CAPACITY];
        size_t head = 0;
        size_t entry_count = 0;

        // These values count all requests, even if the entry array is full.
        // That is to say, `request_count` can exceed `SIZE`, and `data_size`
        // can be greater than the sum of `data_size`s in `entries`.
        size_t request_count = 0;
        size_t data_size = 0;

        size_t append(
            PoolTicket ticket,
            const std::string& url,
            uint64_t range_start,
            uint64_t range_size,
            assets_loader::RequestStatus status)
        {
            size_t index = head;

            auto& entry = entries[head];
            entry.ticket = ticket;
            entry.url = url.substr(0, MAX_URL_SIZE);
            entry.range_start = range_start;
            entry.range_size = range_size;
            entry.status = status;
            entry.data_size = 0;

            head = (head + 1) % CAPACITY;

            if (entry_count < CAPACITY)
            {
                entry_count += 1;
            }

            request_count += 1;

            return index;
        }
    } history;

    ChannelGroup<assets_loader::FromLoaderMessages, assets_loader::ToLoaderMessages> channels;
};

namespace assets_loader
{
namespace
{
void set_request_status(
    AssetsLoader* al,
    AssetsLoader::PoolTicket request_handle,
    Request* request,
    RequestStatus status)
{
    request->status = status;

    auto& history_entry = al->history.entries[request->history_index];
    if (history_entry.ticket == request_handle)
    {
        history_entry.status = status;
    }
}

void set_request_data_size(
    AssetsLoader* al,
    AssetsLoader::PoolTicket request_handle,
    Request* request,
    size_t size)
{
    auto& history_entry = al->history.entries[request->history_index];
    if (history_entry.ticket == request_handle)
    {
        history_entry.data_size = size;
    }

    al->history.data_size += size;
}
} // namespace

void _initialize_common(AssetsLoader* l)
{
    generate_slot_to_queue_lookup(l->slot_to_queue);
}

#if HRZ_DESKTOP
AssetsLoader* create(const char* user_agent, const char* http_referrer, size_t http_cache_size)
{
    AssetsLoader* l = new AssetsLoader();
    l->platform_http_loader = create_platform_loader(user_agent, http_referrer, http_cache_size);
    _initialize_common(l);
    return l;
}
#else
AssetsLoader* create()
{
    AssetsLoader* l = new AssetsLoader();
    l->platform_http_loader = create_platform_loader();
    _initialize_common(l);
    return l;
}
#endif

void destroy(AssetsLoader* l)
{
    assert(l);
    l->platform_http_loader->cleanup();
    delete l;
}

Ticket begin(
    AssetsLoader* l,
    std::string_view url,
    uint64_t range_start,
    uint64_t range_size,
    const HttpHeaders& headers,
    Queue queue,
    uint32_t priority,
    monitoring::ResourceOwner resource_owner,
    std::optional<ChannelRequestId> channel_request_id)
{
    assert(l);

    if (priority > MAX_PRIORITY)
    {
        HRZ_LOG_WARNING("Priority value too large: {}", priority);
        priority = MAX_PRIORITY;
    }

    AssetsLoader::PoolTicket handle = l->requests_pool.alloc();

    Request* r = l->requests_pool.get_object(handle);
    r->status = RequestStatus::Queued;
    r->range_start = range_start;
    r->range_size = range_size;
    r->resource_owner = resource_owner;
    r->channel_request_id = channel_request_id;
    r->url = std::string(url);

    if (hrz::str::starts_with(url, "client:"))
    {
        r->protocol_data.emplace<Request::ClientData>();
    }
    else
    {
        r->protocol_data.emplace<Request::HttpData>().headers = headers;
    }

    InnerTicket pticket;
    pticket.handle = handle;
    pticket.queue = queue;
    pticket.priority = priority;

    auto& q = l->queues[(uint32_t)queue];
    q.push_back(pticket);
    l->queued_request_count++;

    l->should_sort_queues = true;

    r->history_index = l->history.append(handle, r->url, r->range_start, r->range_size, r->status);

    return pticket.ticket;
}

Ticket begin(
    AssetsLoader* l,
    std::string_view url,
    uint64_t range_start,
    uint64_t range_size,
    const HttpHeaders& headers,
    Queue queue,
    uint32_t priority,
    monitoring::ResourceOwner resource_owner)
{
    return begin(
        l, url, range_start, range_size, headers, queue, priority, resource_owner, std::nullopt);
}

Ticket begin(
    AssetsLoader* l,
    std::string_view url,
    const HttpHeaders& headers,
    Queue queue,
    uint32_t priority,
    monitoring::ResourceOwner resource_owner)
{
    return begin(l, url, 0, 0, headers, queue, priority, resource_owner);
}

Ticket reset_priority(AssetsLoader* l, Ticket t, uint32_t priority)
{
    assert(l);

    if (priority > MAX_PRIORITY)
    {
        HRZ_LOG_WARNING("Priority value too large: {}", priority);
        priority = MAX_PRIORITY;
    }

    InnerTicket pt = {t};

    if (!l->requests_pool.is_valid(pt.handle)) return t;

    auto& q = l->queues[pt.queue];
    auto it = std::find(std::begin(q), std::end(q), pt);
    if (it != std::end(q))
    {
        // The request is still in the queue.
        // Create a new platform ticket with the new priority.

        InnerTicket new_pt;
        new_pt.handle = pt.handle;
        new_pt.queue = pt.queue;
        new_pt.priority = priority;

        *it = new_pt;

        l->should_sort_queues = true;

        return new_pt.ticket;
    }
    else
    {
        // The request is not the queue, it is already in the slot table.
        // No need to do anything.
        return t;
    }
}

void end(AssetsLoader* l, Ticket t)
{
    assert(l);

    InnerTicket pt = {t};

    if (!l->requests_pool.is_valid(pt.handle)) return;

    l->ended_requests.push_back(pt);
}

bool is_valid(const AssetsLoader* l, Ticket t)
{
    assert(l);

    InnerTicket pt = {t};
    return l->requests_pool.is_valid(pt.handle);
}

bool is_finished(const AssetsLoader* l, Ticket t)
{
    assert(l);

    InnerTicket pt = {t};
    const Request* r = l->requests_pool.get_object(pt.handle);
    if (!r) return false;

    return r->status == RequestStatus::Loaded || r->status == RequestStatus::Error;
}

std::string_view get_url(const AssetsLoader* l, Ticket t)
{
    assert(l);

    InnerTicket pt = {t};
    const Request* r = l->requests_pool.get_object(pt.handle);
    if (!r) return "";

    return r->url;
}

RequestStatus get_status(const AssetsLoader* l, Ticket t)
{
    assert(l);

    InnerTicket pt = {t};
    const Request* r = l->requests_pool.get_object(pt.handle);
    if (!r) return RequestStatus::Error;

    return r->status;
}

std::string_view get_content_type(const AssetsLoader* l, Ticket t)
{
    assert(l);

    if (!is_finished(l, t)) return {};

    InnerTicket pt = {t};
    const Request* r = l->requests_pool.get_object(pt.handle);
    if (!r) return {};

    return r->content_type;
}

blobs::BlobHandle get_blob(AssetsLoader* l, BlobAllocator* ba, Ticket t)
{
    if (!is_finished(l, t)) return {};

    Request* req = l->requests_pool.get_object(t);
    if (!req) return {};

    if (req->status != RequestStatus::Loaded)
    {
        assert(false);
        HRZ_LOG_ERROR("Request is not loaded");
        return {};
    }

    if (!req->blob.is_valid())
    {
        assert(false);
        HRZ_LOG_ERROR("Blob has already been retrieved");
        return {};
    }

    return std::move(req->blob);
}

void push_load_error_message(AssetsLoader* al, Request* request)
{
    assert(request->channel_request_id.has_value());

    auto it = al->channels.find(request->channel_request_id->channel_id);
    if (it != al->channels.end())
    {
        auto& channel = it->second;
        channel.send(messages::LoadFailure{request->channel_request_id->request_id});
    }
}

void work_message_queues(AssetsLoader* al)
{
    al->channels.work();

    for (auto& it : al->channels)
    {
        auto channel_id = it.first;
        auto& channel = it.second;

        for (auto& message : channel.receive())
        {
            std::visit(
                [&](auto& message)
                {
                    using MessageType = std::decay_t<decltype(message)>;
                    if constexpr (std::is_same_v<MessageType, messages::LoadRequest>)
                    {
                        ChannelRequestId channel_request_id{channel_id, message.request_id};
                        al->channel_tickets.insert(
                            {channel_request_id,
                             begin(
                                 al, message.url, message.range_start, message.range_size,
                                 message.headers, message.queue, message.priority,
                                 message.resource_owner, {channel_request_id})});
                    }
                    else if constexpr (std::is_same_v<MessageType, messages::ResetRequestPriority>)
                    {
                        auto it = al->channel_tickets.find({channel_id, message.request_id});
                        if (it != al->channel_tickets.end())
                        {
                            reset_priority(al, it->second, message.priority);
                        }
                    }
                    else if constexpr (std::is_same_v<MessageType, messages::CancelRequest>)
                    {
                        auto it = al->channel_tickets.find({channel_id, message.request_id});
                        if (it != al->channel_tickets.end())
                        {
                            end(al, it->second);
                        }
                    }
                    else if constexpr (std::is_same_v<MessageType, messages::CreateChannel>)
                    {
                        channel.send(messages::NewChannel{
                            message.request_id, std::move(create_channel(al))});
                    }
                    else
                    {
                        static_assert(hrz::always_false<MessageType>, "Unhandled case");
                    }
                },
                message);
        }
    }
}

void work_ended_requests(AssetsLoader* al, BlobAllocator* ba)
{
    for (InnerTicket pt : al->ended_requests)
    {
        Request* req = al->requests_pool.get_object(pt.handle);
        if (!req) continue;

        if (req->status == RequestStatus::Loading)
        {
            set_request_status(al, pt.handle, req, RequestStatus::Canceled);
            al->platform_http_loader->cancel_request(pt.http_ticket);
        }
        else if (req->status == RequestStatus::Queued)
        {
            set_request_status(al, pt.handle, req, RequestStatus::Canceled);
            auto& q = al->queues[pt.queue];
            auto it = std::find(std::begin(q), std::end(q), pt);
            if (it != std::end(q))
            {
                q.erase(it);
                al->queued_request_count--;
            }
            else
            {
                // If it's not in the queue then it is probably in the slot table.
                for (unsigned int i = FirstRegularQueue; i <= LastRegularQueue; ++i)
                {
                    if (al->slots_table[i] == pt.ticket)
                    {
                        al->slots_table[i] = 0;
                        break;
                    }
                }
            }
        }
        else
        {
            al->platform_http_loader->free_data(pt.http_ticket);
        }

        blobs::cancel(ba, req->blob_ticket);
        req->blob.release();

        if (req->channel_request_id.has_value())
        {
            auto it = al->channel_tickets.find(
                {req->channel_request_id->channel_id, req->channel_request_id->request_id});
            if (it != al->channel_tickets.end())
            {
                al->channel_tickets.erase(it);
            }
        }

        al->running_requests.erase(pt.ticket);
        al->loading_requests.erase(pt.ticket);
        al->requests_to_blob.erase(pt.ticket);
        al->requests_pool.release(pt.handle);
    }
    al->ended_requests.clear();
}

void work_sort_queues(AssetsLoader* al)
{
    HRZ_SCOPED_SAMPLE("sort queues");

    if (al->should_sort_queues)
    {
        for (auto& q : al->queues)
        {
            std::sort(
                q.begin(), q.end(),
                [](const InnerTicket& a, const InnerTicket& b) { return a.priority < b.priority; });
        }
    }
}

void work_emit_requests(AssetsLoader* al, ClientMessageQueue* mq)
{
    HRZ_SCOPED_SAMPLE("emit requests");

    // Launch as many requests as possible.
    bool max_running_requests_reached = false;

    uint32_t started_request_count = 0;

    auto start_request = [&](InnerTicket ticket) -> bool
    {
        Request* req = al->requests_pool.get_object(ticket.handle);
        if (!req) return false;

        if (req->is_http_request())
        {
            const bool success = al->platform_http_loader->start_request(
                ticket.http_ticket, req->url, req->range_start, req->range_size,
                req->http_request().headers);

            if (!success)
            {
                max_running_requests_reached = true;
                return false;
            }

            set_request_status(al, ticket.handle, req, RequestStatus::Loading);
        }
        else
        {
            hrz_proto::AssetRequestMessage message;
            message.set_ticket(ticket.handle);
            message.set_range_start(req->range_start);
            message.set_range_size(req->range_size);
            // Remove "client:" from the url when sending it to the client
            assert(str::starts_with(req->url, "client:"));
            message.set_url(req->url.substr(7));
            client_message_queue::enqueue_asset_request_message(mq, std::move(message));

            set_request_status(al, ticket.handle, req, RequestStatus::Loading);
        }

        al->running_requests.insert(ticket.ticket);
        started_request_count++;

        return true;
    };

    auto start_requests_from_queue = [&](Queue queue_name)
    {
        auto& user_queue = al->queues[(uint32_t)queue_name];
        while (!max_running_requests_reached && !user_queue.empty())
        {
            InnerTicket ticket = {user_queue.back().ticket};
            if (ticket == InnerTicket{0}) continue;

            if (start_request(ticket))
            {
                user_queue.pop_back();
                al->queued_request_count--;
            }
        }
    };

    // Start as many requests as possible from the User first queue.
    start_requests_from_queue(Queue::Early);

    if (max_running_requests_reached) return;

    for (;;)
    {
        // Refill the slots table.
        bool all_slots_empty = true;

        for (unsigned int i = FirstRegularQueue; i <= LastRegularQueue; ++i)
        {
            if (al->slots_table[i] != 0)
            {
                all_slots_empty = false;
                continue;
            }

            assets_loader::Queue slot_queue = al->slot_to_queue[i];
            auto& q = al->queues[(uint32_t)slot_queue];

            if (!q.empty())
            {
                al->slots_table[i] = q.back().ticket;
                q.pop_back();
                al->queued_request_count--;

                all_slots_empty = false;
            }
        }

        if (all_slots_empty) break;

        // Launch pending requests from the slots table.
        for (unsigned int i = FirstRegularQueue; i <= LastRegularQueue; ++i)
        {
            InnerTicket ticket = {al->slots_table[al->current_slot]};
            if (!start_request(ticket) && max_running_requests_reached)
            {
                break;
            }

            al->slots_table[al->current_slot] = 0;
            al->current_slot += 1;
            if (al->current_slot == QueueSlots_Count) al->current_slot = 0;
        }

        if (max_running_requests_reached) break;
    }

    if (!max_running_requests_reached)
    {
        // Start requests from the User last queue if there is some room.
        start_requests_from_queue(Queue::Late);
    }

    HRZ_SET_GAUGE("Queued requests", al->queued_request_count, {});
    HRZ_ADD_TO_GAUGE("Total started requests", started_request_count, {});
}

void work_retrieve_finished(AssetsLoader* al, BlobAllocator* ba)
{
    InnerTicket ticket;
    while (true)
    {
        auto metadata_opt = al->platform_http_loader->dequeue_finished_request();
        if (!metadata_opt) break;

        auto metadata = std::move(metadata_opt).value();
        ticket.http_ticket = metadata.ticket;

        Request* r = al->requests_pool.get_object(ticket.handle);
        if (r)
        {
            r->http_request().headers.swap(metadata.headers);
            r->content_type = metadata.content_type;

            set_request_data_size(al, ticket.handle, r, metadata.data_size);

            if (metadata.status == HttpRequestStatus::Loaded)
            {
                r->blob_ticket = blobs::allocate_blob(ba, metadata.data_size);

                blobs::register_metadata(ba, r->blob_ticket, "type"_ss, "raw downloaded data"_ss);
                blobs::register_metadata(ba, r->blob_ticket, "URL"_ss, r->url);
                blobs::register_owner(ba, r->blob_ticket, r->resource_owner);

                al->requests_to_blob.insert(ticket.ticket);
            }
            else
            {
                RequestStatus status;
                switch (metadata.status)
                {
                    case HttpRequestStatus::Canceled: status = RequestStatus::Canceled; break;
                    case HttpRequestStatus::Error: status = RequestStatus::Error; break;
                    default:
                        assert(false && "Unhandled case");
                        status = RequestStatus::Error;
                        break;
                }

                if (status == RequestStatus::Error && r->channel_request_id.has_value())
                {
                    push_load_error_message(al, r);
                    al->ended_requests.push_back(ticket);
                }

                set_request_status(al, ticket.handle, r, status);
            }
        }
    }
}

void work_blobs(AssetsLoader* al, BlobAllocator* ba)
{
    for (auto it = al->requests_to_blob.begin(); it != al->requests_to_blob.end();)
    {
        auto ticket = *it;
        InnerTicket pt = {ticket};
        auto request = al->requests_pool.get_object(ticket);
        auto allocation_state = blobs::get_state(ba, request->blob_ticket);

        RequestStatus new_status = RequestStatus::Loading;
        bool erase = false;

        if (allocation_state == blobs::BlobState::Allocated)
        {
            request->blob = blobs::to_blob(ba, request->blob_ticket);

            if (request->is_http_request())
            {
                request->blob_data = {std::move(request->blob.get_mutable_data())};

                if (al->platform_http_loader->copy_data(
                        pt.http_ticket, request->blob_data->as_writable_bytes()))
                {
                    al->loading_requests.insert(ticket);
                    new_status = RequestStatus::Loading;
                }
                else
                {
                    HRZ_LOG_ERROR("Could not copy downloaded data into blob");
                    request->blob.release();
                    new_status = RequestStatus::Error;

                    if (request->channel_request_id.has_value())
                    {
                        push_load_error_message(al, request);
                        al->ended_requests.push_back(pt);
                    }
                }

                erase = true;
            }
            else if (request->is_client_request())
            {
                auto client_bytes = request->client_request().provided_bytes;
                if (request->blob.data_size() != client_bytes.size())
                {
                    assert(false);
                    HRZ_LOG_ERROR("Blob has incorrect size for client-provided asset data");
                    request->blob.release();
                    new_status = RequestStatus::Error;

                    if (request->channel_request_id.has_value())
                    {
                        push_load_error_message(al, request);
                        al->ended_requests.push_back(pt);
                    }
                }
                else
                {
                    auto mutable_data = request->blob.get_mutable_data();
                    std::memcpy(
                        mutable_data.as_writable_bytes().data(), client_bytes.data(),
                        client_bytes.size());
                    new_status = RequestStatus::Loaded;

                    if (request->channel_request_id.has_value())
                    {
                        auto it = al->channels.find(request->channel_request_id->channel_id);
                        if (it != al->channels.end())
                        {
                            auto& channel = it->second;
                            channel.send(messages::LoadedData{
                                request->channel_request_id->request_id, std::move(request->blob),
                                ""});
                        }

                        al->ended_requests.push_back(pt);
                    }
                }

                erase = true;
            }
        }
        else if (allocation_state == blobs::BlobState::Error)
        {
            blobs::cancel(ba, request->blob_ticket);
            al->platform_http_loader->free_data(pt.http_ticket);
            new_status = RequestStatus::Error;

            if (request->channel_request_id.has_value())
            {
                push_load_error_message(al, request);
                al->ended_requests.push_back(pt);
            }

            erase = true;
        }

        if (erase)
        {
            set_request_status(al, ticket, request, new_status);
            al->requests_to_blob.erase(it++);
        }
        else
        {
            ++it;
        }
    }
}

void work_load_into_memory(AssetsLoader* al)
{
    for (auto it = al->loading_requests.begin(); it != al->loading_requests.end();)
    {
        auto ticket = *it;
        InnerTicket pt = {ticket};

        if (al->platform_http_loader->is_data_copied(pt.http_ticket))
        {
            auto request = al->requests_pool.get_object(ticket);
            request->blob_data = std::nullopt;
            set_request_status(al, ticket, request, RequestStatus::Loaded);
            al->loading_requests.erase(it++);

            if (request->channel_request_id.has_value())
            {
                auto it = al->channels.find(request->channel_request_id->channel_id);
                if (it != al->channels.end())
                {
                    auto& channel = it->second;
                    channel.send(messages::LoadedData{
                        request->channel_request_id->request_id, std::move(request->blob),
                        request->is_http_request() ? std::move(request->content_type) : ""});
                }

                al->ended_requests.push_back(pt);
            }
        }
        else
        {
            ++it;
        }
    }
}

void work(AssetsLoader* al, BlobAllocator* ba, ClientMessageQueue* mq)
{
    assert(al && ba);
    HRZ_SCOPED_SAMPLE("assets loader work");

    work_message_queues(al);
    work_ended_requests(al, ba);
    work_sort_queues(al);
    work_emit_requests(al, mq);
    al->platform_http_loader->work();
    work_retrieve_finished(al, ba);
    work_blobs(al, ba);
    work_load_into_memory(al);
}

void clear_http_cache(AssetsLoader* al)
{
    al->platform_http_loader->clear_cache();
}

void provide_client_asset_data(
    AssetsLoader* al,
    BlobAllocator* ba,
    const hrz_proto::AssetRequestResponse& response)
{
    InnerTicket ticket{response.ticket()};
    Request* req = al->requests_pool.get_object(ticket.handle);

    if (!req) return;

    req->client_request().provided_bytes = response.data();
    const size_t data_size = response.data().size();

    req->blob_ticket = blobs::allocate_blob(ba, data_size);
    al->requests_to_blob.insert(ticket.ticket);

    set_request_data_size(al, ticket.handle, req, data_size);

    req->content_type = response.mime_type();
}

assets_loader::Channel create_channel(AssetsLoader* al)
{
    assert(al);

    return al->channels.create_channel().second;
}

namespace
{
const char* queue_to_str(assets_loader::Queue queue)
{
    switch (queue)
    {
        case assets_loader::Queue::Early: return "Early";
        case assets_loader::Queue::Late: return "Late";
        case assets_loader::Queue::Dtm: return "DTM";
        case assets_loader::Queue::ImageryBottom: return "Imagery (bottom)";
        case assets_loader::Queue::ImageryMiddle: return "Imagery (middle)";
        case assets_loader::Queue::ImageryTop: return "Imagery (top)";
        case assets_loader::Queue::VectorData: return "Vector data";
        case assets_loader::Queue::ThreeDTiles: return "3D Tiles";
        case assets_loader::Queue::MeshModels: return "Mesh models";
        case assets_loader::Queue::Default: return "Default";
        default: assert(false && "Unhandled case"); return "???";
    }
}

const char* status_to_str(assets_loader::RequestStatus status)
{
    switch (status)
    {
        case assets_loader::RequestStatus::Queued: return "(Queued)";
        case assets_loader::RequestStatus::Loading: return "(Loading)";
        case assets_loader::RequestStatus::Loaded: return "(Loaded)";
        case assets_loader::RequestStatus::Canceled: return "(Canceled)";
        case assets_loader::RequestStatus::Error: return "(Error)";
        default:
            assert(false && "Unhandled case");
            return "(???"
                   ")";
    }
}

mu_Color status_to_text_color(assets_loader::RequestStatus status)
{
    switch (status)
    {
        case assets_loader::RequestStatus::Queued:
        case assets_loader::RequestStatus::Loading:
        case assets_loader::RequestStatus::Canceled: return {160, 160, 160, 255};
        case assets_loader::RequestStatus::Loaded: return {97, 204, 38, 255};
        case assets_loader::RequestStatus::Error: return {255, 32, 32, 255};
        default: assert(false && "Unhandled case"); return {255, 255, 255, 255};
    }
}
} // namespace

void dev_ui(AssetsLoader* al, PlatformContext* platform, mu_Context* ctx, const char* window_name)
{
    fmt::memory_buffer buffer;

    if (mu_begin_window_ex(ctx, window_name, mu_rect(300, 100, 400, 300), MU_OPT_CLOSED))
    {
        int window_width = -1;
        mu_layout_row(ctx, 1, &window_width, 0);

        buffer.clear();
        fmt::format_to(std::back_inserter(buffer), "{} queued requests", al->queued_request_count);
        buffer.push_back(0);
        mu_text(ctx, buffer.data());

        al->platform_http_loader->dev_ui(ctx);

        mu_layout_row(ctx, 1, &window_width, 0);

        if (mu_header(ctx, "Queues"))
        {
            int layout[] = {160, -1};
            mu_layout_row(ctx, 2, layout, 0);

            for (unsigned int i = 0; i < assets_loader::Queue_Count; ++i)
            {
                buffer.clear();
                fmt::format_to(
                    std::back_inserter(buffer), "{} queue", queue_to_str((assets_loader::Queue)i));
                buffer.push_back(0);
                mu_text(ctx, buffer.data());

                buffer.clear();
                fmt::format_to(std::back_inserter(buffer), "{} requests", al->queues[i].size());
                buffer.push_back(0);
                mu_text(ctx, buffer.data());
            }
        }

        if (mu_header(ctx, "Active requests"))
        {
            int layout[] = {-65, 60};
            mu_layout_row(ctx, 2, layout, 0);

            if (!al->running_requests.empty())
            {
                for (assets_loader::Ticket ticket : al->running_requests)
                {
                    Request* req = al->requests_pool.get_object(ticket);
                    if (req)
                    {
                        mu_label(ctx, req->url.c_str());
                        mu_label(ctx, status_to_str(req->status));
                    }
                }
            }
            else
            {
                mu_label(ctx, "(No requests)");
            }
        }

        if (mu_header(ctx, "Request history"))
        {
            static fmt::memory_buffer buffer;

            {
                static const int layout[] = {100, 100, 50, 50, -1};
                mu_layout_row(ctx, 5, layout, 0);

                buffer.clear();
                fmt::format_to(
                    std::back_inserter(buffer), "{} requests", al->history.request_count);
                buffer.push_back(0);
                mu_draw_control_text(
                    ctx, buffer.data(), mu_layout_next(ctx), MU_COLOR_TEXT, MU_OPT_ALIGNRIGHT);

                mu_draw_control_text(
                    ctx, hrz::bytes_to_string(al->history.data_size, buffer), mu_layout_next(ctx),
                    MU_COLOR_TEXT, MU_OPT_ALIGNRIGHT);

                mu_text(ctx, "");

                if (mu_button(ctx, "Clear"))
                {
                    al->history.entry_count = 0;
                    al->history.request_count = 0;
                    al->history.data_size = 0;
                }
            }

            static const int panel_layout = -1;
            mu_layout_row(ctx, 1, &panel_layout, -1);
            static ui::StickyPanelState sticky_panel_state;
            begin_sticky_panel(ctx, &sticky_panel_state, "Request panel");

            for (size_t i = 0; i < al->history.entry_count; ++i)
            {
                size_t index =
                    (al->history.head - al->history.entry_count + i + al->history.CAPACITY)
                    % al->history.CAPACITY;
                const auto& entry = al->history.entries[index];

                static const int row_layout[] = {-193, 60, 60, 60};
                mu_layout_row(ctx, 4, row_layout, 0);

                if (entry.range_start == 0 && entry.range_size == 0)
                {
                    mu_label(ctx, entry.url.c_str());
                }
                else
                {
                    buffer.clear();
                    if (entry.range_size != 0)
                    {
                        fmt::format_to(
                            std::back_inserter(buffer), "{} [{}:{}]", entry.url, entry.range_start,
                            entry.range_start + entry.range_size);
                    }
                    else
                    {
                        fmt::format_to(
                            std::back_inserter(buffer), "{} [{}:]", entry.url, entry.range_start);
                    }
                    buffer.push_back(0);
                    mu_label(ctx, buffer.data());
                }

                mu_text_color(ctx, status_to_str(entry.status), status_to_text_color(entry.status));

                if (entry.status == assets_loader::RequestStatus::Loaded)
                {
                    mu_draw_control_text(
                        ctx, hrz::bytes_to_string(entry.data_size, buffer), mu_layout_next(ctx),
                        MU_COLOR_TEXT, MU_OPT_ALIGNRIGHT);
                }
                else
                {
                    mu_label(ctx, "");
                }

                mu_push_id(ctx, &i, sizeof(i));
                if (mu_button(ctx, "Copy URL"))
                {
                    hrz::platform::copy_to_clipboard(platform, entry.url);
                }
                mu_pop_id(ctx);
            }

            end_sticky_panel(ctx, &sticky_panel_state);
        }

        mu_end_window(ctx);
    }
}

// https://developer.mozilla.org/en-US/docs/Glossary/Forbidden_header_name
HttpHeaders from_proto(const hrz_proto::HttpHeaderList& headers)
{
    static constexpr std::array kForbiddenHeaders = {
        std::string_view("Accept-Charset"),
        std::string_view("Accept-Encoding"),
        std::string_view("Access-Control-Request-Headers"),
        std::string_view("Access-Control-Request-Method"),
        std::string_view("Connection"),
        std::string_view("Content-Length"),
        std::string_view("Cookie"),
        std::string_view("Date"),
        std::string_view("DNT"),
        std::string_view("Expect"),
        std::string_view("Host"),
        std::string_view("Keep-Alive"),
        std::string_view("Origin"),
        std::string_view("Permissions-Policy"),
        std::string_view("Referer"), // LOL
        std::string_view("TE"),
        std::string_view("Trailer"),
        std::string_view("Transfer-Encoding"),
        std::string_view("Upgrade"),
        std::string_view("Via"),
    };

    HttpHeaders packed;

    for (const auto& header : headers.headers())
    {
        if (str::istarts_with(header.name(), "Sec-") || str::istarts_with(header.name(), "Proxy-"))
        {
            HRZ_LOG_WARNING("Forbidden header name: \"{}\", ignoring it", header.name());
            continue;
        }

        bool is_forbidden = false;
        for (std::string_view forbidden : kForbiddenHeaders)
        {
            if (str::iequals(header.name(), forbidden))
            {
                is_forbidden = true;
                break;
            }
        }

        if (is_forbidden)
        {
            HRZ_LOG_WARNING("Forbidden header name: \"{}\", ignoring it", header.name());
            continue;
        }

        packed.set_header(header.name(), header.value());
    }

    return packed;
}

} // namespace assets_loader
} // namespace hrz
