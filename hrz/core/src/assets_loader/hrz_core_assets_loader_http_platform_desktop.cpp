#include "assets_loader/hrz_core_assets_loader.h"
#include "assets_loader/hrz_core_assets_loader_http_platform.h"
#include "hrz_core_version.h"

#include <hrz_common_metrics.h>
#include <hrz_common_profiling.h>
#include <hrz_core_resources.h>
#include <hrz_fnd_char_utils.h>
#include <hrz_fnd_flat_hash_map.h>
#include <hrz_fnd_flat_hash_set.h>
#include <hrz_fnd_log.h>
#include <hrz_fnd_meta.h>
#include <hrz_fnd_string_utils.h>
#include <hrz_fnd_url_utils.h>
#include <hrz_fnd_variant.h>
#include <hrz_http_cache.h>

#include <optional>
extern "C"
{
#include <microui/microui.h>
}
#include <hrz_fnd_format.h>

#include <assert.h>
#include <curl/curl.h>
#include <fmt/format.h>
#include <rapidjson/document.h>

#include <atomic>
#include <deque>
#include <mutex>
#include <thread>

namespace
{
using namespace hrz;
using namespace assets_loader;

static const unsigned int MAX_HOST_CONNECTIONS = 4;

#define L1_CACHE_LINE_SIZE 64

struct alignas(L1_CACHE_LINE_SIZE) RequestSlot
{
    HttpRequestStatus status;
    HttpTicket ticket;
    std::vector<std::byte> data;
    std::unique_ptr<char[]> error_buffer;
    CURL* handle;
    std::string url;
    uint64_t range_start;
    uint64_t range_size;
    std::string content_type;
    hrz::Arena request_headers_arena;
    curl_slist* request_headers;
    hrz::HttpHeaders response_headers;
    int response_code;

    RequestSlot() : request_headers_arena(1024 * 1024) {}
};

static_assert(
    sizeof(RequestSlot) % L1_CACHE_LINE_SIZE == 0,
    "RequestSlot size must be a multiple of 64 bytes");
static_assert(
    alignof(RequestSlot) == L1_CACHE_LINE_SIZE,
    "RequestSlot size must be aligned on 64 bytes");

size_t _header_callback(char* raw_header, size_t size, size_t nitems, void* userdata)
{
    assert(userdata);
    RequestSlot* req = (RequestSlot*)userdata;

    std::string_view header(raw_header, size * nitems);
    header = hrz::str::trim_s(header);

    auto colon = header.find(':');
    if (colon != std::string_view::npos)
    {
        auto name = hrz::str::rtrim_s(header.substr(0, colon));
        auto value = hrz::str::ltrim_s(header.substr(colon + 1));
        req->response_headers.set_header(name, value);
    }

    return nitems * size;
}

size_t _write_callback(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    assert(userdata);
    RequestSlot* req = (RequestSlot*)userdata;

    // Returning 0 aborts the download operation
    if (req->status == HttpRequestStatus::Canceled) return 0;

    req->data.insert(std::end(req->data), (std::byte*)ptr, (std::byte*)ptr + (size * nmemb));

    return size * nmemb;
}
} // namespace

namespace hrz::assets_loader
{
class DesktopHttpLoader : public IHttpLoader
{
    struct AddRequestMsg
    {
        HttpTicket ticket;
        std::string url;
        uint64_t range_start;
        uint64_t range_size;
        HttpHeaders headers;
    };

    struct CancelRequestMsg
    {
        HttpTicket ticket;
    };

    struct FinishedRequest
    {
        ResponseMetadata metadata;
        std::vector<std::byte> data;
    };

    struct RequestData
    {
        std::vector<std::byte> data;
        std::optional<gsl::span<std::byte>> data_dst;
    };

    using Message = std::variant<AddRequestMsg, CancelRequestMsg>;

    // The message queue can be touched by both the main and worker thread.
    // It's used by the main thread to communicate to the worker what to do.
    std::deque<Message> _queue;
    std::mutex _q_mutex;
    std::atomic_int _available_slots_count; // Prevents queueing too many add request messages
    std::atomic_int _queue_size;            // Avoids taking the mutex to check if we have a message

