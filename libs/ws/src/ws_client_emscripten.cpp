#include "ws_client.h"

#include <emscripten/websocket.h>
#include <gsl/gsl-lite.hpp>

#include <memory>
#include <stdint.h>
#include <vector>

enum class EventType
{
    Open,
    Close,
    Message,
    Fail,
};

struct Event
{
    EventType type;
    size_t offset;
    size_t size;
};

namespace ws
{
struct Client
{
    EMSCRIPTEN_WEBSOCKET_T ws;
    bool connected = false;

    std::vector<std::byte> buffer;
    std::vector<Event> events;
};

} // namespace ws

EM_BOOL on_open(int event_type, const EmscriptenWebSocketOpenEvent* event, void* user_data)
{
    ws::Client* client = (ws::Client*)user_data;
    client->events.push_back(Event{EventType::Open, 0, 0});
    client->connected = true;
    return EM_TRUE;
}

EM_BOOL on_close(int event_type, const EmscriptenWebSocketCloseEvent* event, void* user_data)
{
    ws::Client* client = (ws::Client*)user_data;
    client->events.push_back(Event{EventType::Close, 0, 0});
    client->connected = false;
    return EM_TRUE;
}

EM_BOOL on_message(int event_type, const EmscriptenWebSocketMessageEvent* event, void* user_data)
{
    ws::Client* client = (ws::Client*)user_data;

    size_t offset = client->buffer.size();
    size_t size = event->numBytes;

    client->buffer.resize(offset + size);
    memcpy(client->buffer.data() + offset, event->data, size);

    client->events.push_back(Event{EventType::Message, offset, size});
    return EM_TRUE;
}

EM_BOOL on_error(int event_type, const EmscriptenWebSocketErrorEvent* event, void* user_data)
{
    ws::Client* client = (ws::Client*)user_data;
    client->events.push_back(Event{EventType::Fail, 0, 0});
    return EM_TRUE;
}

std::pair<ws::Client*, ws::Status> ws::create_connect(const char* uri)
{
    if (!emscripten_websocket_is_supported()) return std::make_pair(nullptr, Status::NotSupported);

    EmscriptenWebSocketCreateAttributes attrs;
    emscripten_websocket_init_create_attributes(&attrs);
    attrs.url = uri;
    attrs.createOnMainThread = false;

    EMSCRIPTEN_WEBSOCKET_T ws = emscripten_websocket_new(&attrs);
    if (ws <= 0) return std::make_pair(nullptr, Status::ConnectionError);

    std::unique_ptr<Client> client(new Client());

    emscripten_websocket_set_onopen_callback(ws, client.get(), on_open);
    emscripten_websocket_set_onclose_callback(ws, client.get(), on_close);
    emscripten_websocket_set_onmessage_callback(ws, client.get(), on_message);
    emscripten_websocket_set_onerror_callback(ws, client.get(), on_error);

    client->ws = ws;
    return std::make_pair(client.release(), Status::Ok);
}

void ws::close_destroy(Client* client)
{
    if (client->connected)
    {
        emscripten_websocket_close(client->ws, 1000, "Bye bye");
    }
    emscripten_websocket_delete(client->ws);
    delete client;
}

ws::Status ws::send_raw(Client* client, const void* data, size_t size)
{
    if (client->connected)
    {
        if (emscripten_websocket_send_binary(client->ws, (void*)data, (uint32_t)size) < 0)
        {
            return Status::SendError;
        }
        else
        {
            return Status::Ok;
        }
    }
    else
    {
        return Status::NotConnected;
    }
}

void ws::poll(Client* client, ClientHandler* handler)
{
    for (const auto& event : client->events)
    {
        switch (event.type)
        {
            case EventType::Open: handler->on_server_connect(); break;
            case EventType::Close: handler->on_server_lost(); break;
            case EventType::Fail: handler->on_connect_fail(); break;
            case EventType::Message:
                handler->on_raw_message(client->buffer.data() + event.offset, event.size);
                break;
        }
    }

    client->events.clear();
    client->buffer.clear();
}
