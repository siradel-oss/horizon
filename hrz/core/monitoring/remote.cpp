#include "hrz/core/monitoring/remote.h"

#include "hrz/common/blob_allocator.h"
#include "hrz/common/metrics.h"
#include "hrz/common/profiling.h"
#include "hrz/core/client_messages.h"
#include "hrz/core/clock.h"
#include "hrz/core/monitoring/monitoring.h"
#include "hrz/monitoring/monitoring.h"
#include "hrz/protocol/monitoring/message.pb.h"
#include "hrz/version/version.h"

#include <fmt/chrono.h>
#include <string.h>
#include <ws_client.h>

#include <string>

extern "C"
{
#include <microui/microui.h>
}

using PbArena = google::protobuf::Arena;

namespace
{

std::string generate_session_id()
{
    auto now = std::chrono::system_clock::now();
    return fmt::format("{:%H%M%S%d%m%y}", now);
}

} // namespace

namespace hrz
{
struct RemoteMonitoring : public ws::ClientHandler
{
    enum class Status
    {
        NotConnected,
        Connecting,
        Connected,
        Disconnected,
        FailedToConnect,
        Denied,
    };

    PbArena _messages_arena;
    hrz_monitoring::MessageBuffer* _message_buffer{};

    std::string _session_id;

    ws::Client* _client = nullptr;
    Status _status = Status::NotConnected;
    std::vector<monitoring::ConnectionCallback> _connection_callbacks;

    std::string _last_error;
    std::string _denial_message;
    char _address_buffer[128] = "ws://127.0.0.1:8500";

    static constexpr double SEND_MESSAGES_INTERVAL_MS = 1000.0;
    double _last_send_messages_ms = hrz::clock::CurrentFrameRealTime.ms;

    bool _gpu_resources_snapshot_requested = false;
    bool _blob_snapshot_requested = false;

    bool _thread_names_dumped = false;

    bool _send_to_message_queue = false;

    virtual ~RemoteMonitoring() = default;

    void push_messages(const std::function<void(hrz_monitoring::MonitoringMessages*)>& callback)
    {
        auto* msgs = PbArena::Create<hrz_monitoring::MonitoringMessages>(&_messages_arena);

        callback(msgs);

        if (msgs->messages_size() > 0)
        {
            hrz_monitoring::push_messages(_message_buffer, *msgs);
        }

        _messages_arena.Reset();
    }

    void send_messages(ClientMessageQueue* queue)
    {
        std::span<const std::byte> data = hrz_monitoring::get_written_data(_message_buffer);
        if (_status == Status::Connected)
        {
            ws::send_raw(_client, data.data(), data.size());
        }
        if (_send_to_message_queue)
        {
            hrz_proto::MonitoringDataMessage message;
            message.set_data(data.data(), data.size());
            client_message_queue::enqueue_monitoring_data_message(queue, std::move(message));
        }
        hrz_monitoring::reset_buffer(_message_buffer);
    }

    void try_connect()
    {
        auto result = ws::create_connect(_address_buffer);
        if (result.second == ws::Status::Ok)
        {
            _last_error.clear();
            _status = RemoteMonitoring::Status::Connecting;
            _client = result.first;
        }
        else
        {
            _last_error = fmt::format(
                "Couldn't connect to {}: {}", _address_buffer, ws::to_string(result.second));
            _status = RemoteMonitoring::Status::NotConnected;
        }
    }

    void on_connect_fail() override
    {
        _status = Status::FailedToConnect;
        for (auto& callback : _connection_callbacks)
        {
            callback(false);
        }
        _connection_callbacks.clear();
    }

    void on_server_connect() override
    {
        push_messages(
            [this](hrz_monitoring::MonitoringMessages* msgs)
            {
                auto* msg = msgs->add_messages();
                auto* hello = msg->mutable_client_hello();
                hello->set_client_version(hrz::Version);
                hello->set_session_id(_session_id);
            });

        _status = Status::Connected;
        _thread_names_dumped = false;

        for (auto& callback : _connection_callbacks)
        {
            callback(true);
        }
        _connection_callbacks.clear();
    }

    void on_server_lost() override { _status = Status::Disconnected; }

    void on_raw_message(const void* data, size_t size) override
    {
        hrz_monitoring::parse_messages(
            {(const std::byte*)data, size},
            [this](const hrz_monitoring_proto::MonitoringMessage* message)
            {
                if (message->has_server_denial())
                {
                    _status = Status::Denied;
                    _denial_message = message->server_denial().message();
                }
            });
    }

    void disconnect()
    {
        ws::close_destroy(_client);
        _client = nullptr;
        _status = RemoteMonitoring::Status::NotConnected;
        _connection_callbacks.clear();
        hrz_monitoring::reset_buffer(_message_buffer);
    }

    void work(
        const hrz::Monitoring* monitoring,
        hrz::ClientMessageQueue* queue,
        const hrz::BlobAllocator* ba,
        const hrz::LayersInfo* layers_info)
    {
        if (_client)
        {
            ws::poll(_client, this);
        }

        if (_status == Status::FailedToConnect)
        {
            disconnect();
            _last_error = fmt::format("Couldn't establish connection to {}", _address_buffer);
        }
        else if (_status == Status::Disconnected)
        {
            disconnect();
            _last_error = fmt::format("Connection to {} lost", _address_buffer);
        }
        else if (_status == Status::Denied)
        {
            disconnect();
            _last_error =
                fmt::format("Connection to {} denied: {}", _address_buffer, _denial_message);
        }

        if (_status == Status::Connected || _send_to_message_queue)
        {
            work_connected(monitoring, queue, ba, layers_info);
        }
    }

