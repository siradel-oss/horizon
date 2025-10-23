#include "hrz_fnd_log.h"

#include <mutex>

#if HRZ_EMSCRIPTEN
#    include <emscripten/emscripten.h>
#endif

#include "hrz_fnd_path_utils.h"
#include "hrz_fnd_time.h"

namespace
{
#if HRZ_EMSCRIPTEN
int severity_to_em_flag(hrz::log::Severity severity)
{
    switch (severity)
    {
        case hrz::log::Severity::Warning: return EM_LOG_WARN;
        case hrz::log::Severity::Error: return EM_LOG_ERROR;
        default: return 0;
    }
}
#endif
} // namespace

namespace hrz::log
{
enum
{
    HISTORY_COUNT = 128,
    MAX_MESSAGE_LENGTH = 512,
};

struct LogLine
{
    std::string line;
    Severity severity;
};

struct History
{
    LogLine lines[HISTORY_COUNT];
    uint32_t head = 0; // Next insert index
    uint32_t count = 0;
};

Severity g_log_filter_level = Severity::Debug;
History g_history = {};
std::mutex g_history_mutex;

uint32_t get_history(std::span<LogLineView> history)
{
    const std::lock_guard<std::mutex> lock(g_history_mutex);

    uint32_t count = std::min((uint32_t)history.size(), g_history.count);
    uint32_t index = (g_history.head + HISTORY_COUNT - count) % HISTORY_COUNT;

    for (uint32_t i = 0; i < count; ++i)
    {
        if (index == HISTORY_COUNT)
        {
            index = 0;
        }

        auto& log_line = g_history.lines[index];

        history[i].line = log_line.line.c_str();
        history[i].severity = log_line.severity;

        index += 1;
    }

    return count;
}

LogLine* _acquire_history_line()
{
    const std::lock_guard<std::mutex> lock(g_history_mutex);

    uint32_t index = g_history.head;
    if (g_history.count < HISTORY_COUNT) g_history.count += 1;
    g_history.head = (g_history.head + 1) % HISTORY_COUNT;
    return &g_history.lines[index];
}

inline const char* _severity_to_string(Severity severity)
{
    switch (severity)
    {
        case Severity::Debug: return "Debug";
        case Severity::Info: return "Info";
        case Severity::Warning: return "WARNING";
        case Severity::Error: return "ERROR";
        default: return "";
    }
}

void clear_history()
{
    const std::lock_guard<std::mutex> lock(g_history_mutex);

    g_history.count = 0;
}

void set_log_filter_level(Severity filter_level)
{
    g_log_filter_level = (Severity)std::min(filter_level, Severity::Error);
}

void message(
    const char* prefix,
    Severity severity,
    std::string_view message,
    const std::source_location& location)
{
    if (severity < g_log_filter_level)
    {
        return;
    }

    const std::string_view file = path::basename_s(location.file_name());

    const double since_epoch = hrz::now_ms() / 1000.0;
    const double millis = std::floor((since_epoch - std::floor(since_epoch)) * 1000.0);
    const int minutes = (int)std::floor(since_epoch / 60.0);
    const int seconds = (int)std::floor(since_epoch) - 60 * minutes;

    LogLine* log_line = _acquire_history_line();

    log_line->severity = severity;

    std::string* str = &log_line->line;
    str->resize(MAX_MESSAGE_LENGTH);

    const size_t length =
        (size_t)fmt::format_to_n(
            std::to_address(str->begin()), MAX_MESSAGE_LENGTH,
            "{} [{:>4}:{:02}.{:03}] [{:^7}] ({}:{}) {}", prefix, minutes, seconds, millis,
            _severity_to_string(severity), fmt::string_view(file.data(), file.size()),
            location.line(), fmt::string_view(message.data(), message.size()))
            .size;
    str->resize(length);

#if HRZ_EMSCRIPTEN
    emscripten_log(EM_LOG_CONSOLE | severity_to_em_flag(severity), str->c_str());
#else
    fmt::print("{}\n", str->c_str());
    fflush(stdout);
#endif
}

} // namespace hrz::log
