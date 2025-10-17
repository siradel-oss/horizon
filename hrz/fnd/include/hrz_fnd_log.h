#pragma once

#include "hrz_fnd_defines.h"

#include <fmt/core.h>
#include <fmt/format.h>

#include <span>
#include <string_view>

namespace hrz::log
{
// Keep this enum in line with Mycelium's LogSeverity
enum class Severity : uint8_t
{
    Debug = 0,
    Info,
    Warning,
    Error,
};

struct LogLineView
{
    const char* line;
    Severity severity;
};

// Don't call this directly, use the macros HRZ_LOG_DEBUG, HRZ_LOG_INFO,
// HRZ_LOG_WARNING and HRZ_LOG_ERROR.
void message(
    const char* prefix,
    Severity severity,
    std::string_view message,
    const char* file,
    int line);

/**
 * Retrieves zero-terminated strings (and the associated severities) from
 * the log history and places as much of them as possible into the given span.
 * The strings are still owned by the system and may be invalidated by any
 * call to the logging system.
 * The number of retrieved lines is returned.
 * The lines are in ascending time order.
 *
 * @Todo(HRZ-98) We could probably use a acquire/release mechanism to lock
 * the logging system instead of a mutex, and alternatively a copying version
 * for systems that don't need to call this too often but want to hold on to
 * the returned data.
 */
uint32_t get_history(std::span<LogLineView> history);

void clear_history();

/**
 * Filter-out all log messages that are strictly less severe than
 * the provided `filter_level`.
 *
 * A `filter_level` of `Severity::Debug` (0) doesn't filter anything while a
 * filter set to `Severity::Error` (3) leaves only error messages.
 */
void set_log_filter_level(Severity filter_level);

} // namespace hrz::log

#ifndef HRZ_LOG_PREFIX
#    error Please specify a value for HRZ_LOG_PREFIX
#endif

#define HRZ_LOG_XSTRINGIFY(X) #X
#define HRZ_LOG_STRINGIFY(X) HRZ_LOG_XSTRINGIFY(X)

#define HRZ_LOG(SEVERITY, FMT, ...)                  \
    ::hrz::log::message(                             \
        HRZ_LOG_STRINGIFY(HRZ_LOG_PREFIX), SEVERITY, \
        ::fmt::format(FMT_STRING(FMT), ##__VA_ARGS__), __FILE__, __LINE__)

#ifndef NDEBUG
#    define HRZ_LOG_DEBUG(FMT, ...) HRZ_LOG(::hrz::log::Severity::Debug, FMT, ##__VA_ARGS__)
#else
#    define HRZ_LOG_DEBUG(FMT, ...)
#endif

#define HRZ_LOG_INFO(FMT, ...) HRZ_LOG(::hrz::log::Severity::Info, FMT, ##__VA_ARGS__)
#define HRZ_LOG_WARNING(FMT, ...) HRZ_LOG(::hrz::log::Severity::Warning, FMT, ##__VA_ARGS__)
#define HRZ_LOG_ERROR(FMT, ...) HRZ_LOG(::hrz::log::Severity::Error, FMT, ##__VA_ARGS__)
