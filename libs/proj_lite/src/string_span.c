#include "proj_lite_internal/string_span.h"

#include <stdlib.h>

static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

pl_StringSpan pl_ss_split_at_first(pl_StringSpan* ss, char c)
{
    pl_StringSpan first_slice = *ss;

    while (ss->begin < ss->end && *ss->begin != c)
    {
        ss->begin += 1;
    }

    first_slice.end = ss->begin;
    return first_slice;
}

void pl_ss_trim(pl_StringSpan* ss)
{
    while (ss->begin < ss->end && is_space(ss->begin[0]))
        ss->begin += 1;
    // @Safety begin < end, so end - 1 is >= begin, which is readable.
    while (ss->begin < ss->end && is_space(ss->end[-1]))
        ss->end -= 1;
}

double pl_ss_to_double(const pl_StringSpan* ss)
{
    size_t len = pl_ss_size(ss);

    if (len > 31) return 0.0;

    char buffer[32];
    memcpy(&buffer[0], ss->begin, len);
    buffer[len] = '\0';

    char* _;
    return strtod(buffer, &_);
}

int pl_ss_to_int(const pl_StringSpan* ss)
{
    size_t len = pl_ss_size(ss);

    if (len > 31) return 0.0;

    char buffer[32];
    memcpy(&buffer[0], ss->begin, len);
    buffer[len] = '\0';

    return atoi(buffer);
}
