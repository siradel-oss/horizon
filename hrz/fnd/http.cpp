#include "hrz/fnd/http.h"

#include "absl/time/civil_time.h"
#include "absl/time/time.h"
#include "hrz/fnd/char_utils.h"
#include "hrz/fnd/hash.h"
#include "hrz/fnd/inlined_vector.h"
#include "hrz/fnd/int128.h"
#include "hrz/fnd/string_utils.h"

#include <assert.h>
#include <fmt/core.h>

namespace
{
void normalize_header_name(const char* begin, const char* end, char* out)
{
    for (const char* it = begin; it != end; ++it, ++out)
    {
        *out = (char)hrz::ascii_to_lower(*it);
    }
}

hrz::uint128 get_header_key(std::string_view name)
{
    char buffer[64];
    if (name.size() > 64) name = name.substr(0, 64);

    normalize_header_name(name.data(), name.data() + name.size(), buffer);
    return hrz::murmur3_x64_128(std::span<const std::byte>((const std::byte*)buffer, name.size()));
}
} // namespace

hrz::HttpHeaders& hrz::HttpHeaders::operator=(const HttpHeaders& other)
{
    if (&other == this) return *this;

    clear();

    _headers.reserve(other._headers.size());
    for (const auto& entry : other._headers)
    {
        _headers[entry.first] = {
            _arena.zstr(entry.second.name), _arena.zstr_span(entry.second.value_str())};
    }

    _hash_full = other._hash_full;
    _hash_content = other._hash_content;
    _dirty_hash = other._dirty_hash;

    return *this;
}

void hrz::HttpHeaders::set_header(std::string_view name, std::string_view value)
{
    if (name.empty()) return;

    hrz::uint128 key = get_header_key(name);

    auto it = _headers.find(key);
    if (it != _headers.end())
    {
        // Recycle the previous memory if possible
        if (it->second.value.size() >= value.size())
        {
            char* dst = it->second.value.data();
            memcpy(dst, value.data(), value.size());
            dst[value.size()] = '\0';
            it->second.value = std::span<char>(dst, value.size());
        }
        else
        {
            it->second.value = _arena.zstr_span(value);
        }
    }
    else
    {
        _headers[key] = {_arena.zstr(name), _arena.zstr_span(value)};
    }

    _dirty_hash = true;
}

void hrz::HttpHeaders::remove_header(std::string_view name)
{
    _headers.erase(get_header_key(name));
    _dirty_hash = true;
}

std::string_view hrz::HttpHeaders::get_header(std::string_view name) const
{
    hrz::uint128 key = get_header_key(name);

    auto it = _headers.find(key);
    if (it != _headers.end())
    {
        return it->second.value_str();
    }
    else
    {
        return {};
    }
}

static bool is_accept_header(std::string_view name)
{
    static constexpr std::string_view kAccept = "accept";
    if (name.size() < 6) return false;
    return hrz::str::iequals(name.substr(0, 6), kAccept);
}

void hrz::HttpHeaders::refresh_hashes() const
{
    if (!_dirty_hash) return;

    uint64_t hash_full = 0;
    uint64_t hash_content = 0;

    // Sort the headers in a canonical order.
    // It's not alphabetical, but it's guaranteed to be always the same.
    hrz::InlinedVector<hrz::uint128, 8> full_order;
    hrz::InlinedVector<hrz::uint128, 8> content_order;
    full_order.reserve(_headers.size());

    for (const auto& entry : _headers)
    {
        full_order.push_back(entry.first);

        if (is_accept_header(entry.second.name))
        {
            content_order.push_back(entry.first);
        }
    }

    std::ranges::sort(full_order);
    std::ranges::sort(content_order);

    for (const hrz::uint128 key : full_order)
    {
        hash_full = hrz::hash_values(
            hash_full, key.high, key.low,
            hrz::murmur3_x64_64(_headers.find(key)->second.value_str()));
    }

    for (const hrz::uint128 key : content_order)
    {
        hash_content = hrz::hash_values(
            hash_content, key.high, key.low,
            hrz::murmur3_x64_64(_headers.find(key)->second.value_str()));
    }

    _hash_full = hash_full;
    _hash_content = hash_content;
    _dirty_hash = false;
}

uint64_t hrz::HttpHeaders::hash_full() const
{
    if (_dirty_hash)
    {
        refresh_hashes();
    }
    return _hash_full;
}

