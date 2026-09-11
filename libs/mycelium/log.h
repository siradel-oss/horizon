// SPDX-FileCopyrightText: Copyright 2023 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include <source_location>
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
    LogSeverity severity,
    const char* message,
    size_t message_length,
    const std::source_location& location);

void set_log_filter_level(LogSeverity filter_level);
void set_log_callback(LogFunction callback);

} // namespace my
