#include "log.h"

#if __EMSCRIPTEN__
#    include <emscripten/emscripten.h>
#endif

#include <inttypes.h>

#include <iterator>

namespace my
{
namespace
{

const char* severity_to_string(LogSeverity severity)
{
    switch (severity)
    {
        case LogSeverity::Debug: return "Debug";
        case LogSeverity::Info: return "Info";
        case LogSeverity::Warning: return "WARNING";
        case LogSeverity::Error: return "ERROR";
        default: return "";
    }
}

#if __EMSCRIPTEN__
int severity_to_em_flag(LogSeverity severity)
{
    switch (severity)
    {
        case LogSeverity::Warning: return EM_LOG_WARN;
        case LogSeverity::Error: return EM_LOG_ERROR;
        default: return 0;
    }
}
#endif

} // namespace

constexpr size_t MAX_MESSAGE_LENGTH = 512;

LogSeverity g_log_filter_level = LogSeverity::Debug;
LogFunction g_log_function = nullptr;

void set_log_filter_level(LogSeverity filter_level)
{
    g_log_filter_level = filter_level;
}

void set_log_callback(LogFunction callback)
{
    g_log_function = callback;
}

void log_message(
    LogSeverity severity,
    const std::string& message,
    const std::source_location& location)
{
    if (severity < g_log_filter_level)
    {
        return;
    }

    if (g_log_function != nullptr)
    {
        g_log_function(severity, message.c_str(), message.size(), location);
    }
    else
    {
#ifdef __EMSCRIPTEN__
        std::string str;
        fmt::format_to_n(
            std::back_inserter(str), MAX_MESSAGE_LENGTH, "[{:^7}] ({}:{}) {}",
            severity_to_string(severity), location.file_name(), location.line(), message);
        emscripten_log(EM_LOG_CONSOLE | severity_to_em_flag(severity), str.c_str());
#else
        printf(
            "[%s] (%s:%" PRIuLEAST32 ") %s\n", severity_to_string(severity), location.file_name(),
            location.line(), message.c_str());
        fflush(stdout);
#endif
    }
}

} // namespace my
