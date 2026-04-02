#pragma once

#include <stdint.h>

namespace data
{

class Database;

}

namespace server
{

enum class State
{
    WaitingForConnection,
    ClientConnected,
    Off
};

bool start(uint16_t port);
void close();

void poll(data::Database&);

State status();
uint16_t port();

bool has_session_id();

bool is_database_clear_allowed();
void allow_database_clear(bool value);

bool error();
const char* error_string();

} // namespace server
