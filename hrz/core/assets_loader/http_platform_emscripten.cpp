
#include "hrz/common/metrics.h"
#include "hrz/common/profiling.h"
#include "hrz/core/assets_loader/http_platform.h"
#include "hrz/core/js/lib.h"
#include "hrz/fnd/flat_hash_map.h"
#include "hrz/fnd/flat_hash_set.h"
#include "hrz/fnd/format.h"
#include "hrz/fnd/hash.h"
#include "hrz/fnd/log.h"
#include "hrz/fnd/url_utils.h"
#include "hrz/fnd/variant.h"

#include <emscripten.h>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <atomic>
#include <cassert>
#include <optional>
#include <thread>
#include <vector>

extern "C"
{
#include <microui/microui.h>
}

namespace hrz::assets_loader
{
class EmscriptenHttpLoader : public IHttpLoader
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

    struct CopyRequestMsg
    {
        HttpTicket ticket;
    };

    struct ReleaseDataMsg
    {
        HttpTicket ticket;
    };

    using Message = std::variant<AddRequestMsg, CancelRequestMsg, CopyRequestMsg, ReleaseDataMsg>;

    struct RequestSlot
    {
        HttpRequestStatus status;
        HttpTicket ticket;
        std::string url;
        uint64_t range_start;
        uint64_t range_size;
        int handle;
        int status_code;
        hrz::HttpHeaders response_headers;
    };

    struct FinishedRequest
    {
        ResponseMetadata metadata;
        int handle;
    };

    struct RequestData
    {
        int handle;
        size_t data_size;
        bool has_fetch_data;
        std::optional<std::span<std::byte>> data_dst;
        bool has_data; // true if the data has been copied to `data_dst`
        bool is_data_owned;
    };

    std::string _base_url;

    // The message queue can be touched by both the main and worker thread.
    // It's used by the main thread to communicate to the worker what to do.
    std::deque<Message> _queue; // Requires locking _queue_mutex
    std::mutex _queue_mutex;
    std::atomic_int _queue_size;
    std::atomic_int _available_slots_count; // Prevents queueing too many add request messages

    // Slots are touched by the worker thread only.
    std::vector<RequestSlot> _slots;
    std::vector<size_t> _available_slots;
    hrz::flat_hash_map<HttpTicket, size_t> _running_requests;
    std::atomic_int _running_requests_count;

    // This is essentially a message queue for the worker thread to give
    // results back to the main thead.
    std::deque<FinishedRequest> _newly_finished_requests; // Requires locking _newly_finished_mutex.
    std::mutex _newly_finished_mutex;
    std::atomic_int
        _newly_finished_count; // Avoids taking the mutex to check if we have finished requests
    hrz::flat_hash_map<HttpTicket, FinishedRequest> _finished_requests; // Main thread use only
    size_t _finished_requests_data_size = 0;
    size_t _retrieved_requests_data_size = 0;

    hrz::flat_hash_map<HttpTicket, RequestData> _requests_data; // Requires locking _data_mutex.
    std::mutex _data_mutex;

    hrz::flat_hash_set<HttpTicket> _canceled_requests; // Main thread use only

    std::thread _thread;
    std::atomic_bool _running;

    hrz::ThreadProfiler* _profiler;
    hrz::ThreadMetricsRegistry* _metrics;

public:
    EmscriptenHttpLoader()
    {
        _base_url = hrz_js_get_base_url();
        HRZ_LOG_INFO("Using base URL: {}", _base_url);

        _slots.resize(MAX_AVAILABLE_HANDLES);
        for (size_t i = 0; i < MAX_AVAILABLE_HANDLES; ++i)
        {
            _available_slots.push_back(i);
        }
        _queue_size.store(0);

        _running_requests_count.store(0);
        _finished_requests.reserve(MAX_AVAILABLE_HANDLES * 4);
        _newly_finished_count.store(0);
        _available_slots_count.store(MAX_AVAILABLE_HANDLES);
        _requests_data.reserve(MAX_AVAILABLE_HANDLES * 4);

        _running.store(true);
        _thread = std::thread(&EmscriptenHttpLoader::worker_init_func, this);
    }