    // Woker state can be touched only by the worker thread while it's running.
    // We only communicate via the queue.
    CURLM* _multi_handle;
    std::vector<RequestSlot> _slots;
    std::vector<size_t> _available_slots;
    hrz::flat_hash_map<HttpTicket, size_t> _running_http_requests;

    // This is essentially a message queue for the worker thread to give
    // results back to the main thead. It's not a queue but you know...
    std::mutex _finished_mutex;
    std::vector<FinishedRequest> _finished_requests;
    std::atomic_int
        _finished_count; // Avoids taking the mutex to check if we have finished requests

    hrz::flat_hash_set<HttpTicket> _canceled_requests;

    // Once requests are finished, their data is placed here until it's freed.
    hrz::flat_hash_map<HttpTicket, RequestData> _finished_requests_data;
    size_t _finished_requests_data_size = 0;

    std::thread _thread;
    std::atomic_bool _running;

    hrz::ThreadProfiler* _profiler;
    hrz::ThreadMetricsRegistry* _metrics;

public:
    DesktopHttpLoader(const char* user_agent, const char* http_referrer)
    {
        // CURL_GLOBAL_WIN32:
        //   Initialises win32 on Windows, does nothing on other platforms.
        // CURL_GLOBAL_SSL:
        //   Enable SSL on SSL-enabled systems, does nothing elsewhere.
        curl_global_init(CURL_GLOBAL_WIN32 | CURL_GLOBAL_SSL);
        _multi_handle = curl_multi_init();
        curl_multi_setopt(_multi_handle, CURLMOPT_MAX_HOST_CONNECTIONS, MAX_HOST_CONNECTIONS);
        // @Todo(HRZ-337): multiplexing requires HTTP2.
        // curl_multi_setopt(p->multi_handle, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);

        gsl::span<const std::byte> cacert_data = hrz_res::get_data(hrz_res::Resources::CaCertPem);
        curl_blob cacert_blob;
        cacert_blob.data = (void*)cacert_data.data();
        cacert_blob.len = cacert_data.size();
        cacert_blob.flags =
            CURL_BLOB_NOCOPY; // No need to copy because the cacert should be in rodata.

        for (size_t i = 0; i < MAX_AVAILABLE_HANDLES; ++i)
        {
            _slots.emplace_back();
            _slots[i].error_buffer.reset(new char[CURL_ERROR_SIZE]);

            CURL* handle = curl_easy_init();
            curl_easy_setopt(handle, CURLOPT_HEADERFUNCTION, _header_callback);
            curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, _write_callback);
            curl_easy_setopt(handle, CURLOPT_CAINFO_BLOB, &cacert_blob);
            curl_easy_setopt(handle, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NATIVE_CA);
            curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 1);
            curl_easy_setopt(handle, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
            curl_easy_setopt(handle, CURLOPT_ACCEPT_ENCODING, "");
            curl_easy_setopt(handle, CURLOPT_USERAGENT, user_agent);
            curl_easy_setopt(handle, CURLOPT_ERRORBUFFER, _slots[i].error_buffer.get());
            curl_easy_setopt(handle, CURLOPT_REFERER, http_referrer);

            _slots[i].handle = handle;

            _available_slots.push_back(i);
        }

        _finished_requests.reserve(MAX_AVAILABLE_HANDLES * 4);
        _finished_count.store(0);
        _available_slots_count.store(MAX_AVAILABLE_HANDLES);
        _queue_size.store(0);

        _running.store(true);
        _thread = std::thread(&DesktopHttpLoader::work_thread, this);
    }

    ~DesktopHttpLoader() override = default;

    void cleanup() override
    {
        curl_multi_wakeup(_multi_handle);
        _running.store(false);
        HRZ_LOG_INFO("Waiting for thread to stop");
        _thread.join();

        for (auto it : _running_http_requests)
        {
            curl_multi_remove_handle(_multi_handle, _slots[it.second].handle);
            _available_slots.push_back(it.second);
        }
        _running_http_requests.clear();
        _available_slots_count.store(MAX_AVAILABLE_HANDLES);

        _finished_requests.clear();
        _finished_count.store(0);

        assert(_available_slots.size() == MAX_AVAILABLE_HANDLES);
        for (auto i : _available_slots)
        {
            curl_easy_cleanup(_slots[i].handle);
        }

        curl_multi_cleanup(_multi_handle);
        curl_global_cleanup();
    }