    void work_connected(
        const hrz::Monitoring* monitoring,
        hrz::ClientMessageQueue* queue,
        const hrz::BlobAllocator* ba,
        const hrz::LayersInfo* layers_info)
    {
        assert(_status == Status::Connected);

        double now = hrz::clock::CurrentFrameRealTime.ms;
        bool force_send_now = false;

        if (!_thread_names_dumped)
        {
            hrz_monitoring_proto::MonitoringMessage message;
            auto* threads = message.mutable_threads();

            hrz::profiling::dump_thread_names(
                [&](uint32_t thread_id, std::string_view thread_name)
                {
                    auto* thread = threads->add_threads();
                    thread->set_id(thread_id);
                    *(thread->mutable_name()) = std::string(thread_name);
                });

            hrz_monitoring::push_message(_message_buffer, message);
            _thread_names_dumped = true;
        }

        if (_gpu_resources_snapshot_requested)
        {
            monitoring->dump_gpu_resources(layers_info, _message_buffer);
            _gpu_resources_snapshot_requested = false;
            force_send_now = true;
        }

        if (_blob_snapshot_requested)
        {
            blobs::dump_blobs(ba, layers_info, _message_buffer);
            _blob_snapshot_requested = false;
            force_send_now = true;
        }

        if (force_send_now || now - _last_send_messages_ms > SEND_MESSAGES_INTERVAL_MS)
        {
            profiling::flush_messages(
                [this](const hrz_monitoring::MessageBuffer* buffer)
                { hrz_monitoring::append_messages(_message_buffer, buffer); });
            metrics::flush_messages([this](const hrz_monitoring::MessageBuffer* buffer)
                                    { hrz_monitoring::append_messages(_message_buffer, buffer); });

            send_messages(queue);
            _last_send_messages_ms = now;
        }
    }
};

namespace monitoring
{
RemoteMonitoring* create_remote_monitoring()
{
    RemoteMonitoring* mon = new RemoteMonitoring();

    mon->_message_buffer = hrz_monitoring::create_buffer();
    mon->_session_id = generate_session_id();

    return mon;
}

void destroy(RemoteMonitoring* mon)
{
    hrz_monitoring::destroy_buffer(mon->_message_buffer);
    delete mon;
}

void work(
    RemoteMonitoring* mon,
    ClientMessageQueue* queue,
    const Monitoring* core_mon,
    const BlobAllocator* allocator,
    const LayersInfo* layers_info)
{
    mon->work(core_mon, queue, allocator, layers_info);
}

void set_monitoring_server_address(RemoteMonitoring* mon, std::string_view address)
{
    size_t size = std::min(sizeof(mon->_address_buffer) - 1, address.size());
    memcpy(mon->_address_buffer, address.data(), size);
    mon->_address_buffer[size] = '\0';
}

bool is_connected(const RemoteMonitoring* mon)
{
    return mon->_status == RemoteMonitoring::Status::Connected;
}

void try_connect(RemoteMonitoring* mon, const ConnectionCallback& callback)
{
    if (mon->_status == RemoteMonitoring::Status::Connected) return;

    if (callback)
    {
        mon->_connection_callbacks.push_back(callback);
    }

    if (mon->_status == RemoteMonitoring::Status::Connecting) return;

    mon->try_connect();
}

void disconnect(RemoteMonitoring* mon)
{
    if (mon->_status != RemoteMonitoring::Status::Connected
        && mon->_status != RemoteMonitoring::Status::Connecting)
        return;
    mon->disconnect();
}

void schedule_gpu_snapshot(RemoteMonitoring* mon)
{
    mon->_gpu_resources_snapshot_requested = true;
}

void schedule_blob_allocator_snapshot(RemoteMonitoring* mon)
{
    mon->_blob_snapshot_requested = true;
}

void set_message_queue_sending_enabled(RemoteMonitoring* mon, bool enabled)
{
    mon->_send_to_message_queue = enabled;
}

void draw_remote_connection(RemoteMonitoring* mon, JobScheduler* job_scheduler, mu_Context* ctx)
{
    switch (mon->_status)
    {
        case RemoteMonitoring::Status::NotConnected:
        {
            static int layout[] = {-1};
            mu_layout_row(ctx, 1, layout, 0);

            mu_text(ctx, "Monitoring server address");

            bool should_connect = false;

            if (mu_textbox(ctx, mon->_address_buffer, sizeof(mon->_address_buffer)) & MU_RES_SUBMIT)
            {
                should_connect = true;
            }

            if (mu_button(ctx, "Connect..."))
            {
                should_connect = true;
            }

            if (!mon->_last_error.empty())
            {
                mu_text_color(ctx, mon->_last_error.c_str(), mu_Color{255, 55, 55, 255});
            }

            if (should_connect)
            {
                mon->try_connect();
            }
            break;
        }
        case RemoteMonitoring::Status::Connecting:
        {
            static int layout[] = {-1};
            mu_layout_row(ctx, 1, layout, 0);

            std::string str = fmt::format("Connecting to {}...", mon->_address_buffer);
            mu_text(ctx, str.c_str());

            break;
        }
        case RemoteMonitoring::Status::Connected:
        {
            static int layout[] = {-1};
            mu_layout_row(ctx, 1, layout, 0);

            std::string str = fmt::format("Connected to {}.", mon->_address_buffer);
            mu_text(ctx, str.c_str());

            if (mu_button(ctx, "GPU resources snapshot"))
            {
                mon->_gpu_resources_snapshot_requested = true;
            }

            if (mu_button(ctx, "Blob allocator snapshot"))
            {
                mon->_blob_snapshot_requested = true;
            }

            if (mu_button(ctx, "Disconnect"))
            {
                mon->disconnect();
            }
            break;
        }
        default: break;
    }
}

} // namespace monitoring
} // namespace hrz