    ~EmscriptenHttpLoader() override = default;

    void cleanup() override
    {
        _running.store(false);
        HRZ_LOG_INFO("Waiting for worker thread to stop");
        _thread.join();

        for (auto it : _running_requests)
        {
            int handle = _slots[it.second].handle;
            hrz_js_fetch_abort(handle);
            hrz_js_fetch_clean(handle);
            _available_slots.push_back(it.second);
        }
        _running_requests.clear();
        _running_requests_count.store(0);
        _available_slots_count.store(MAX_AVAILABLE_HANDLES);

        for (auto it : _newly_finished_requests)
        {
            hrz_js_fetch_clean(it.handle);
        }
        _finished_requests.clear();
        _newly_finished_count.store(0);

        for (auto it : _requests_data)
        {
            if (it.second.has_fetch_data)
            {
                hrz_js_fetch_clean(it.second.handle);
            }
        }
        _requests_data.clear();

        _finished_requests_data_size = 0;
        _retrieved_requests_data_size = 0;
    }

    void enqueue_message(Message&& msg)
    {
        const std::lock_guard<std::mutex> lock(_queue_mutex);
        _queue.push_back(std::move(msg));
        _queue_size.fetch_add(1);
    }

private:
    void start_request_in_slot(AddRequestMsg& typed_msg, size_t slot_index)
    {
        RequestSlot* req = &_slots[slot_index];
        req->status = HttpRequestStatus::Loading;
        req->ticket = typed_msg.ticket;
        req->url.swap(typed_msg.url);
        req->range_start = typed_msg.range_start;
        req->range_size = typed_msg.range_size;
        req->response_headers.clear();

        std::string headers_json;
        {
            rapidjson::Document doc;
            doc.SetArray();

            for (const auto& header : typed_msg.headers)
            {
                rapidjson::Value name, value;

                // This only works because we guarantee those strings are null-terminated.
                // Be careful with this!
                name.SetString(header.second.name.data(), doc.GetAllocator());
                value.SetString(header.second.value.data(), doc.GetAllocator());

                doc.PushBack(name, doc.GetAllocator());
                doc.PushBack(value, doc.GetAllocator());
            }

            rapidjson::StringBuffer buffer;
            rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
            doc.Accept(writer);

            headers_json = buffer.GetString();
        }

        // @Workaround(011-Chromium-RangeRequests-Cache)
        if (req->range_size > 0 || req->range_start > 0)
        {
            uint64_t hash = hrz::hash_values(req->range_start, req->range_size);
            req->url =
                hrz::url::append_query_parameters(req->url, {{"_hrz", fmt::format("{:x}", hash)}});
        }

        req->handle = hrz_js_fetch_data(
            req->url.data(), (double)req->range_start, (double)req->range_size,
            headers_json.c_str(), "GET", nullptr, on_request_load, on_request_error, this,
            slot_index);

        _running_requests.insert({req->ticket, slot_index});
        _running_requests_count.fetch_add(1);
    }

    void set_request_status_from_code(RequestSlot* req)
    {
        req->status = (req->status_code >= 200 && req->status_code < 300)
            ? HttpRequestStatus::Loaded
            : HttpRequestStatus::Error;
    }

    static void on_request_load(int handle, int code, void* ptr_arg, int int_arg)
    {
        assert(ptr_arg);
        ((EmscriptenHttpLoader*)ptr_arg)->finish_request_on_load(int_arg, handle, code);
    }

    void finish_request_on_load(size_t slot_index, int handle, int code)
    {
        RequestSlot* req = &_slots[slot_index];

        if (req->handle == handle)
        {
            const char* headers_json_str = hrz_js_fetch_get_headers(handle);
            if (headers_json_str)
            {
                rapidjson::Document doc;
                doc.Parse(headers_json_str);
                if (doc.IsArray())
                {
                    const auto& array = doc.GetArray();
                    int header_count = array.Size() / 2;
                    for (int i = 0; i < header_count; ++i)
                    {
                        const auto& name = array[i * 2 + 0];
                        const auto& value = array[i * 2 + 1];
                        if (name.IsString() && name.GetStringLength() > 0 && value.IsString())
                        {
                            req->response_headers.set_header(name.GetString(), value.GetString());
                        }
                    }
                }
            }

            req->status_code = code;
            set_request_status_from_code(req);
            finish_running_request(req, slot_index);
        }
    }