uint64_t hrz::HttpHeaders::hash_content() const
{
    if (_dirty_hash)
    {
        refresh_hashes();
    }
    return _hash_content;
}

void hrz::HttpHeaders::clear()
{
    _dirty_hash = true;
    _headers.clear();
    _arena.reset();
}

void hrz::HttpHeaders::swap(HttpHeaders& other)
{
    std::swap(_dirty_hash, other._dirty_hash);
    std::swap(_hash_content, other._hash_content);
    std::swap(_hash_full, other._hash_full);
    std::swap(_arena, other._arena);
    std::swap(_headers, other._headers);
}

namespace
{
constexpr int parse_triple_character_id(const char* str)
{
    return (hrz::ascii_to_lower((int)str[0]) * 128 + hrz::ascii_to_lower((int)str[1])) * 128
        + hrz::ascii_to_lower((int)str[2]);
}

constexpr int parse_number(std::string_view str)
{
    int value = 0;
    for (const char c : str)
    {
        value = value * 10 + (int)(c - '0');
    }
    return value;
}

} // namespace

// https://www.rfc-editor.org/rfc/rfc9110.html#section-5.6.7-10
// Example "Fri, 10 Dec 1982 22:03:11 GMT"
hrz::HttpTime hrz::HttpTime::from_imf_fixdate(std::string_view str)
{
    str = str::trim_s(str, ' ');
    if (str.size() < 29) return HttpTime::invalid();

    // A date could be double-quoted.
    if (str.front() == '"' && str.back() == '"' && str.size() >= 2)
    {
        str = str.substr(1, str.size() - 2);
    }

    if (str.size() != 29) return HttpTime::invalid();

    // @ is alphabetic, $ is digit
    static constexpr const char* VALIDATION = "@@@, $$ @@@ $$$$ $$:$$:$$ gmt";

    for (int i = 0; i < 29; ++i)
    {
        char c = str[i];
        switch (VALIDATION[i])
        {
            case '@':
                if (!hrz::is_ascii_letter(c))
                {
                    return HttpTime::invalid();
                }
                break;
            case '$':
                if (!hrz::is_ascii_digit(c))
                {
                    return HttpTime::invalid();
                }
                break;
            default:
                if (VALIDATION[i] != hrz::ascii_to_lower(c))
                {
                    return HttpTime::invalid();
                }
                break;
        }
    }

    switch (parse_triple_character_id(str.data()))
    {
        case parse_triple_character_id("Mon"):
        case parse_triple_character_id("Tue"):
        case parse_triple_character_id("Wed"):
        case parse_triple_character_id("Thu"):
        case parse_triple_character_id("Fri"):
        case parse_triple_character_id("Sat"):
        case parse_triple_character_id("Sun"): break;
        default: return HttpTime::invalid();
    }

    int month = 0;
    switch (parse_triple_character_id(str.data() + 8))
    {
        case parse_triple_character_id("Jan"): month = 1; break;
        case parse_triple_character_id("Feb"): month = 2; break;
        case parse_triple_character_id("Mar"): month = 3; break;
        case parse_triple_character_id("Apr"): month = 4; break;
        case parse_triple_character_id("May"): month = 5; break;
        case parse_triple_character_id("Jun"): month = 6; break;
        case parse_triple_character_id("Jul"): month = 7; break;
        case parse_triple_character_id("Aug"): month = 8; break;
        case parse_triple_character_id("Sep"): month = 9; break;
        case parse_triple_character_id("Oct"): month = 10; break;
        case parse_triple_character_id("Nov"): month = 11; break;
        case parse_triple_character_id("Dec"): month = 12; break;
        default: return HttpTime::invalid();
    }

    const int day_of_month = parse_number(str.substr(5, 2));
    const int year = parse_number(str.substr(12, 4));
    const int hours = parse_number(str.substr(17, 2));
    const int minutes = parse_number(str.substr(20, 2));
    const int seconds = parse_number(str.substr(23, 2));

    const absl::CivilSecond civil(year, month, day_of_month, hours, minutes, seconds);
    const absl::Time time = absl::FromCivil(civil, absl::UTCTimeZone());

    return HttpTime(absl::ToUnixSeconds(time));
}