    void enqueue_message(Message&& msg)
    {
        const std::lock_guard<std::mutex> lock(_q_mutex);
        _queue.push_back(std::move(msg));
        _queue_size.fetch_add(1);

        // Wake up the worker thread in case it's waiting on data to arrive
        curl_multi_wakeup(_multi_handle);
    }

    bool start_request(
        HttpTicket ticket,
        std::string_view url,
        uint64_t range_start,
        uint64_t range_size,
        const HttpHeaders& headers) override
    {
        // Single producer, single consumer. The main thread only decrements and
        // the worker thread only increments so we don't have to worry about
        // another decrement to 0 before we also decrement. Otherwise we'd need
        // a loop with a compare exchange.
        if (_available_slots_count.load() == 0) return false;
        _available_slots_count.fetch_sub(1);

        enqueue_message(
            Message(AddRequestMsg{ticket, std::string(url), range_start, range_size, headers}));

        return true;
    }

    void cancel_request(HttpTicket ticket) override
    {
        _canceled_requests.insert(ticket);
        enqueue_message(Message(CancelRequestMsg{ticket}));
    }

    void work() override { HRZ_SET_GAUGE("Running requests", running_requests_count(), {}); }

    void start_http_request_thread(AddRequestMsg& typed_msg, size_t slot_index)
    {
        RequestSlot* req = &_slots[slot_index];
        req->status = HttpRequestStatus::Loading;
        req->data.clear();
        req->ticket = typed_msg.ticket;
        req->url.swap(typed_msg.url);
        req->range_start = typed_msg.range_start;
        req->range_size = typed_msg.range_size;
        req->content_type.clear();
        req->request_headers_arena.reset();
        req->response_headers.clear();

        // We construct ourselves the list of headers so that we can allocate
        // it on an arena instead of using curl's inefficient process.
        curl_slist* headers = nullptr;
        for (const auto& entry : typed_msg.headers)
        {
            auto name = entry.second.name;
            auto value = entry.second.value_str();

            size_t full_header_size = name.size() + value.size() + 3;

            // We'll write to the arena: "Name: Value\0".
            Arena::Span<char> buffer =
                req->request_headers_arena.alloc_array<char>(full_header_size);
            memcpy(buffer.ptr, name.data(), name.size());
            buffer.ptr[name.size()] = ':';
            buffer.ptr[name.size() + 1] = ' ';
            memcpy(buffer.ptr + name.size() + 2, value.data(), value.size());
            buffer.ptr[full_header_size - 1] = '\0';

            curl_slist* list_entry = req->request_headers_arena.alloc<curl_slist>();
            list_entry->data = buffer.ptr;
            list_entry->next = std::exchange(headers, list_entry);
        }

        curl_easy_setopt(req->handle, CURLOPT_PRIVATE, req);
        curl_easy_setopt(req->handle, CURLOPT_HEADERDATA, req);
        curl_easy_setopt(req->handle, CURLOPT_WRITEDATA, req);
        curl_easy_setopt(req->handle, CURLOPT_URL, req->url.c_str());
        curl_easy_setopt(req->handle, CURLOPT_HTTPHEADER, headers);

        if (req->range_start != 0 || req->range_size != 0)
        {
            std::string range_str = std::to_string(req->range_start);
            range_str += "-";
            if (req->range_size != 0)
            {
                range_str += std::to_string(req->range_start + req->range_size - 1);
            }

            curl_easy_setopt(req->handle, CURLOPT_RANGE, range_str.c_str());
        }
        else
        {
            curl_easy_setopt(req->handle, CURLOPT_RANGE, nullptr);
        }

        _running_http_requests.insert(std::make_pair(req->ticket, slot_index));
        curl_multi_add_handle(_multi_handle, req->handle);
    }

