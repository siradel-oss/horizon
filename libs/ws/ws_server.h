#pragma once

#include <ws_common.h>

#include <stddef.h>
#include <stdint.h>
#include <utility>

namespace ws
{

struct ServerHandler
{
    virtual void on_client_connect() = 0;
    virtual void on_client_disconnect() = 0;
    virtual void on_raw_message(const void* data, size_t size) = 0;
};

struct Server;

std::pair<Server*, Status> create_server(uint16_t port);
void destroy_server(Server*);

Status send_raw(Server*, const void* data, size_t size);
Status end_connection(Server*, const char* reason);
void poll(Server*, ServerHandler* handler);

} // namespace ws
