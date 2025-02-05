#include "server.h"

#include "data.h"

#include <hrz_monitoring.h>

#include <gsl/gsl-lite.hpp>
#include <ws_server.h>

#include <cassert>
#include <string>

const std::string UNKNOWN_SESSION_ID = "0";

namespace server
{
struct Handler : public ws::ServerHandler
{
    ws::Server* _server = nullptr;
    uint16_t _port = 8500;

    ws::Status _last_status;

    hrz_monitoring::MessageBuffer* _message_buffer = nullptr;

    bool _client_connected = false;
    bool _client_first_message_received = false;
    bool _client_rejected = false;
    bool _stuck = false;

    bool _database_clear_allowed = false;

    std::string _session_id = "";

    data::Database* _database;

    Handler() { _message_buffer = hrz_monitoring::create_buffer(); }

    ~Handler() { hrz_monitoring::destroy_buffer(_message_buffer); }

    State status()
    {
        if (!_server) return State::Off;
        if (!_client_connected) return State::WaitingForConnection;
        return State::ClientConnected;
    }

    bool start(uint16_t port)
    {
        assert(status() == State::Off);

        auto connection = ws::create_server(port);

        _last_status = connection.second;
        _port = port;
        _client_connected = false;
        _client_first_message_received = false;
        _client_rejected = false;
        _stuck = false;
        _session_id = "";

        if (_last_status != ws::Status::Ok)
        {
            return false;
        }

        _server = connection.first;
        return true;
    }

    void close()
    {
        if (status() == State::Off) return;

        ws::destroy_server(_server);
        _server = nullptr;
    }

    void poll(data::Database& database)
    {
        if (status() == State::Off) return;

        if (_stuck)
        {
            close();
            return;
        }

        _database = &database;
        ws::poll(_server, this);

        if (_client_rejected)
        {
            hrz_monitoring_proto::MonitoringMessage denial_message;
            denial_message.mutable_server_denial()->set_message(
                "The server will currently only accept the client that was last connected.");

            hrz_monitoring::push_message(_message_buffer, denial_message);
            auto raw_message = hrz_monitoring::get_written_data(_message_buffer);
            ws::send_raw(_server, raw_message.data(), raw_message.size());
            hrz_monitoring::reset_buffer(_message_buffer);

            ws::end_connection(
                _server,
                "Session id mismatch (Restart the monitoring server to begin a new session)");
            _client_rejected = false;
        }
    }

    virtual void on_client_connect()
    {
        _client_connected = true;
        _client_first_message_received = false;
    }

    virtual void on_client_disconnect()
    {
        _client_connected = false;

        // After the first client's connection, the server will only accept clients sending
        // a "Hello" message with an identical session_id.
        //
        // If the current client has disconnected without giving a session_id (like older versions
        // of Horizon), then we have no way of ensuring that the next connexion will correspond to
        // the same monitoring session. We want to close the server to avoid polluting the previous
        // session data.
        if (_session_id.empty() && !_database_clear_allowed)
        {
            _stuck = true;
        }
    }

    virtual void on_raw_message(const void* data, size_t size)
    {
        gsl::span<std::byte> byte_data((std::byte*)data, size);

        hrz_monitoring::parse_messages(
            byte_data,
            [this](const hrz_monitoring_proto::MonitoringMessage* message)
            {
                if (!_client_first_message_received)
                {
                    if (!message->has_client_hello())
                    {
                        _client_rejected = true;
                        return;
                    }

                    bool client_accepted =
                        _session_id.empty() || _session_id == message->client_hello().session_id();

                    if (!client_accepted)
                    {
                        if (_database_clear_allowed && !_database->empty())
                        {
                            _database->clear();
                        }
                        else
                        {
                            _client_rejected = true;
                            return;
                        }
                    }

                    _client_first_message_received = true;
                    _session_id = message->client_hello().session_id();
                }

                _database->register_message(message);
            });

        _database->process_messages();
    }
};

static Handler s_handler;

bool start(uint16_t port)
{
    return s_handler.start(port);
}

void close()
{
    s_handler.close();
}

void poll(data::Database& database)
{
    s_handler.poll(database);
}

State status()
{
    return s_handler.status();
}

uint16_t port()
{
    return s_handler._port;
}

bool has_session_id()
{
    return !s_handler._session_id.empty();
}

bool is_database_clear_allowed()
{
    return s_handler._database_clear_allowed;
}

void allow_database_clear(bool value)
{
    s_handler._database_clear_allowed = value;
}

bool error()
{
    return s_handler._last_status != ws::Status::Ok;
}

const char* error_string()
{
    return ws::to_string(s_handler._last_status);
}

} // namespace server