    void finish_request_in_slot_thread(
        size_t slot_index,
        HttpRequestStatus new_status,
        bool got_partial_content)
    {
        RequestSlot* req = &_slots[slot_index];
        FinishedRequest finished_request;

        // All requests, including the canceled ones go through the finished
        // requests queue, but we don't bother copying data when it's not needed.
        if (req->status != HttpRequestStatus::Canceled)
        {
            if (req->range_start != 0 || req->range_size != 0)
            {
                if (got_partial_content)
                {
                    if (req->range_size != req->data.size())
                    {
                        new_status = HttpRequestStatus::Error;
                    }
                }
                else if (req->response_code >= 200 && req->response_code < 300)
                {
                    // We only wanted a range of the data, but the server responded
                    // with the full data (or the data was encoded in the URL).
                    // We have to extract the desired range ourselves.

                    size_t full_size = req->data.size();

                    if (req->range_size != 0)
                    {
                        auto range_end = req->range_start + req->range_size;
                        if (range_end < req->data.size())
                        {
                            req->data.resize(range_end);
                        }
                    }
                    if (req->range_start != 0)
                    {
                        if (req->range_start < req->data.size())
                        {
                            req->data.erase(
                                req->data.begin(), req->data.begin() + req->range_start);
                        }
                        else
                        {
                            new_status = HttpRequestStatus::Error;
                        }
                    }

                    if (new_status != HttpRequestStatus::Error)
                    {
                        req->response_code = 206;
                        req->response_headers.set_header(
                            "Content-Range",
                            fmt::format(
                                "bytes {}-{}/{}", req->range_start,
                                req->range_start + req->data.size() - 1, full_size));
                    }
                }
            }

            finished_request.metadata.data_size = req->data.size();
            finished_request.data.swap(req->data);
            finished_request.metadata.content_type = req->content_type;
            finished_request.metadata.headers.swap(req->response_headers);
        }

        finished_request.metadata.status = new_status;
        finished_request.metadata.ticket = req->ticket;
        finished_request.metadata.code = req->response_code;

        {
            std::lock_guard<std::mutex> lock(_finished_mutex);
            _finished_requests.push_back(std::move(finished_request));
            _finished_count.fetch_add(1);
        }

        _available_slots.push_back(slot_index);
        _available_slots_count.fetch_add(1);
    }

    void do_data_request_thread(AddRequestMsg& typed_msg, size_t slot_index)
    {
        RequestSlot* req = &_slots[slot_index];
        req->status = HttpRequestStatus::Loading;
        req->data.clear();
        req->ticket = typed_msg.ticket;
        req->url.swap(typed_msg.url);
        req->range_start = typed_msg.range_start;
        req->range_size = typed_msg.range_size;

        HttpRequestStatus new_status = HttpRequestStatus::Loaded;

        url::EncodedData result;
        if (url::parse_data_url_s(req->url, &result))
        {
            std::string_view content_type_span =
                (result.mime_type == "") ? std::string_view("text/plain") : result.mime_type;

            req->content_type = std::string(content_type_span);

            if (result.is_base64)
            {
                size_t decoded_len_hint = hrz::str::decode_base64_size_hint(
                    result.encoded_payload, hrz::str::Base64DecodingVariant::Both);

                req->data.resize(decoded_len_hint);

                size_t decoded_len = hrz::str::decode_base64_s(
                    result.encoded_payload, req->data, hrz::str::Base64DecodingVariant::Both);

                if (decoded_len_hint != decoded_len)
                {
                    new_status = HttpRequestStatus::Error;
                }
                else
                {
                    req->data.resize(decoded_len);
                    req->response_code = 200;
                }
            }
            else
            {
                int decoded_len;
                char* decoded_payload = curl_easy_unescape(
                    req->handle, result.encoded_payload.data(), result.encoded_payload.size(),
                    &decoded_len);

                req->data.resize(decoded_len);
                req->response_code = 200;
                memcpy(req->data.data(), decoded_payload, decoded_len);
                curl_free(decoded_payload);
            }
        }
        else
        {
            new_status = HttpRequestStatus::Error;
        }

        finish_request_in_slot_thread(slot_index, new_status, false);
    }