    static void on_request_error(
        int handle,
        int error_code,
        const char* message,
        void* ptr_arg,
        int int_arg)
    {
        assert(ptr_arg);
        ((EmscriptenHttpLoader*)ptr_arg)
            ->finish_request_on_error(int_arg, handle, error_code, message);
    }

    void finish_request_on_error(size_t slot_index, int handle, int error_code, const char* message)
    {
        RequestSlot* req = &_slots[slot_index];

        if (req->handle == handle)
        {
            req->status_code = error_code;
            set_request_status_from_code(req);
            finish_running_request(req, slot_index);
        }
    }

    void finish_running_request(RequestSlot* req, size_t slot_index)
    {
        assert(_running_requests.at(req->ticket) == slot_index);

        _running_requests.erase(req->ticket);
        finish_request_in_slot(req, slot_index);
    }

    void finish_request_in_slot(RequestSlot* req, size_t slot_index)
    {
        FinishedRequest finished_request;

        assert(
            req->status == HttpRequestStatus::Loaded || req->status == HttpRequestStatus::Canceled
            || req->status == HttpRequestStatus::Error);

        finished_request.metadata.ticket = req->ticket;
        finished_request.metadata.code = req->status_code;
        finished_request.metadata.status = req->status;
        finished_request.metadata.content_type =
            std::string(req->response_headers.get_header("Content-Type"));
        finished_request.metadata.headers.swap(req->response_headers);
        finished_request.metadata.data_size =
            req->status == HttpRequestStatus::Loaded ? hrz_js_fetch_get_data_size(req->handle) : 0;
        finished_request.handle = req->handle;

        if (req->status == HttpRequestStatus::Loaded)
        {
            std::lock_guard<std::mutex> lock(_data_mutex);

            _requests_data.insert(
                {req->ticket,
                 {
                     finished_request.handle,
                     finished_request.metadata.data_size,
                     true,
                     std::nullopt,
                     false,
                     false,
                 }});

            _finished_requests_data_size += finished_request.metadata.data_size;
        }

        {
            std::lock_guard<std::mutex> lock(_newly_finished_mutex);

            _newly_finished_requests.push_back(std::move(finished_request));
            _newly_finished_count.fetch_add(1);
        }

        _available_slots.push_back(slot_index);
        _available_slots_count.fetch_add(1);
    }

