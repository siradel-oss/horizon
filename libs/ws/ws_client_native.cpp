#include "ws_client.h"

#include <websocketpp/client.hpp>
#include <websocketpp/config/asio_no_tls_client.hpp>

using RawClient = websocketpp::client<websocketpp::config::asio_client>;

struct OpenHandlerClient
{
    ws::Client* client;
    void operator ()(websocketpp::connection_hdl con);
};

struct CloseHandlerClient
{
    ws::Client* client;
    void operator ()(websocketpp::connection_hdl con);
};

struct FailHandlerClient
{
    ws::Client* client;
    void operator ()(websocketpp::connection_hdl con);
};

struct MessageHandlerClient
{
    ws::Client* client;
    void operator ()(websocketpp::connection_hdl, RawClient::message_ptr msg);
};

namespace ws
{

struct Client
{
    std::unique_ptr<RawClient> raw;
    bool connected = false;
    RawClient::connection_ptr connection;

    ClientHandler* current_handler = nullptr;

    OpenHandlerClient raw_open_handler;
    CloseHandlerClient raw_close_handler;
    MessageHandlerClient raw_message_handler;
    FailHandlerClient raw_fail_handler;

    Client() :
        raw_open_handler{this},
        raw_close_handler{this},
        raw_message_handler{this},
        raw_fail_handler{this}
    {
    }
};

} // namespace ws

void OpenHandlerClient::operator ()(websocketpp::connection_hdl con)
{
    client->connected = true;
    if (client->current_handler)
    {
        client->current_handler->on_server_connect();
    }
}

void CloseHandlerClient::operator ()(websocketpp::connection_hdl con)
{
    client->connected = false;
    if (client->current_handler)
    {
        client->current_handler->on_server_lost();
    }
}

void FailHandlerClient::operator ()(websocketpp::connection_hdl con)
{
    if (client->current_handler)
    {
        client->current_handler->on_connect_fail();
    }
}

void MessageHandlerClient::operator ()(websocketpp::connection_hdl, RawClient::message_ptr msg)
{
    if (client->current_handler
        && (msg->get_opcode() == websocketpp::frame::opcode::text
            || msg->get_opcode() == websocketpp::frame::opcode::binary))
    {
        client->current_handler->on_raw_message(
            msg->get_payload().data(), msg->get_payload().size());
    }
}

void ws::close_destroy(Client* client)
{
    if (client->connected)
    {
        websocketpp::lib::error_code ec;
        client->connection->close(websocketpp::close::status::normal, "Bye bye", ec);
    }

    client->raw->stop();
    delete client;
}

ws::Status ws::send_raw(Client* client, const void* data, size_t size)
{
    if (client->connected)
    {
        websocketpp::lib::error_code ec;
        client->raw->send(client->connection, data, size, websocketpp::frame::opcode::binary, ec);
        return ec ? Status::SendError : Status::Ok;
    }
    else
    {
        return Status::NotConnected;
    }
}

std::pair<ws::Client*, ws::Status> ws::create_connect(const char* uri)
{
    websocketpp::lib::error_code ec;

    std::unique_ptr<Client> client(new Client());
    std::unique_ptr<RawClient> raw(new RawClient());

    raw->init_asio(ec);
    if (ec) return std::make_pair(nullptr, Status::NotSupported);

#ifdef NDEBUG
    raw->clear_access_channels(websocketpp::log::alevel::all);
    raw->clear_error_channels(websocketpp::log::elevel::all);
#else
    raw->clear_access_channels(
        websocketpp::log::alevel::frame_payload | websocketpp::log::alevel::frame_header);
#endif

    raw->set_open_handler(client->raw_open_handler);
    raw->set_close_handler(client->raw_close_handler);
    raw->set_message_handler(client->raw_message_handler);
    raw->set_fail_handler(client->raw_fail_handler);
    raw->set_reuse_addr(true);

    client->raw = std::move(raw);

    client->connection = client->raw->get_connection(uri, ec);
    if (ec) return std::make_pair(nullptr, Status::ConnectionError);

    client->raw->connect(client->connection);

    return std::make_pair(client.release(), Status::Ok);
}

void ws::poll(Client* client, ClientHandler* handler)
{
    client->current_handler = handler;
    while (client->raw->poll())
    {
    }
    client->current_handler = nullptr;
}
