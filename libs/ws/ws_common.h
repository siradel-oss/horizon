#pragma once

namespace ws
{

#define STATUS_MSGS             \
    STATUS_MSG(Ok)              \
    STATUS_MSG(NotSupported)    \
    STATUS_MSG(ConnectionError) \
    STATUS_MSG(NotConnected)    \
    STATUS_MSG(SendError)       \
    STATUS_MSG(ListenError)     \
    STATUS_MSG(AcceptError)

enum class Status
{
#define STATUS_MSG(NAME) NAME,
    STATUS_MSGS
#undef STATUS_MSG
};

static const char* to_string(Status s)
{
    switch (s)
    {
#define STATUS_MSG(NAME) \
    case Status::NAME: return #NAME;
        STATUS_MSGS
#undef STATUS_MSG
        default: return "Unknown";
    }
}

#undef STATUS_MSGS

} // namespace ws