    void worker_work()
    {
        HRZ_SCOPED_SAMPLE("work");

        if (!_running)
        {
            emscripten_cancel_main_loop();
            hrz::profiling::destroy_thread_profiler(_profiler);
            hrz::metrics::destroy_thread_registry(_metrics);
            return;
        }

        // Dequeue messages
        while (_queue_size.load() > 0)
        {
            HRZ_SCOPED_SAMPLE("dequeue messages");

            Message msg;
            {
                std::lock_guard<std::mutex> lock(_queue_mutex);

                msg = std::move(_queue.front());
                _queue.pop_front();
                _queue_size.fetch_sub(1);
            }

            switch (msg.index())
            {
                case index_of_variant<Message, AddRequestMsg>():
                {
                    AddRequestMsg& typed_msg = std::get<AddRequestMsg>(msg);

                    assert(!_available_slots.empty());
                    size_t slot_index = _available_slots.back();
                    _available_slots.pop_back();

                    start_request_in_slot(typed_msg, slot_index);
                    break;
                }
                case index_of_variant<Message, CancelRequestMsg>():
                {
                    CancelRequestMsg& typed_msg = std::get<CancelRequestMsg>(msg);

                    auto it = _running_requests.find(typed_msg.ticket);
                    if (it != _running_requests.end())
                    {
                        size_t slot_index = it->second;
                        _running_requests.erase(it);
                        _running_requests_count.fetch_sub(1);

                        RequestSlot* req = &_slots[slot_index];
                        req->status = HttpRequestStatus::Canceled;

                        hrz_js_fetch_abort(req->handle);
                        hrz_js_fetch_clean(req->handle);

                        finish_request_in_slot(req, slot_index);
                    }

                    break;
                }
                case index_of_variant<Message, CopyRequestMsg>():
                {
                    CopyRequestMsg& typed_msg = std::get<CopyRequestMsg>(msg);

                    std::lock_guard<std::mutex> lock(_data_mutex);

                    auto it = _requests_data.find(typed_msg.ticket);
                    if (it != _requests_data.end())
                    {
                        RequestData& data = it->second;

                        if (data.has_fetch_data && !data.has_data && data.data_dst.has_value()
                            && data.data_dst->size_bytes() == data.data_size)
                        {
                            hrz_js_fetch_copy_data(
                                data.handle, data.data_dst->data(), data.data_size);
                            data.has_data = true;
                            data.is_data_owned = false;
                        }
                    }
                    break;
                }
                case index_of_variant<Message, ReleaseDataMsg>():
                {
                    ReleaseDataMsg& typed_msg = std::get<ReleaseDataMsg>(msg);

                    std::lock_guard<std::mutex> lock(_data_mutex);

                    auto it = _requests_data.find(typed_msg.ticket);
                    if (it != _requests_data.end())
                    {
                        RequestData& data = it->second;

                        if (data.has_fetch_data)
                        {
                            hrz_js_fetch_clean(data.handle);
                            data.has_fetch_data = false;
                            _finished_requests_data_size -= data.data_size;
                        }

                        _requests_data.erase(it);
                    }
                    break;
                }
                default: break;
            }
        }

        hrz::metrics::finish_thread_registry_frame();
        hrz::metrics::synchronize_thread_registry();
        hrz::profiling::synchronize_thread_profiler();
    }

    static void worker_loop_func(void* loader) { ((EmscriptenHttpLoader*)loader)->worker_work(); }

    void worker_init_func()
    {
        _profiler = hrz::profiling::create_thread_profiler("Asset loader");
        _metrics = hrz::metrics::create_thread_registry(true);

        // JS events, including fetch (XHR) requests, must be handled by the
        // JS VM to advance. Otherwise they're stuck, and fetch requests are
        // not even started.
        // To advance these events, the Wasm VM must yield execution back to
        // the JS VM. Simply waiting, for example with a condition_variable,
        // does not work, as the JS thread of the worker isn't unblocked.
        // The only way to make the execution flow go back to the JS code is
        // to return from the Wasm (C++) thread.
        // However if we returned, the thread would obviously be terminated,
        // and we would have to create a new one each time we want to de-
        // queue messages. But creating a new thread does not guarantee that
        // it gets executed on the same worker as the first thread. This is
        // a problem because we manage JS objects (the XHR requests) from
        // the C++ side, and JS objects are not shared among multiple wor-
        // kers. To properly manage our requests, we must be sure that each
        // time we execute C++ code, we are on the same JS worker.
        // We can call C++ from the JS side, which ensures that we are on
        // the right worker, but if done in hrz_js_fetch_* function it
        // would only work when reacting to JS events. We also need to de-
        // queue C++ events. To have the C++ code called from JS, often
        // enough that we can dequeue events as they appear, we rely on
        // Emscripten's main loop mechanism (which is not only for the main
        // thread).
        // (Calling emscripten_current_thread_process_queued_calls() allows
        // running calls made from other threads with
        // emscripten_dispatch_to_thread(), but not XHR callbacks. And even
        // then, it wouldn't have been really simpler, as waiting on a con-
        // dition would have needed a timeout for processing queued calls,
        // and it would have been conceptually close to using a main loop.)
        //      -tpetillon, 2023-03-01
        emscripten_set_main_loop_arg(worker_loop_func, (void*)this, 0, 0);
    }

public:
    bool start_request(
        HttpTicket ticket,
        std::string_view url_s,
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

        std::string url;
        if (!url_s.starts_with("data:") && hrz::url::is_relative(url_s))
        {
            url = hrz::url::join({_base_url, url_s});
        }
        else
        {
            url = std::string(url_s);
        }

        enqueue_message(Message(AddRequestMsg{ticket, url, range_start, range_size, headers}));

        return true;
    }

