#pragma once

#include "mycelium/log.h"

#include <fmt/core.h>
#include <fmt/format.h>

#include <source_location>
#include <string>

namespace my
{
// Don't call this directly, use the macros MY_LOG_DEBUG, MY_LOG_INFO,
// MY_LOG_WARNING and MY_LOG_ERROR.
void log_message(
    LogSeverity severity,
    const std::string& message,
    const std::source_location& location = std::source_location::current());

} // namespace my

#define MY_LOG_XSTRINGIFY(X) #X
#define MY_LOG_STRINGIFY(X) MY_LOG_XSTRINGIFY(X)

#define MY_LOG(SEVERITY, FMT, ...) \
    ::my::log_message(SEVERITY, ::fmt::format(FMT_STRING(FMT), ##__VA_ARGS__))

#ifndef NDEBUG
#    define MY_LOG_DEBUG(FMT, ...) MY_LOG(::my::LogSeverity::Debug, FMT, ##__VA_ARGS__)
#else
#    define MY_LOG_DEBUG(FMT, ...)
#endif

#define MY_LOG_INFO(FMT, ...) MY_LOG(::my::LogSeverity::Info, FMT, ##__VA_ARGS__)
#define MY_LOG_WARNING(FMT, ...) MY_LOG(::my::LogSeverity::Warning, FMT, ##__VA_ARGS__)
#define MY_LOG_ERROR(FMT, ...) MY_LOG(::my::LogSeverity::Error, FMT, ##__VA_ARGS__)
