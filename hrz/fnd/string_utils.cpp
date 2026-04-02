#include "hrz/fnd/string_utils.h"

#include "absl/strings/numbers.h"
#include "hrz/fnd/char_utils.h"
#include "hrz/fnd/log.h"

#include <algorithm>
#include <cassert>

namespace hrz::str
{

std::string_view rtrim_s(std::string_view str, char c)
{
    int index = (int)str.size() - 1;
    while (index >= 0 && str[index] == c)
        index -= 1;
    return str.substr(0, index + 1);
}

std::string rtrim(std::string_view str, char c)
{
    return std::string(rtrim_s(str, c));
}

std::string_view rtrim_s(std::string_view str)
{
    int index = (int)str.size() - 1;
    while (index >= 0 && hrz::is_ascii_whitespace(str[index]))
        index -= 1;
    return str.substr(0, index + 1);
}

std::string rtrim(std::string_view str)
{
    return std::string(rtrim_s(str));
}

std::string_view ltrim_s(std::string_view str, char c)
{
    size_t index = 0;
    while (index < str.size() && str[index] == c)
        index += 1;
    return str.substr(index);
}

std::string ltrim(std::string_view str, char c)
{
    return std::string(ltrim_s(str, c));
}

std::string_view ltrim_s(std::string_view str)
{
    size_t index = 0;
    while (index < str.size() && hrz::is_ascii_whitespace(str[index]))
        index += 1;
    return str.substr(index);
}

std::string ltrim(std::string_view str)
{
    return std::string(ltrim_s(str));
}

std::string_view trim_s(std::string_view str, char c)
{
    return rtrim_s(ltrim_s(str, c), c);
}

std::string trim(std::string_view str, char c)
{
    return std::string(trim_s(str, c));
}

std::string_view trim_s(std::string_view str)
{
    return rtrim_s(ltrim_s(str));
}

std::string trim(std::string_view str)
{
    return std::string(trim_s(str));
}

int rfind(std::string_view str, char c)
{
    auto index = str.rfind(c);
    if (index == std::string_view::npos)
        return -1;
    else
        return (int)index;
}

int find(std::string_view str, char c)
{
    auto index = str.find(c);
    if (index == std::string_view::npos)
        return -1;
    else
        return (int)index;
}

std::pair<std::string_view, std::string_view> split(std::string_view str, char c)
{
    int i = find(str, c);
    if (i >= 0)
    {
        return {str.substr(0, i), str.substr(i + 1)};
    }
    else
    {
        return {str, ""};
    }
}

bool iequals(std::string_view a, std::string_view b)
{
    if (a.size() != b.size()) return false;

    // From pugixml.cpp
    auto tolower_ascii = [](char ch)
    { return (unsigned int)(ch - 'A') < 26 ? (char)(ch | ' ') : ch; };

    for (size_t i = 0; i < a.size(); ++i)
    {
        if (tolower_ascii(a[i]) != tolower_ascii(b[i]))
        {
            return false;
        }
    }

    return true;
}

bool istarts_with(std::string_view str, std::string_view start)
{
    if (start.size() > str.size()) return false;
    return iequals(str.substr(0, start.size()), start);
}

bool iends_with(std::string_view str, std::string_view end)
{
    if (end.size() > str.size()) return false;
    return iequals(str.substr(str.size() - end.size()), end);
}

struct Base64Chars
{
    const uint8_t decoding_table[128];
    const uint8_t encoding_table[64];
    uint8_t padding;
};

static const Base64Chars base64_variants[] = {
    {{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 62,   0xff,
      0xff, 0xff, 63,   52,   53,   54,   55,   56,   57,   58,   59,   60,   61,   0xff, 0xff,
      0xff, 0,    0xff, 0xff, 0xff, 0,    1,    2,    3,    4,    5,    6,    7,    8,    9,
      10,   11,   12,   13,   14,   15,   16,   17,   18,   19,   20,   21,   22,   23,   24,
      25,   0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 26,   27,   28,   29,   30,   31,   32,   33,
      34,   35,   36,   37,   38,   39,   40,   41,   42,   43,   44,   45,   46,   47,   48,
      49,   50,   51,   0xff, 0xff, 0xff, 0xff, 0xff},
     {65,  66,  67,  68,  69,  70,  71,  72,  73,  74,  75,  76,  77,  78,  79,  80,
      81,  82,  83,  84,  85,  86,  87,  88,  89,  90,  97,  98,  99,  100, 101, 102,
      103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118,
      119, 120, 121, 122, 48,  49,  50,  51,  52,  53,  54,  55,  56,  57,  43,  47},
     '='},
    {{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      62,   0xff, 0xff, 52,   53,   54,   55,   56,   57,   58,   59,   60,   61,   0xff, 0xff,
      0xff, 0,    0xff, 0xff, 0xff, 0,    1,    2,    3,    4,    5,    6,    7,    8,    9,
      10,   11,   12,   13,   14,   15,   16,   17,   18,   19,   20,   21,   22,   23,   24,
      25,   0xff, 0xff, 0xff, 0xff, 63,   0xff, 26,   27,   28,   29,   30,   31,   32,   33,
      34,   35,   36,   37,   38,   39,   40,   41,   42,   43,   44,   45,   46,   47,   48,
      49,   50,   51,   0xff, 0xff, 0xff, 0xff, 0xff},
     {65,  66,  67,  68,  69,  70,  71,  72,  73,  74,  75,  76,  77,  78,  79,  80,
      81,  82,  83,  84,  85,  86,  87,  88,  89,  90,  97,  98,  99,  100, 101, 102,
      103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118,
      119, 120, 121, 122, 48,  49,  50,  51,  52,  53,  54,  55,  56,  57,  45,  95},
     '='},
    {{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 62,   0xff,
      62,   0xff, 63,   52,   53,   54,   55,   56,   57,   58,   59,   60,   61,   0xff, 0xff,
      0xff, 0,    0xff, 0xff, 0xff, 0,    1,    2,    3,    4,    5,    6,    7,    8,    9,
      10,   11,   12,   13,   14,   15,   16,   17,   18,   19,   20,   21,   22,   23,   24,
      25,   0xff, 0xff, 0xff, 0xff, 63,   0xff, 26,   27,   28,   29,   30,   31,   32,   33,
      34,   35,   36,   37,   38,   39,   40,   41,   42,   43,   44,   45,   46,   47,   48,
      49,   50,   51,   0xff, 0xff, 0xff, 0xff, 0xff},
     // No encoding table as the "Both" variant can only be used for decoding.
     {},
     '='}
};

size_t decode_base64_size_hint(std::string_view str, Base64DecodingVariant variant)
{
    if (str.size() % 4 != 0 || str.size() == 0) return 0;

    size_t group_count = str.size() / 4;
    size_t char_count = group_count * 3;
    uint8_t padding = base64_variants[static_cast<int32_t>(variant)].padding;

    if (str[str.size() - 1] == padding) char_count -= 1;
    if (str[str.size() - 2] == padding) char_count -= 1;

    return char_count;
}

inline int _decode_base64_group(
    const char* group,
    std::byte* output,
    const Base64Chars& base64_variant)
{
    // There are values > 127
    uint32_t group_uint;
    memcpy(&group_uint, group, 4);
    if ((group_uint & 0x80808080) != 0) return 0;

    uint8_t group_decoded[4] = {
        static_cast<uint8_t>(base64_variant.decoding_table[static_cast<int>(group[0])]),
        static_cast<uint8_t>(base64_variant.decoding_table[static_cast<int>(group[1])]),
        static_cast<uint8_t>(base64_variant.decoding_table[static_cast<int>(group[2])]),
        static_cast<uint8_t>(base64_variant.decoding_table[static_cast<int>(group[3])])
    };

    uint32_t group_decoded_uint;
    memcpy(&group_decoded_uint, group_decoded, 4);

    // There are non-valid chars
    if ((group_decoded_uint & 0xc0c0c0c0) != 0) return 0;

    output[0] = std::byte((group_decoded[0] << 2) | (group_decoded[1] >> 4));
    output[1] = std::byte((group_decoded[1] << 4) | (group_decoded[2] >> 2));
    output[2] = std::byte((group_decoded[2] << 6) | (group_decoded[3] >> 0));

    return 3 - (int)(group[3] == base64_variant.padding)
        - (int)(group[2] == base64_variant.padding);
}

size_t decode_base64(
    std::string_view str,
    std::vector<std::byte>* data,
    Base64DecodingVariant variant)
{
    assert(data);

    size_t size = decode_base64_size_hint(str, variant);
    data->resize(data->size() + size);

    std::span<std::byte> data_span(*data);
    return decode_base64_s(str, data_span, variant);
}

size_t decode_base64_s(
    std::string_view str,
    std::span<std::byte> data,
    Base64DecodingVariant variant)
{
    size_t size = decode_base64_size_hint(str, variant);

    assert(data.size() >= size);

    if (size == 0 || data.size() < size) return 0;

    const Base64Chars& base64_variant = base64_variants[static_cast<int32_t>(variant)];
    bool has_padding = str[str.size() - 1] == base64_variant.padding;
    size_t group_count = str.size() / 4;
    size_t full_group_count = group_count;

    if (has_padding)
    {
        full_group_count -= 1;
    }

    size_t decoded_count = 0;

    for (size_t i = 0; i < full_group_count; ++i)
    {
        const char* group = str.data() + i * 4;
        std::byte decoded[3];

        int decoded_count_group = _decode_base64_group(group, decoded, base64_variant);

        if (decoded_count_group != 3) return decoded_count;

        data[decoded_count + 0] = decoded[0];
        data[decoded_count + 1] = decoded[1];
        data[decoded_count + 2] = decoded[2];

        decoded_count += 3;
    }

    if (has_padding)
    {
        const char* group = str.data() + (group_count - 1) * 4;
        std::byte decoded[3];

        int decoded_count_group = _decode_base64_group(group, decoded, base64_variant);

        for (int i = 0; i < decoded_count_group; ++i)
        {
            data[decoded_count + i] = decoded[i];
        }

        decoded_count += decoded_count_group;
    }

    return decoded_count;
}

size_t encode_base64(
    std::span<const std::byte> data,
    std::string* str,
    Base64EncodingVariant variant)
{
    size_t initial_string_size = str->size();

    const Base64Chars& base64_variant = base64_variants[static_cast<int32_t>(variant)];

    // Inspired from https://github.com/Mbed-TLS/mbedtls/blob/development/library/base64.c
    const size_t n = (data.size() / 3) * 3;

    size_t i;
    for (i = 0; i < n; i += 3)
    {
        uint8_t src[3] = {(uint8_t)data[i], (uint8_t)data[i + 1], (uint8_t)data[i + 2]};

        str->push_back(base64_variant.encoding_table[(src[0] >> 2) & 0x3f]);
        str->push_back(
            base64_variant.encoding_table[(((src[0] & 0x03) << 4) + (src[1] >> 4)) & 0x3f]);
        str->push_back(
            base64_variant.encoding_table[(((src[1] & 0x0f) << 2) + (src[2] >> 6)) & 0x3f]);
        str->push_back(base64_variant.encoding_table[src[2] & 0x3f]);
    }

    if (i < data.size())
    {
        const bool has_two = data.size() - i == 2;

        uint8_t src[2] = {
            (uint8_t)data[i++],
            has_two ? (uint8_t)data[i] : (uint8_t)0,
        };

        str->push_back(base64_variant.encoding_table[(src[0] >> 2) & 0x3f]);
        str->push_back(
            base64_variant.encoding_table[(((src[0] & 0x03) << 4) + (src[1] >> 4)) & 0x3f]);

        if (has_two)
        {
            str->push_back(base64_variant.encoding_table[((src[1] & 0x0f) << 2) & 0x3f]);
        }
        else
        {
            str->push_back(base64_variant.padding);
        }

        str->push_back(base64_variant.padding);
    }

    return str->size() - initial_string_size;
}

size_t encode_code_point_to_utf8(uint32_t code_point, std::span<char> buffer)
{
#define CHECK_BUFFER_SIZE(N)                           \
    if (buffer.size() < N)                             \
    {                                                  \
        HRZ_LOG_ERROR("Not enough space in buffer");   \
        assert(false && "Not enough space in buffer"); \
        return 0;                                      \
    }

    if (code_point < 0x7f)
    {
        // Code points in the range [U+0000, U+007F] represent ASCII characters. UTF-8 has a
        // 1:1 mapping with the ASCII character encoding. So we can simply emit the code
        // point as is.

        CHECK_BUFFER_SIZE(1);
        buffer[0] = (char)code_point;
        return 1;
    }
    else if (code_point < 0x7ff)
    {
        // Code points in the range [U+0080, U+07FF]. UTF-8 encodes these characters on two
        // bytes with the following encoding: 110x.xxxx 10xx.xxxx (x's represent bits of
        // the Unicode code point).

        CHECK_BUFFER_SIZE(2);
        buffer[0] = (char)(0xc0 | ((code_point >> 6) & 0x1f));
        buffer[1] = (char)(0x80 | (code_point & 0x3f));
        return 2;
    }
    else if (code_point < 0xffff)
    {
        // Code points in the range [U+8000, U+FFFF]. UTF-8 encodes these characters on
        // three bytes with the following encoding: 1110.xxxx 10xx.xxxx 10xx.xxxx (x's
        // represent bits of the Unicode code point).

        CHECK_BUFFER_SIZE(3);
        buffer[0] = (char)(0xe0 | ((code_point >> 12) & 0xf));
        buffer[1] = (char)(0x80 | ((code_point >> 6) & 0x3f));
        buffer[2] = (char)(0x80 | (code_point & 0x3f));
        return 3;
    }
    else if (code_point < 0x10ffff)
    {
        // Code points in the range [U+10000, U+10FFFF]. UTF-8 encodes these characters on
        // four bytes with the following encoding: 1111.xxxx 10xx.xxxx 10xx.xxxx 10xx.xxxx
        // (x's represent bits of the Unicode code point).

        CHECK_BUFFER_SIZE(4);
        buffer[0] = (char)(0xf0 | ((code_point >> 18) & 0x7f));
        buffer[1] = (char)(0x80 | ((code_point >> 12) & 0x3f));
        buffer[2] = (char)(0x80 | ((code_point >> 6) & 0x3f));
        buffer[3] = (char)(0x80 | (code_point & 0x3f));
        return 4;
    }
    else
    {
        // Invalid Unicode code point.
        return 0;
    }
#undef CHECK_BUFFER_SIZE
}

std::string sanitize_named_fmt_arguments(
    std::string fmt_string,
    std::span<const std::string_view> valid_args)
{
    size_t start = 0;
    while (true)
    {
        start = fmt_string.find('{', start);
        if (start == std::string::npos) break;

        const size_t end = fmt_string.find('}', start);
        if (end == std::string::npos) break;

        auto substitution = std::string_view(fmt_string).substr(start + 1, end - start - 1);

        if (std::ranges::find(valid_args, substitution) == valid_args.end())
        {
            fmt_string.erase(start, end - start + 1);
        }
        else
        {
            start = end;
        }
    }

    return fmt_string;
}

// We want to parse things like "1.0" as 1. We can't just parse as double then
// cast otherwise we could parse numbers too large to fit in an integer.  So
// instead we prepare the string by trimming it and keeping only the digits.
std::string_view prepare_parse_integer(std::string_view str)
{
    str = trim_s(str);

    size_t end = 0;
    while (end < str.size()
           && (hrz::is_ascii_digit(str[end]) || str[end] == '-' || str[end] == '+'))
    {
        end += 1;
    }

    return str.substr(0, end);
}

std::optional<int64_t> parse_int64(std::string_view str)
{
    str = prepare_parse_integer(str);

    int64_t value = 0;
    if (absl::SimpleAtoi(str, &value))
    {
        return value;
    }

    return std::nullopt;
}

std::optional<uint64_t> parse_uint64(std::string_view str)
{
    str = prepare_parse_integer(str);

    uint64_t value = 0;
    if (absl::SimpleAtoi(str, &value))
    {
        return value;
    }

    return std::nullopt;
}

std::optional<double> parse_double(std::string_view str)
{
    str = trim_s(str);

    double value = 0;
    if (absl::SimpleAtod(str, &value))
    {
        return value;
    }

    return std::nullopt;
}

} // namespace hrz::str