    void cancel_request(HttpTicket ticket) override
    {
        free_data(ticket);

        _canceled_requests.insert(ticket);
        enqueue_message(Message(CancelRequestMsg{ticket}));
    }

    void work() override { HRZ_SET_GAUGE("Running requests", _running_requests_count.load(), {}); }

    std::optional<ResponseMetadata> dequeue_finished_request() override
    {
        if (_newly_finished_count == 0) return std::nullopt;

        std::lock_guard<std::mutex> lock(_newly_finished_mutex);

        while (!_newly_finished_requests.empty())
        {
            auto request = std::move(_newly_finished_requests.front());
            _newly_finished_requests.pop_front();
            _newly_finished_count.fetch_sub(1);

            auto canceled_it = _canceled_requests.find(request.metadata.ticket);
            if (canceled_it != _canceled_requests.end())
            {
                _canceled_requests.erase(canceled_it);
            }
            else
            {
                ResponseMetadata metadata = request.metadata;
                _finished_requests.insert({request.metadata.ticket, std::move(request)});
                return {metadata};
            }
        }

        return std::nullopt;
    }

    bool copy_data(HttpTicket ticket, std::span<std::byte> dst) override
    {
        {
            std::lock_guard<std::mutex> lock(_data_mutex);

            auto it = _requests_data.find(ticket);
            if (it != _requests_data.end() && it->second.data_size == dst.size_bytes()
                && !it->second.has_data && !it->second.data_dst.has_value())
            {
                it->second.data_dst = {dst};
            }
            else
            {
                return false;
            }
        }

        enqueue_message(Message(CopyRequestMsg{ticket}));

        return true;
    }

    bool is_data_copied(HttpTicket ticket) override
    {
        std::lock_guard<std::mutex> lock(_data_mutex);

        auto it = _requests_data.find(ticket);
        if (it != _requests_data.end())
        {
            RequestData& data = it->second;

            return data.data_dst.has_value() && data.has_data;
        }

        return false;
    }

    void free_data(HttpTicket ticket) override
    {
        _finished_requests.erase(ticket);

        {
            std::lock_guard<std::mutex> lock(_data_mutex);

            auto it = _requests_data.find(ticket);
            if (it != _requests_data.end())
            {
                RequestData& data = it->second;

                if (data.has_data)
                {
                    assert(data.data_dst.has_value());

                    if (data.is_data_owned)
                    {
                        std::free(data.data_dst->data());
                        assert(_retrieved_requests_data_size >= data.data_size);
                        _retrieved_requests_data_size -= data.data_size;
                    }

                    data.has_data = false;
                }

                data.data_dst = std::nullopt;
            }
        }

        enqueue_message(Message(ReleaseDataMsg{ticket}));
    }

    void dev_ui(mu_Context* ctx) override
    {
        if (mu_header(ctx, "Emscripten HTTP loader"))
        {
            fmt::memory_buffer buffer;

            buffer.clear();
            fmt::format_to(
                std::back_inserter(buffer), "{} requests in progress", _running_requests.size());
            buffer.push_back(0);
            mu_text(ctx, buffer.data());

            buffer.clear();
            hrz::bytes_to_string(_finished_requests_data_size, buffer, false, false);
            fmt::format_to(std::back_inserter(buffer), " stored (JavaScript)");
            buffer.push_back(0);
            mu_text(ctx, buffer.data());

            buffer.clear();
            hrz::bytes_to_string(_retrieved_requests_data_size, buffer, false, false);
            fmt::format_to(std::back_inserter(buffer), " stored (Wasm)");
            buffer.push_back(0);
            mu_text(ctx, buffer.data());
        }
    }
};

std::unique_ptr<IHttpLoader> create_platform_loader()
{
    return std::make_unique<EmscriptenHttpLoader>();
}

} // namespace hrz::assets_loader
