#include "ws_server.h"

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

using RawServer = websocketpp::server<websocketpp::config::asio>;

struct OpenHandlerServer
{
    ws::Server* server;
    void operator()(websocketpp::connection_hdl con);
};

struct CloseHandlerServer
{
    ws::Server* server;
    void operator()(websocketpp::connection_hdl con);
};

struct ValidateHandlerServer
{
    ws::Server* server;
    bool operator()(websocketpp::connection_hdl con);
};

struct MessageHandlerServer
{
    ws::Server* server;
    void operator()(websocketpp::connection_hdl, RawServer::message_ptr msg);
};

namespace ws
{
struct Server
{
    std::unique_ptr<RawServer> raw;
    bool accepting_connection = true;
    websocketpp::connection_hdl connection;

    ServerHandler* current_handler = nullptr;

    OpenHandlerServer raw_open_handler;
    CloseHandlerServer raw_close_handler;
    ValidateHandlerServer raw_validate_handler;
    MessageHandlerServer raw_message_handler;

    Server() :
        raw_open_handler{this},
        raw_close_handler{this},
        raw_validate_handler{this},
        raw_message_handler{this}
    {
    }
};

} // namespace ws

bool ValidateHandlerServer::operator()(websocketpp::connection_hdl con)
{
    return server->accepting_connection;
}

void OpenHandlerServer::operator()(websocketpp::connection_hdl con)
{
    assert(server->accepting_connection);
    server->accepting_connection = false;
    server->current_handler->on_client_connect();
    server->connection = con;
}

void CloseHandlerServer::operator()(websocketpp::connection_hdl con)
{
    assert(!server->accepting_connection);
    server->accepting_connection = true;
    server->current_handler->on_client_disconnect();
    server->connection.reset();
}

void MessageHandlerServer::operator()(websocketpp::connection_hdl, RawServer::message_ptr msg)
{
    if (msg->get_opcode() == websocketpp::frame::opcode::text
        || msg->get_opcode() == websocketpp::frame::opcode::binary)
    {
        assert(!server->accepting_connection);
        server->current_handler->on_raw_message(
            msg->get_payload().data(), msg->get_payload().size());
    }
}

std::pair<ws::Server*, ws::Status> ws::create_server(uint16_t port)
{
    websocketpp::lib::error_code ec;

    std::unique_ptr<RawServer> raw(new RawServer());
    std::unique_ptr<Server> server(new Server());

    raw->init_asio(ec);
    if (ec) return std::make_pair(nullptr, Status::NotSupported);

    raw->start_perpetual();

#ifdef NDEBUG
    raw->clear_access_channels(websocketpp::log::alevel::all);
    raw->clear_error_channels(websocketpp::log::elevel::all);
#else
    raw->clear_access_channels(
        websocketpp::log::alevel::frame_payload | websocketpp::log::alevel::frame_header);
#endif

    raw->set_reuse_addr(true);
    raw->set_listen_backlog(1);
    raw->set_open_handler(server->raw_open_handler);
    raw->set_close_handler(server->raw_close_handler);
    raw->set_validate_handler(server->raw_validate_handler);
    raw->set_message_handler(server->raw_message_handler);

    raw->listen(port, ec);
    if (ec) return std::make_pair(nullptr, Status::ListenError);

    raw->start_accept(ec);
    if (ec) return std::make_pair(nullptr, Status::AcceptError);

    server->raw = std::move(raw);

    return std::make_pair(server.release(), Status::Ok);
}

void ws::destroy_server(Server* server)
{
    websocketpp::lib::error_code ec;
    server->raw->stop_listening(ec);
    server->raw->stop_perpetual();
    delete server;
}

ws::Status ws::send_raw(Server* server, const void* data, size_t size)
{
    assert(server && !server->accepting_connection);
    if (!server->accepting_connection)
    {
        auto con = server->connection.lock();
        assert(con);
        if (con)
        {
            websocketpp::lib::error_code ec;
            server->raw->send(con, data, size, websocketpp::frame::opcode::binary, ec);
            return ec ? Status::SendError : Status::Ok;
        }
        else
        {
            return Status::NotConnected;
        }
    }
    else
    {
        return Status::NotConnected;
    }
}

ws::Status ws::end_connection(Server* server, const char* reason)
{
    assert(server && !server->accepting_connection);
    if (!server->accepting_connection)
    {
        auto con = server->connection.lock();
        assert(con);
        if (con)
        {
            websocketpp::lib::error_code ec;
            server->raw->close(con, websocketpp::close::status::normal, reason);
            return ec ? Status::SendError : Status::Ok;
        }
        else
        {
            return Status::NotConnected;
        }
    }
    else
    {
        return Status::NotConnected;
    }
}

void ws::poll(Server* server, ServerHandler* handler)
{
    server->current_handler = handler;
    while (server->raw->poll())
    {
    }
    server->current_handler = nullptr;
}