hrz::HttpTime hrz::HttpDefaultClock::now() const
{
    return HttpTime(absl::ToUnixSeconds(absl::Now()));
}

// https://www.rfc-editor.org/rfc/rfc9110.html#section-5.6.7-10
// Example "Fri, 10 Dec 1982 22:03:11 GMT"
void hrz::HttpTime::write_imf_fixdate(std::span<char> buffer) const
{
    assert(buffer.size() >= 30);
    if (buffer.size() < 30) return;

    static_assert((int)absl::Weekday::monday == 0, "Days!");
    static_assert((int)absl::Weekday::tuesday == 1, "Days!");
    static_assert((int)absl::Weekday::wednesday == 2, "Days!");
    static_assert((int)absl::Weekday::thursday == 3, "Days!");
    static_assert((int)absl::Weekday::friday == 4, "Days!");
    static_assert((int)absl::Weekday::saturday == 5, "Days!");
    static_assert((int)absl::Weekday::sunday == 6, "Days!");

    static constexpr const char* days[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    static constexpr const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                             "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    auto info = absl::UTCTimeZone().At(absl::FromUnixSeconds(_timestamp));
    auto wd = absl::GetWeekday(info.cs);

    char* end = fmt::format_to_n(
                    buffer.data(), 29, "{}, {:02} {} {:04} {:02}:{:02}:{:02} GMT", days[(int)wd],
                    info.cs.day(), months[info.cs.month() - 1], info.cs.year(), info.cs.hour(),
                    info.cs.minute(), info.cs.second())
                    .out;

    assert(end == buffer.data() + 29);
    *end = '\0';
}

const char* hrz::HttpTime::get_imf_fixdate() const
{
    static thread_local char buffer[30];
    write_imf_fixdate(buffer);
    return buffer;
}

namespace
{
// This parser returns pieces of strings separated by a given separator.
// It parses those pieces of strings to separate key/value pairs separated
// by a '=' if it exists.
class HttpHeaderValueParser
{
    const char* _it;
    const char* _end;
    char _sep;

    void ltrim()
    {
        while (_it < _end && hrz::is_ascii_whitespace(*_it))
        {
            ++_it;
        }
    }

    void skip_inside_string()
    {
        while (_it < _end && *_it != '"')
        {
            if (*_it == '\\')
                _it += 2;
            else
                _it += 1;
        }
    }

public:
    HttpHeaderValueParser(std::string_view str, char sep) :
        _it(str.data()), _end(str.data() + str.size()), _sep(sep)
    {
    }

    std::optional<std::pair<std::string_view, std::string_view>> next()
    {
        ltrim();

        if (_it >= _end) return std::nullopt;

        const char* begin = _it;

        assert(!hrz::is_ascii_whitespace(*begin));

        const char* eq = nullptr;
        while (_it < _end && *_it != _sep)
        {
            if (*_it == '"')
            {
                ++_it;

                skip_inside_string();

                if (_it >= _end || *_it != '"')
                {
                    // Handle unterminated strings
                    _it = _end;
                    return std::nullopt;
                }

                ++_it;
            }
            else
            {
                if (!eq && *_it == '=') eq = _it;
                ++_it;
            }
        }

        const char* end = _it;

        // Go past the separator if this is where we stopped
        if (*_it == _sep) ++_it;

        if (begin == end)
        {
            // Empty string, probably two commas separating just whitespace, or two subsequent
            // commas. Anyway an HTTP client is supposed to accept as much unexpected input as
            // possible so...
            return std::make_pair(std::string_view(), std::string_view());
        }
        else
        {
            assert(begin < end);

            std::string_view first = "";
            std::string_view second = "";

            if (!eq)
            {
                first = std::string_view(begin, end - begin);
            }
            else
            {
                assert(begin <= eq && eq <= end);
                first = std::string_view(begin, eq - begin);
                second = std::string_view(eq + 1, end - eq - 1);
            }

            return std::make_pair(hrz::str::trim_s(first), hrz::str::trim_s(second));
        }
    }
};
} // namespace

void hrz::parse_http_header_value(
    std::string_view str,
    char separator,
    const std::function<bool(std::string_view, std::string_view)>& callback)
{
    HttpHeaderValueParser parse(str, separator);

    while (true)
    {
        auto res = parse.next();
        if (!res) return;
        if (!callback(res.value().first, res.value().second)) return;
    }
}
