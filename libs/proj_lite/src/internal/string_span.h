// SPDX-FileCopyrightText: Copyright 2019 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include <string.h>

#include <stdbool.h>
#include <stdint.h>

typedef struct pl_StringSpan
{
    const char* begin;
    const char* end;
} pl_StringSpan;

static pl_StringSpan pl_ss_from_str_length(const char* str, size_t length)
{
    pl_StringSpan span;
    span.begin = str;
    span.end = str + length;
    return span;
}

// str must be null-terminated!
inline static pl_StringSpan pl_ss_from_zstr(const char* str)
{
    return pl_ss_from_str_length(str, strlen(str));
}

inline static bool pl_ss_empty(const pl_StringSpan* ss)
{
    return ss->begin >= ss->end;
}

inline static size_t pl_ss_size(const pl_StringSpan* ss)
{
    return (size_t)ss->end - (size_t)ss->begin;
}

inline static bool pl_ss_compare(const pl_StringSpan* ss, const char* str)
{
    return strncmp(str, ss->begin, pl_ss_size(ss)) == 0;
}

// Returns the first slice. Becomes the second one.
pl_StringSpan pl_ss_split_at_first(pl_StringSpan* ss, char c);
void pl_ss_trim(pl_StringSpan* ss);
double pl_ss_to_double(const pl_StringSpan* ss);
int pl_ss_to_int(const pl_StringSpan* ss);