    void work_thread()
    {
        _profiler = hrz::profiling::create_thread_profiler("Assets loader");
        _metrics = hrz::metrics::create_thread_registry(true);

        while (_running.load())
        {
            // Dequeue some message
            if (_queue_size.load() > 0)
            {
                std::lock_guard<std::mutex> lock(_q_mutex);
                while (!_queue.empty())
                {
                    Message msg = std::move(_queue.front());
                    _queue.pop_front();
                    _queue_size.fetch_sub(1);

                    std::visit(
                        [this](auto& msg)
                        {
                            using T = std::decay_t<decltype(msg)>;
                            if constexpr (std::is_same_v<T, AddRequestMsg>)
                            {
                                assert(!_available_slots.empty());
                                size_t slot_index = _available_slots.back();
                                _available_slots.pop_back();

                                if (hrz::str::starts_with(msg.url, "data:"))
                                {
                                    do_data_request_thread(msg, slot_index);
                                }
                                else
                                {
                                    start_http_request_thread(msg, slot_index);
                                }
                            }
                            else if constexpr (std::is_same_v<T, CancelRequestMsg>)
                            {
                                auto it = _running_http_requests.find(msg.ticket);
                                if (it != _running_http_requests.end())
                                {
                                    size_t slot_index = it->second;
                                    _running_http_requests.erase(it);

                                    RequestSlot* req = &_slots[slot_index];
                                    req->status = HttpRequestStatus::Canceled;

                                    curl_multi_remove_handle(_multi_handle, req->handle);
                                    finish_request_in_slot_thread(slot_index, req->status, false);
                                }
                            }
                            else
                            {
                                static_assert(hrz::always_false<T>, "Unknown message type");
                            }
                        },
                        msg);
                }
            }

            // Advance curl work
            CURLMcode ret_code;
            int running_requests;
            while ((ret_code = curl_multi_perform(_multi_handle, &running_requests))
                   == CURLM_CALL_MULTI_PERFORM)
                ;

            if (ret_code != CURLM_OK)
            {
                HRZ_LOG_ERROR("cURL multi perform failed!");
            }

            // Retrieve as much data as possible.
            int messages_left;
            CURLMsg* m;
            while ((m = curl_multi_info_read(_multi_handle, &messages_left)))
            {
                HRZ_SCOPED_SAMPLE("assets loader desktop retrieve data");

                if (m->msg != CURLMSG_DONE) continue;

                RequestSlot* req;
                long response_code;

                curl_easy_getinfo(m->easy_handle, CURLINFO_RESPONSE_CODE, &response_code);
                curl_easy_getinfo(m->easy_handle, CURLINFO_PRIVATE, &req);

                assert(req->status == HttpRequestStatus::Loading);

                auto running_it = _running_http_requests.find(req->ticket);
                assert(running_it != _running_http_requests.end());

                size_t slot_index = running_it->second;
                _running_http_requests.erase(running_it);

                assert(&_slots[slot_index] == req);

                curl_multi_remove_handle(_multi_handle, req->handle);

                // 200 OK
                // 204 No Content
                // 206 Partial Content
                // 304 Not Modified
                HttpRequestStatus new_status = (response_code == 200 || response_code == 204
                                                || response_code == 206 || response_code == 304)
                    ? HttpRequestStatus::Loaded
                    : HttpRequestStatus::Error;
                bool got_partial_content = response_code == 206;

                req->response_code = (int)response_code;

                if (new_status == HttpRequestStatus::Loaded)
                {
                    const char* content_type;
                    curl_easy_getinfo(m->easy_handle, CURLINFO_CONTENT_TYPE, &content_type);

                    if (content_type != nullptr)
                    {
                        req->content_type = content_type;
                    }
                }
                else if (new_status == HttpRequestStatus::Error)
                {
                    HRZ_LOG_ERROR("Couldn't load {} ({}).", req->url.c_str(), response_code);
                    if (response_code == 0)
                    {
                        HRZ_LOG_ERROR("{}", req->error_buffer.get());
                    }
                }

                finish_request_in_slot_thread(slot_index, new_status, got_partial_content);
            }

            if (_queue_size.load() == 0)
            {
                ret_code = curl_multi_poll(_multi_handle, nullptr, 0, 100, &running_requests);
            }

            hrz::metrics::finish_thread_registry_frame();
            hrz::metrics::synchronize_thread_registry();
            hrz::profiling::synchronize_thread_profiler();
        }

        hrz::profiling::destroy_thread_profiler(_profiler);
        hrz::metrics::destroy_thread_registry(_metrics);
    }

