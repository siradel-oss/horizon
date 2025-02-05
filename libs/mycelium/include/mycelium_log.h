#pragma once

#include <stddef.h>
#include <stdint.h>

namespace my
{
enum class LogSeverity : uint8_t
{
    Debug = 0,
    Info,
    Warning,
    Error,
};

typedef void (*LogFunction)(
    const char* prefix,
    LogSeverity severity,
    const char* message,
    size_t message_length,
    const char* file,
    int line);

void set_log_filter_level(LogSeverity filter_level);
void set_log_callback(LogFunction callback);

} // namespace my
