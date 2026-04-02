#pragma once

#include <ws_common.h>

#include <stddef.h>
#include <utility>

namespace ws
{

struct ClientHandler
{
    virtual ~ClientHandler() = default;

    virtual void on_server_connect() = 0;
    virtual void on_server_lost() = 0;
    virtual void on_raw_message(const void* data, size_t size) = 0;
    virtual void on_connect_fail() = 0;
};

struct Client;

std::pair<Client*, Status> create_connect(const char* uri);
void close_destroy(Client*);

Status send_raw(Client*, const void* data, size_t size);
void poll(Client*, ClientHandler*);

} // namespace ws