    size_t running_requests_count() const
    {
        return MAX_AVAILABLE_HANDLES - _available_slots_count.load();
    }

    std::optional<ResponseMetadata> dequeue_finished_request() override
    {
        if (_finished_count.load() == 0) return std::nullopt;

        std::lock_guard<std::mutex> lock(_finished_mutex);

        while (!_finished_requests.empty())
        {
            auto request = std::move(_finished_requests.back());
            _finished_requests.pop_back();

            bool canceled = false;

            auto canceled_it = _canceled_requests.find(request.metadata.ticket);
            if (canceled_it != _canceled_requests.end())
            {
                _canceled_requests.erase(canceled_it);
                canceled = true;
                request.metadata.status = HttpRequestStatus::Canceled;
            }
            else
            {
                _finished_requests_data_size += request.data.size();
                _finished_requests_data.insert(std::move(std::make_pair(
                    request.metadata.ticket, RequestData{std::move(request.data), std::nullopt})));
            }

            _finished_count.fetch_sub(1);

            if (!canceled)
            {
                return request.metadata;
            }
        }

        return std::nullopt;
    }

    void dev_ui(mu_Context* ctx) override
    {
        if (mu_header(ctx, "cURL HTTP loader"))
        {
            fmt::memory_buffer buffer;

            buffer.clear();
            fmt::format_to(
                std::back_inserter(buffer), "{} requests in progress", running_requests_count());
            buffer.push_back(0);
            mu_text(ctx, buffer.data());

            buffer.clear();
            bytes_to_string(_finished_requests_data_size, buffer, false, false);
            fmt::format_to(std::back_inserter(buffer), " stored");
            buffer.push_back(0);
            mu_text(ctx, buffer.data());
        }
    }

    bool copy_data(HttpTicket ticket, gsl::span<std::byte> dst) override
    {
        auto it = _finished_requests_data.find(ticket);
        if (it != _finished_requests_data.end())
        {
            if (dst.size() == it->second.data.size() && !it->second.data_dst.has_value())
            {
                std::memcpy(dst.data(), it->second.data.data(), it->second.data.size());
                it->second.data_dst = {dst};
                return true;
            }
            else
            {
                return false;
            }
        }
        else
        {
            return false;
        }
    }

    bool is_data_copied(HttpTicket ticket) override
    {
        auto it = _finished_requests_data.find(ticket);
        if (it != _finished_requests_data.end())
        {
            return it->second.data_dst.has_value();
        }
        else
        {
            return false;
        }
    }

    void free_data(HttpTicket ticket) override
    {
        auto it = _finished_requests_data.find(ticket);
        if (it != _finished_requests_data.end())
        {
            _finished_requests_data_size -= it->second.data.size();
            _finished_requests_data.erase(it);
        }
    }
};

std::unique_ptr<IHttpLoader> create_platform_loader(
    const char* user_agent,
    const char* referrer,
    size_t cache_size)
{
    auto loader = std::make_unique<DesktopHttpLoader>(user_agent, referrer);

    if (cache_size >= 1024 * 1024)
    {
        HRZ_LOG_INFO("HTTP cache enabled with size {}", cache_size);
        static hrz::HttpDefaultClock clock;
        return hrz::create_http_cache_loader(&clock, std::move(loader), cache_size);
    }
    else
    {
        HRZ_LOG_INFO("HTTP cache disabled because its size was too low ({} < 1MiB)", cache_size);
        return loader;
    }
}

} // namespace hrz::assets_loader
